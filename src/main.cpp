#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio_player.h"
#include "machine.h"
#include "text_codec.h"

namespace fs = std::filesystem;

namespace {

// Redirected output has no console to accept wide characters. The old fallback
// was std::wcout, which narrows through the "C" locale, fails on the first
// character it cannot represent and then swallows the rest -- the help text
// used to break off mid-word at "Pou". UTF-8 bytes match the
// SetConsoleOutputCP(CP_UTF8) in wmain and survive a pipe or a file unchanged.
void WriteAllBytes(HANDLE output, const std::string& bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written = 0;
    if (!WriteFile(output, bytes.data() + offset,
                   static_cast<DWORD>(bytes.size() - offset), &written,
                   nullptr) ||
        written == 0)
      return;
    offset += written;
  }
}

void Print(const std::wstring& text) {
  if (text.empty()) return;
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (output == nullptr || output == INVALID_HANDLE_VALUE) return;

  DWORD mode = 0;
  if (GetConsoleMode(output, &mode)) {
    size_t offset = 0;
    while (offset < text.size()) {
      DWORD written = 0;
      if (!WriteConsoleW(output, text.data() + offset,
                         static_cast<DWORD>(text.size() - offset), &written,
                         nullptr) ||
          written == 0)
        return;
      offset += written;
    }
    return;
  }

  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return;
  std::string bytes(static_cast<size_t>(size), 0);
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      bytes.data(), size, nullptr, nullptr);
  WriteAllBytes(output, bytes);
}

fs::path ExecutableDirectory() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
  buffer.resize(length);
  return fs::path(buffer).parent_path();
}

fs::path PickDiskFolder() {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) return {};
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                     FOS_PATHMUSTEXIST);
  dialog->SetTitle(L"Vyberte priečinok, ktorý bude diskom Eureky A4");
  fs::path result;
  if (SUCCEEDED(dialog->Show(nullptr))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return result;
}

// Asked only when the machine is already stopped, so a blocking read is safe.
// Returns false without waiting when there is no console to ask on.
bool AskYesNo(const std::wstring& question) {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode)) return false;
  Print(question);
  for (;;) {
    INPUT_RECORD record{};
    DWORD read = 0;
    if (!ReadConsoleInputW(input, &record, 1, &read) || !read) return false;
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
    const wchar_t ch = record.Event.KeyEvent.uChar.UnicodeChar;
    if (ch == L'a' || ch == L'A' || ch == L'y' || ch == L'Y') {
      Print(L"ano\r\n");
      return true;
    }
    if (ch == L'n' || ch == L'N' || ch == 0x1b) {
      Print(L"nie\r\n");
      return false;
    }
  }
}

uint8_t SpecialKey(const KEY_EVENT_RECORD& key) {
  const bool shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0;
  const bool alt = (key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
  uint8_t code = 0;
  if (key.wVirtualKeyCode >= VK_F1 && key.wVirtualKeyCode <= VK_F10)
    code = static_cast<uint8_t>(0xc0 + key.wVirtualKeyCode - VK_F1);
  else {
    switch (key.wVirtualKeyCode) {
      case VK_UP: code = 0x81; break;
      case VK_DOWN: code = 0x82; break;
      case VK_LEFT: code = 0x84; break;
      case VK_RIGHT: code = 0x88; break;
      case VK_HOME: code = 0x85; break;
      case VK_END: code = 0x86; break;
      case VK_PRIOR: code = 0x89; break;
      case VK_NEXT: code = 0x8a; break;
      case VK_INSERT: code = 0x8b; break;
      case VK_DELETE: code = 0x8c; break;
      default: return 0;
    }
  }
  if (shift) code |= 0x10;
  if (alt) code |= 0x20;
  return code;
}

bool PumpKeyboard(EurekaMachine& machine, bool& reset, bool& dump) {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD available = 0;
  if (input == INVALID_HANDLE_VALUE || !GetNumberOfConsoleInputEvents(input, &available))
    return true;
  while (available-- > 0) {
    INPUT_RECORD record{};
    DWORD read = 0;
    if (!ReadConsoleInputW(input, &record, 1, &read) || !read) break;
    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
    const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
    const bool ctrl = (key.dwControlKeyState &
                       (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0;
    if (ctrl && shift && key.wVirtualKeyCode == 'Q') return false;
    if (ctrl && shift && key.wVirtualKeyCode == 'R') {
      reset = true;
      continue;
    }
    if (ctrl && shift && key.wVirtualKeyCode == 'D') {
      dump = true;
      continue;
    }
    if (const uint8_t special = SpecialKey(key)) {
      machine.QueueKey(special);
      continue;
    }
    wchar_t ch = key.uChar.UnicodeChar;
    if (!ch) {
      switch (key.wVirtualKeyCode) {
        case VK_RETURN: ch = L'\r'; break;
        case VK_BACK: ch = L'\b'; break;
        case VK_TAB: ch = L'\t'; break;
        case VK_ESCAPE: ch = 0x1b; break;
        default: break;
      }
    }
    if (!ch && ctrl && key.wVirtualKeyCode >= 'A' && key.wVirtualKeyCode <= 'Z')
      ch = static_cast<wchar_t>(key.wVirtualKeyCode - 'A' + 1);
    if (!ch) continue;
    const auto encoded = EncodeKamenicky(std::wstring_view(&ch, 1));
    if (!encoded.empty()) machine.QueueKey(encoded.front());
  }
  return true;
}

void PrintUsage() {
  Print(L"Eureka A4 Emulator\r\n\r\n"
        L"Použitie: EurekaA4Emulator.exe [--rom A4ROM.DMP] [--disk PRIECINOK]\r\n"
        L"                              [--ram-disk] [--no-disk] [--diag]\r\n"
        L"Ak --disk vynecháte, zobrazí sa výber priečinka; jeho zrušením sa\r\n"
        L"Eureka spustí bez diskety.\r\n"
        L"--ram-disk dá prázdnu disketu, ktorá žije len v pamäti; pri ukončení\r\n"
        L"sa emulátor spýta, či ju uložiť do priečinka.\r\n"
        L"--no-disk spustí Eureku bez diskety a bez pýtania.\r\n"
        L"--diag zapne záznam zahodených zápisov, portov bez modelu a zmien\r\n"
        L"riadiacich latchov. Výpis: Ctrl+Shift+D, aj pri ukončení.\r\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetConsoleTitleW(L"Eureka A4 Emulator");
  SetConsoleOutputCP(CP_UTF8);

  fs::path rom = ExecutableDirectory() / L"A4ROM.DMP";
  fs::path disk;
  bool ramDisk = false;
  bool noDisk = false;
  bool diagnostics = false;
  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    if ((argument == L"--help" || argument == L"-h")) {
      PrintUsage();
      CoUninitialize();
      return 0;
    }
    if (argument == L"--diag") {
      diagnostics = true;
      continue;
    }
    if (argument == L"--ram-disk") {
      ramDisk = true;
      continue;
    }
    if (argument == L"--no-disk") {
      noDisk = true;
      continue;
    }
    if ((argument == L"--rom" || argument == L"--disk") && index + 1 < argc) {
      fs::path value = argv[++index];
      if (argument == L"--rom") rom = value;
      else disk = value;
      continue;
    }
    Print(L"Neznámy parameter: " + argument + L"\r\n");
    PrintUsage();
    CoUninitialize();
    return 2;
  }
  // Cancelling the picker is a choice, not an error: a real A4 runs perfectly
  // well with an empty drive and says so when a disk function is asked for.
  if (disk.empty() && !ramDisk && !noDisk) disk = PickDiskFolder();

  auto machine = std::make_unique<EurekaMachine>();
  std::wstring error;
  if (!machine->LoadRom(rom, error)) {
    Print(L"Chyba: " + error + L"\r\n");
    CoUninitialize();
    return 1;
  }
  std::wstring diskDescription = L"žiadna, mechanika je prázdna";
  if (ramDisk) {
    machine->CreateRamDisk();
    diskDescription = L"prázdna disketa v pamäti";
  } else if (!disk.empty()) {
    if (!machine->MountDisk(disk, error)) {
      Print(L"Chyba: " + error + L"\r\n");
      CoUninitialize();
      return 1;
    }
    diskDescription = disk.wstring();
  }
  machine->diagnostics().set_enabled(diagnostics);
  machine->Reset();

  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD oldMode = 0;
  if (input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &oldMode)) {
    SetConsoleMode(input, (oldMode | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS) &
                           ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                             ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE));
  }

  AudioPlayer audio;
  if (!audio.Open(EurekaMachine::kAudioHz))
    Print(L"Upozornenie: zvukové zariadenie sa nepodarilo otvoriť.\r\n");

  Print(L"Eureka A4 je zapnutá. Disk: " + diskDescription + L"\r\n"
        L"Klávesy Windows sa posielajú do Eureky; F1-F10 a kurzory fungujú "
        L"vrátane Shift/Alt.\r\n"
        L"Shift+F7 spustí program z disku. Ctrl+Shift+R resetuje, "
        L"Ctrl+Shift+Q uloží disk a skončí.\r\n\r\n");

  using Clock = std::chrono::steady_clock;
  auto epoch = Clock::now();
  uint64_t epochCycles = machine->cycles();
  bool running = true;
  while (running) {
    bool reset = false;
    bool dump = false;
    running = PumpKeyboard(*machine, reset, dump);
    if (!running) break;
    if (dump) {
      Print(diagnostics
                ? machine->diagnostics().Report()
                : std::wstring(L"\r\nDiagnostika je vypnutá; spustite s "
                               L"--diag.\r\n"));
    }
    if (reset) {
      machine->Reset();
      audio.Close();
      audio.Open(EurekaMachine::kAudioHz);
      epoch = Clock::now();
      epochCycles = 0;
      Print(L"\r\n[Eureka bola resetovaná]\r\n");
    }

    const auto now = Clock::now();
    const long double seconds = std::chrono::duration<long double>(now - epoch).count();
    const uint64_t target = epochCycles + static_cast<uint64_t>(
        seconds * static_cast<long double>(EurekaMachine::kCpuHz));
    unsigned steps = 0;
    bool blocked = false;
    while (machine->cycles() < target && steps++ < 200000) {
      if (!machine->Step()) {
        blocked = true;
        break;
      }
    }
    // A real Eureka's CPU waits at console input. Do not accumulate a wall
    // clock debt that would make the emulation race after the next keypress.
    if (blocked) {
      epoch = now;
      epochCycles = machine->cycles();
    }

    auto output = machine->TakeConsoleOutput();
    if (!output.empty()) Print(DecodeKamenicky(output.data(), output.size()));
    audio.Submit(machine->TakeAudio());

    // Written back once the guest has finished with the disk, not on a clock:
    // an export taken mid-update would see a half-written directory.
    if (machine->DiskSettled() && !machine->FlushDisk(error)) {
      Print(L"\r\nChyba pri ukladaní disku: " + error + L"\r\n");
      running = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  if (!machine->FlushDisk(error))
    Print(L"\r\nChyba pri ukladaní disku: " + error + L"\r\n");
  audio.Close();
  if (input != INVALID_HANDLE_VALUE && oldMode) SetConsoleMode(input, oldMode);

  if (ramDisk && machine->disk().StoredFiles() > 0) {
    const std::size_t stored = machine->disk().StoredFiles();
    if (AskYesNo(L"\r\nNa diskete v pamäti " +
                 (stored == 1 ? std::wstring(L"je 1 súbor")
                              : L"sú súbory (" + std::to_wstring(stored) + L")") +
                 L". Chcete ich uložiť do priečinka? (A/N) ")) {
      const fs::path target = PickDiskFolder();
      if (target.empty())
        Print(L"Ukladanie zrušené, obsah diskety sa stratí.\r\n");
      else if (!machine->ExportDisk(target, error))
        Print(L"Chyba pri ukladaní: " + error + L"\r\n");
      else
        Print(L"Uložené do " + target.wstring() + L"\r\n");
    }
  }
  if (diagnostics) Print(machine->diagnostics().Report());
  Print(L"\r\nEureka A4 bola vypnutá.\r\n");
  CoUninitialize();
  return 0;
}
