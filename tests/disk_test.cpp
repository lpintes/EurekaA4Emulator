// Capacity and import rules of the diskette that has a home folder.
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
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "disk_stash.h"
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
  Check(disk.home() == fs::weakly_canonical(second),
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
  Check(disk.home().empty(), "po_vysunuti_nie_je_ziadny_priecinok");
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
  disk.CreateEmpty(false);
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

// A homeless diskette made formatted behaves like any other, and formatting over
// data really does destroy it -- that is what formatting is.
void FormattedRamDiskAndReformatting() {
  VirtualDisk disk;
  disk.CreateEmpty();
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

// The write protect notch, as far as the medium is concerned.  The controller
// refuses a protected write before it ever gets here (machine.cpp raises bit 6
// instead of running the command), so what this pins down is the second lock:
// nothing reaches the image by any other road either, the BIOS shortcut
// included.  Reading is untouched -- SYSEQU.LIB has bit 6 in write_error_mask
// and not in read_error_mask.
void WriteProtectStopsWritesAndNothingElse() {
  VirtualDisk disk;
  disk.CreateEmpty();
  Check(!disk.write_protected(), "nova_disketa_nie_je_chranena");

  uint8_t sector[512];
  std::fill_n(sector, sizeof(sector), 0x5a);
  Check(disk.WritePhysicalSector(2, 0, 3, sector), "nechranena_disketa_prijme_zapis");

  disk.set_write_protected(true);
  uint8_t other[512];
  std::fill_n(other, sizeof(other), 0x77);
  Check(!disk.WritePhysicalSector(2, 0, 3, other),
        "chranena_disketa_odmietne_zapis_cez_radic");
  uint8_t record[128];
  std::fill_n(record, sizeof(record), 0x77);
  Check(!disk.WriteRecord(0, 0, record), "chranena_disketa_odmietne_zapis_cez_bios");

  uint8_t back[512]{};
  Check(disk.ReadPhysicalSector(2, 0, 3, back), "chranena_disketa_sa_stale_cita");
  Check(back[0] == 0x5a, "odmietnuty_zapis_data_nezmenil");

  // The notch is a property of the diskette, so it cannot outlive the one it
  // was set for: a protected diskette taken out and a fresh one put in would
  // otherwise be protected too, and silently.
  const fs::path folder = MakeFolder("ochrana-priecinok");
  MakeFile(folder / L"SUBOR.TXT", 1000, 3);
  std::wstring error;
  Check(disk.Mount(folder, error), "priecinkova_disketa_sa_pripoji_po_chranenej",
        Narrow(error));
  Check(!disk.write_protected(), "vlozena_disketa_ochranu_nezdedi");

  disk.set_write_protected(true);
  disk.Eject();
  Check(!disk.write_protected(), "po_vysunuti_ochrana_nezostane");
  disk.CreateEmpty();
  Check(!disk.write_protected(), "nova_ram_disketa_ochranu_nezdedi");
}

// The stash: where a diskette is while it is not in the drive.
//
// This is the layer the owner's bulk-copy report turned on (29. 8. 2026).  The
// ROM fills the target, sends the user back for the source, then asks for the
// target again to finish the file it began -- and a slot that made a fresh
// empty diskette each time meant the target came back blank, the ROM said
// "soubor nelze najít" and cancelled the job having written nothing.  A slot
// is a place, not a recipe.
void StashKeepsDiskettesInMemory() {
  DiskStash stash;
  Check(stash.empty(), "novy zasobnik je prazdny");

  // On the heap here too: 800 KiB is more than a thread stack has to spare,
  // and this test used to crash before it printed a line.
  auto target = std::make_unique<VirtualDisk>();
  target->CreateEmpty();
  uint8_t sector[512];
  std::fill_n(sector, sizeof(sector), 0x42);
  Check(target->WritePhysicalSector(1, 0, 1, sector), "na cielovu sa da pisat");

  Check(stash.Put(2, *target), "disketa v pamati sa odlozi");
  Check(stash.holds(2), "slot 2 ju drzi");
  Check(!stash.empty(), "zasobnik uz nie je prazdny");

  // The same diskette back, with what was written on it -- the whole point.
  auto back = stash.Take(2);
  Check(back != nullptr, "disketa sa vrati");
  uint8_t read[512]{};
  Check(back && back->ReadPhysicalSector(1, 0, 1, read) && read[0] == 0x42,
        "vratena disketa ma svoj obsah");
  Check(!stash.holds(2), "po vybrati je slot prazdny");
  Check(stash.empty(), "zasobnik je zase prazdny");
  Check(stash.Take(2) == nullptr, "prazdny slot nevrati nic");
}

// A diskette with a home is declined on purpose: the folder holds it.
// Keeping a copy would be both wasteful and wrong -- a folder the user changed
// meanwhile has to come back changed, which is what the same diskette would do.
void StashDeclinesFolderDiskettes() {
  const fs::path folder = MakeFolder("zasobnik-priecinok");
  MakeFile(folder / L"SUBOR.TXT", 1000, 3);
  auto disk = std::make_unique<VirtualDisk>();
  std::wstring error;
  Check(disk->Mount(folder, error), "priecinkova_disketa_sa_pripoji_do_zasobnika",
        Narrow(error));

  DiskStash stash;
  Check(!stash.Put(1, *disk), "priecinkova disketa sa neodklada");
  Check(stash.empty(), "zasobnik po nej zostal prazdny");

  // Nor is an empty drive a diskette.
  auto nothing = std::make_unique<VirtualDisk>();
  Check(!stash.Put(1, *nothing), "prazdna mechanika sa neodklada");
}

// Every slot keeps its own diskette, and one slot's does not disturb another's.
// Nine places, not one shelf: a shelf would mean the second diskette put away
// destroyed the first, which is the same silent loss in a smaller box.
void StashKeepsEverySlotApart() {
  DiskStash stash;
  auto first = std::make_unique<VirtualDisk>();
  first->CreateEmpty();
  auto second = std::make_unique<VirtualDisk>();
  second->CreateEmpty();
  uint8_t sector[512];
  std::fill_n(sector, sizeof(sector), 0x11);
  first->WritePhysicalSector(0, 0, 1, sector);
  std::fill_n(sector, sizeof(sector), 0x22);
  second->WritePhysicalSector(0, 0, 1, sector);

  Check(stash.Put(1, *first), "prva disketa ide do slotu 1");
  Check(stash.Put(5, *second), "druha ide do slotu 5");
  Check(stash.holds(1) && stash.holds(5), "oba sloty drzia svoju disketu");

  auto back1 = stash.Take(1);
  auto back5 = stash.Take(5);
  uint8_t read[512]{};
  Check(back1 && back1->ReadPhysicalSector(0, 0, 1, read) && read[0] == 0x11,
        "slot 1 vratil svoju disketu");
  Check(back5 && back5->ReadPhysicalSector(0, 0, 1, read) && read[0] == 0x22,
        "slot 5 vratil svoju disketu");
  Check(stash.empty(), "po vybrati je zasobnik prazdny");

  // Slot 0 is not a place: a diskette with no slot has only the drive holding
  // it, and what happens to it is the user's decision, not this class's.
  Check(!stash.Put(0, *back1), "slot nula neexistuje");
  Check(stash.Take(0) == nullptr, "zo slotu nula sa nic nevrati");

  // What the emulator asks when it is closing: those diskettes are about to
  // stop existing, and one with files on it is worth a question.
  Check(!stash.HoldsAnythingWritten(), "prazdny zasobnik nema co stratit");
  auto empty = std::make_unique<VirtualDisk>();
  empty->CreateEmpty();
  stash.Put(3, *empty);
  Check(!stash.HoldsAnythingWritten(), "prazdna disketa nie je co stratit");
  stash.Put(4, *back1);
  Check(stash.HoldsAnythingWritten(), "disketa so suborom uz je");
}

// Lays bytes into the image the only way a guest can: through the controller.
// offset and the data length have to be whole 512 byte sectors.
bool PutImageBytes(VirtualDisk& disk, std::size_t offset,
                   const std::vector<uint8_t>& data) {
  for (std::size_t done = 0; done < data.size(); done += 512) {
    const std::size_t index = (offset + done) / 512;
    const unsigned track = static_cast<unsigned>(index / 10);
    if (!disk.WritePhysicalSector(track / 2, track % 2,
                                  static_cast<unsigned>(index % 10) + 1,
                                  data.data() + done)) {
      return false;
    }
  }
  return true;
}

// One directory entry: name, record count and the blocks holding the data.
struct DirEntry {
  std::string stem;
  std::string type;
  unsigned records = 1;
  unsigned firstBlock = 0;
  unsigned blocks = 1;
};

// The entries packed the way the directory really holds them, sixteen to a
// sector, so a test can lay down more than sixteen files.
std::vector<uint8_t> DirectoryImage(const std::vector<DirEntry>& entries) {
  const std::size_t sectors = (entries.size() + 15) / 16;
  std::vector<uint8_t> image(sectors * 512, 0xe5);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const DirEntry& e = entries[i];
    uint8_t* d = image.data() + i * 32;
    std::fill(d, d + 32, 0);
    std::fill(d + 1, d + 12, ' ');
    std::copy(e.stem.begin(), e.stem.end(), d + 1);
    std::copy(e.type.begin(), e.type.end(), d + 9);
    d[15] = static_cast<uint8_t>(e.records);
    for (unsigned slot = 0; slot < e.blocks; ++slot) {
      d[16 + slot * 2] = static_cast<uint8_t>(e.firstBlock + slot);
      d[17 + slot * 2] = static_cast<uint8_t>((e.firstBlock + slot) >> 8);
    }
  }
  return image;
}

// A program written by the guest onto an unsaved diskette and then saved to a
// host folder.  Nothing in imported_ knows its length -- there was no host file
// to import -- so the export has only the diskette to go by, and 01Ah inside a
// tokenised BASIC program is data, not the end of it.  Measured on the real
// LET.BAS: 9856 bytes with a 01Ah at 6399, exported as 6399 and the program
// gone from there on, silently.
void RamDisketteKeepsBinaryFilesWhole() {
  VirtualDisk disk;
  disk.CreateEmpty();

  // 77 records, the size of LET.BAS, in five blocks starting right after the
  // directory.  0C2h and the 16-bit program length are the real .BAS header.
  const std::size_t exact = 9856;
  std::vector<uint8_t> program(10240, 0x1a);
  program[0] = 0xc2;
  program[1] = static_cast<uint8_t>((9801 - 3) & 0xff);
  program[2] = static_cast<uint8_t>((9801 - 3) >> 8);
  for (std::size_t i = 3; i < 9801; ++i)
    program[i] = static_cast<uint8_t>(1 + (i * 37) % 200);
  program[6399] = 0x1a;  // where the real file has one
  program[6400] = 0x1a;

  // A text file next to it, so the rule that 01Ah does end a .TXT stays checked
  // rather than merely removed.
  std::vector<uint8_t> note(2048, 0xe5);
  const std::string text = "Poznamka.";
  std::copy(text.begin(), text.end(), note.begin());
  note[text.size()] = 0x1a;

  // And the same content under a name with no extension at all, which is what a
  // note typed on the machine looks like.  There is no type to go by, so it
  // takes the safe branch: whole records, 01Ah left where it is.
  std::vector<uint8_t> plain(2048, 0xe5);
  std::copy(text.begin(), text.end(), plain.begin());
  plain[text.size()] = 0x1a;

  std::vector<DirEntry> entries;
  entries.push_back({"HRALET", "BAS", 77, 4, 5});
  entries.push_back({"POZNAMKA", "TXT", 1, 9, 1});
  entries.push_back({"POZNAMKY", "", 1, 10, 1});

  const bool laid =
      PutImageBytes(disk, 0, DirectoryImage(entries)) &&
      PutImageBytes(disk, 4 * 2048, program) &&
      PutImageBytes(disk, 9 * 2048, note) &&
      PutImageBytes(disk, 10 * 2048, plain);
  Check(laid && disk.StoredFiles() == 3, "ram_disketa_prijala_zapis_hosta",
        "v adresari " + std::to_string(disk.StoredFiles()));
  if (!laid) return;

  const fs::path target = MakeFolder("ram-export");
  std::wstring error;
  Check(disk.ExportTo(target, error), "ram_disketa_sa_ulozi_do_priecinka",
        Narrow(error));

  const std::vector<uint8_t> exported = ReadAll(target / "HRALET.BAS");
  Check(exported.size() == exact, "bas_z_ram_diskety_sa_neskrati_na_1ah",
        "ulozenych " + std::to_string(exported.size()) + " z " +
            std::to_string(exact));
  Check(exported.size() == exact &&
            std::equal(exported.begin(), exported.end(), program.begin()),
        "bas_z_ram_diskety_je_bajt_na_bajt");

  const std::vector<uint8_t> savedNote = ReadAll(target / "POZNAMKA.TXT");
  Check(savedNote.size() == text.size(), "txt_z_ram_diskety_konci_na_1ah",
        "ulozenych " + std::to_string(savedNote.size()) + " z " +
            std::to_string(text.size()));

  // No dot in the name, so IsTextType never gets a type to judge: the file
  // arrives under its bare name and keeps its whole record.
  const std::vector<uint8_t> savedPlain = ReadAll(target / "POZNAMKY");
  Check(fs::exists(target / "POZNAMKY"), "subor_bez_pripony_sa_exportuje");
  Check(savedPlain.size() == 128, "subor_bez_pripony_sa_neoreze",
        "ulozenych " + std::to_string(savedPlain.size()) + " z 128");
}

// Which types the export may cut at 01Ah, pinned one type at a time.  The list
// in IsTextType is an allowlist and its two mistakes cost differently: a type
// wrongly called text loses data for good, a type wrongly called binary keeps
// a few bytes of padding.  So the roster below is the barrier -- moving a type
// across it has to fail this test rather than surface as a damaged file months
// later (6.23).
//
// Text: the word processor's own output (FILE-FMT.D says its last character is
// always 01Ah) and the development disk's sources.  Binary: everything Eureka
// itself writes.  Measured over 723 real files, cutting these at the first
// 01Ah would have destroyed 61 of 108 .BAS, 78 of 81 .COM, 95 of 240 .MEL and
// all 65 archives.
void FileTypeClassificationIsPinned() {
  struct Type {
    const char* extension;
    bool text;
  };
  static const Type roster[] = {
      {"TXT", true},  {"DOC", true},  {"PAS", true},  {"C", true},
      {"H", true},    {"ASM", true},  {"MAC", true},  {"LIB", true},
      {"INC", true},  {"BAT", true},  {"SUB", true},
      {"BAS", false}, {"COM", false}, {"MEL", false}, {"TEL", false},
      {"DIA", false}, {"DAT", false}, {"ARK", false}, {"ARC", false},
      {"MBS", false}, {"OVR", false}, {"SYS", false}, {"GRF", false},
      {"SNG", false}, {"X0", false},  {"X1", false},  {"DEF", false},
      {"LST", false}, {"", false},
  };
  // .DEF and .LST are text by the manual ("a text file that can be edited
  // freely with the Word Processor", "a simple text file") and are still off
  // the list: the owner's call, since an unlisted type only carries padding
  // while a wrongly listed one loses data.  Moving them is a decision, not a
  // bug fix, so it belongs here as a deliberate edit.
  //
  // The empty extension is the note typed on the machine with no dot in its
  // name at all -- there is no type to judge, so it keeps its whole record.

  // "AAA", the marker, then real content behind it.  A type treated as text
  // comes back as the three bytes before the marker; any other type keeps the
  // whole record, so the two outcomes cannot be confused.
  std::vector<uint8_t> content(2048, 0x1a);
  const char* head = "AAA";
  const char* tail = "BBB";
  std::copy(head, head + 3, content.begin());
  std::copy(tail, tail + 3, content.begin() + 4);

  VirtualDisk disk;
  disk.CreateEmpty();
  std::vector<DirEntry> entries;
  unsigned block = 4;
  for (const Type& type : roster) {
    char stem[16];
    std::snprintf(stem, sizeof stem, "T%02u", static_cast<unsigned>(entries.size()));
    entries.push_back({stem, type.extension, 1, block, 1});
    if (!PutImageBytes(disk, block * 2048, content)) {
      Check(false, "klasifikacia_typov_zapis", "blok " + std::to_string(block));
      return;
    }
    ++block;
  }
  if (!PutImageBytes(disk, 0, DirectoryImage(entries))) {
    Check(false, "klasifikacia_typov_adresar");
    return;
  }

  const fs::path target = MakeFolder("typy-export");
  std::wstring error;
  if (!disk.ExportTo(target, error)) {
    Check(false, "klasifikacia_typov_export", Narrow(error));
    return;
  }

  std::string wrong;
  unsigned compared = 0;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const Type& type = roster[i];
    std::string name = entries[i].stem;
    if (*type.extension) name += std::string(".") + type.extension;
    const std::size_t got = ReadAll(target / name).size();
    const std::size_t want = type.text ? 3u : 128u;
    if (got != want) {
      wrong += (wrong.empty() ? "" : ", ") + name + " " + std::to_string(got) +
               " (cakane " + std::to_string(want) + ")";
    }
    ++compared;
  }
  Check(compared == entries.size() && wrong.empty(),
        "klasifikacia_typov_je_pribita", wrong);
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
  WriteProtectStopsWritesAndNothingElse();
  StashKeepsDiskettesInMemory();
  StashDeclinesFolderDiskettes();
  StashKeepsEverySlotApart();
  RamDisketteKeepsBinaryFilesWhole();
  FileTypeClassificationIsPinned();
  EmptyFolderAndMissingFolder();

  fs::remove_all(Root(), ec);
  std::cout << (failures == 0 ? "PASS" : "FAIL") << " mode=DISK kontrol=" << checks
            << " chyb=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
