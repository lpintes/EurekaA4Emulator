#include "com_link.h"

#include <windows.h>

#include <algorithm>
#include <string_view>

namespace {

// How long an idle read waits before it is issued again.  The read returns as
// soon as anything arrives, so this only bounds a wait nobody is in.
constexpr DWORD kReadWaitMs = 1000;

// The system's own wording, in the user's language.
std::wstring ErrorText(DWORD code) {
  wchar_t* buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring text = length != 0 ? std::wstring(buffer, length)
                                  : L"chyba " + std::to_wstring(code);
  if (buffer != nullptr) LocalFree(buffer);
  while (!text.empty() &&
         (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
    text.pop_back();
  return text;
}

// "9600 Bd 8N1", the way a terminal program writes it.
std::wstring Describe(const LineFormat& format) {
  const wchar_t parity = format.parity == LineFormat::Parity::kEven  ? L'E'
                         : format.parity == LineFormat::Parity::kOdd ? L'O'
                                                                     : L'N';
  return std::to_wstring(format.baud) + L" Bd " + std::to_wstring(format.dataBits) +
         parity + std::to_wstring(format.stopBits);
}

// Started, or already finished -- which an overlapped handle still reports
// through the event.  Anything else means the port is gone.
bool Started(BOOL result) {
  return result || GetLastError() == ERROR_IO_PENDING;
}

}  // namespace

ComLink::ComLink() {
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

ComLink::~ComLink() {
  Close();
  CloseHandle(stop_);
  CloseHandle(wake_);
}

bool ComLink::Open(const std::wstring& name, std::wstring& error) {
  Close();
  // The \\.\ prefix is what reaches COM10 and above, and names such as
  // com0com's CNCA0 that do not start with COM at all.
  const HANDLE port = CreateFileW((L"\\\\.\\" + name).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (port == INVALID_HANDLE_VALUE) {
    error = ErrorText(GetLastError());
    return false;
  }
  port_ = port;
  // A read returns whatever has arrived as soon as anything has; writes
  // never time out, the machine already paces them by the character time.
  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = kReadWaitMs;
  LineFormat format;
  {
    std::lock_guard guard(lock_);
    format = format_;
    formatChanged_ = false;
    rtsChanged_ = false;
  }
  std::wstring detail;
  if (!SetupComm(port, 4096, 4096) || !SetCommTimeouts(port, &timeouts) ||
      !ApplyFormat(format, detail) || !SetCommMask(port, EV_CTS) ||
      !PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
    error = detail.empty() ? ErrorText(GetLastError()) : detail;
    CloseHandle(port);
    port_ = nullptr;
    return false;
  }
  DWORD modem = 0;
  GetCommModemStatus(port, &modem);
  cts_ = (modem & MS_CTS_ON) != 0;
  ResetEvent(stop_);
  open_ = true;
  worker_ = std::thread(&ComLink::Serve, this);
  return true;
}

void ComLink::Close() {
  if (worker_.joinable()) {
    SetEvent(stop_);
    worker_.join();
  }
  if (port_ != nullptr) {
    CloseHandle(port_);
    port_ = nullptr;
  }
  open_ = false;
  cts_ = false;
  std::lock_guard guard(lock_);
  inbox_.clear();
  outbox_.clear();
}

void ComLink::SetLineFormat(const LineFormat& format) {
  {
    std::lock_guard guard(lock_);
    if (format_ == format) return;
    format_ = format;
    formatChanged_ = true;
  }
  SetEvent(wake_);
}

bool ComLink::ClearToSend() {
  return open_ && cts_;
}

void ComLink::SetRequestToSend(bool asserted) {
  {
    std::lock_guard guard(lock_);
    if (rts_ == asserted) return;
    rts_ = asserted;
    rtsChanged_ = true;
  }
  SetEvent(wake_);
}

void ComLink::Transmit(uint8_t character) {
  if (!open_) return;
  {
    std::lock_guard guard(lock_);
    outbox_.push_back(static_cast<char>(character));
  }
  SetEvent(wake_);
}

std::optional<uint8_t> ComLink::Receive() {
  std::lock_guard guard(lock_);
  if (inbox_.empty()) return std::nullopt;
  const uint8_t character = inbox_.front();
  inbox_.pop_front();
  return character;
}

void ComLink::Notify(Event event, const std::wstring& detail) {
  if (listener_) listener_(event, detail);
}

// RTS is set from rts_ along with the rest: DCB is the one place the driver
// keeps it, and RTS_CONTROL_HANDSHAKE would take it away from the firmware.
bool ComLink::ApplyFormat(const LineFormat& format, std::wstring& detail) {
  const HANDLE port = port_;
  DCB dcb{};
  dcb.DCBlength = sizeof dcb;
  if (!GetCommState(port, &dcb)) {
    detail = ErrorText(GetLastError());
    return false;
  }
  // 0 is the external clock, which shifts no bits at all; whatever the port
  // ran at is as good as anything else for a line that carries nothing.
  if (format.baud != 0) dcb.BaudRate = format.baud;
  dcb.ByteSize = format.dataBits;
  dcb.Parity = format.parity == LineFormat::Parity::kEven  ? EVENPARITY
               : format.parity == LineFormat::Parity::kOdd ? ODDPARITY
                                                           : NOPARITY;
  dcb.fParity = format.parity != LineFormat::Parity::kNone;
  dcb.StopBits = format.stopBits == 2 ? TWOSTOPBITS : ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = FALSE;
  // DTR up for as long as the port is ours: on a null-modem cable it is the
  // far end's DSR and DCD, "somebody is there".
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  {
    std::lock_guard guard(lock_);
    dcb.fRtsControl = rts_ ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
  }
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fNull = FALSE;
  dcb.fErrorChar = FALSE;
  // A framing or parity error must not stall the port until somebody calls
  // ClearCommError; the firmware does not model line errors either.
  dcb.fAbortOnError = FALSE;
  if (!SetCommState(port, &dcb)) {
    detail = L"Port neprijal nastavenie " + Describe(format) + L": " +
             ErrorText(GetLastError());
    return false;
  }
  return true;
}

void ComLink::Serve() {
  const HANDLE port = port_;
  OVERLAPPED reading{};
  OVERLAPPED writing{};
  OVERLAPPED watching{};
  reading.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  writing.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  watching.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  char received[4096];
  // What is on its way to the port; it stays here until the write finishes.
  std::string sending;
  DWORD happened = 0;
  bool readPending = false;
  bool writePending = false;
  bool watchPending = false;
  bool lost = false;
  // Why the port went away, taken before anything else can overwrite it.
  DWORD why = 0;
  const auto fail = [&] {
    why = GetLastError();
    lost = true;
  };

  while (!lost) {
    // Reset before taking the queues: a call after this sets it again, so no
    // wake-up falls between the two.
    ResetEvent(wake_);
    std::optional<LineFormat> format;
    std::optional<bool> rts;
    {
      std::lock_guard guard(lock_);
      if (formatChanged_) format = format_;
      if (rtsChanged_ && !formatChanged_) rts = rts_;
      formatChanged_ = false;
      rtsChanged_ = false;
      if (!writePending) {
        sending += outbox_;
        outbox_.clear();
      }
    }
    std::wstring detail;
    if (format && !ApplyFormat(*format, detail)) Notify(Event::kFormatRefused, detail);
    if (rts) EscapeCommFunction(port, *rts ? SETRTS : CLRRTS);

    if (!writePending && !sending.empty()) {
      if (Started(WriteFile(port, sending.data(), static_cast<DWORD>(sending.size()), nullptr,
                            &writing)))
        writePending = true;
      else
        fail();
    }
    if (!readPending && !lost) {
      if (Started(ReadFile(port, received, sizeof received, nullptr, &reading)))
        readPending = true;
      else
        fail();
    }
    if (!watchPending && !lost) {
      if (Started(WaitCommEvent(port, &happened, &watching))) watchPending = true;
      else fail();
    }
    if (lost) break;

    HANDLE events[] = {stop_, wake_, reading.hEvent, writing.hEvent, watching.hEvent};
    if (WaitForMultipleObjects(5, events, FALSE, INFINITE) == WAIT_OBJECT_0) break;

    DWORD done = 0;
    if (readPending && HasOverlappedIoCompleted(&reading)) {
      readPending = false;
      ResetEvent(reading.hEvent);
      if (!GetOverlappedResult(port, &reading, &done, FALSE)) {
        fail();
      } else if (done != 0) {
        std::lock_guard guard(lock_);
        inbox_.insert(inbox_.end(), received, received + done);
      }
    }
    if (writePending && HasOverlappedIoCompleted(&writing)) {
      writePending = false;
      ResetEvent(writing.hEvent);
      if (!GetOverlappedResult(port, &writing, &done, FALSE)) fail();
      else sending.erase(0, done);
    }
    if (watchPending && HasOverlappedIoCompleted(&watching)) {
      watchPending = false;
      ResetEvent(watching.hEvent);
      DWORD modem = 0;
      if (!GetOverlappedResult(port, &watching, &done, FALSE) ||
          !GetCommModemStatus(port, &modem))
        fail();
      else
        cts_ = (modem & MS_CTS_ON) != 0;
    }
  }

  // The buffers and events above belong to the driver until each operation
  // has finished, cancelled or not.
  CancelIoEx(port, nullptr);
  DWORD done = 0;
  if (readPending) GetOverlappedResult(port, &reading, &done, TRUE);
  if (writePending) GetOverlappedResult(port, &writing, &done, TRUE);
  if (watchPending) GetOverlappedResult(port, &watching, &done, TRUE);
  CloseHandle(reading.hEvent);
  CloseHandle(writing.hEvent);
  CloseHandle(watching.hEvent);
  if (!lost) return;
  // Let go of a port that is gone, so that the adapter can come back under
  // the same name; Close() finds nothing left to close.
  CloseHandle(port);
  port_ = nullptr;
  cts_ = false;
  open_ = false;
  Notify(Event::kLost, ErrorText(why));
}

std::vector<std::wstring> ListComPorts() {
  std::vector<std::wstring> ports;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ,
                    &key) != ERROR_SUCCESS)
    return ports;
  for (DWORD index = 0;; ++index) {
    wchar_t name[256];
    DWORD nameSize = 256;
    wchar_t data[256];
    DWORD dataSize = sizeof data;
    DWORD type = 0;
    const LSTATUS status = RegEnumValueW(key, index, name, &nameSize, nullptr, &type,
                                         reinterpret_cast<BYTE*>(data), &dataSize);
    if (status == ERROR_NO_MORE_ITEMS) break;
    if (status != ERROR_SUCCESS || type != REG_SZ) continue;
    std::wstring port(data, dataSize / sizeof(wchar_t));
    while (!port.empty() && port.back() == L'\0') port.pop_back();
    if (!port.empty()) ports.push_back(port);
  }
  RegCloseKey(key);
  // By the number, so that COM10 does not land between COM1 and COM2: the
  // shorter of two names with the same letters has the smaller number.
  std::sort(ports.begin(), ports.end(), [](const std::wstring& a, const std::wstring& b) {
    const std::wstring_view digits = L"0123456789";
    const std::size_t aStem = a.find_last_not_of(digits) + 1;
    const std::size_t bStem = b.find_last_not_of(digits) + 1;
    if (a.compare(0, aStem, b, 0, bStem) != 0) return a < b;
    if (a.size() - aStem != b.size() - bStem) return a.size() - aStem < b.size() - bStem;
    return a < b;
  });
  return ports;
}
