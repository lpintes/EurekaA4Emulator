#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "audio_player.h"
#include "machine.h"
#include "text_codec.h"

namespace fs = std::filesystem;

namespace {

// std::this_thread::sleep_for is no use for pacing this loop: on MinGW it
// always waits a whole 15.6 ms system tick whatever it is asked for, and
// timeBeginPeriod does not change that (it only fixes ::Sleep).  Measured, the
// two millisecond sleep below ran the main loop at 63 Hz, not 500 -- which set
// the keyboard poll rate and left the audio queue sitting at 55 ms.  A high
// resolution waitable timer honours the request without raising the timer
// resolution for every other process on the machine.
class PreciseTimer {
 public:
  PreciseTimer() {
    handle_ = CreateWaitableTimerExW(nullptr, nullptr,
                                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
    // Before Windows 10 1803 the flag is rejected; a plain timer then rounds
    // to the tick exactly as ::Sleep would, which is the old behaviour.
    if (!handle_)
      handle_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
  }
  ~PreciseTimer() { if (handle_) CloseHandle(handle_); }
  PreciseTimer(const PreciseTimer&) = delete;
  PreciseTimer& operator=(const PreciseTimer&) = delete;

  void Wait(double milliseconds) const {
    if (!handle_) {
      ::Sleep(static_cast<DWORD>(milliseconds));
      return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(milliseconds * 10000.0);
    if (!SetWaitableTimer(handle_, &due, 0, nullptr, nullptr, FALSE)) {
      ::Sleep(static_cast<DWORD>(milliseconds));
      return;
    }
    WaitForSingleObject(handle_, INFINITE);
  }

 private:
  HANDLE handle_ = nullptr;
};

// Sound is this machine's whole user interface, so the emulated clock is
// steered by the audio buffer rather than left at whatever depth the device
// happened to start with.  Twenty two is the lowest target the loop still
// tracks to within about two milliseconds; below twenty the driver's own
// buffering becomes the floor, the controller pushes against it and
// overshoots instead, and the spread measured 10-48 ms.  Measured here at 22:
// 24 ms average while speaking, never closer than 16 ms to running dry.
constexpr double kTargetLatencyMs = 22.0;

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

// The ROM lives outside the repository, so it is rarely next to the EXE.
// A4ROM is the variable the project's tools already use.
std::vector<fs::path> RomCandidates() {
  std::vector<fs::path> candidates;
  const auto add = [&candidates](const fs::path& candidate) {
    if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
      candidates.push_back(candidate);
  };
  wchar_t buffer[32768];
  const DWORD length = GetEnvironmentVariableW(L"A4ROM", buffer, 32768);
  if (length > 0 && length < 32768) add(fs::path(std::wstring(buffer, length)));
  add(ExecutableDirectory() / L"A4ROM.DMP");
  add(ExecutableDirectory().parent_path() / L"A4ROM.DMP");
  std::error_code ec;
  const fs::path working = fs::current_path(ec);
  if (!ec) add(working / L"A4ROM.DMP");
  return candidates;
}

// Launched from a file manager, this process owns its console window, and the
// window dies with it -- taking any error message with it.  A blind user gets
// no chance at all to read what went wrong, so hold the window until a key.
bool OwnsConsole() {
  DWORD pids[4]{};
  return GetConsoleProcessList(pids, 4) == 1;
}

void HoldConsole() {
  if (!OwnsConsole()) return;
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode)) return;
  Print(L"\r\nStlačte ľubovoľný kláves...\r\n");
  SetConsoleMode(input, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
  FlushConsoleInputBuffer(input);
  for (;;) {
    INPUT_RECORD record{};
    DWORD read = 0;
    if (!ReadConsoleInputW(input, &record, 1, &read) || !read) break;
    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) break;
  }
  SetConsoleMode(input, mode);
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
      // KB.H and KB.LIB disagree about these two.  The ROM settles it: its
      // own table for the PC keyboard maps scan code 52h, Insert, to 8Dh and
      // 53h, Delete, to 8Eh (DF05), which is what KB.LIB says.
      case VK_INSERT: code = 0x8d; break;
      case VK_DELETE: code = 0x8e; break;
      default: return 0;
    }
  }
  if (shift) code |= 0x10;
  if (alt) code |= 0x20;
  return code;
}

// How the host keyboard is presented to the machine.  The Eureka had exactly
// two keyboards -- the braille one built in and the optional PC one on the
// serial port -- and so has the emulator.  There used to be a third, "default",
// which handed text straight to the ROM's own queue; it was a shortcut past the
// hardware and it is gone.  Everything the host types now arrives the way it
// would have arrived on a real machine.
enum class InputMode {
  kBraille,  // the six dot keys, chorded
  kPc,       // the optional IBM PC keyboard on the serial port
};

// Perkins entry on a QWERTY keyboard: the six dot keys under the fingers,
// pressed as a chord.  Nothing here turns dots into letters -- the machine
// does that itself from the pattern on its row 0, in whichever of its three
// tables is currently selected, so this only presses keys.
struct HostKeyboard {
  InputMode mode = InputMode::kPc;
  uint8_t held = 0;   // dot keys physically down at this moment
  uint8_t chord = 0;  // every dot pressed since the current chord began
  bool chordShift = false;  // shift held at any point during that chord
  uint8_t arrows = 0;       // cursor keys physically down at this moment
  uint8_t arrowChord = 0;   // every cursor key pressed since the first went down
  ULONGLONG arrowStamp = 0; // host time of the last cursor key event
  uint8_t mods = 0;         // modifiers the machine is being told are held
};

// The four cursor keys are one keypad, not four keys: the ROM reads them as a
// bit set on row 8Ch, so several held at once mean a chord of their own.  That
// is why Home is up plus left (85h) and why all four together are a command --
// 8Fh, k_udlr in KB.LIB, the one that switches the Eureka off from the Main
// Menu (CP 8Fh at 18154).
uint8_t ArrowBit(WORD virtualKey) {
  switch (virtualKey) {
    case VK_UP: return 1;
    case VK_DOWN: return 2;
    case VK_LEFT: return 4;
    case VK_RIGHT: return 8;
    default: return 0;
  }
}

// A key-up can go missing: let the console window lose focus mid-press and the
// release is delivered to whoever took the focus.  A cursor key left believed
// down would then suppress every later single one -- and on this machine a key
// that does nothing is indistinguishable from a key that never arrived, because
// both are silence.  Windows repeats a held key about thirty times a second, so
// anything not heard from for two seconds is not under a finger; two seconds is
// also well above the longest first-repeat delay Windows offers, so a slowly
// assembled chord is never mistaken for a stale one.
void ForgetStaleArrows(HostKeyboard& host) {
  const ULONGLONG now = GetTickCount64();
  if (now - host.arrowStamp > 2000) host.arrows = host.arrowChord = 0;
  host.arrowStamp = now;
}

// Which modifiers the ROM is currently being told are held, one bit each so
// the difference against what Windows reports is a single XOR.
enum : uint8_t {
  kModShift = 1,
  kModCtrl = 2,
  kModAlt = 4,    // left Alt
  kModAltGr = 8,  // right Alt, the one that selects the DF98 table
};

// The modifiers are not forwarded as key events at all.  Windows reports the
// whole modifier state on every single record, and that report is the only
// thing here worth trusting: measured 26. 8. 2026, the release of AltGr
// arrives with ENHANCED_KEY already clear, so a break built from the event
// alone goes out as a plain 38h -- which clears the *left* Alt bit in C670h
// and leaves the right one set for good.  The machine then reads every key
// through the AltGr table until it is reset.
//
// Reconciling against the reported state also survives a key-up lost to a
// focus change, which is the same trap ForgetStaleArrows exists for on the
// cursor keypad.
uint8_t WantedModifiers(DWORD state) {
  uint8_t mods = 0;
  if ((state & SHIFT_PRESSED) != 0) mods |= kModShift;
  if ((state & LEFT_ALT_PRESSED) != 0) mods |= kModAlt;
  if ((state & RIGHT_ALT_PRESSED) != 0) mods |= kModAltGr;
  // Windows builds AltGr out of left Ctrl plus right Alt on every layout that
  // has one, and reports both.  A real PC keyboard sends only the right Alt,
  // so that Ctrl is a host artifact and must not reach the wire: with it held
  // the ROM masks every character above 3Fh to a control code at 1DE16, and
  // AltGr+2 arrives as 00h instead of '@'.  Measured both ways.
  if ((mods & kModAltGr) == 0 &&
      (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0)
    mods |= kModCtrl;
  return mods;
}

void SendModifier(EurekaMachine& machine, uint8_t mod, bool down) {
  uint8_t code = 0;
  switch (mod) {
    case kModShift: code = 0x2a; break;
    case kModCtrl: code = 0x1d; break;
    case kModAlt: code = 0x38; break;
    // The right Alt is the one extended key among them, so it needs the E0
    // prefix on the break as much as on the make -- DFD6 lists both 38h and
    // B8h, and only behind an E0 does the decoder ever look there (1DD48).
    case kModAltGr: machine.QueueScanCode(0xe0); code = 0x38; break;
    default: return;
  }
  machine.QueueScanCode(down ? code : static_cast<uint8_t>(code | 0x80));
}

// Brings the machine's idea of the modifiers in line with the host's.  Called
// on every key event in PC mode, and with nothing held on the way out of it:
// leaving a modifier down there would leave it down for good.
void SyncModifiers(EurekaMachine& machine, HostKeyboard& host, uint8_t wanted) {
  // Let go before pressing, so that on the way from one Alt to the other the
  // machine never has both of them down at once.
  for (uint8_t mod : {kModShift, kModCtrl, kModAlt, kModAltGr})
    if ((host.mods & mod) != 0 && (wanted & mod) == 0)
      SendModifier(machine, mod, false);
  for (uint8_t mod : {kModShift, kModCtrl, kModAlt, kModAltGr})
    if ((host.mods & mod) == 0 && (wanted & mod) != 0)
      SendModifier(machine, mod, true);
  host.mods = wanted;
}

// The row bits run in Perkins key order, left to right, not in dot number
// order: bit 0 is dot 3 and bit 2 is dot 1.  Under the hands that is exactly
// the natural layout, F D S going outwards on the left and J K L on the right.
uint8_t BrailleBit(WORD virtualKey) {
  switch (virtualKey) {
    case 'F': return 0x04;       // dot 1
    case 'D': return 0x02;       // dot 2
    case 'S': return 0x01;       // dot 3
    case 'J': return 0x08;       // dot 4
    case 'K': return 0x10;       // dot 5
    case 'L': return 0x20;       // dot 6
    case VK_SPACE: return 0x80;  // the space bar, which is also the ALT key
    default: return 0;
  }
}

// Forwards one host key event to the machine's serial keyboard.  Windows
// already hands us an XT set 1 scan code in wVirtualScanCode, and the grey
// keys are the same code with the enhanced flag, which on the wire is an E0h
// prefix -- so this is a wire, not a translation table.  Everything else, the
// Czech QWERTZ layout at DF05 included, is the ROM's own work.
void SendScanCode(EurekaMachine& machine, const KEY_EVENT_RECORD& key) {
  const uint8_t code = static_cast<uint8_t>(key.wVirtualScanCode);
  if (code == 0 || code >= 0x80) return;
  // SyncModifiers owns these, from the state Windows reports rather than from
  // records that can arrive without their ENHANCED_KEY flag.
  if (code == 0x2a || code == 0x36 || code == 0x1d || code == 0x38) return;
  if ((key.dwControlKeyState & ENHANCED_KEY) != 0) machine.QueueScanCode(0xe0);
  machine.QueueScanCode(key.bKeyDown ? code : static_cast<uint8_t>(code | 0x80));
}

const wchar_t* ModeName(InputMode mode) {
  return mode == InputMode::kBraille ? L"braillovská" : L"externá PC";
}

// With --diag, every key event is echoed with what the emulator made of it.
// A keyboard that does nothing is otherwise impossible to tell apart from a
// key that never arrived: both are silence.
void TraceKey(const KEY_EVENT_RECORD& key, InputMode mode, const wchar_t* took) {
  wchar_t line[200];
  swprintf(line, 200,
           L"[kláves %ls vk=%02X sc=%02X stav=%04X režim=%ls -> %ls]\r\n",
           key.bKeyDown ? L"dole" : L"hore",
           static_cast<unsigned>(key.wVirtualKeyCode),
           static_cast<unsigned>(key.wVirtualScanCode),
           static_cast<unsigned>(key.dwControlKeyState), ModeName(mode), took);
  Print(line);
}

bool PumpKeyboard(EurekaMachine& machine, HostKeyboard& host, bool& reset,
                  bool& dump, bool trace) {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD available = 0;
  if (input == INVALID_HANDLE_VALUE || !GetNumberOfConsoleInputEvents(input, &available))
    return true;
  while (available-- > 0) {
    INPUT_RECORD record{};
    DWORD read = 0;
    if (!ReadConsoleInputW(input, &record, 1, &read) || !read) break;
    if (record.EventType != KEY_EVENT) continue;
    const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
    if (!key.bKeyDown) {
      if (host.mode == InputMode::kPc) {
        if (trace) TraceKey(key, host.mode, L"scan kód");
        SyncModifiers(machine, host, WantedModifiers(key.dwControlKeyState));
        SendScanCode(machine, key);
        continue;
      }
      // A braille chord is finished by letting go, not by pressing: the dots
      // go down one at a time and only the whole pattern means anything, so
      // it is sent when the last finger comes up.
      if (const uint8_t dot = BrailleBit(key.wVirtualKeyCode)) {
        host.held &= static_cast<uint8_t>(~dot);
        // Shift belongs to the chord and is collected the same way the dots
        // are, on every event of it: let go of shift before the last dot comes
        // up and this final record no longer carries it.
        if ((key.dwControlKeyState & SHIFT_PRESSED) != 0) host.chordShift = true;
        if (host.held == 0 && host.chord != 0) {
          machine.PressBraille(host.chord, host.chordShift);
          host.chord = 0;
          host.chordShift = false;
        }
        continue;
      }
      if (const uint8_t arrow = ArrowBit(key.wVirtualKeyCode)) {
        ForgetStaleArrows(host);
        host.arrows &= static_cast<uint8_t>(~arrow);
        if (host.arrows != 0) continue;  // fingers still on the keypad
        const uint8_t chord = host.arrowChord;
        host.arrowChord = 0;
        // chord == 0 means ForgetStaleArrows just wiped the set, so this is a
        // lone key however it got here.
        if (chord == arrow || chord == 0) {
          machine.ReleaseKey(SpecialKey(key));
        } else {
          // More than one cursor key was down: send the union, keeping only
          // the modifier bits of the code the single keys would have used.
          machine.QueueKey(
              static_cast<uint8_t>(0x80 | chord | (SpecialKey(key) & 0x30)));
        }
        continue;
      }
      // Only the twenty-key keyboard has a released state worth reporting;
      // text goes into a queue and has nothing to let go of.
      if (const uint8_t special = SpecialKey(key)) machine.ReleaseKey(special);
      continue;
    }
    // The same reading the machine gets, so an emulator shortcut can never be
    // triggered by the left Ctrl that Windows pairs with AltGr: without this
    // AltGr+K switched the keyboard instead of typing.
    const uint8_t wanted = WantedModifiers(key.dwControlKeyState);
    const bool ctrl = (wanted & kModCtrl) != 0;
    const bool shift = (wanted & kModShift) != 0;
    if (trace)
      TraceKey(key, host.mode,
               ctrl && shift ? L"skratka emulátora?" : L"do Eureky");
    if (ctrl && shift && key.wVirtualKeyCode == 'Q') return false;
    if (ctrl && shift && key.wVirtualKeyCode == 'R') {
      reset = true;
      continue;
    }
    if (ctrl && shift && key.wVirtualKeyCode == 'D') {
      dump = true;
      continue;
    }
    // One key for the one thing there is to choose now that there are two
    // keyboards instead of three.  Ctrl+K and not Ctrl+Shift+K, which is the
    // shape of every other shortcut here: the owner picked it, knowing that
    // the ROM does honour Ctrl+letter on the PC keyboard -- the decoder masks
    // it to a control code at 1DE0E -- so the guest loses 0Bh in that mode.
    // No ROM application was found to react to it.
    if (ctrl && key.wVirtualKeyCode == 'K') {
      if (host.mode == InputMode::kPc) SyncModifiers(machine, host, 0);
      host.mode = host.mode == InputMode::kPc ? InputMode::kBraille
                                              : InputMode::kPc;
      host.held = host.chord = host.arrows = host.arrowChord = 0;
      host.chordShift = false;
      Print(host.mode == InputMode::kBraille
                ? L"\r\n[Braillovská klávesnica: F D S sú body 1 2 3, "
                  L"J K L body 4 5 6, medzerník je medzerník; shift robí "
                  L"veľké písmeno a so samotným medzerníkom je Escape. "
                  L"Písmená sa nepíšu, píše sa bodmi, tak ako na stroji]\r\n"
                : L"\r\n[Externá klávesnica PC: píše sa po českej "
                  L"klávesnici, ako keby bola pripojená k Eureke]\r\n");
      continue;
    }
    if (host.mode == InputMode::kPc) {
      SyncModifiers(machine, host, wanted);
      SendScanCode(machine, key);
      continue;
    }
    // Auto-repeat resends key-down without a key-up, so a dot already in the
    // chord must not count as a second finger.
    // Ctrl is not a key on this keyboard, so Ctrl+letter is not a dot either;
    // leaving it out also keeps the emulator's own shortcuts out of the chord.
    if (const uint8_t dot = ctrl ? 0 : BrailleBit(key.wVirtualKeyCode)) {
      host.held |= dot;
      host.chord |= dot;
      if (shift) host.chordShift = true;
      continue;
    }
    if (const uint8_t arrow = ArrowBit(key.wVirtualKeyCode)) {
      ForgetStaleArrows(host);
      host.arrows |= arrow;
      host.arrowChord |= arrow;
      // One cursor key on its own goes down straight away, so navigation stays
      // immediate and the ROM's own typematic keeps repeating it.  A second
      // finger landing on top of it makes a chord, and a chord only means
      // something whole, so it waits for the last finger to come up -- the way
      // a braille chord does.  The single key that already went is not a bug:
      // fingers landing more than one scan apart look exactly like that to the
      // real machine too.
      if (host.arrows == arrow) machine.QueueKey(SpecialKey(key));
      continue;
    }
    if (const uint8_t special = SpecialKey(key)) {
      machine.QueueKey(special);
      continue;
    }
    // Nothing else on a braille keyboard is a key.  Text used to be handed
    // straight to the ROM's queue from here, which no machine could do; who
    // wants to write in this mode writes dots, as on the machine.  Saying so
    // matters more here than anywhere else: on this machine a key that does
    // nothing is indistinguishable from a key that never arrived, because both
    // are silence.
    if (trace)
      TraceKey(key, host.mode,
               L"ignorované, braillovská klávesnica text nepíše — píšte bodmi");
  }
  return true;
}

// Slovak counts in three shapes -- 1 položku, 2 položky, 5 položiek -- and a
// screen reader speaks the ending rather than letting the eye skip it.
std::wstring CountItems(std::size_t number) {
  const wchar_t* word = number == 1 ? L"položku"
                        : (number >= 2 && number <= 4 ? L"položky" : L"položiek");
  return std::to_wstring(number) + L" " + word;
}

void PrintUsage() {
  Print(L"Eureka A4 Emulator\r\n\r\n"
        L"Použitie: EurekaA4Emulator.exe [--rom A4ROM.DMP] [--disk PRIECINOK]\r\n"
        L"                              [--ram-disk] [--no-disk] [--diag]\r\n"
        L"                              [--braille] [--pc]\r\n"
        L"Ak --disk vynecháte, zobrazí sa výber priečinka; jeho zrušením sa\r\n"
        L"Eureka spustí bez diskety.\r\n"
        L"--ram-disk dá prázdnu disketu, ktorá žije len v pamäti; pri ukončení\r\n"
        L"sa emulátor spýta, či ju uložiť do priečinka.\r\n"
        L"--no-disk spustí Eureku bez diskety a bez pýtania.\r\n"
        L"ROM sa hľadá v premennej A4ROM, vedľa EXE, o úroveň vyššie a\r\n"
        L"v aktuálnom priečinku.\r\n"
        L"Štartuje sa v režime externej klávesnice PC; --braille štartuje\r\n"
        L"rovno v braillovskom, keby skratku Ctrl+K žral terminál. --pc je\r\n"
        L"odvtedy len výslovné potvrdenie predvoľby.\r\n"
        L"--diag zapne záznam zahodených zápisov, portov bez modelu a zmien\r\n"
        L"riadiacich latchov, a k tomu záznam každej klávesovej udalosti.\r\n"
        L"Výpis: Ctrl+Shift+D, aj pri ukončení.\r\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetConsoleTitleW(L"Eureka A4 Emulator");
  SetConsoleOutputCP(CP_UTF8);

  fs::path rom;
  fs::path disk;
  bool ramDisk = false;
  bool noDisk = false;
  bool diagnostics = false;
  // Starting straight in a mode, without the shortcut.  A console host that
  // keeps Ctrl+K for itself is otherwise indistinguishable from a mode that
  // does not work, and both look like silence.
  InputMode startMode = InputMode::kPc;
  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    if ((argument == L"--help" || argument == L"-h")) {
      PrintUsage();
      HoldConsole();
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
    if (argument == L"--pc") {
      startMode = InputMode::kPc;
      continue;
    }
    if (argument == L"--braille") {
      startMode = InputMode::kBraille;
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
    HoldConsole();
    CoUninitialize();
    return 2;
  }
  // Cancelling the picker is a choice, not an error: a real A4 runs perfectly
  // well with an empty drive and says so when a disk function is asked for.
  if (disk.empty() && !ramDisk && !noDisk) disk = PickDiskFolder();

  std::vector<fs::path> tried;
  if (rom.empty()) {
    std::error_code ec;
    for (const fs::path& candidate : RomCandidates()) {
      tried.push_back(candidate);
      if (fs::is_regular_file(candidate, ec)) {
        rom = candidate;
        break;
      }
    }
  }

  auto machine = std::make_unique<EurekaMachine>();
  std::wstring error;
  if (rom.empty() || !machine->LoadRom(rom, error)) {
    if (rom.empty()) {
      error = L"Nenašiel som ROM. Hľadal som tu:\r\n";
      for (const fs::path& candidate : tried) error += L"  " + candidate.wstring() + L"\r\n";
      error += L"Zadajte ju cez --rom CESTA alebo nastavte premennú A4ROM.";
    }
    Print(L"Chyba: " + error + L"\r\n");
    HoldConsole();
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
      HoldConsole();
      CoUninitialize();
      return 1;
    }
    diskDescription = disk.wstring();
    // A subfolder cannot go on a CP/M diskette.  Said out loud, because from
    // inside the machine an absent file looks exactly like a lost one.
    const std::vector<std::wstring>& skipped = machine->disk().skipped_entries();
    if (!skipped.empty()) {
      std::wstring list;
      for (const std::wstring& leaf : skipped) {
        if (!list.empty()) list += L", ";
        list += leaf;
      }
      Print(L"Upozornenie: " + CountItems(skipped.size()) +
            L" som na disketu nedal, Eureka nepozná podpriečinky: " + list +
            L"\r\n");
    }
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

  HostKeyboard host;
  host.mode = startMode;
  Print(L"Eureka A4 je zapnutá. Disk: " + diskDescription + L"\r\n"
        L"Klávesy Windows sa posielajú do Eureky; F1-F10 a kurzory fungujú "
        L"vrátane Shift/Alt.\r\n"
        L"F9 je režim, F10 povie, kde ste; Shift+F9 stav batérie, "
        L"Shift+F10 sebekontrolu.\r\n"
        L"Píše sa v režime " + ModeName(host.mode) +
        L". Ctrl+K prepína medzi externou klávesnicou PC\r\n"
        L"a braillovskou (F D S J K L); braillovská píše len bodmi, tak ako "
        L"stroj.\r\n"
        L"Ak skratku žerie terminál, dá sa štartovať aj s --braille.\r\n"
        L"Shift+F7 spustí program z disku. Ctrl+Shift+R resetuje, "
        L"Ctrl+Shift+Q uloží disk a skončí.\r\n"
        L"Eureku vypnete tak ako naozaj: v hlavnom menu podržte všetky štyri "
        L"kurzorové klávesy naraz.\r\n"
        L"Vypne sa aj sama po piatich minútach nečinnosti, tridsať sekúnd "
        L"vopred to ohlási tónmi.\r\n\r\n");

  using Clock = std::chrono::steady_clock;
  PreciseTimer timer;
  auto lastTick = Clock::now();
  // Guest cycles the wall clock has earned so far.  Fractional because the
  // rate is nudged by a couple of percent to steer the audio buffer.
  long double guestClock = 0.0L;
  bool running = true;
  while (running) {
    bool reset = false;
    bool dump = false;
    running = PumpKeyboard(*machine, host, reset, dump, diagnostics);
    if (!running) break;
    if (dump) {
      Print(std::wstring(L"\r\n[Režim písania: ") + ModeName(host.mode) +
            L"]\r\n");
      Print(diagnostics
                ? machine->diagnostics().Report()
                : std::wstring(L"\r\nDiagnostika je vypnutá; spustite s "
                               L"--diag.\r\n"));
    }
    if (reset) {
      machine->Reset();
      // The chosen writing mode is the user's, not the machine's, so it
      // survives; a chord caught half-pressed does not.
      host.held = host.chord = host.arrows = host.arrowChord = 0;
      audio.Close();
      audio.Open(EurekaMachine::kAudioHz);
      lastTick = Clock::now();
      guestClock = 0.0L;
      Print(L"\r\n[Eureka bola resetovaná]\r\n");
    }

    const auto now = Clock::now();
    double delta = std::chrono::duration<double>(now - lastTick).count();
    lastTick = now;
    // A scheduling stall is the host's problem, not the guest's; letting one
    // through unclamped would turn into a sprint through the speech engine.
    if (delta > 0.25) delta = 0.25;

    // Slewing the emulated clock by at most two percent is how the audio
    // buffer is held at kTargetLatencyMs.  It is the right knob rather than
    // dropping or repeating samples: the DAC output stays coherent, and two
    // percent of 6.144 MHz is a third of a semitone nobody can hear.  Without
    // it nothing ever drains the queue the device started out with, because
    // producer and consumer both run at exactly real time.
    double rate = static_cast<double>(EurekaMachine::kCpuHz);
    if (audio.Ready()) {
      const double error = audio.QueuedMs() - kTargetLatencyMs;
      rate *= 1.0 - std::clamp(error * 0.002, -0.02, 0.02);
    }
    guestClock += static_cast<long double>(rate) * delta;

    const uint64_t target = static_cast<uint64_t>(guestClock);
    // The CPU is never parked now, so it always runs the cycles the wall clock
    // has earned: the ROM waits for a key by spinning in its own dispatcher,
    // and the DAC and the filter behind it are fed by that spinning exactly as
    // they are on the hardware.  RenderIdle used to paper over the gap and has
    // no gap left to paper over.
    unsigned steps = 0;
    while (machine->cycles() < target && steps++ < 200000)
      if (!machine->Step()) break;  // switched itself off
    // If the host could not keep up after all, forgive the debt rather than
    // carry it: catching up faster than real time would garble the speech.
    const uint64_t done = machine->cycles();
    const long double ceiling =
        static_cast<long double>(done) +
        static_cast<long double>(EurekaMachine::kCpuHz) / 4.0L;
    if (guestClock > ceiling) guestClock = ceiling;

    auto output = machine->TakeConsoleOutput();
    if (!output.empty()) Print(DecodeKamenicky(output.data(), output.size()));
    audio.Submit(machine->TakeAudio());

    // The machine switched itself off: the four cursor keys from the Main Menu,
    // or five minutes of nobody touching it.  It says "konec" first, and that
    // announcement is still inside the sound device when the strobe fires --
    // AudioPlayer::Close() calls waveOutReset and would throw it away -- so
    // wait for it to play out before leaving the loop.  Two seconds is a
    // ceiling for a device that stops reporting progress, not a pause.
    if (machine->powered_off()) {
      const auto until = Clock::now() + std::chrono::seconds(2);
      while (audio.Ready() && audio.QueuedMs() > 1.0 && Clock::now() < until)
        timer.Wait(5.0);
      // C45Ah is FFh here: the firmware's own marker that this was a clean
      // power-down, read back at 180CB so the machine resumes where the user
      // was instead of initialising.  Once the host keeps RAM across runs, it
      // is this byte that makes the difference between switching on and a
      // hard reset -- which is what Ctrl+Shift+R does today.
      Print(L"\r\n[Eureka sa vypla. Na skutočnom stroji by RAM aj hodiny "
            L"zostali pod napätím a ďalšie zapnutie by pokračovalo tam, kde "
            L"ste skončili.]\r\n");
      running = false;
      break;
    }

    // Written back once the guest has finished with the disk, not on a clock:
    // an export taken mid-update would see a half-written directory.
    if (machine->DiskSettled() && !machine->FlushDisk(error)) {
      Print(L"\r\nChyba pri ukladaní disku: " + error + L"\r\n");
      running = false;
    }
    timer.Wait(2.0);
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
  Print(L"\r\nEmulátor skončil.\r\n");
  HoldConsole();
  CoUninitialize();
  return 0;
}
