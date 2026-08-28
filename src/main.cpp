// Start-up: read the command line, get a ROM and a disk, put a window on the
// screen and hand the machine to its own thread.  Everything that used to run
// here -- the pacing loop, the keyboard, the audio -- lives in
// emulator_thread.cpp now, because a window cannot host a loop like that: a
// dropped-down menu or a modal dialog spins its own message loop and would
// have stopped the machine mid-word.
//
// This is a GUI subsystem program, so nothing here may report through the
// console: there is not one unless the user asked for it.  Whatever the user
// has to be told goes in a message box, which a screen reader announces and
// reads; the console carries diagnostics only.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "emulator_thread.h"
#include "host_console.h"
#include "machine.h"
#include "main_window.h"
#include "settings.h"
#include "win/dialog.h"
#include "win/window.h"

namespace fs = std::filesystem;

namespace {

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

// Start-up failures happen before there is a window to report them in, and
// this program has no console to fall back on.  A message box is the one
// surface that always exists, and a screen reader announces it as a dialog and
// reads it out -- which the console never did on its own.
int Fail(const std::wstring& message) {
  MessageBoxW(nullptr, message.c_str(), L"Eureka A4", MB_OK | MB_ICONERROR);
  // Also to the shell it was started from, if there was one.  Safe to attach
  // here and nowhere else along this path: the process exits immediately
  // afterwards, so it cannot be what keeps somebody's console window alive.
  host::AttachToParentConsole();
  host::Print(L"Chyba: " + message + L"\r\n");
  return 1;
}

// For what the user has to know but has not gone wrong.  No attaching: this
// one is followed by a whole session, and holding a shell's console open for
// all of it is the thing being avoided.
void Warn(const std::wstring& message) {
  MessageBoxW(nullptr, message.c_str(), L"Eureka A4", MB_OK | MB_ICONWARNING);
  host::Print(L"Upozornenie: " + message + L"\r\n");
}

void PrintUsage() {
  host::Print(
      L"Eureka A4 Emulator\r\n\r\n"
      L"Použitie: EurekaA4Emulator.exe [--rom A4ROM.DMP] [--disk PRIECINOK]\r\n"
      L"                              [--ram-disk] [--no-disk] [--diag]\r\n"
      L"                              [--braille] [--pc]\r\n"
      L"Ak --disk vynecháte, vloží sa disketa z minulého spustenia. Pri\r\n"
      L"prvom spustení, keď si emulátor nemá čo pamätať, sa zobrazí výber\r\n"
      L"priečinka; jeho zrušením sa Eureka spustí bez diskety.\r\n"
      L"Zapamätaná disketa a ďalšie nastavenia sú v priečinku config vedľa\r\n"
      L"EXE, ak taký priečinok vytvoríte, inak v %APPDATA%\\EurekaA4.\r\n"
      L"--ram-disk dá prázdnu disketu, ktorá žije len v pamäti; pri ukončení\r\n"
      L"sa emulátor spýta, či ju uložiť do priečinka.\r\n"
      L"--no-disk spustí Eureku bez diskety a bez pýtania.\r\n"
      L"ROM sa hľadá v premennej A4ROM, vedľa EXE, o úroveň vyššie a\r\n"
      L"v aktuálnom priečinku.\r\n"
      L"Štartuje sa v režime externej klávesnice; --braille štartuje rovno\r\n"
      L"v braillovskom. Prepína sa aj za behu, cez F11, Ctrl+K alebo\r\n"
      L"v ponuke.\r\n"
      L"--diag zapne záznam zahodených zápisov, portov bez modelu a zmien\r\n"
      L"riadiacich latchov, a k tomu záznam každej klávesovej udalosti.\r\n"
      L"Záznam ide na konzolu; výpis je F11, Ctrl+D a aj pri ukončení.\r\n"
      L"Dá sa zapnúť aj za behu v Nastaveniach.\r\n"
      L"Konzola sa otvorí len s --diag alebo pri zapnutí diagnostiky; bez\r\n"
      L"nej má emulátor iba svoje okno.\r\n\r\n"
      L"Ponuku okna otvára F12 — nie Alt ani F10, tie patria Eureke.\r\n"
      L"Zoznam skratiek je v ponuke Pomocník alebo pod F11, Ctrl+H.\r\n");
}

// Holds the argv that CommandLineToArgvW allocates, so no path out of
// Run() has to remember to free it.
class CommandLine {
 public:
  CommandLine() { argv_ = CommandLineToArgvW(GetCommandLineW(), &argc_); }
  ~CommandLine() { if (argv_) LocalFree(argv_); }
  CommandLine(const CommandLine&) = delete;
  CommandLine& operator=(const CommandLine&) = delete;
  int count() const { return argv_ ? argc_ : 0; }
  const wchar_t* operator[](int index) const { return argv_[index]; }

 private:
  int argc_ = 0;
  wchar_t** argv_ = nullptr;
};

int Run() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  // Deliberately *not* attaching to the parent console here.  It would be free
  // and windowless, but a console outlives its shell for as long as anything
  // is still attached to it: launched from a batch file that then exits, the
  // shell's window would stay on screen, empty, for the whole session --
  // exactly the stray console this change is getting rid of.  Attaching is
  // done where output actually happens, in OpenConsole and in Fail.
  //
  // The manifest asks for Common Controls 6; without this the dialogs fall
  // back to the version 5 controls.
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);

  const CommandLine command;
  const int argc = command.count();

  fs::path rom;
  fs::path disk;
  bool ramDisk = false;
  bool noDisk = false;
  bool diagnostics = false;
  // Starting straight in a mode, without the shortcut.
  InputMode startMode = InputMode::kPc;
  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = command[index];
    if (argument == L"--help" || argument == L"-h") {
      host::OpenConsole();
      PrintUsage();
      host::HoldConsole();
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
      fs::path value = command[++index];
      if (argument == L"--rom") rom = value;
      else disk = value;
      continue;
    }
    host::OpenConsole();
    host::Print(L"Neznámy parameter: " + argument + L"\r\n");
    PrintUsage();
    host::HoldConsole();
    CoUninitialize();
    return 2;
  }
  // The one thing that opens a console without being asked twice: the user
  // asked for diagnostics, so a window to read them in is the point.
  if (diagnostics) host::OpenConsole();
  Settings settings(Settings::FindFile());
  settings.Load();

  // Without --disk the emulator puts back the diskette it had last time, so a
  // normal start asks nothing at all.  The folder picker is left for the first
  // run, when there is nothing to put back.
  bool askForFolder = disk.empty() && !ramDisk && !noDisk;
  if (askForFolder && !settings.last_disk().empty()) {
    askForFolder = false;
    const fs::path remembered = settings.last_disk();
    std::error_code ec;
    if (fs::is_directory(remembered, ec)) {
      disk = remembered;
    } else if (MessageBoxW(
                   nullptr,
                   (L"Disketa z minulého spustenia sa nedá nájsť:\r\n\r\n" +
                    remembered.wstring() +
                    L"\r\n\r\nEureka sa spustí s prázdnou mechanikou. Má si "
                    L"emulátor tento priečinok pamätať aj naďalej? Ak je to "
                    L"odpojený disk, odpovedzte Áno.")
                       .c_str(),
                   L"Eureka A4", MB_YESNO | MB_ICONQUESTION) == IDNO) {
      // Forgetting it silently would throw away a setting the user made;
      // never forgetting it would mean this same box at every start with the
      // drive unplugged.  So it is their call, and it is asked once.
      settings.SetLastDisk(L"");
      std::wstring saveError;
      if (!settings.Save(saveError)) Warn(saveError);
    }
    // Either way the drive stays empty, which is a state the machine handles
    // and reports for itself (6.6).
  }
  // Cancelling the picker is a choice, not an error: a real A4 runs perfectly
  // well with an empty drive and says so when a disk function is asked for.
  if (askForFolder)
    disk = win::PickFolder(nullptr,
                           L"Vyberte priečinok, ktorý bude diskom Eureky A4");

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
      for (const fs::path& candidate : tried)
        error += L"  " + candidate.wstring() + L"\r\n";
      error += L"Zadajte ju cez --rom CESTA alebo nastavte premennú A4ROM.";
    }
    CoUninitialize();
    return Fail(error);
  }

  if (ramDisk) {
    machine->CreateRamDisk();
  } else if (!disk.empty()) {
    if (!machine->MountDisk(disk, error)) {
      CoUninitialize();
      return Fail(error);
    }
    // Subfolders are left out and nothing is said about it: a CP/M diskette has
    // no directories at all, so this is the rule the disk works by, not an
    // incident to report at every start.  It is in README instead.

    // Remembered as the canonical path the mount actually used, not as it was
    // typed.  Saved here rather than at exit, and reported if it fails: this
    // is the moment a diskette was chosen, so a setting that did not stick can
    // still be acted on.  At exit the same box would only be in the way.
    const std::wstring mounted = machine->disk().folder().wstring();
    if (settings.last_disk() != mounted) {
      settings.SetLastDisk(mounted);
      std::wstring saveError;
      if (!settings.Save(saveError)) Warn(saveError);
    }
  }
  machine->diagnostics().set_enabled(diagnostics);
  machine->Reset();

  // Described by the same function the worker uses when a diskette is swapped
  // in later, so the drive cannot be named one way at start-up and another way
  // afterwards.
  const DiskLabels labels = DescribeDisk(machine->disk());

  EmulatorThread emulator;
  MainWindow window(emulator, settings, rom.wstring(), labels.description,
                    labels.name, machine->disk().folder().wstring(),
                    machine->disk().present());
  if (!window.Create()) {
    CoUninitialize();
    return Fail(L"Okno emulátora sa nepodarilo vytvoriť.");
  }
  // Started only once the window exists: the worker posts its notifications to
  // that HWND, and one arriving before there is a window to take it would be
  // lost with no sign of it.
  emulator.Start(std::move(machine), window.handle(), startMode, diagnostics);
  window.Show(SW_SHOW);
  SetFocus(window.handle());

  win::RunMessageLoop(window.handle(), window.accelerators(),
                      window.hostAccelerators(),
                      [&window] { return window.HostShortcutsActive(); });

  // The machine comes back here to be shut down, so nothing below shares it
  // with a running thread.
  machine = emulator.Stop();
  if (machine && !machine->FlushDisk(error))
    MessageBoxW(nullptr, (L"Chyba pri ukladaní disku:\r\n\r\n" + error).c_str(),
                L"Eureka A4", MB_OK | MB_ICONERROR);

  // Asked about what is actually in the drive now, not about how the run
  // started: a diskette can be swapped mid-run, so --ram-disk no longer means
  // there is still a RAM diskette here -- and a folder-backed one needs no
  // offer, it is already on disk.
  if (machine && machine->disk().media() == VirtualDisk::Media::kRam &&
      machine->disk().StoredFiles() > 0) {
    const std::size_t stored = machine->disk().StoredFiles();
    const std::wstring question =
        L"Na diskete v pamäti " +
        (stored == 1 ? std::wstring(L"je 1 súbor")
                     : L"sú súbory (" + std::to_wstring(stored) + L")") +
        L".\r\n\r\nChcete ich uložiť do priečinka?";
    if (MessageBoxW(nullptr, question.c_str(), L"Eureka A4",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
      const std::wstring target = win::PickFolder(
          nullptr, L"Vyberte priečinok, do ktorého sa disketa uloží");
      if (target.empty())
        Warn(L"Ukladanie zrušené, obsah diskety sa stratí.");
      else if (!machine->ExportDisk(target, error))
        MessageBoxW(nullptr, (L"Chyba pri ukladaní:\r\n\r\n" + error).c_str(),
                    L"Eureka A4", MB_OK | MB_ICONERROR);
      else
        MessageBoxW(nullptr, (L"Disketa bola uložená do:\r\n\r\n" + target).c_str(),
                    L"Eureka A4", MB_OK | MB_ICONINFORMATION);
    }
  }
  if (machine && machine->diagnostics().enabled()) {
    host::Print(machine->diagnostics().Report());
    host::HoldConsole();
  }
  CoUninitialize();
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return Run(); }
