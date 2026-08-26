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

// CP/M has no directories, so a subfolder cannot go on the diskette -- but it
// must not disappear without a word either.
void SubfolderIsReported() {
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
  Check(disk.skipped_entries().size() == 1 &&
            disk.skipped_entries().front() == L"vnutri",
        "podpriecinok_sa_ohlasi");
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
  SubfolderIsReported();
  UnreadableFileIsAnError();
  FileSpanningTwoExtents();
  EmptyFile();
  NameCollision();
  EmptyFolderAndMissingFolder();

  fs::remove_all(Root(), ec);
  std::cout << (failures == 0 ? "PASS" : "FAIL") << " mode=DISK kontrol=" << checks
            << " chyb=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
