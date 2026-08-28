#include "virtual_disk.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Straight out of the Disk Parameter Block the firmware hands back (technical
// manual, appendix on BDOS service 31): BSH 4 and BLM 15 make a block 2 KiB,
// DSM 399 means 400 of them, DRM 255 means 256 directory entries, ALL reserves
// the first four blocks for the 8 KiB directory and OFF 0 leaves no system
// tracks.  Chapter 9 states the same capacity the other way round: 800 K in
// total, 8 K directory, "leaving 792K available for data storage".
constexpr unsigned kBlockSize = 2048;
constexpr unsigned kDirectoryBytes = 8192;
constexpr unsigned kDirectoryEntries = 256;
constexpr unsigned kFirstDataBlock = kDirectoryBytes / kBlockSize;
constexpr unsigned kLastBlock = 399;
constexpr unsigned kAvailableBlocks = kLastBlock + 1 - kFirstDataBlock;
// One directory entry holds one extent because EXM is 0: eight 16-bit block
// numbers, so 16 KiB or 128 records, whichever runs out first.
constexpr unsigned kRecordsPerEntry = 128;
constexpr unsigned kRecordSizeBytes = VirtualDisk::kRecordSize;

wchar_t FoldLatin(wchar_t ch) {
  switch (ch) {
    case L'Á': case L'Ä': case L'á': case L'ä': return L'A';
    case L'Č': case L'č': return L'C';
    case L'Ď': case L'ď': return L'D';
    case L'É': case L'Ě': case L'é': case L'ě': return L'E';
    case L'Í': case L'í': return L'I';
    case L'Ĺ': case L'Ľ': case L'ĺ': case L'ľ': return L'L';
    case L'Ň': case L'ň': return L'N';
    case L'Ó': case L'Ô': case L'Ö': case L'ó': case L'ô': case L'ö': return L'O';
    case L'Ŕ': case L'Ř': case L'ŕ': case L'ř': return L'R';
    case L'Š': case L'š': return L'S';
    case L'Ť': case L'ť': return L'T';
    case L'Ú': case L'Ů': case L'Ü': case L'ú': case L'ů': case L'ü': return L'U';
    case L'Ý': case L'ý': return L'Y';
    case L'Ž': case L'ž': return L'Z';
    default: return ch;
  }
}

std::string CpmPart(const std::wstring& source, std::size_t maximum) {
  std::string result;
  for (wchar_t original : source) {
    wchar_t folded = FoldLatin(original);
    if (folded >= L'a' && folded <= L'z') folded -= L'a' - L'A';
    char out = '_';
    if ((folded >= L'A' && folded <= L'Z') ||
        (folded >= L'0' && folded <= L'9')) {
      out = static_cast<char>(folded);
    } else if (folded == L'_' || folded == L'-' || folded == L'$' ||
               folded == L'#' || folded == L'@' || folded == L'!' ||
               folded == L'%' || folded == L'&' || folded == L'~' ||
               folded == L'^') {
      out = static_cast<char>(folded);
    }
    if (result.empty() || result.back() != '_' || out != '_') result.push_back(out);
    if (result.size() == maximum) break;
  }
  while (!result.empty() && (result.back() == '_' || result.back() == ' ')) result.pop_back();
  return result.empty() ? "FILE" : result;
}

std::string Trim(const uint8_t* begin, std::size_t length) {
  std::string value(reinterpret_cast<const char*>(begin), length);
  for (char& ch : value) ch = static_cast<char>(ch & 0x7f);
  while (!value.empty() && value.back() == ' ') value.pop_back();
  return value;
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

unsigned BlocksFor(uint64_t size) {
  return static_cast<unsigned>((size + kBlockSize - 1) / kBlockSize);
}

unsigned EntriesFor(uint64_t size) {
  const uint64_t records = (size + kRecordSizeBytes - 1) / kRecordSizeBytes;
  return static_cast<unsigned>(
      std::max<uint64_t>(1, (records + kRecordsPerEntry - 1) / kRecordsPerEntry));
}

}  // namespace

bool VirtualDisk::Mount(const fs::path& folder, std::wstring& error) {
  std::error_code ec;
  fs::path absolute = fs::weakly_canonical(folder, ec);
  if (ec || !fs::is_directory(absolute, ec)) {
    error = L"Vybraný diskový priečinok neexistuje alebo nie je prístupný.";
    return false;
  }
  folder_ = absolute;
  media_ = Media::kFolder;
  // A host folder is a filesystem: it is formatted by definition, and there
  // is no state in which some of its tracks are missing.
  formatted_.set();
  if (BuildImage(error)) return true;
  // A refused diskette leaves nothing behind: the half-built file list would
  // otherwise still be reported as if it were mounted.
  media_ = Media::kNone;
  folder_.clear();
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
  folder_.clear();
  media_ = Media::kNone;
  dirty_ = false;
}

// A scratch diskette that exists only for this session: 0E5h everywhere is
// exactly what a freshly formatted CP/M disk looks like, so the firmware sees
// an empty directory without any host folder behind it.
//
// Unformatted, it is 0E5h too, but no track answers: the drive is what a blank
// out of the box was, and the machine says "vadny disk" until Shift+F8 has
// been through it.
void VirtualDisk::CreateRamDisk(bool formatted) {
  image_.fill(0xe5);
  if (formatted) formatted_.set(); else formatted_.reset();
  imported_.clear();
  folder_.clear();
  media_ = Media::kRam;
  dirty_ = false;
}

bool VirtualDisk::ValidTrack(unsigned cylinder, unsigned side) {
  return cylinder < kLogicalTracks / 2 && side <= 1;
}

// The format routine walks one track at a time and verifies each one it wrote,
// so this is what turns a blank into a working diskette -- and, on a diskette
// that already had data, what wipes the track it just went over.  That is not
// carelessness, it is what formatting is.
void VirtualDisk::FormatTrack(unsigned cylinder, unsigned side) {
  // A host folder is a filesystem, not a magnetic surface: the format still
  // reports success, but nothing is laid down and no file is erased (6.5).
  if (media_ != Media::kRam || !ValidTrack(cylinder, side)) return;
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
  if (media_ == Media::kNone) return 0;
  std::map<std::string, bool> names;
  for (unsigned index = 0; index < kDirectoryEntries; ++index) {
    const uint8_t* entry = image_.data() + index * 32;
    if (entry[0] > 0x1f) continue;
    const std::string name = DirectoryName(entry);
    if (!name.empty()) names[name] = true;
  }
  return names.size();
}

std::string VirtualDisk::MakeCpmName(const fs::path& path) {
  const std::string stem = CpmPart(path.stem().wstring(), 8);
  const std::string type = CpmPart(path.extension().wstring().substr(
      path.extension().wstring().empty() ? 0 : 1), 3);
  return type.empty() || path.extension().empty() ? stem : stem + "." + type;
}

std::string VirtualDisk::UniqueCpmName(
    const std::string& requested, const std::unordered_map<std::string, bool>& used) {
  if (!used.contains(requested)) return requested;
  const std::size_t dot = requested.find('.');
  std::string stem = requested.substr(0, dot);
  const std::string type = dot == std::string::npos ? "" : requested.substr(dot);
  for (unsigned number = 1; number < 1000; ++number) {
    const std::string suffix = "~" + std::to_string(number);
    const std::string candidate = stem.substr(0, 8 - std::min<std::size_t>(8, suffix.size())) +
                                  suffix + type;
    if (!used.contains(candidate)) return candidate;
  }
  return "COLLIDE.$$$";
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
  fs::directory_iterator entry(folder_, ec);
  if (ec) {
    error = L"Priečinok " + folder_.wstring() + L" sa nedá prečítať.";
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
    // bookkeeping (.eureka-trash and friends) belongs to the host too.
    if (!leaf.starts_with(L".eureka-") && fs::is_regular_file(status)) {
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
      error = L"Prehľadávanie priečinka " + folder_.wstring() + L" sa prerušilo, "
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
  error += L"Rozdeľte priečinok na viac priečinkov a striedajte ich ako diskety.";
  return false;
}

bool VirtualDisk::BuildImage(std::wstring& error) {
  image_.fill(0xe5);
  imported_.clear();

  std::vector<SourceFile> files;
  if (!ScanFolder(files, error)) return false;
  if (!CheckCapacity(files, error)) return false;

  std::unordered_map<std::string, bool> used;
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
    std::string cpmName = UniqueCpmName(MakeCpmName(file.path), used);
    used[cpmName] = true;

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

    const std::size_t dot = cpmName.find('.');
    const std::string stem = cpmName.substr(0, dot);
    const std::string type = dot == std::string::npos ? "" : cpmName.substr(dot + 1);
    unsigned blockCursor = nextBlock;
    unsigned remainingRecords = records;
    for (unsigned extentNumber = 0; extentNumber < extents; ++extentNumber) {
      uint8_t* directory = image_.data() + directoryIndex++ * 32;
      std::fill(directory, directory + 32, 0);
      directory[0] = 0;
      std::fill(directory + 1, directory + 9, ' ');
      std::fill(directory + 9, directory + 12, ' ');
      std::copy(stem.begin(), stem.end(), directory + 1);
      std::copy(type.begin(), type.end(), directory + 9);
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
  const std::string stem = Trim(entry + 1, 8);
  const std::string type = Trim(entry + 9, 3);
  return type.empty() ? stem : stem + "." + type;
}

bool VirtualDisk::IsTextType(const std::string& cpmName) {
  const std::size_t dot = cpmName.find('.');
  if (dot == std::string::npos) return false;
  const std::string type = cpmName.substr(dot + 1);
  return type == "BAS" || type == "TXT" || type == "DOC" || type == "PAS" ||
         type == "C" || type == "H" || type == "ASM" || type == "MAC" ||
         type == "LIB" || type == "INC" || type == "BAT" || type == "SUB";
}

std::wstring VirtualDisk::DecodeCpmName(const std::string& cpmName) {
  return std::wstring(cpmName.begin(), cpmName.end());
}

bool VirtualDisk::ExportImage(const fs::path& destination, bool writeBack,
                              std::wstring& error) {
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
    files[name].name = name;
    files[name].extents.push_back(extent);
  }

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
    fs::path output = known ? imported->second.path
                            : destination / DecodeCpmName(name);
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
  }

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
  // A RAM diskette has no host folder to write back to; main() offers to
  // export it once, when the machine is switched off.
  if (media_ != Media::kFolder || !dirty_) return true;
  if (!ExportImage(folder_, true, error)) return false;
  dirty_ = false;
  return true;
}

bool VirtualDisk::ExportTo(const fs::path& target, std::wstring& error) {
  if (media_ == Media::kNone) return true;
  std::error_code ec;
  fs::create_directories(target, ec);
  if (!fs::is_directory(target, ec)) {
    error = L"Cieľový priečinok neexistuje a nedá sa vytvoriť.";
    return false;
  }
  fs::path absolute = fs::weakly_canonical(target, ec);
  if (ec) absolute = target;
  if (!ExportImage(absolute, false, error)) return false;
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
  // An unformatted track has no sector headers on it, so the controller finds
  // nothing to match: Record Not Found, which is what machine.cpp turns a
  // false return into.
  if (!TrackFormatted(cylinder, side)) return false;
  const std::size_t offset = ((cylinder * 2 + side) * 10 + sector - 1) * 512;
  std::copy_n(source, 512, image_.data() + offset);
  dirty_ = true;
  return true;
}
