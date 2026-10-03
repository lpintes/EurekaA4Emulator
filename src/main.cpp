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
#include <atomic>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "emulator_thread.h"
#include "host_console.h"
#include "machine.h"
#include "main_window.h"
#include "settings.h"
#include "update.h"
#include "updater.h"
#include "version.h"
#include "win/dialog.h"
#include "win/window.h"

namespace fs = std::filesystem;

namespace {

// Claimed by whoever shuts the machine down first: the ordinary exit below, or
// the console-close rescue on the thread the system injects when the
// diagnostic window is closed.  Both call EmulatorThread::Stop, which joins
// the worker and hands the machine over, and doing that twice at once is not
// something Stop is written for.
std::atomic<bool> shutdownTaken{false};

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

// The drive sound set (HANDOFF 6.54): the user's own folder when there is
// one, the set built into the EXE otherwise.  A folder that is there and fails
// is said out loud -- the built-in sounds would play in its place and look
// exactly like success, and nobody would know why their recordings were not
// heard.  Said only while the sound is switched on; otherwise it is nothing
// the user is listening for.
DriveSound LoadDriveSound(bool on) {
  DriveSound sound;
  std::string error;
  auto wide = [](const std::string& utf8) {
    std::wstring out(utf8.size(), L'\0');
    out.resize(MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                   out.data(), static_cast<int>(out.size())));
    return out;
  };
  const fs::path own = Settings::DriveSoundsDirectory();
  std::error_code ec;
  if (!own.empty() && fs::is_directory(own, ec)) {
    if (sound.LoadSamples(own, error)) return sound;
    if (on)
      Warn(L"Vlastné zvuky disketovej mechaniky z priečinka\r\n\r\n" + own.wstring() +
           L"\r\n\r\nsa nedali načítať: " + wide(error) + L".\r\n\r\nHrajú sa vstavané.");
  }
  // RCDATA named after the file's stem, upper-cased with '_' for '-'
  // (eureka.rc).
  const bool builtIn = sound.LoadSamples(
      [](const std::string& stem, std::vector<uint8_t>& bytes) {
        std::wstring name;
        for (char c : stem)
          name += c == '-' ? L'_' : static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(c)));
        // 10 is RT_RCDATA, spelt out because the macro follows UNICODE and
        // this call is the wide one either way.
        const HRSRC found = FindResourceW(nullptr, name.c_str(), MAKEINTRESOURCEW(10));
        if (!found) return false;
        const auto* data = static_cast<const uint8_t*>(LockResource(LoadResource(nullptr, found)));
        if (!data) return false;
        bytes.assign(data, data + SizeofResource(nullptr, found));
        return true;
      },
      error);
  // A broken build, not something the user did; still not to be silent.
  if (!builtIn && on)
    Warn(L"Zvuky disketovej mechaniky sa nedali načítať: " + wide(error) + L".");
  return sound;
}

// The automatic update check (ea4-hg9.4).  Before the ROM, the diskette and
// the machine, so taking an update is only swapping the EXE: nothing runs yet,
// no RAM or diskette needs saving -- the snapshot of the last power-down is
// already on disk and the new version resumes from it.
//
// Capped at three seconds, DNS included, so a slow or missing network costs
// the start that and no more; a failure says nothing and is tried again next
// start.  A check the user asks for is not capped (ea4-hg9.5).  At most once
// a day, and only a release build asks: a development build has no number to
// compare and must not replace itself with a published EXE.
//
// True when the new version has been started and this one should just leave.
bool UpdateBeforeStart(Settings& settings) {
  if (!settings.check_updates()) return false;
  const std::optional<version::Number> current = version::CurrentNumber();
  if (!current) return false;
  const std::wstring today = updater::Today();
  if (!update::CheckDue(settings.last_update_check(), today)) return false;

  const updater::Latest latest = updater::FetchLatestWithin(3000);
  if (latest.kind == updater::Latest::Kind::kFailed) return false;
  // Saved now, before any restart: the new version starts while this one is
  // still here, and must find today's date and not ask again.  Not reported
  // when it fails -- nothing the user did is lost, only the date, and the cost
  // is one more check tomorrow.
  std::wstring ignored;
  settings.SetLastUpdateCheck(today);
  settings.Save(ignored);
  bool restarted = false;
  if (latest.kind == updater::Latest::Kind::kFound &&
      update::ShouldOffer(current, latest.tag, settings.skipped_version())) {
    switch (updater::AskToUpdate(nullptr, latest.tag, /*restartsItself=*/true)) {
      case updater::Choice::kSkip:
        // Kept as the version, "2026.9.2", not the tag: it is what a person
        // reading the settings file would look for.
        settings.SetSkippedVersion(
            latest.tag.substr(latest.tag.starts_with(L'v') ? 1 : 0));
        settings.Save(ignored);
        break;
      case updater::Choice::kLater:
        break;
      case updater::Choice::kUpdate:
        // A failure has been said by the updater; the start then goes on
        // with this version.
        restarted = updater::DownloadAndInstall(nullptr, latest.tag, true) ==
                    updater::Installed::kRestarted;
        break;
    }
  }
  return restarted;
}

void PrintUsage() {
  host::Print(
      L"Eureka A4 Emulator " + std::wstring(version::Current()) + L"\r\n\r\n"
      L"Použitie: EurekaA4Emulator.exe [--rom A4ROM.DMP] [--disk PRIECINOK]\r\n"
      L"                              [--ram-disk] [--no-disk] [--diag]\r\n"
      L"                              [--braille] [--pc] [--version]\r\n"
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

// The way out: stop the machine, write the diskette and the RAM, and offer to
// keep the diskettes that have no folder.  A function because two roads take
// it -- the end of Run, and a restart for an update (ea4-hg9.5), which has to
// run all of it while the window still stands: the new version is started
// right after, and Windows lets its window take the focus only if the process
// starting it has the focus then (updater.cpp, Finish).
//
// forUpdate also writes the RAM snapshot when the machine was not switched
// off.  For the same reason as the console-close rescue: restarting for an
// update is not a decision about the machine, so the session carries on.
void ShutDown(EmulatorThread& emulator, const Settings& settings,
              std::unique_ptr<EurekaMachine>& machine, bool forUpdate) {
  std::wstring error;
  // The machine comes back here to be shut down, so nothing below shares it
  // with a running thread.  Whichever of the two got here first does the
  // stopping; the other leaves `machine` empty and every step below is already
  // written to skip on that.
  if (!shutdownTaken.exchange(true)) machine = emulator.Stop();
  if (machine && !machine->FlushDisk(error))
    MessageBoxW(nullptr, (L"Chyba pri ukladaní disku:\r\n\r\n" + error).c_str(),
                L"Eureka A4", MB_OK | MB_ICONERROR);

  // The RAM snapshot, written after the disk so the two agree.  Only when the
  // machine was switched off for real -- the cursor-key chord or the idle
  // timeout -- never on a bare window close: that is the battery cut-off
  // switch, after which the hardware initialises from scratch (HANDOFF 6.15).
  // Or for an update restart, see above.
  if (machine && (machine->powered_off() || forUpdate) && settings.keep_ram()) {
    const fs::path snapFile = Settings::SnapshotFile();
    std::wstring snapError;
    if (snapFile.empty())
      Warn(L"Stav pamäte sa nedá uložiť: systém nepovedal, kde je priečinok "
           L"aplikačných dát. Ďalší štart začne inicializáciou.");
    else if (!machine->SaveSnapshot(snapFile, snapError))
      Warn(snapError);
    // Second and separate, so pamat.bin stays what older versions read.
    // Without the module the file is not touched: see the load above.
    else if (machine->extra_ram()) {
      const fs::path extraFile = Settings::ExtraRamSnapshotFile();
      if (!machine->SaveExtraRamSnapshot(extraFile, snapError)) Warn(snapError);
    }
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
}

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
    if (argument == L"--version") {
      host::OpenConsole();
      host::Print(std::wstring(version::Current()) + L"\r\n");
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
  updater::RemoveLeftover();
  if (UpdateBeforeStart(settings)) {
    CoUninitialize();
    return 0;
  }
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
  //
  // The extra RAM module (HANDOFF 6.51) is fitted first, so the load knows to
  // read its file.  With the module fitted that file follows pamat.bin
  // wherever pamat.bin is consumed or dropped.  With it off the file is left
  // alone, whatever happens to pamat.bin: a version that does not know the
  // key drops it from nastavenia.txt on its next save, and that must not cost
  // the contents.  The one exception is keep_ram switched off, which is the
  // user asking for nothing to be kept.
  machine->SetExtraRam(settings.extra_ram());
  bool warmResumed = false;
  const fs::path snapshotFile = Settings::SnapshotFile();
  const fs::path extraRamFile = Settings::ExtraRamSnapshotFile();
  const auto dropExtraRam = [&extraRamFile] {
    std::error_code ec;
    if (!extraRamFile.empty()) fs::remove(extraRamFile, ec);
  };
  if (!settings.keep_ram()) {
    // The switch is off (ea4-dh1).  Make sure nothing lingers to be loaded
    // later -- the file may be left over from before it was turned off, or
    // from a hand edit of nastavenia.txt.
    std::error_code ec;
    if (!snapshotFile.empty()) fs::remove(snapshotFile, ec);
    dropExtraRam();
  } else if (!snapshotFile.empty()) {
    std::wstring snapError;
    std::error_code ec;
    switch (machine->LoadSnapshot(snapshotFile, snapError)) {
      case EurekaMachine::SnapshotResult::kOk:
        fs::remove(snapshotFile, ec);
        if (machine->extra_ram() && !extraRamFile.empty()) {
          // Missing is a module fitted since the last power-down: it starts
          // empty, and nothing is said.  Corrupt is said, once.
          if (machine->LoadExtraRamSnapshot(extraRamFile, snapError) ==
              EurekaMachine::SnapshotResult::kCorrupt)
            Warn(snapError);
          dropExtraRam();
        }
        machine->PowerOn();
        warmResumed = true;
        break;
      case EurekaMachine::SnapshotResult::kMissing:
        // First run, or nothing to resume -- cold start, nothing said.  A cold
        // start clears the module too, so its file goes with it.
        if (machine->extra_ram()) dropExtraRam();
        break;
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
        if (machine->extra_ram()) dropExtraRam();
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
  emulator.SetDriveSound(LoadDriveSound(settings.drive_sound()), settings.drive_sound());
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
  // Asked for outright, because after an update this window is started by a
  // process that is leaving (updater.cpp, Finish), and ShowWindow alone left
  // it behind the terminal.  Windows grants it only where it is allowed --
  // started by whoever had the focus -- so an ordinary start is unchanged.
  SetForegroundWindow(window.handle());
  SetFocus(window.handle());

  // Closing the diagnostic console kills this process outright -- it cannot be
  // refused, see host_console.cpp and HANDOFF 6.50 -- so none of the shutdown
  // below would run and the machine's memory would be gone without a word.
  // This is what can still be done in the few seconds the system grants: take
  // the machine off the worker, write the diskette out and save the RAM.
  //
  // It deliberately differs from the exit path in one thing: the snapshot is
  // written even though the machine was not switched off.  A bare close of the
  // emulator's own window is the battery cut-off switch on purpose (6.15), but
  // this is not that window and closing it is not a decision about the
  // machine -- it is an accident with the same keystroke.
  //
  // No dialogs: this runs on a thread the system injects, there are seconds
  // rather than minutes, and a window opened from here would be a window
  // nobody can answer.  An unsaved diskette therefore cannot be rescued; that
  // is the remaining cost of the accident.
  host::SetCloseRescue([&emulator, &settings] {
    if (shutdownTaken.exchange(true)) return;
    std::unique_ptr<EurekaMachine> dying = emulator.Stop();
    if (!dying) return;
    std::wstring ignored;
    dying->FlushDisk(ignored);
    if (!settings.keep_ram()) return;
    const fs::path snapFile = Settings::SnapshotFile();
    if (snapFile.empty() || !dying->SaveSnapshot(snapFile, ignored)) return;
    // The extra RAM module's file too, as on the exit path below: without it
    // the next start would resume with the module empty and say nothing
    // (HANDOFF 6.51).
    if (dying->extra_ram())
      dying->SaveExtraRamSnapshot(Settings::ExtraRamSnapshotFile(), ignored);
  });

  // A manual update check that the user answered with a restart (ea4-hg9.5).
  // The window calls this before it closes, so the shutdown and the new
  // version's start both happen while it still has the focus.
  bool shutDownForUpdate = false;
  window.SetUpdateRestart([&] {
    shutDownForUpdate = true;
    ShutDown(emulator, settings, machine, /*forUpdate=*/true);
    std::wstring restartError;
    if (!updater::Restart(restartError)) Warn(restartError);
  });

  win::RunMessageLoop(window.handle(), window.accelerators(),
                      window.hostAccelerators(),
                      [&window] { return window.HostShortcutsActive(); });

  // Unregistered before anything it captured goes out of scope: the console
  // could be closed at this very moment, and the handler runs on its own
  // thread.  This narrows the race, it does not close it: a rescue that took
  // its copy just before this line can still be inside emulator.Stop() or
  // reading settings while Run() returns and destroys both.  Left so on
  // purpose -- it takes a console close in the same instant as the main
  // window's, and the process is being killed either way.
  host::SetCloseRescue(nullptr);

  // Once only: a second run would ask about the unsaved diskettes again.
  if (!shutDownForUpdate) ShutDown(emulator, settings, machine, false);

  if (machine && machine->diagnostics().enabled()) {
    host::Print(machine->diagnostics().Report());
    host::HoldConsole();
  }
  CoUninitialize();
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return Run(); }
