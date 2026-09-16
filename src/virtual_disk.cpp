#include "virtual_disk.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// The geometry below and the 8.3 names further down are cpm_disk.h's, not this
// file's.  disk_layout.cpp plans against the very same numbers and the very
// same naming, and a second copy of either would let a plan promise a diskette
// that this file then cannot build -- silently, because both halves would go
// on compiling and testing green.
using cpm::BlocksFor;
using cpm::EntriesFor;
using cpm::kAvailableBlocks;
using cpm::kBlockSize;
using cpm::kDirectoryEntries;
using cpm::kFirstDataBlock;
using cpm::kLastBlock;
using cpm::kRecordsPerEntry;

std::string Trim(const uint8_t* begin, std::size_t length) {
  std::string value(reinterpret_cast<const char*>(begin), length);
  for (char& ch : value) ch = static_cast<char>(ch & 0x7f);
  while (!value.empty() && value.back() == ' ') value.pop_back();
  return value;
}

// A name field as the firmware matches it: bit 7 ignored, case kept.  Both are
// measured, not assumed -- a file named with a lower-case letter is a
// different file to it, and one differing only in bit 7 is the same (6.36).
std::string NameOf(const uint8_t* name) {
  const std::string stem = Trim(name, 8);
  const std::string type = Trim(name + 8, 3);
  return type.empty() ? stem : stem + "." + type;
}

constexpr std::size_t kNameBytes = 11;

// What a host file name cannot carry: the attribute bits CP/M keeps in bit 7
// of the name and lower-case letters, which the import folds away because a
// host file called readme.txt has to arrive as README.TXT.  Copy protection
// lives in exactly these -- EUŠOU checks bit 7 of its own name, Sokoban opens
// a file with a lower-case letter in it (HANDOFF 6.36) -- so for the files
// that need it the exact bytes go into this one file next to them.  It exists
// only while some file needs it, so a plain diskette folder stays plain.
const wchar_t kExactNamesFile[] = L".eureka";
// Written first and renamed over, so a failed write cannot leave half a list.
const wchar_t kExactNamesScratch[] = L".eureka-novy";

std::string PlainName(const std::string& cpmName) {
  std::string exact(kNameBytes, ' ');
  const std::size_t dot = cpmName.find('.');
  const std::string stem = cpmName.substr(0, dot);
  const std::string type = dot == std::string::npos ? "" : cpmName.substr(dot + 1);
  std::copy(stem.begin(), stem.end(), exact.begin());
  std::copy(type.begin(), type.end(), exact.begin() + 8);
  return exact;
}

bool NeedsExactName(const std::string& exact) {
  for (unsigned char ch : exact) {
    const unsigned plain = ch & 0x7f;
    if (ch != plain || (plain >= 'a' && plain <= 'z')) return true;
  }
  return false;
}

std::string ToHex(const std::string& bytes) {
  static const char kDigits[] = "0123456789ABCDEF";
  std::string text;
  for (unsigned char ch : bytes) {
    text += kDigits[ch >> 4];
    text += kDigits[ch & 15];
  }
  return text;
}

bool FromHex(const std::string& text, std::string& bytes) {
  if (text.size() != kNameBytes * 2) return false;
  auto digit = [](char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
  };
  bytes.clear();
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const int high = digit(text[i]);
    const int low = digit(text[i + 1]);
    if (high < 0 || low < 0) return false;
    bytes += static_cast<char>(high * 16 + low);
  }
  return true;
}

// Host file name -> exact name bytes.  A line that does not parse is skipped,
// and so is a line for a file that is no longer there: it simply never gets
// looked up.  A file that exists and cannot be read is an error, because
// going on without it would hand the guest names its programs refuse.
bool ReadExactNames(const fs::path& folder,
                    std::map<std::wstring, std::string>& names,
                    std::wstring& error) {
  const fs::path file = folder / kExactNamesFile;
  std::error_code ec;
  const bool exists = fs::exists(file, ec);
  std::ifstream input;
  if (exists) input.open(file, std::ios::binary);
  if (ec || (exists && !input)) {
    error = L"Súbor .eureka v priečinku diskety sa nedá prečítať, preto som "
            L"disketu nezostavil.";
    return false;
  }
  std::string line;
  while (exists && std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::size_t tab = line.rfind('\t');
    std::string exact;
    if (tab == std::string::npos || tab == 0 ||
        !FromHex(line.substr(tab + 1), exact)) {
      continue;
    }
    const std::u8string leaf(line.begin(), line.begin() + tab);
    names[fs::path(leaf).wstring()] = exact;
  }
  if (input.bad()) {
    error = L"Pri čítaní súboru .eureka v priečinku diskety nastala chyba, "
            L"disketu som nezostavil.";
    return false;
  }
  return true;
}

// Rewritten only when the list really changed, and removed once nothing needs
// it any more.
bool WriteExactNames(const fs::path& folder, const std::string& content,
                     std::wstring& error) {
  const fs::path file = folder / kExactNamesFile;
  std::error_code ec;
  if (content.empty()) {
    fs::remove(file, ec);
    if (ec) {
      error = L"Nemožno odstrániť súbor " + file.wstring();
      return false;
    }
    return true;
  }
  {
    std::ifstream current(file, std::ios::binary);
    if (current) {
      const std::string existing((std::istreambuf_iterator<char>(current)), {});
      if (existing == content) return true;
    }
  }
  const fs::path scratch = folder / kExactNamesScratch;
  {
    std::ofstream output(scratch, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output.flush()) {
      error = L"Nemožno zapísať súbor " + file.wstring();
      return false;
    }
  }
  fs::rename(scratch, file, ec);
  if (ec) {
    fs::remove(scratch, ec);
    error = L"Nemožno zapísať súbor " + file.wstring();
    return false;
  }
  return true;
}

// Windows compares file names without regard to case, the firmware with it.
// Two diskette files that differ only in case would otherwise land in one host
// file, the second overwriting the first.  ASCII is enough: the names this is
// asked about come out of the directory with bit 7 cleared.
std::wstring FoldCase(std::wstring leaf) {
  for (wchar_t& ch : leaf) {
    if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
  }
  return leaf;
}

std::wstring FreeLeaf(const std::wstring& wanted, const std::set<std::wstring>& taken) {
  if (!taken.contains(FoldCase(wanted))) return wanted;
  const std::size_t dot = wanted.find(L'.');
  const std::wstring stem = wanted.substr(0, dot);
  const std::wstring type = dot == std::wstring::npos ? L"" : wanted.substr(dot);
  for (unsigned number = 1;; ++number) {
    const std::wstring candidate = stem + L"~" + std::to_wstring(number) + type;
    if (!taken.contains(FoldCase(candidate))) return candidate;
  }
}

std::wstring Kibibytes(uint64_t blocks) {
  return std::to_wstring(blocks * (kBlockSize / 1024)) + L" KiB";
}

// Slovak counts in three shapes -- 1 blok, 2 bloky, 5 blokov -- and this message
// is read aloud by a screen reader, where a wrong ending is heard, not skimmed.
std::wstring Count(uint64_t number, const wchar_t* one, const wchar_t* few,
                   const wchar_t* many) {
  return std::to_wstring(number) + L" " +
         (number == 1 ? one : (number >= 2 && number <= 4 ? few : many));
}
}  // namespace

bool VirtualDisk::Mount(const fs::path& folder, std::wstring& error,
                        bool* tooBig) {
  std::error_code ec;
  fs::path absolute = fs::weakly_canonical(folder, ec);
  if (ec || !fs::is_directory(absolute, ec)) {
    error = L"Vybraný diskový priečinok neexistuje alebo nie je prístupný.";
    return false;
  }
  home_ = absolute;
  present_ = true;
  // The notch rides with the diskette, so a newly mounted one is unprotected
  // until the host says otherwise.
  write_protected_ = false;
  // A host folder is a filesystem: it is formatted by definition, and there
  // is no state in which some of its tracks are missing.
  formatted_.set();
  if (BuildImage(error, tooBig)) return true;
  // A refused diskette leaves nothing behind: the half-built file list would
  // otherwise still be reported as if it were mounted.
  present_ = false;
  home_.clear();
  imported_.clear();
  formatted_.reset();
  return false;
}

// The image is wiped rather than merely marked absent, so that a diskette put
// in afterwards cannot show a single byte of the one before it.  present() is
// what the firmware sees: with no medium the drive reports no INDEX pulse and
// every Type II command comes back Record Not Found (6.6).
void VirtualDisk::Eject() {
  image_.fill(0);
  formatted_.reset();
  imported_.clear();
  home_.clear();
  present_ = false;
  dirty_ = false;
  write_protected_ = false;
}

// A diskette with no home: 0E5h everywhere is exactly what a freshly formatted
// CP/M disk looks like, so the firmware sees an empty directory without any
// host folder behind it.  SaveAs is what gives it one.
//
// Unformatted, it is 0E5h too, but no track answers: the drive is what a blank
// out of the box was, and the machine says "vadny disk" until Shift+F8 has
// been through it.
void VirtualDisk::CreateEmpty(bool formatted) {
  image_.fill(0xe5);
  if (formatted) formatted_.set(); else formatted_.reset();
  imported_.clear();
  home_.clear();
  present_ = true;
  dirty_ = false;
  write_protected_ = false;
}

bool VirtualDisk::ValidTrack(unsigned cylinder, unsigned side) {
  return cylinder < kLogicalTracks / 2 && side <= 1;
}

// The format routine walks one track at a time and verifies each one it wrote,
// so this is what turns a blank into a working diskette -- and, on a diskette
// that already had data, what wipes the track it just went over.  That is not
// carelessness, it is what formatting is.
void VirtualDisk::FormatTrack(unsigned cylinder, unsigned side) {
  // A home folder is a filesystem, not a magnetic surface: the format still
  // reports success, but nothing is laid down and no file is erased (6.5).
  if (!formatting_erases() || !ValidTrack(cylinder, side)) return;
  const unsigned track = cylinder * 2 + side;
  formatted_.set(track);
  const std::size_t offset = track * kRecordsPerTrack * kRecordSize;
  std::fill_n(image_.data() + offset, kRecordsPerTrack * kRecordSize, 0xe5);
  dirty_ = true;
}

bool VirtualDisk::TrackFormatted(unsigned cylinder, unsigned side) const {
  if (!ValidTrack(cylinder, side)) return false;
  return formatted_.test(cylinder * 2 + side);
}

std::size_t VirtualDisk::StoredFiles() const {
  if (!present_) return 0;
  std::map<std::string, bool> names;
  for (unsigned index = 0; index < kDirectoryEntries; ++index) {
    const uint8_t* entry = image_.data() + index * 32;
    if (entry[0] > 0x1f) continue;
    const std::string name = DirectoryName(entry);
    if (!name.empty()) names[name] = true;
  }
  return names.size();
}

uint64_t VirtualDisk::Hash(const uint8_t* data, std::size_t size) {
  uint64_t hash = 1469598103934665603ull;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

// Everything the host folder offers, in the order the diskette will show it.
// Anything that cannot be looked at is an error rather than a quiet omission:
// a file missing from the diskette is indistinguishable from a lost file, and
// the machine gives its user no way to notice.
bool VirtualDisk::ScanFolder(std::vector<SourceFile>& files, std::wstring& error) {
  std::error_code ec;
  fs::directory_iterator entry(home_, ec);
  if (ec) {
    error = L"Priečinok " + home_.wstring() + L" sa nedá prečítať.";
    return false;
  }
  const fs::directory_iterator end;
  while (entry != end) {
    const fs::path path = entry->path();
    const std::wstring leaf = path.filename().wstring();
    const fs::file_status status = entry->status(ec);
    if (ec) {
      error = L"Položku " + leaf + L" v priečinku diskety sa nepodarilo "
              L"preskúmať, preto som disketu nezostavil.";
      return false;
    }
    // Only files, and only the top level.  A CP/M directory entry is a name,
    // a user number and extents -- there is nowhere to hang a tree -- so a
    // subfolder is skipped and stays untouched in the host folder.  Our own
    // bookkeeping (.eureka, .eureka-trash and friends) belongs to the host too.
    if (!leaf.starts_with(L".eureka") && fs::is_regular_file(status)) {
      SourceFile file;
      file.path = path;
      file.size = entry->file_size(ec);
      if (ec) {
        error = L"Veľkosť súboru " + leaf + L" sa nedá zistiť, preto neviem "
                L"povedať, či sa priečinok na disketu zmestí.";
        return false;
      }
      files.push_back(std::move(file));
    }
    entry.increment(ec);
    if (ec) {
      error = L"Prehľadávanie priečinka " + home_.wstring() + L" sa prerušilo, "
              L"disketu som nezostavil.";
      return false;
    }
  }
  std::sort(files.begin(), files.end(), [](const SourceFile& left, const SourceFile& right) {
    return left.path.filename().wstring() < right.path.filename().wstring();
  });
  return true;
}

// Checked up front so the message can name the real limit.  A folder can sit
// well under 800 KiB and still not fit: CP/M hands out 2 KiB blocks, so 258
// melodies of a few hundred bytes each claim 258 blocks between them.
bool VirtualDisk::CheckCapacity(const std::vector<SourceFile>& files,
                                std::wstring& error) const {
  uint64_t neededBlocks = 0;
  uint64_t neededEntries = 0;
  uint64_t totalBytes = 0;
  for (const SourceFile& file : files) {
    neededBlocks += BlocksFor(file.size);
    neededEntries += EntriesFor(file.size);
    totalBytes += file.size;
  }
  if (neededBlocks <= kAvailableBlocks && neededEntries <= kDirectoryEntries) return true;

  error = L"Priečinok sa nezmestí na disketu Eureky.\r\n";
  if (neededBlocks > kAvailableBlocks) {
    error += L"Jeho obsah (" + Count(files.size(), L"súbor", L"súbory", L"súborov") +
             L") zaberie " + Count(neededBlocks, L"blok", L"bloky", L"blokov") +
             L" po 2 KiB, disketa má " + std::to_wstring(kAvailableBlocks) +
             L", teda " + Kibibytes(kAvailableBlocks) + L". Prebytok je " +
             Count(neededBlocks - kAvailableBlocks, L"blok", L"bloky", L"blokov") +
             L", teda " + Kibibytes(neededBlocks - kAvailableBlocks) + L".\r\n";
    if (totalBytes <= static_cast<uint64_t>(kAvailableBlocks) * kBlockSize) {
      error += L"Aj jednobajtový súbor zaberie celý 2 KiB blok, preto sa "
               L"priečinok nezmestí, hoci má dokopy " +
               std::to_wstring((totalBytes + 1023) / 1024) + L" KiB.\r\n";
    }

    // The largest files are the ones worth moving out, and their names are the
    // only part of this message the user can act on straight away.
    std::vector<const SourceFile*> largest;
    for (const SourceFile& file : files) largest.push_back(&file);
    std::sort(largest.begin(), largest.end(),
              [](const SourceFile* left, const SourceFile* right) {
                return left->size > right->size;
              });
    largest.resize(std::min<std::size_t>(3, largest.size()));
    if (!largest.empty()) {
      error += L"Najväčšie súbory: ";
      for (std::size_t i = 0; i < largest.size(); ++i) {
        if (i) error += L", ";
        error += largest[i]->path.filename().wstring() + L" (" +
                 Kibibytes(BlocksFor(largest[i]->size)) + L")";
      }
      error += L".\r\n";
    }
  }
  if (neededEntries > kDirectoryEntries) {
    error += L"Jeho obsah (" + Count(files.size(), L"súbor", L"súbory", L"súborov") +
             L") potrebuje " +
             Count(neededEntries, L"položku", L"položky", L"položiek") +
             L" adresára, disketa má " + std::to_wstring(kDirectoryEntries) +
             L". Viac súborov na jednu disketu nejde ani vtedy, keď sú maličké; "
             L"súbor nad 16 KiB si navyše vyžiada ďalšiu položku.\r\n";
  }
  // The advice used to end here -- "split the folder into several folders and
  // swap them like diskettes" -- and that is now a thing the emulator does
  // rather than a thing it asks for (6.22).  Naming the menu item and nothing
  // else on purpose: the window offers to open it straight away, and a
  // sentence that promised the offer would be a lie everywhere else this
  // message is shown.
  error += L"Rozdeliť ho na diskety vie ponuka Disketa → Rozdeliť kolekciu "
           L"na diskety.";
  return false;
}

bool VirtualDisk::BuildImage(std::wstring& error, bool* tooBig) {
  image_.fill(0xe5);
  imported_.clear();

  std::vector<SourceFile> files;
  if (!ScanFolder(files, error)) return false;
  if (!CheckCapacity(files, error)) {
    if (tooBig) *tooBig = true;
    return false;
  }

  std::map<std::wstring, std::string> exactNames;
  if (!ReadExactNames(home_, exactNames, error)) return false;

  std::set<std::string> used;
  unsigned directoryIndex = 0;
  unsigned nextBlock = kFirstDataBlock;

  for (const SourceFile& file : files) {
    const std::wstring leaf = file.path.filename().wstring();
    std::ifstream input(file.path, std::ios::binary);
    if (!input) {
      error = L"Súbor " + leaf + L" sa nedá otvoriť, preto som disketu "
              L"nezostavil. Bez tohto hlásenia by na nej jednoducho chýbal.";
      return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
    if (input.bad()) {
      error = L"Pri čítaní súboru " + leaf + L" nastala chyba, disketu som "
              L"nezostavil.";
      return false;
    }
    // A recorded name is taken only while no file before it holds the same
    // name; otherwise the file falls back to an ordinary one rather than
    // shadow another.
    std::string exact;
    std::string cpmName;
    const auto recorded = exactNames.find(leaf);
    if (recorded != exactNames.end()) {
      const std::string name = NameOf(
          reinterpret_cast<const uint8_t*>(recorded->second.data()));
      if (!name.empty() && !used.contains(name)) {
        exact = recorded->second;
        cpmName = name;
      }
    }
    if (cpmName.empty()) {
      cpmName = cpm::UniqueName(cpm::MakeName(file.path), used);
      exact = PlainName(cpmName);
    }
    used.insert(cpmName);

    const unsigned records = static_cast<unsigned>((data.size() + kRecordSize - 1) / kRecordSize);
    const unsigned blocks = BlocksFor(data.size());
    const unsigned extents = EntriesFor(data.size());
    // CheckCapacity has already ruled this out; it can only happen if the
    // folder changed under us between the scan and the read.
    if (directoryIndex + extents > kDirectoryEntries ||
        nextBlock + blocks > kLastBlock + 1) {
      error = L"Priečinok sa počas pripájania zmenil a jeho obsah sa už na "
              L"disketu nezmestí. Skúste emulátor spustiť znovu.";
      return false;
    }

    unsigned blockCursor = nextBlock;
    unsigned remainingRecords = records;
    for (unsigned extentNumber = 0; extentNumber < extents; ++extentNumber) {
      uint8_t* directory = image_.data() + directoryIndex++ * 32;
      std::fill(directory, directory + 32, 0);
      directory[0] = 0;
      std::copy(exact.begin(), exact.end(), directory + 1);
      directory[12] = static_cast<uint8_t>(extentNumber & 0x1f);
      directory[14] = static_cast<uint8_t>(extentNumber >> 5);
      const unsigned extentRecords = std::min(kRecordsPerEntry, remainingRecords);
      directory[15] = static_cast<uint8_t>(extentRecords);
      const unsigned extentBlocks = (extentRecords + 15) / 16;
      for (unsigned slot = 0; slot < extentBlocks; ++slot) {
        directory[16 + slot * 2] = static_cast<uint8_t>(blockCursor);
        directory[17 + slot * 2] = static_cast<uint8_t>(blockCursor >> 8);
        ++blockCursor;
      }
      remainingRecords -= extentRecords;
    }

    if (!data.empty()) {
      std::copy(data.begin(), data.end(), image_.begin() + nextBlock * kBlockSize);
      std::fill(image_.begin() + nextBlock * kBlockSize + data.size(),
                image_.begin() + (nextBlock + blocks) * kBlockSize, 0x1a);
    }
    imported_[cpmName] = {file.path, data.size(), Hash(data.data(), data.size())};
    nextBlock += blocks;
  }
  dirty_ = false;
  return true;
}

std::string VirtualDisk::DirectoryName(const uint8_t* entry) {
  return NameOf(entry + 1);
}

// Only for types on which 01Ah really is the end of the file.  The technical
// manual says it of the word processor's output ("the last character emitted is
// always an End of File character (ASCII code $1A)", FILE-FMT.D, and a reader
// is to ignore everything after it); the rest are the development disk's
// sources, which are CP/M text too.
//
// .BAS is deliberately NOT here.  A saved BASIC program is binary: a 3 byte
// header (0C2h or 0E2h, then the program length as a 16-bit word) followed by
// tokenised lines, in which 01Ah is an ordinary data byte -- and it is not rare,
// LET.BAS carries one at offset 6399 of 9856.  Cut there, a third of the
// program is gone and BASIC refuses to load it: the header still asks for 9798
// bytes and the file no longer has them.  The export itself says nothing, so
// the loss surfaces only when the backup is needed.
bool VirtualDisk::IsTextType(const std::string& cpmName) {
  const std::size_t dot = cpmName.find('.');
  if (dot == std::string::npos) return false;
  const std::string type = cpmName.substr(dot + 1);
  return type == "TXT" || type == "DOC" || type == "PAS" ||
         type == "C" || type == "H" || type == "ASM" || type == "MAC" ||
         type == "LIB" || type == "INC" || type == "BAT" || type == "SUB";
}

std::wstring VirtualDisk::DecodeCpmName(const std::string& cpmName) {
  return std::wstring(cpmName.begin(), cpmName.end());
}

bool VirtualDisk::ExportImage(const fs::path& destination, bool writeBack,
                              std::wstring& error, ImportedFiles* adopted) {
  std::map<std::string, ExportedFile> files;
  for (unsigned index = 0; index < kDirectoryEntries; ++index) {
    const uint8_t* entry = image_.data() + index * 32;
    if (entry[0] > 0x1f) continue;
    const std::string name = DirectoryName(entry);
    if (name.empty()) continue;
    Extent extent;
    extent.number = entry[12] + (static_cast<unsigned>(entry[14]) << 5);
    extent.records = entry[15];
    for (unsigned slot = 0; slot < 8; ++slot) {
      extent.blocks[slot] = entry[16 + slot * 2] |
                            (static_cast<uint16_t>(entry[17 + slot * 2]) << 8);
    }
    ExportedFile& file = files[name];
    file.name = name;
    file.extents.push_back(extent);
    if (extent.number < file.exact_name_extent) {
      file.exact_name.assign(reinterpret_cast<const char*>(entry + 1), kNameBytes);
      file.exact_name_extent = extent.number;
    }
  }

  // Host names already spoken for.  Every file the folder had is reserved up
  // front, so a new file whose name differs from one of them only in case gets
  // a name of its own instead of that file's.
  std::set<std::wstring> taken;
  if (writeBack) {
    for (const auto& [name, imported] : imported_)
      taken.insert(FoldCase(imported.path.filename().wstring()));
  }
  std::string exactNames;

  for (auto& [name, file] : files) {
    std::sort(file.extents.begin(), file.extents.end(),
              [](const Extent& a, const Extent& b) { return a.number < b.number; });
    std::vector<uint8_t> data;
    for (const Extent& extent : file.extents) {
      unsigned remaining = extent.records;
      for (uint16_t block : extent.blocks) {
        if (!block || remaining == 0 || block > kLastBlock) break;
        const unsigned records = std::min(16u, remaining);
        const uint8_t* begin = image_.data() + block * kBlockSize;
        data.insert(data.end(), begin, begin + records * kRecordSize);
        remaining -= records;
      }
    }

    auto imported = imported_.find(name);
    const bool known = writeBack && imported != imported_.end();
    const fs::path output =
        known ? imported->second.path
              : destination / FreeLeaf(DecodeCpmName(name), taken);
    taken.insert(FoldCase(output.filename().wstring()));
    // Recorded before the unchanged-file shortcut below: a program that only
    // sets an attribute changes no byte of the file, and that is exactly what
    // EUSPATH.COM does.
    if (NeedsExactName(file.exact_name)) {
      const std::u8string leaf = output.filename().u8string();
      exactNames.append(leaf.begin(), leaf.end());
      exactNames += '\t' + ToHex(file.exact_name) + '\n';
    }
    std::size_t length = data.size();
    if (imported != imported_.end() && imported->second.exact_size <= data.size()) {
      bool paddingOnly = true;
      for (std::size_t i = imported->second.exact_size; i < data.size(); ++i) {
        if (data[i] != 0x1a && data[i] != 0) { paddingOnly = false; break; }
      }
      if (paddingOnly) length = imported->second.exact_size;
    } else if (IsTextType(name)) {
      auto end = std::find(data.begin(), data.end(), 0x1a);
      length = static_cast<std::size_t>(end - data.begin());
    }

    // Only when updating in place: an untouched file needs no rewrite.  An
    // export to a fresh folder has to produce every file, changed or not.
    if (known && length == imported->second.exact_size &&
        Hash(data.data(), length) == imported->second.hash) {
      continue;
    }
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) {
      error = L"Nemožno zapísať súbor " + output.wstring();
      return false;
    }
    stream.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(length));
    // What the new home holds now, in the shape a mount would have left
    // behind: the host path, the exact length written and its hash.  Without
    // the length being the *written* one, the first Flush after adopting
    // would think every file had changed and rewrite the lot.
    if (adopted)
      (*adopted)[name] = {output, length, Hash(data.data(), length)};
  }

  if (!WriteExactNames(destination, exactNames, error)) return false;
  if (!writeBack) return true;

  // Deletions are made recoverable by moving the original host file into a
  // private trash folder rather than erasing it.
  fs::path trash = destination / L".eureka-trash";
  for (const auto& [name, imported] : imported_) {
    if (files.contains(name)) continue;
    std::error_code ec;
    fs::create_directories(trash, ec);
    fs::path destination = trash / imported.path.filename();
    unsigned suffix = 1;
    while (fs::exists(destination, ec)) {
      destination = trash / (imported.path.stem().wstring() + L"-" +
                              std::to_wstring(suffix++) + imported.path.extension().wstring());
    }
    fs::rename(imported.path, destination, ec);
    if (ec) {
      error = L"Nemožno presunúť zmazaný súbor do .eureka-trash.";
      return false;
    }
  }
  return true;
}

bool VirtualDisk::Flush(std::wstring& error) {
  // A diskette with no home has nowhere to write back to; SaveAs is what
  // gives it one, and main() offers that on the way out.
  if (!has_home() || !dirty_) return true;
  if (!ExportImage(home_, true, error)) return false;
  dirty_ = false;
  return true;
}

bool VirtualDisk::SaveAs(const fs::path& target, std::wstring& error) {
  if (!present_) return true;
  std::error_code ec;
  fs::create_directories(target, ec);
  if (!fs::is_directory(target, ec)) {
    error = L"Cieľový priečinok neexistuje a nedá sa vytvoriť.";
    return false;
  }
  fs::path absolute = fs::weakly_canonical(target, ec);
  if (ec) absolute = target;
  // Collected rather than kept from before: the file list is what ties the
  // image to a folder, and after this the folder is a different one.  A failed
  // write leaves the old home and the old list untouched, so a diskette that
  // could not be saved is still the diskette it was.
  ImportedFiles adopted;
  if (!ExportImage(absolute, false, error, &adopted)) return false;
  imported_ = std::move(adopted);
  home_ = absolute;
  // A folder is a filesystem, so the diskette now carries a format whether or
  // not it did a moment ago.  That is the one thing saving really changes
  // about the medium: an unformatted diskette written out to a folder is a
  // formatted diskette afterwards, because a folder cannot be half laid down.
  formatted_.set();
  dirty_ = false;
  return true;
}

bool VirtualDisk::ReadRecord(unsigned track, unsigned record, uint8_t* destination) const {
  // CP/M BIOS logical sectors are numbered 0..SPT-1. Physical WD177x
  // sectors remain 1-based and are handled separately below.
  if (!present() || track >= kTracks || record >= kRecordsPerTrack) return false;
  // The BIOS stub goes straight to the image and never near the controller,
  // so the unformatted state has to be honoured here too.  Otherwise a blank
  // diskette would refuse the FDC and answer the BIOS, which is a machine no
  // real one ever was.
  if (!formatted_.test(track)) return false;
  const std::size_t offset = (track * kRecordsPerTrack + record) * kRecordSize;
  std::copy_n(image_.data() + offset, kRecordSize, destination);
  return true;
}

bool VirtualDisk::WriteRecord(unsigned track, unsigned record, const uint8_t* source) {
  if (!present() || track >= kTracks || record >= kRecordsPerTrack) return false;
  if (!formatted_.test(track)) return false;
  // The BIOS stub bypasses the controller, so the notch has to be honoured
  // here as well; otherwise a protected diskette would refuse the FDC and
  // accept the shortcut, which is no machine that ever existed.
  if (write_protected_) return false;
  const std::size_t offset = (track * kRecordsPerTrack + record) * kRecordSize;
  std::copy_n(source, kRecordSize, image_.data() + offset);
  dirty_ = true;
  return true;
}

bool VirtualDisk::ReadPhysicalSector(unsigned cylinder, unsigned side, unsigned sector,
                                     uint8_t* destination) const {
  if (!present() || cylinder >= 80 || side > 1 || sector == 0 || sector > 10)
    return false;
  // An unformatted track has no sector headers on it, so the controller finds
  // nothing to match: Record Not Found, which is what machine.cpp turns a
  // false return into.
  if (!TrackFormatted(cylinder, side)) return false;
  const std::size_t offset = ((cylinder * 2 + side) * 10 + sector - 1) * 512;
  std::copy_n(image_.data() + offset, 512, destination);
  return true;
}

bool VirtualDisk::WritePhysicalSector(unsigned cylinder, unsigned side, unsigned sector,
                                      const uint8_t* source) {
  if (!present() || cylinder >= 80 || side > 1 || sector == 0 || sector > 10)
    return false;
  // machine.cpp refuses the command outright with the Write Protect bit, so
  // this is the belt to that braces: nothing reaches the image behind it.
  if (write_protected_) return false;
  // An unformatted track has no sector headers on it, so the controller finds
  // nothing to match: Record Not Found, which is what machine.cpp turns a
  // false return into.
  if (!TrackFormatted(cylinder, side)) return false;
  const std::size_t offset = ((cylinder * 2 + side) * 10 + sector - 1) * 512;
  std::copy_n(source, 512, image_.data() + offset);
  dirty_ = true;
  return true;
}
