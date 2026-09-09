#ifndef EUREKA_CPM_DISK_H
#define EUREKA_CPM_DISK_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

// What one diskette costs and what its files are called -- in one copy.
//
// virtual_disk.cpp lays the real image out with these numbers and
// disk_layout.cpp plans against them.  A second copy of them is the kind of
// disagreement this project keeps paying for: a splitter that packed 400
// blocks while the image gave out at 396 would hand back a plan that cannot
// be carried out, and it would say so nowhere.
namespace cpm {

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
constexpr unsigned kRecordSize = 128;

// A block is claimed whole, so a one-byte file costs the same 2 KiB as a
// 2048-byte one; that is why a folder well under 792 KiB can still not fit.
constexpr unsigned BlocksFor(std::uint64_t size) {
  return static_cast<unsigned>((size + kBlockSize - 1) / kBlockSize);
}

// An empty file still owns a directory entry -- it is the entry that makes it
// exist -- so this never returns zero.
constexpr unsigned EntriesFor(std::uint64_t size) {
  const std::uint64_t records = (size + kRecordSize - 1) / kRecordSize;
  return static_cast<unsigned>(
      std::max<std::uint64_t>(1, (records + kRecordsPerEntry - 1) / kRecordsPerEntry));
}

// One field of an 8.3 name: accented letters folded, everything CP/M will not
// take replaced by an underscore, cut to `maximum` characters.
std::string NamePart(const std::wstring& source, std::size_t maximum);

// The 8.3 name a host file gets on the diskette.  The splitter has to ask the
// same question the image builder will ask later, or its plan would promise
// names the diskette does not end up carrying.
std::string MakeName(const std::filesystem::path& path);

// The same name with ~1, ~2 ... appended until it is free.  A renamed file is
// a file its program can no longer find, so this is a last resort and the
// caller has to say so out loud (HANDOFF 6.22).
std::string UniqueName(const std::string& requested, const std::set<std::string>& used);

}  // namespace cpm

#endif
