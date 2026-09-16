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

// Slovak counts in three shapes and the verb goes with them: "je 1 súbor",
// "sú 3 súbory", "je 5 súborov".  This line is read aloud by a screen reader,
// where a wrong ending is heard and not skimmed past -- the same reason
// virtual_disk.cpp bends its numbers rather than printing "súbory (3)".
std::wstring FileCount(std::size_t count) {
  const std::wstring number = std::to_wstring(count);
  if (count == 1) return L"je 1 súbor";
  if (count >= 2 && count <= 4) return L"sú " + number + L" súbory";
  return L"je " + number + L" súborov";
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
      L"--ram-disk dá prázdnu neuloženú disketu, teda takú, ktorá zatiaľ nemá\r\n"
      L"priečinok. Dá jej ho F11, Ctrl+U; pri ukončení sa emulátor spýta sám.\r\n"
      L"--no-disk spustí Eureku bez diskety a bez pýtania.\r\n"
      L"ROM sa hľadá v premennej A4ROM, vedľa EXE, o úroveň vyššie a\r\n"
      L"v aktuálnom priečinku.\r\n"
      L"Štartuje sa na klávesnici z minulého spustenia; --braille a --pc tú\r\n"
      L"voľbu pre toto spustenie prebijú. Prepína sa aj za behu, cez F11,\r\n"
      L"Ctrl+K alebo v ponuke.\r\n"
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
  // Starting straight in a mode, without the shortcut.  Whether the command
  // line named one at all is kept apart from which one: the settings carry a
  // mode too, and this run's explicit answer has to beat the last run's.
  InputMode startMode = InputMode::kPc;
  bool modeFromCommandLine = false;
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
      modeFromCommandLine = true;
      continue;
    }
    if (argument == L"--braille") {
      startMode = InputMode::kBraille;
      modeFromCommandLine = true;
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
  // The keyboard the last run ended on.  Silent on purpose: the tone belongs to
  // a change, and a start is not one -- the title says which keyboard this is
  // for any moment afterwards, so NVDA+T answers it (ea4-0vw).
  if (!modeFromCommandLine)
    startMode = settings.braille_keyboard() ? InputMode::kBraille
                                            : InputMode::kPc;

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
    machine->CreateEmptyDisk();
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
    const std::wstring mounted = machine->disk().home().wstring();
    // The notch this diskette was left with.  Set before the window exists,
    // so a locked diskette is never writable even for the length of a boot.
    machine->SetDiskWriteProtected(settings.disk_locked(mounted));
    if (settings.last_disk() != mounted) {
      settings.SetLastDisk(mounted);
      std::wstring saveError;
      if (!settings.Save(saveError)) Warn(saveError);
    }
  }
  machine->diagnostics().set_enabled(diagnostics);
  // Where the sliders were left.  Set before the worker exists, so the first
  // sentence is already spoken at the rate and volume the user chose.
  machine->SetRatePot(sliders::RatePotLevel(settings.speech_rate()));
  machine->SetVolume(sliders::VolumeGain(settings.volume()));

  // Warm-resume from the RAM snapshot the last clean power-down left behind, so
  // the machine comes up where the user stopped -- on the real Eureka the RAM
  // and the clock sit on a supply that switching off never cuts (HANDOFF 6.15).
  // The file is consumed here: read once and deleted, so a later bare window
  // close (which on the hardware is the battery cut-off switch) comes up cold
  // with "inicializace eureky", and a fresh snapshot is written only by the
  // next real power-down.
  bool warmResumed = false;
  const fs::path snapshotFile = Settings::SnapshotFile();
  if (!settings.keep_ram()) {
    // The switch is off (ea4-dh1).  Make sure nothing lingers to be loaded
    // later -- the file may be left over from before it was turned off, or
    // from a hand edit of nastavenia.txt.
    std::error_code ec;
    if (!snapshotFile.empty()) fs::remove(snapshotFile, ec);
  } else if (!snapshotFile.empty()) {
    std::wstring snapError;
    std::error_code ec;
    switch (machine->LoadSnapshot(snapshotFile, snapError)) {
      case EurekaMachine::SnapshotResult::kOk:
        fs::remove(snapshotFile, ec);
        machine->PowerOn();
        warmResumed = true;
        break;
      case EurekaMachine::SnapshotResult::kMissing:
        break;  // first run, or nothing to resume -- cold start, nothing said
      case EurekaMachine::SnapshotResult::kCorrupt:
      case EurekaMachine::SnapshotResult::kRomMismatch: {
        // The user's call, so a dialog and not host::Print -- the console may
        // not exist.  Yes: hard start and drop the unusable file.  No: quit,
        // leaving it for a run with the ROM it belongs to.
        const std::wstring question =
            snapError +
            L"\r\n\r\nSpustiť Eureku na tvrdo, bez obnovy poslednej relácie?";
        if (MessageBoxW(nullptr, question.c_str(), L"Eureka A4",
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
          CoUninitialize();
          return 0;
        }
        fs::remove(snapshotFile, ec);
        break;
      }
    }
  }
  if (!warmResumed) machine->Reset();

  // Described by the same function the worker uses when a diskette is swapped
  // in later, so the drive cannot be named one way at start-up and another way
  // afterwards.
  EmulatorThread emulator;
  MainWindow window(emulator, settings, rom.wstring(),
                    DescribeDisk(machine->disk()));
  if (!window.Create()) {
    CoUninitialize();
    return Fail(L"Okno emulátora sa nepodarilo vytvoriť.");
  }
  // Started only once the window exists: the worker posts its notifications to
  // that HWND, and one arriving before there is a window to take it would be
  // lost with no sign of it.
  emulator.Start(std::move(machine), window.handle(), startMode, diagnostics);
  // Every slot the settings mark unsaved gets its diskette back.  The marker
  // survives in nastavenia.txt and the shelf does not, so without this a slot
  // set up in an earlier run would start the next one naming a diskette that
  // does not exist -- and its lock could not be set until it had been inserted
  // once.  Posted rather than reached for directly: the shelf is the worker's.
  //
  // Nine empty diskettes cost about 7 MB of heap in the worst case, and only
  // for slots the user actually marked.  An empty one carries no files, so
  // nothing here makes the closing question ask about diskettes nobody wrote.
  for (int slot = 1; slot <= Settings::kSlots; ++slot)
    if (SlotIsUnsaved(settings.slot(slot))) emulator.PostEnsureSlotDiskette(slot);
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

  // The RAM snapshot, written after the disk so the two agree.  Only when the
  // machine was switched off for real -- the cursor-key chord or the idle
  // timeout -- never on a bare window close: that is the battery cut-off
  // switch, after which the hardware initialises from scratch (HANDOFF 6.15).
  if (machine && machine->powered_off() && settings.keep_ram()) {
    const fs::path snapFile = Settings::SnapshotFile();
    std::wstring snapError;
    if (snapFile.empty())
      Warn(L"Stav pamäte sa nedá uložiť: systém nepovedal, kde je priečinok "
           L"aplikačných dát. Ďalší štart začne inicializáciou.");
    else if (!machine->SaveSnapshot(snapFile, snapError))
      Warn(snapError);
  }

  // Asked about what is actually in the drive now, not about how the run
  // started: a diskette can be swapped mid-run, so --ram-disk no longer means
  // there is still an unsaved diskette here -- and one that has a home needs
  // no offer, it is already on disk.
  //
  // This was the one place in the program that already thought in these terms:
  // it offers a folder that does not exist yet, which is Save As over an
  // unnamed document, while everything else called the same diskette "v
  // pamäti" and left it that way.  Now Ctrl+U does the same thing during the
  // run, and this is only the last chance rather than the only one.
  if (machine && machine->disk().present() && !machine->disk().has_home() &&
      machine->disk().StoredFiles() > 0) {
    const std::wstring question =
        L"Na neuloženej diskete " + FileCount(machine->disk().StoredFiles()) +
        L".\r\n\r\nChcete ju uložiť do priečinka?";
    if (MessageBoxW(nullptr, question.c_str(), L"Eureka A4",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
      // A name that does not exist yet is the point here: this diskette never
      // had a folder, so there is nothing to pick.  SaveDiskAs creates it.
      const std::wstring target = win::PickFolderToCreate(
          nullptr, L"Kam sa má disketa uložiť", L"Disketa");
      if (target.empty())
        Warn(L"Ukladanie zrušené, obsah diskety sa stratí.");
      else if (!machine->SaveDiskAs(target, error))
        MessageBoxW(nullptr, (L"Chyba pri ukladaní:\r\n\r\n" + error).c_str(),
                    L"Eureka A4", MB_OK | MB_ICONERROR);
      else
        MessageBoxW(nullptr, (L"Disketa bola uložená do:\r\n\r\n" + target).c_str(),
                    L"Eureka A4", MB_OK | MB_ICONINFORMATION);
    }
  }
  // The same offer for the diskettes waiting in the quick-choice slots.  They
  // have no folder and exist nowhere else, so the process ending is the moment
  // they stop existing -- exactly the silent loss the drive's own diskette is
  // already protected from.
  for (int slot = 1; slot <= DiskStash::kSlots; ++slot) {
    auto kept = emulator.stash().Take(slot);
    if (!kept || kept->StoredFiles() == 0) continue;
    const std::wstring question =
        L"V slote " + std::to_wstring(slot) + L" je neuložená disketa, na "
        L"ktorej " + FileCount(kept->StoredFiles()) +
        L".\r\n\r\nChcete ju uložiť do priečinka?";
    if (MessageBoxW(nullptr, question.c_str(), L"Eureka A4",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
      continue;
    const std::wstring target = win::PickFolderToCreate(
        nullptr, L"Kam sa má disketa uložiť",
        (L"Disketa " + std::to_wstring(slot)).c_str());
    if (target.empty())
      Warn(L"Ukladanie zrušené, obsah diskety sa stratí.");
    else if (!kept->SaveAs(target, error))
      MessageBoxW(nullptr, (L"Chyba pri ukladaní:\r\n\r\n" + error).c_str(),
                  L"Eureka A4", MB_OK | MB_ICONERROR);
    else
      MessageBoxW(nullptr, (L"Disketa bola uložená do:\r\n\r\n" + target).c_str(),
                  L"Eureka A4", MB_OK | MB_ICONINFORMATION);
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
