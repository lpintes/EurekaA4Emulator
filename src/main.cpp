#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
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

// How the host keyboard is presented to the machine.  The Eureka itself had
// two keyboards, the braille one built in and the PC one on the serial port,
// and the emulator can be either.  The third is not a keyboard at all: it is
// the convenience the emulator adds on top, and it is deliberately not called
// "the Eureka keyboard", because that name belongs to the braille one.
enum class InputMode {
  kDefault,  // the twenty keys, with text handed straight to the ROM's queue
  kBraille,  // the six dot keys, chorded
  kPc,       // the optional IBM PC keyboard on the serial port
};

// Perkins entry on a QWERTY keyboard: the six dot keys under the fingers,
// pressed as a chord.  Nothing here turns dots into letters -- the machine
// does that itself from the pattern on its row 0, in whichever of its three
// tables is currently selected, so this only presses keys.
struct HostKeyboard {
  InputMode mode = InputMode::kDefault;
  uint8_t held = 0;   // dot keys physically down at this moment
  uint8_t chord = 0;  // every dot pressed since the current chord began
};

// Leaving PC mode with a modifier down would leave it down for good: the ROM
// tracks shift, control and alt itself from make and break codes, and the
// break code would never arrive.  So let go of all of them on the way out.
void ReleaseModifiers(EurekaMachine& machine) {
  for (uint8_t code : {0x2a, 0x36, 0x1d, 0x38}) {
    machine.QueueScanCode(static_cast<uint8_t>(code | 0x80));
    if (code == 0x1d || code == 0x38) {
      machine.QueueScanCode(0xe0);
      machine.QueueScanCode(static_cast<uint8_t>(code | 0x80));
    }
  }
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
  if ((key.dwControlKeyState & ENHANCED_KEY) != 0) machine.QueueScanCode(0xe0);
  machine.QueueScanCode(key.bKeyDown ? code : static_cast<uint8_t>(code | 0x80));
}

const wchar_t* ModeName(InputMode mode) {
  switch (mode) {
    case InputMode::kBraille: return L"braillovská";
    case InputMode::kPc: return L"externá PC";
    default: return L"default";
  }
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
        SendScanCode(machine, key);
        continue;
      }
      // A braille chord is finished by letting go, not by pressing: the dots
      // go down one at a time and only the whole pattern means anything, so
      // it is sent when the last finger comes up.
      if (const uint8_t dot = host.mode == InputMode::kBraille
                                  ? BrailleBit(key.wVirtualKeyCode)
                                  : 0) {
        host.held &= static_cast<uint8_t>(~dot);
        if (host.held == 0 && host.chord != 0) {
          machine.PressBraille(host.chord);
          host.chord = 0;
        }
        continue;
      }
      // Only the twenty-key keyboard has a released state worth reporting;
      // text goes into a queue and has nothing to let go of.
      if (const uint8_t special = SpecialKey(key)) machine.ReleaseKey(special);
      continue;
    }
    const bool ctrl = (key.dwControlKeyState &
                       (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const bool shift = (key.dwControlKeyState & SHIFT_PRESSED) != 0;
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
    if (ctrl && shift && (key.wVirtualKeyCode == 'B' || key.wVirtualKeyCode == 'E')) {
      const InputMode wanted = key.wVirtualKeyCode == 'B' ? InputMode::kBraille
                                                          : InputMode::kPc;
      if (host.mode == InputMode::kPc) ReleaseModifiers(machine);
      host.mode = host.mode == wanted ? InputMode::kDefault : wanted;
      host.held = host.chord = 0;
      switch (host.mode) {
        case InputMode::kBraille:
          Print(L"\r\n[Braillovská klávesnica: F D S sú body 1 2 3, "
                L"J K L body 4 5 6, medzerník je medzerník]\r\n");
          break;
        case InputMode::kPc:
          Print(L"\r\n[Externá klávesnica PC: píše sa po českej klávesnici, "
                L"ako keby bola pripojená k Eureke]\r\n");
          break;
        case InputMode::kDefault:
          Print(L"\r\n[Späť na default]\r\n");
          break;
      }
      continue;
    }
    if (host.mode == InputMode::kPc) {
      SendScanCode(machine, key);
      continue;
    }
    // Auto-repeat resends key-down without a key-up, so a dot already in the
    // chord must not count as a second finger.
    if (const uint8_t dot = host.mode == InputMode::kBraille && !ctrl
                                ? BrailleBit(key.wVirtualKeyCode)
                                : 0) {
      host.held |= dot;
      host.chord |= dot;
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
        L"                              [--braille] [--pc]\r\n"
        L"Ak --disk vynecháte, zobrazí sa výber priečinka; jeho zrušením sa\r\n"
        L"Eureka spustí bez diskety.\r\n"
        L"--ram-disk dá prázdnu disketu, ktorá žije len v pamäti; pri ukončení\r\n"
        L"sa emulátor spýta, či ju uložiť do priečinka.\r\n"
        L"--no-disk spustí Eureku bez diskety a bez pýtania.\r\n"
        L"ROM sa hľadá v premennej A4ROM, vedľa EXE, o úroveň vyššie a\r\n"
        L"v aktuálnom priečinku.\r\n"
        L"--braille a --pc štartujú rovno v tom režime písania, keby skratky\r\n"
        L"Ctrl+Shift+B a Ctrl+Shift+E žral terminál.\r\n"
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
  // keeps Ctrl+Shift+E for itself is otherwise indistinguishable from a mode
  // that does not work, and both look like silence.
  InputMode startMode = InputMode::kDefault;
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
        L". Ctrl+Shift+B prepne na braillovskú klávesnicu\r\n"
        L"(F D S J K L), Ctrl+Shift+E na externú klávesnicu PC; tou istou "
        L"skratkou späť na default.\r\n"
        L"Ak skratky žerie terminál, dá sa štartovať aj s --braille alebo "
        L"--pc.\r\n"
        L"Shift+F7 spustí program z disku. Ctrl+Shift+R resetuje, "
        L"Ctrl+Shift+Q uloží disk a skončí.\r\n\r\n");

  using Clock = std::chrono::steady_clock;
  auto epoch = Clock::now();
  uint64_t epochCycles = machine->cycles();
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
      host.held = host.chord = 0;
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
  HoldConsole();
  CoUninitialize();
  return 0;
}
