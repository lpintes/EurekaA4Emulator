// The settings file: what survives a run, and that a hand-edited one cannot
// take the emulator's slots away with it.
//
// The parser earns a test of its own because its failure mode is the quiet one
// this project keeps meeting.  A path written through the wrong encoding comes
// back mangled and nothing reports it -- the slot simply points somewhere
// else, and Ctrl+3 stops finding the diskette it found yesterday.  So the
// round trip here is checked in Slovak, with diacritics, and not in ASCII:
// ASCII would pass through every encoding this could accidentally use.
//
// Everything is done in the system temp folder.  The real settings file is a
// moving target and belongs to whoever is running the tests.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <windows.h>

#include "settings.h"

namespace fs = std::filesystem;

namespace {

int checks = 0;
int failures = 0;

std::string Narrow(const std::wstring& text) {
  std::string result;
  for (wchar_t ch : text) result += ch < 128 ? static_cast<char>(ch) : '?';
  return result;
}

void Check(bool passed, const std::string& name, const std::string& detail = "") {
  ++checks;
  if (!passed) ++failures;
  std::cout << (passed ? "  ok   " : "  CHYBA ") << name;
  if (!passed && !detail.empty()) std::cout << ": " << detail;
  std::cout << "\n";
}

fs::path Root() {
  static const fs::path root =
      fs::temp_directory_path() /
      ("eureka-settings-test-" + std::to_string(GetCurrentProcessId()));
  return root;
}

fs::path FileNamed(const std::string& name) { return Root() / name; }

// Writes raw bytes, so that a file exactly as some editor would have left it
// can be handed to the parser -- BOM or none, CRLF or LF.
void WriteRaw(const fs::path& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string ReadRaw(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

// A path with everything that breaks a naive encoding: Slovak diacritics, a
// dash that is not ASCII, and a space in the middle.
const wchar_t kAwkwardPath[] = L"C:\\Diskety\\Príbehy — kópia\\Ľudové piesne";

void MissingFileIsDefaults() {
  Settings settings(FileNamed("neexistuje.txt"));
  settings.Load();
  Check(settings.last_disk().empty(), "chybajuci subor znamena defaulty");
  Check(settings.slot(1).empty(), "chybajuci subor nechava sloty prazdne");
}

void KeepRamSwitchRoundTrips() {
  // Absent from the file means on: a settings file from a version that never
  // knew the switch must not silently stop keeping the RAM (ea4-dh1).
  {
    Settings settings(FileNamed("neexistuje.txt"));
    settings.Load();
    Check(settings.keep_ram(), "chybajuci subor: zachovanie RAM je zapnute");
  }
  const fs::path file = FileNamed("zachovat-ram.txt");
  std::wstring error;
  {
    Settings settings(file);
    Check(settings.keep_ram(), "novy Settings ma zapnute");
    settings.SetKeepRam(false);
    Check(settings.Save(error), "ulozenie s vypnutym prejde", Narrow(error));
  }
  {
    Settings loaded(file);
    loaded.Load();
    Check(!loaded.keep_ram(), "vypnute prezije zapis aj citanie");
  }
  Check(ReadRaw(file).find("zachovat-ram=0") != std::string::npos,
        "vypnutie je v subore ako zachovat-ram=0");
  {
    Settings settings(file);
    settings.Load();
    settings.SetKeepRam(true);
    settings.Save(error);
  }
  {
    Settings loaded(file);
    loaded.Load();
    Check(loaded.keep_ram(), "zapnutie sa da vratit");
  }
  Check(ReadRaw(file).find("zachovat-ram=1") != std::string::npos,
        "zapnutie je v subore ako zachovat-ram=1");
}

void SlidersRoundTrip() {
  // Absent means both sliders in the middle, where a first run starts too.
  {
    Settings settings(FileNamed("neexistuje.txt"));
    settings.Load();
    Check(settings.speech_rate() == sliders::kRateDefault,
          "chybajuci subor: rychlost je v strede");
    Check(settings.volume() == sliders::kVolumeDefault,
          "chybajuci subor: hlasitost je v strede");
  }
  const fs::path file = FileNamed("posuvniky.txt");
  std::wstring error;
  {
    Settings settings(file);
    settings.SetSpeechRate(3);
    settings.SetVolume(0);
    Check(settings.Save(error), "ulozenie posuvnikov prejde", Narrow(error));
  }
  {
    Settings loaded(file);
    loaded.Load();
    Check(loaded.speech_rate() == 3, "rychlost prezije zapis aj citanie");
    Check(loaded.volume() == 0, "stlmena hlasitost prezije zapis aj citanie");
  }
  const std::string bytes = ReadRaw(file);
  Check(bytes.find("rychlost-reci=3") != std::string::npos,
        "rychlost je v subore ako rychlost-reci=3");
  Check(bytes.find("hlasitost=0") != std::string::npos,
        "hlasitost je v subore ako hlasitost=0");

  // A hand edit.  Reading a word as zero would start a machine that says
  // nothing, so a word keeps the default; a number past the end is pulled
  // back to the end.
  const fs::path edited = FileNamed("posuvniky-rucne.txt");
  WriteRaw(edited, "rychlost-reci=99\r\nhlasitost=nahlas\r\n");
  {
    Settings loaded(edited);
    loaded.Load();
    Check(loaded.speech_rate() == sliders::kRatePositions - 1,
          "rychlost za koncom sa pritiahne na koniec");
    Check(loaded.volume() == sliders::kVolumeDefault,
          "hlasitost, ktora nie je cislo, zostane predvolena");
  }
  Settings outside(file);
  outside.SetSpeechRate(-5);
  outside.SetVolume(1000);
  Check(outside.speech_rate() == 0 &&
            outside.volume() == sliders::kVolumePositions - 1,
        "nastavenie mimo rozsahu sa pritiahne");
}

// Not about the file, but this is the one test that runs without a ROM, and
// the mapping is what the saved positions mean.
void SliderPositionsMapToTheHardware() {
  // The firmware keeps five bits of the rate pot (00258), so every position
  // has to fall inside its own step.
  bool ownStep = true;
  for (int position = 0; position < sliders::kRatePositions; ++position)
    if ((sliders::RatePotLevel(position) >> 3) != position) ownStep = false;
  Check(ownStep, "kazda poloha rychlosti padne do svojho kroku firmveru");
  Check((sliders::RatePotLevel(sliders::kRateDefault) >> 3) == (0x80 >> 3),
        "stred dava ten isty reload ako doterajsich 80h");
  Check(sliders::RatePotLevel(sliders::kRateDefault) != 0x80,
        "stred nesedi presne na urovni ticha");
  Check(sliders::VolumeGain(0) == 0.0, "hlasitost 0 je ticho");
  Check(sliders::VolumeGain(sliders::kVolumePositions - 1) == 1.0,
        "vrch hlasitosti nemeni vystup");
  Check(std::abs(20.0 * std::log10(sliders::VolumeGain(sliders::kVolumeDefault)) +
                 10.0) < 1e-9,
        "stred hlasitosti je 10 dB pod vrchom");
  bool rising = true;
  for (int position = 1; position < sliders::kVolumePositions; ++position)
    if (!(sliders::VolumeGain(position) > sliders::VolumeGain(position - 1)))
      rising = false;
  Check(rising, "hlasitost s polohou rastie");
}

void RoundTripKeepsDiacritics() {
  const fs::path file = FileNamed("kolotoc.txt");
  std::wstring error;
  {
    Settings settings(file);
    settings.SetLastDisk(kAwkwardPath);
    settings.SetSlot(1, kAwkwardPath);
    settings.SetSlot(9, L"D:\\Hudba");
    Check(settings.Save(error), "ulozenie prejde", Narrow(error));
  }
  Settings loaded(file);
  loaded.Load();
  Check(loaded.last_disk() == kAwkwardPath,
        "posledna disketa prezije diakritiku", Narrow(loaded.last_disk()));
  Check(loaded.slot(1) == kAwkwardPath, "slot 1 prezije diakritiku",
        Narrow(loaded.slot(1)));
  Check(loaded.slot(9) == L"D:\\Hudba", "slot 9 prezije");
  // The bytes on disk have to be UTF-8, not whatever the host code page is:
  // an ANSI file would read back correctly here and be wrong on a machine
  // with another code page.
  const std::string bytes = ReadRaw(file);
  Check(bytes.starts_with("\xef\xbb\xbf"), "subor zacina BOM");
  // Split on purpose: \xad would otherwise swallow the "be" after it as more
  // hex digits and mean something else entirely.
  Check(bytes.find("Pr\xc3\xad" "behy") != std::string::npos,
        "diakritika je na disku v UTF-8");
}

void SaveOverwritesRatherThanAppends() {
  const fs::path file = FileNamed("prepis.txt");
  std::wstring error;
  Settings settings(file);
  settings.SetSlot(1, L"C:\\Prve");
  settings.Save(error);
  settings.SetSlot(1, L"C:\\Druhe");
  settings.Save(error);

  Settings loaded(file);
  loaded.Load();
  Check(loaded.slot(1) == L"C:\\Druhe", "druhe ulozenie prepise prve",
        Narrow(loaded.slot(1)));
  Check(ReadRaw(file).find("Prve") == std::string::npos,
        "stary riadok v subore nezostal");
}

void ClearedSlotDisappears() {
  const fs::path file = FileNamed("vymaz.txt");
  std::wstring error;
  Settings settings(file);
  settings.SetSlot(3, L"C:\\Nieco");
  settings.Save(error);
  settings.SetSlot(3, L"");
  settings.Save(error);

  Settings loaded(file);
  loaded.Load();
  Check(loaded.slot(3).empty(), "vyprazdneny slot sa neulozi");
}

// The write protect notch, which belongs to the diskette and not to the drive
// or to a slot.  This is the part the ROM's bulk copy depends on: it insists
// the source diskette be protected and then sends the user back and forth
// between source and target, so a lock that ended when the diskette came out
// would be gone the first time it went back in -- and the machine would stop
// with "zdrojový disk není chráněn proti zápisu".
void DiskLockSurvivesTheFile() {
  const fs::path file = FileNamed("zamok.txt");
  std::wstring error;
  Settings settings(file);
  settings.SetDiskLocked(L"C:\\Hry\\e_games", true);
  settings.SetDiskLocked(L"C:\\Pracovna", false);
  Check(settings.Save(error), "subor so zamkom sa ulozi", Narrow(error));

  Settings loaded(file);
  loaded.Load();
  Check(loaded.disk_locked(L"C:\\Hry\\e_games"), "zamok diskety prezil subor");
  Check(!loaded.disk_locked(L"C:\\Pracovna"), "nezamknuta disketa zostala volna");
  Check(!loaded.disk_locked(L""), "prazdna cesta nie je zamknuta");

  // Taken off again, the line has to go: a lock that outlived the act that
  // ended it is exactly the state the whole notch exists to make visible.
  loaded.SetDiskLocked(L"C:\\Hry\\e_games", false);
  Check(loaded.Save(error), "subor bez zamku sa ulozi", Narrow(error));
  Settings again(file);
  again.Load();
  Check(!again.disk_locked(L"C:\\Hry\\e_games"), "zruseny zamok sa neulozi");
}

// One folder spelled several ways is one diskette.  The picker, a slot typed
// by hand and VirtualDisk's canonical path differ in case, in the slash and
// in a trailing separator, so a raw comparison would lose the lock whenever
// the same diskette arrived by another road -- and lose it silently.
void DiskLockIgnoresPathSpelling() {
  Settings settings(FileNamed("zamok-cesty.txt"));
  settings.SetDiskLocked(L"C:\\Hry\\E_Games", true);
  Check(settings.disk_locked(L"c:\\hry\\e_games"), "zamok neriesi velke pismena");
  Check(settings.disk_locked(L"C:/Hry/E_Games"), "zamok neriesi tvar lomky");
  Check(settings.disk_locked(L"C:\\Hry\\E_Games\\"),
        "zamok neriesi koncovu lomku");
  Check(!settings.disk_locked(L"C:\\Hry"), "nadradeny priecinok nie je zamknuty");

  // And locking the same diskette twice must not write it down twice: the
  // second spelling would then outlive the unlock done through the first.
  settings.SetDiskLocked(L"c:/hry/e_games/", true);
  settings.SetDiskLocked(L"C:\\Hry\\E_Games", false);
  Check(!settings.disk_locked(L"C:\\Hry\\E_Games"),
        "odomknutie zrusi zamok bez ohladu na tvar cesty");
}

// The rule the slots dialog asks with, so that two slots holding one folder
// cannot end up ticked differently.  One rule, one place: a second copy would
// disagree with this one on exactly the spellings that matter.
void SameDiskIsOneRule() {
  Check(Settings::SameDisk(L"C:\\Hry", L"c:/hry\\"), "ta ista disketa dvoma zapismi");
  Check(!Settings::SameDisk(L"C:\\Hry", L"C:\\Hry2"), "ine priecinky su ine diskety");
  Check(!Settings::SameDisk(L"", L""), "prazdna cesta nie je disketa");
}

// An unsaved diskette has no path, and this list is keyed by path, so a line
// in the file would point at nothing.  Its lock lasts as long as the diskette,
// which is the honest span for it.
//
// What this does *not* mean is that such a diskette cannot be locked.  The
// notch is a member of VirtualDisk and works on any medium; only remembering
// it needs a name.  The slots dialog used to read this refusal as "cannot be
// locked" and greyed its box, which is what 6.26 was -- see DiskStash's
// StashKeepsTheLock in disk_test for the other half.
void MemoryDisketteIsNeverRemembered() {
  Settings settings(FileNamed("zamok-pamat.txt"));
  settings.SetDiskLocked(kSlotUnsaved, true);
  Check(!settings.disk_locked(kSlotUnsaved), "neulozena disketa sa nezapamata");
}

void CommentsAndBlanksAreIgnored() {
  const fs::path file = FileNamed("komentare.txt");
  WriteRaw(file,
           "# Toto je komentar\r\n"
           "\r\n"
           "   \r\n"
           "# posledna-disketa=C:\\Pasca\r\n"
           "posledna-disketa=C:\\Spravne\r\n");
  Settings settings(file);
  settings.Load();
  Check(settings.last_disk() == L"C:\\Spravne",
        "komentar s rovnitkom sa neberie ako kluc",
        Narrow(settings.last_disk()));
}

void JunkLinesDoNotCostTheRest() {
  const fs::path file = FileNamed("smeti.txt");
  WriteRaw(file,
           "slot1=C:\\Prvy\r\n"
           "riadok bez rovnitka\r\n"
           "neznamy-kluc=hodnota\r\n"
           "slot99=mimo rozsahu\r\n"
           "slotX=nezmysel\r\n"
           "=hodnota bez kluca\r\n"
           "slot2=C:\\Druhy\r\n");
  Settings settings(file);
  settings.Load();
  // The point of the test: a bad line in the middle must not stop the parser,
  // or one stray character costs the user every slot after it.
  Check(settings.slot(1) == L"C:\\Prvy", "slot pred smetim prezije");
  Check(settings.slot(2) == L"C:\\Druhy", "slot za smetim prezije",
        Narrow(settings.slot(2)));
}

void BothLineEndingsRead() {
  const fs::path lf = FileNamed("lf.txt");
  WriteRaw(lf, "slot1=C:\\Unix\nslot2=C:\\Druhy\n");
  Settings unix(lf);
  unix.Load();
  Check(unix.slot(1) == L"C:\\Unix", "LF konce riadkov sa precitaju",
        Narrow(unix.slot(1)));

  const fs::path noBom = FileNamed("bezbom.txt");
  WriteRaw(noBom, "slot1=C:\\BezBom\r\n");
  Settings plain(noBom);
  plain.Load();
  Check(plain.slot(1) == L"C:\\BezBom", "subor bez BOM sa precita");
}

void SpacesAroundEqualsAreTrimmed() {
  const fs::path file = FileNamed("medzery.txt");
  WriteRaw(file, "  slot1  =  C:\\Cesta s medzerou  \r\n");
  Settings settings(file);
  settings.Load();
  // Trimmed at the ends, kept in the middle: a folder name may well contain
  // spaces and this is the only chance to get that wrong.
  Check(settings.slot(1) == L"C:\\Cesta s medzerou",
        "medzery okolo sa orezu, vnutorne zostanu", Narrow(settings.slot(1)));
}

void SlotRangeIsSafe() {
  Settings settings(FileNamed("rozsah.txt"));
  settings.SetSlot(0, L"C:\\Nula");
  settings.SetSlot(10, L"C:\\Desat");
  settings.SetSlot(-1, L"C:\\Zaporny");
  Check(settings.slot(0).empty() && settings.slot(10).empty() &&
            settings.slot(-1).empty(),
        "slot mimo rozsahu je prazdny a nikam nezapise");
  Check(settings.slot(1).empty() && settings.slot(9).empty(),
        "zapis mimo rozsahu neprepisal platny slot");
}

void UnwritablePlaceIsReported() {
  // A file where a folder would have to be, so create_directories and the
  // write both fail.  The emulator has to hear about this: a slot that did
  // not stick has to say so at the moment it did not stick.
  const fs::path blocker = FileNamed("prekazka");
  WriteRaw(blocker, "nie som priecinok\n");
  Settings settings(blocker / "nastavenia.txt");
  std::wstring error;
  Check(!settings.Save(error), "zapis do nemozneho miesta zlyha");
  Check(!error.empty(), "zlyhanie ma dovod");
}

void NowhereToSaveIsReported() {
  Settings settings(fs::path{});
  std::wstring error;
  Check(!settings.Save(error), "prazdna cesta znamena zlyhanie");
  Check(!error.empty(), "prazdna cesta ma dovod");
  // Load must stay quiet about it: no file is the normal first run.
  settings.Load();
  Check(settings.last_disk().empty(), "citanie z prazdnej cesty je ticho");
}

// A slot can hold a diskette that lives only in memory, and that is written
// into the same field as a folder path.  The two must never be mistaken for
// each other: '*' cannot start a Windows path, which is the whole reason the
// markers look the way they do.
void MemorySlotsSurviveTheFile() {
  const fs::path file = FileNamed("pamat.txt");
  std::wstring error;
  {
    Settings settings(file);
    settings.SetSlot(1, kSlotUnsaved);
    settings.SetSlot(3, L"C:\\Diskety\\Príbehy");
    Check(settings.Save(error), "ulozenie so slotom v pamati", Narrow(error));
  }
  Settings loaded(file);
  loaded.Load();
  Check(SlotIsUnsaved(loaded.slot(1)), "slot s pamatou prezije");
  Check(!SlotIsUnsaved(loaded.slot(3)), "cesta sa nepomyli s pamatou");
  Check(!SlotIsUnsaved(L""), "prazdny slot nie je pamat");
  // An older file may hold the marker this version no longer writes.  It has
  // to read as the memory slot and not as a folder called
  // "*pamat-nenaformatovana", which is what a plain path test would do.
  Check(SlotIsUnsaved(L"*pamat-nenaformatovana"),
        "stara znacka z minulej verzie sa berie ako pamat");
}

void SlotNamesAreReadable() {
  Check(SlotDisplayName(L"") == L"(prázdny)", "prazdny slot sa vola prazdny",
        Narrow(SlotDisplayName(L"")));
  // Deliberately not "nová prázdna": the slot hands the same diskette back
  // with everything written on it, so a name promising an empty one would be
  // wrong from the first save on.
  Check(SlotDisplayName(kSlotUnsaved) == L"Neuložená disketa",
        "slot v pamati ma meno", Narrow(SlotDisplayName(kSlotUnsaved)));
  // The name is the leaf, not the whole path: it goes in a menu item that a
  // screen reader reads out, where a full path is noise.
  Check(SlotDisplayName(L"C:\\Diskety\\Slovník") == L"Slovník",
        "slot s cestou sa vola podla priecinka",
        Narrow(SlotDisplayName(L"C:\\Diskety\\Slovník")));
  Check(SlotDisplayName(L"C:\\Diskety\\Slovník\\") == L"Slovník",
        "koncova lomka meno nezrusi");
}

void RealFileIsFoundSomewhere() {
  const fs::path file = Settings::FindFile();
  // Nothing is created here -- this only asks where it would go.
  Check(!file.empty(), "najde sa miesto pre nastavenia");
  Check(file.filename() == L"nastavenia.txt", "subor sa vola nastavenia.txt",
        file.filename().string());
  const fs::path portable = ExecutableDirectory() / L"config";
  std::error_code ec;
  const bool isPortable = fs::is_directory(portable, ec);
  Check(isPortable == (file.parent_path() == portable),
        "prenosny rezim plati prave vtedy, ked je vedla EXE priecinok config");
}

}  // namespace

int main() {
  std::error_code ec;
  fs::remove_all(Root(), ec);
  fs::create_directories(Root(), ec);
  if (ec) {
    std::cout << "FAIL nepodarilo sa vytvorit " << Root().string() << "\n";
    return 1;
  }

  MissingFileIsDefaults();
  KeepRamSwitchRoundTrips();
  SlidersRoundTrip();
  SliderPositionsMapToTheHardware();
  RoundTripKeepsDiacritics();
  SaveOverwritesRatherThanAppends();
  ClearedSlotDisappears();
  DiskLockSurvivesTheFile();
  DiskLockIgnoresPathSpelling();
  SameDiskIsOneRule();
  MemoryDisketteIsNeverRemembered();
  CommentsAndBlanksAreIgnored();
  JunkLinesDoNotCostTheRest();
  BothLineEndingsRead();
  SpacesAroundEqualsAreTrimmed();
  SlotRangeIsSafe();
  MemorySlotsSurviveTheFile();
  SlotNamesAreReadable();
  UnwritablePlaceIsReported();
  NowhereToSaveIsReported();
  RealFileIsFoundSomewhere();

  fs::remove_all(Root(), ec);
  std::cout << (failures == 0 ? "PASS" : "FAIL")
            << " mode=SETTINGS kontrol=" << checks << " chyb=" << failures
            << "\n";
  return failures == 0 ? 0 : 1;
}
