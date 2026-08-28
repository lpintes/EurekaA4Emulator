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
  RoundTripKeepsDiacritics();
  SaveOverwritesRatherThanAppends();
  ClearedSlotDisappears();
  CommentsAndBlanksAreIgnored();
  JunkLinesDoNotCostTheRest();
  BothLineEndingsRead();
  SpacesAroundEqualsAreTrimmed();
  SlotRangeIsSafe();
  UnwritablePlaceIsReported();
  NowhereToSaveIsReported();
  RealFileIsFoundSomewhere();

  fs::remove_all(Root(), ec);
  std::cout << (failures == 0 ? "PASS" : "FAIL")
            << " mode=SETTINGS kontrol=" << checks << " chyb=" << failures
            << "\n";
  return failures == 0 ? 0 : 1;
}
