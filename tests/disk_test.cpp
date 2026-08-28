// Capacity and import rules of the folder-backed diskette.
//
// Everything this test needs it makes for itself in the system temp folder: a
// real diskette folder is a moving target (files get added between two runs)
// and a test that reads one measures whatever happened to be there.  The
// numbers checked here come from the Disk Parameter Block in the technical
// manual -- 396 free blocks of 2 KiB and 256 directory entries -- so a change
// in the disk model has to break this test before it breaks a diskette.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <windows.h>

#include "virtual_disk.h"

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
      ("eureka-disk-test-" + std::to_string(GetCurrentProcessId()));
  return root;
}

fs::path MakeFolder(const std::string& name) {
  const fs::path folder = Root() / name;
  std::error_code ec;
  fs::remove_all(folder, ec);
  fs::create_directories(folder, ec);
  return folder;
}

// Content that is stable across runs but different per file, so a block landing
// under the wrong file cannot pass unnoticed.
void MakeFile(const fs::path& path, std::size_t size, unsigned seed) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  std::string data(size, '\0');
  for (std::size_t i = 0; i < size; ++i)
    data[i] = static_cast<char>(1 + (i * 37 + seed * 11) % 200);
  stream.write(data.data(), static_cast<std::streamsize>(size));
}

fs::path FillFolder(const std::string& name, unsigned count, std::size_t size) {
  const fs::path folder = MakeFolder(name);
  for (unsigned i = 0; i < count; ++i) {
    char leaf[16];
    std::snprintf(leaf, sizeof leaf, "F%03u.BIN", i);
    MakeFile(folder / leaf, size, i);
  }
  return folder;
}

bool Contains(const std::wstring& haystack, const std::wstring& needle) {
  return haystack.find(needle) != std::wstring::npos;
}

std::vector<uint8_t> ReadAll(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)), {});
}

// Exports the mounted image into a fresh folder and compares it with the
// source, file by file.  Host names here are already valid 8.3 names, so the
// diskette hands them back unchanged.
bool ExportMatches(VirtualDisk& disk, const fs::path& source,
                   const std::string& name, std::string& detail) {
  const fs::path target = MakeFolder(name + "-export");
  std::wstring error;
  if (!disk.ExportTo(target, error)) {
    detail = "export zlyhal: " + Narrow(error);
    return false;
  }
  unsigned compared = 0;
  for (const auto& entry : fs::directory_iterator(source)) {
    if (!entry.is_regular_file()) continue;
    const fs::path exported = target / entry.path().filename();
    if (!fs::exists(exported)) {
      detail = "chyba subor " + entry.path().filename().string();
      return false;
    }
    if (ReadAll(entry.path()) != ReadAll(exported)) {
      detail = "lisi sa obsah " + entry.path().filename().string();
      return false;
    }
    ++compared;
  }
  if (compared == 0) {
    detail = "nebolo co porovnat";
    return false;
  }
  return true;
}

// 198 files of 4 KiB claim 396 blocks, which is every block the directory does
// not already hold.  The export proves the last block (399) is readable, not
// merely counted.
void FullDiskette() {
  const fs::path folder = FillFolder("plna", 198, 4096);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(mounted && disk.imported_files() == 198, "plna_disketa_396_blokov",
        mounted ? "naimportovanych " + std::to_string(disk.imported_files())
                : Narrow(error));
  if (!mounted) return;
  std::string detail;
  Check(ExportMatches(disk, folder, "plna", detail), "plna_disketa_export", detail);
}

void TwoBlocksTooMany() {
  const fs::path folder = FillFolder("o-dva-bloky", 199, 4096);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(!mounted, "o_dva_bloky_navyse_odmietne", "priecinok sa pripojil");
  Check(!mounted && Contains(error, L"Prebytok je 2 bloky") &&
            Contains(error, L"4 KiB"),
        "o_dva_bloky_navyse_pomenuje_prebytok", Narrow(error));
  // Nothing of a refused diskette may stay behind.
  Check(disk.imported_files() == 0 && !disk.present(),
        "odmietnuta_disketa_nezanecha_zvysky");
}

void FullDirectory() {
  const fs::path folder = FillFolder("plny-adresar", 256, 128);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(mounted && disk.StoredFiles() == 256, "plny_adresar_256_poloziek",
        mounted ? "v adresari " + std::to_string(disk.StoredFiles())
                : Narrow(error));
}

void OneEntryTooMany() {
  const fs::path folder = FillFolder("o-polozku", 257, 128);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(!mounted && Contains(error, L"položiek adresára") &&
            Contains(error, L"257"),
        "o_polozku_navyse_odmietne", mounted ? "priecinok sa pripojil" : Narrow(error));
}

// 397 files of a single byte occupy 397 blocks: the whole point of checking
// blocks instead of bytes.
void TinyFilesEatWholeBlocks() {
  const fs::path folder = FillFolder("drobne", 397, 1);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(!mounted && Contains(error, L"397 blokov"), "drobne_subory_zaberu_cele_bloky",
        mounted ? "priecinok sa pripojil" : Narrow(error));
}

// CP/M has no directories, so a subfolder is skipped on purpose.  The other
// half of that decision is that it must survive untouched on the host side:
// ignoring a folder and quietly damaging it are not the same thing.
void SubfolderIsIgnored() {
  const fs::path folder = MakeFolder("s-podpriecinkom");
  MakeFile(folder / "HRA.BAS", 500, 1);
  fs::create_directory(folder / "vnutri");
  MakeFile(folder / "vnutri" / "SKRYTA.BAS", 500, 2);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(mounted && disk.imported_files() == 1, "podpriecinok_sa_neimportuje",
        mounted ? "naimportovanych " + std::to_string(disk.imported_files())
                : Narrow(error));
  const bool flushed = disk.Flush(error);
  Check(flushed && fs::is_directory(folder / "vnutri") &&
            fs::is_regular_file(folder / "vnutri" / "SKRYTA.BAS"),
        "podpriecinok_zostane_nedotknuty", flushed ? "" : Narrow(error));
}

// A file the host will not open used to be skipped in silence, which on the
// machine is indistinguishable from a file that was never there.
void UnreadableFileIsAnError() {
  const fs::path folder = MakeFolder("nedostupny");
  MakeFile(folder / "DOBRY.BIN", 100, 1);
  MakeFile(folder / "ZAMKNUTY.BIN", 100, 2);
  const std::wstring locked = (folder / "ZAMKNUTY.BIN").wstring();
  HANDLE handle = CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    Check(false, "nedostupny_subor_je_chyba", "subor sa nepodarilo zamknut");
    return;
  }
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  CloseHandle(handle);
  Check(!mounted && Contains(error, L"ZAMKNUTY.BIN"), "nedostupny_subor_je_chyba",
        mounted ? "priecinok sa pripojil" : Narrow(error));
}

// EXM is 0, so one directory entry covers 128 records (16 KiB); a longer file
// needs a second entry and stays one file in the directory.
void FileSpanningTwoExtents() {
  const fs::path folder = MakeFolder("dva-extenty");
  MakeFile(folder / "DLHY.BIN", 20000, 7);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(mounted && disk.StoredFiles() == 1, "subor_cez_dva_extenty",
        mounted ? "v adresari " + std::to_string(disk.StoredFiles()) : Narrow(error));
  if (!mounted) return;
  std::string detail;
  Check(ExportMatches(disk, folder, "dva-extenty", detail),
        "subor_cez_dva_extenty_export", detail);
}

void EmptyFile() {
  const fs::path folder = MakeFolder("prazdny-subor");
  MakeFile(folder / "PRAZDNY.BIN", 0, 1);
  MakeFile(folder / "PLNY.BIN", 300, 2);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(mounted && disk.StoredFiles() == 2, "prazdny_subor_ma_polozku",
        mounted ? "v adresari " + std::to_string(disk.StoredFiles()) : Narrow(error));
  if (!mounted) return;
  std::string detail;
  Check(ExportMatches(disk, folder, "prazdny-subor", detail),
        "prazdny_subor_export", detail);
}

// Two host names that fold onto the same 8.3 name must still arrive as two
// files, not one overwriting the other.
void NameCollision() {
  const fs::path folder = MakeFolder("kolizia");
  MakeFile(folder / L"dlhy nazov jeden.bas", 200, 1);
  MakeFile(folder / L"dlhy nazov dva.bas", 300, 2);
  VirtualDisk disk;
  std::wstring error;
  const bool mounted = disk.Mount(folder, error);
  Check(mounted && disk.StoredFiles() == 2, "kolizia_mien_8_3",
        mounted ? "v adresari " + std::to_string(disk.StoredFiles()) : Narrow(error));
}

// Changing the diskette while the machine runs.  The guest needs no telling --
// EurekaDOS re-logs the drive from the directory checksums by itself (HANDOFF
// 6.17) -- but only because the host really does put a different image under
// it.  An image merely written over, with the old file still readable in a
// block the new one does not use, would give the guest a directory that
// disagrees with its own data.
void SwapReplacesTheWholeImage() {
  const fs::path first = MakeFolder("vymena-prva");
  MakeFile(first / L"PRVA.TXT", 4000, 1);
  MakeFile(first / L"NAVYSE.TXT", 9000, 2);
  const fs::path second = MakeFolder("vymena-druha");
  MakeFile(second / L"DRUHA.TXT", 4000, 3);

  VirtualDisk disk;
  std::wstring error;
  Check(disk.Mount(first, error) && disk.StoredFiles() == 2,
        "prva_disketa_ma_dva_subory", Narrow(error));

  uint8_t before[512]{};
  Check(disk.ReadPhysicalSector(0, 0, 1, before), "prva_disketa_sa_cita");

  Check(disk.Mount(second, error), "vymena_za_druhu_prejde", Narrow(error));
  Check(disk.StoredFiles() == 1, "po_vymene_je_v_adresari_len_novy_subor",
        "v adresari " + std::to_string(disk.StoredFiles()));
  Check(disk.folder() == fs::weakly_canonical(second),
        "po_vymene_ukazuje_disk_na_novy_priecinok");

  uint8_t after[512]{};
  Check(disk.ReadPhysicalSector(0, 0, 1, after), "druha_disketa_sa_cita");
  // The directory is the first four blocks, so sector 1 holds entries.  Two
  // different file sets cannot leave it identical.
  Check(std::memcmp(before, after, sizeof(before)) != 0,
        "adresar_po_vymene_nie_je_ten_isty");

  // The 9 KiB file from the first diskette occupied blocks the second one
  // never touches.  If the image were reused rather than rebuilt, its bytes
  // would still be sitting there for the guest to read back.
  uint8_t leftovers[512]{};
  bool anythingLeft = false;
  for (unsigned sector = 1; sector <= 10 && !anythingLeft; ++sector) {
    if (!disk.ReadPhysicalSector(1, 1, sector, leftovers)) continue;
    for (uint8_t byte : leftovers)
      if (byte != 0xe5 && byte != 0) anythingLeft = true;
  }
  Check(!anythingLeft, "po_vymene_nezostali_data_z_prvej_diskety");
}

void EjectLeavesAnEmptyDrive() {
  const fs::path folder = MakeFolder("vysunutie");
  MakeFile(folder / L"NIECO.TXT", 3000, 4);
  VirtualDisk disk;
  std::wstring error;
  Check(disk.Mount(folder, error), "disketa_na_vysunutie_sa_pripoji",
        Narrow(error));

  disk.Eject();
  Check(!disk.present(), "po_vysunuti_nie_je_medium");
  Check(disk.StoredFiles() == 0, "po_vysunuti_je_adresar_prazdny");
  Check(disk.folder().empty(), "po_vysunuti_nie_je_ziadny_priecinok");
  uint8_t sector[512]{};
  // What the firmware sees: no medium means Record Not Found on every read,
  // which is how it reaches its own "vadny disk" (6.6).
  Check(!disk.ReadPhysicalSector(0, 0, 1, sector),
        "z_prazdnej_mechaniky_sa_neda_citat");
  Check(!disk.WritePhysicalSector(0, 0, 1, sector),
        "do_prazdnej_mechaniky_sa_neda_pisat");

  // And the drive takes a diskette again afterwards.
  Check(disk.Mount(folder, error) && disk.StoredFiles() == 1,
        "po_vysunuti_sa_da_vlozit_znovu", Narrow(error));
}

// A diskette that has never been formatted.  It is in the drive -- the index
// hole goes round -- but no track answers, which is how a real blank behaved
// and how the machine reaches its own "vadny disk".  Formatting it track by
// track is what Write Track does, and that is the only way it becomes usable.
void UnformattedDisketteAnswersNothing() {
  VirtualDisk disk;
  disk.CreateRamDisk(false);
  Check(disk.present(), "nenaformatovana_disketa_je_vlozena");

  uint8_t sector[512]{};
  Check(!disk.ReadPhysicalSector(0, 0, 1, sector),
        "nenaformatovana_stopa_sa_neda_citat");
  Check(!disk.WritePhysicalSector(0, 0, 1, sector),
        "na_nenaformatovanu_stopu_sa_neda_pisat");
  // The BIOS stub bypasses the controller entirely, so it has to refuse too:
  // a diskette that says no to the FDC and yes to the BIOS is a machine that
  // never existed.
  uint8_t record[128]{};
  Check(!disk.ReadRecord(0, 0, record), "bios_cesta_odmietne_nenaformatovanu");
  Check(!disk.WriteRecord(0, 0, record),
        "bios_zapis_odmietne_nenaformatovanu");
  Check(disk.StoredFiles() == 0, "nenaformatovana_nema_adresar");

  // One track laid down, and only that one works.
  disk.FormatTrack(0, 0);
  Check(disk.TrackFormatted(0, 0), "naformatovana_stopa_je_naformatovana");
  Check(disk.ReadPhysicalSector(0, 0, 1, sector),
        "po_naformatovani_sa_stopa_cita");
  Check(sector[0] == 0xe5, "naformatovana_stopa_je_prazdna");
  Check(!disk.TrackFormatted(0, 1), "susedna_strana_zostala_nenaformatovana");
  Check(!disk.ReadPhysicalSector(1, 0, 1, sector),
        "dalsi_cylinder_zostal_nenaformatovany");

  // The whole diskette, the way the firmware's format routine walks it.
  for (unsigned cylinder = 0; cylinder < 80; ++cylinder)
    for (unsigned side = 0; side <= 1; ++side) disk.FormatTrack(cylinder, side);
  Check(disk.ReadPhysicalSector(79, 1, 10, sector),
        "po_celom_formatovani_odpoveda_aj_posledna_stopa");
  Check(disk.ReadRecord(0, 0, record), "po_formatovani_odpoveda_aj_bios_cesta");
}

// A RAM diskette made formatted behaves like any other, and formatting over
// data really does destroy it -- that is what formatting is.
void FormattedRamDiskAndReformatting() {
  VirtualDisk disk;
  disk.CreateRamDisk();
  uint8_t sector[512]{};
  Check(disk.ReadPhysicalSector(0, 0, 1, sector),
        "naformatovana_ram_disketa_sa_cita_hned");

  std::fill_n(sector, sizeof(sector), 0x42);
  Check(disk.WritePhysicalSector(3, 1, 5, sector), "na_ram_disketu_sa_da_pisat");
  uint8_t back[512]{};
  Check(disk.ReadPhysicalSector(3, 1, 5, back) && back[0] == 0x42,
        "zapisane_data_sa_precitaju_spat");

  disk.FormatTrack(3, 1);
  Check(disk.ReadPhysicalSector(3, 1, 5, back) && back[0] == 0xe5,
        "formatovanie_stopu_naozaj_zmaze");
}

// The same call behind a host folder must change nothing: the diskette is the
// user's own folder and the emulated machine formatting is not a reason to
// delete their files (6.5).
void FormattingAFolderDiskChangesNothing() {
  const fs::path folder = MakeFolder("format-priecinok");
  MakeFile(folder / L"DOLEZITE.TXT", 3000, 7);
  VirtualDisk disk;
  std::wstring error;
  Check(disk.Mount(folder, error), "priecinkova_disketa_sa_pripoji",
        Narrow(error));

  uint8_t before[512]{};
  Check(disk.ReadPhysicalSector(0, 0, 1, before),
        "priecinkova_disketa_sa_cita");
  disk.FormatTrack(0, 0);
  uint8_t after[512]{};
  Check(disk.ReadPhysicalSector(0, 0, 1, after),
        "po_formatovani_sa_stale_cita");
  Check(std::memcmp(before, after, sizeof(before)) == 0,
        "formatovanie_priecinkovej_diskety_adresar_nezmeni");
  Check(disk.StoredFiles() == 1, "subor_v_priecinku_formatovanie_prezil");
}

void EmptyFolderAndMissingFolder() {
  const fs::path folder = MakeFolder("prazdny");
  VirtualDisk disk;
  std::wstring error;
  Check(disk.Mount(folder, error) && disk.imported_files() == 0,
        "prazdny_priecinok_sa_pripoji", Narrow(error));
  VirtualDisk missing;
  Check(!missing.Mount(Root() / "tento-neexistuje", error),
        "neexistujuci_priecinok_odmietne");
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

  FullDiskette();
  TwoBlocksTooMany();
  FullDirectory();
  OneEntryTooMany();
  TinyFilesEatWholeBlocks();
  SubfolderIsIgnored();
  UnreadableFileIsAnError();
  FileSpanningTwoExtents();
  EmptyFile();
  NameCollision();
  SwapReplacesTheWholeImage();
  EjectLeavesAnEmptyDrive();
  UnformattedDisketteAnswersNothing();
  FormattedRamDiskAndReformatting();
  FormattingAFolderDiskChangesNothing();
  EmptyFolderAndMissingFolder();

  fs::remove_all(Root(), ec);
  std::cout << (failures == 0 ? "PASS" : "FAIL") << " mode=DISK kontrol=" << checks
            << " chyb=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
