#ifndef EUREKA_VIRTUAL_DISK_H
#define EUREKA_VIRTUAL_DISK_H

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class VirtualDisk {
 public:
  static constexpr std::size_t kSize = 800u * 1024u;
  static constexpr unsigned kTracks = 160;
  static constexpr unsigned kRecordsPerTrack = 40;
  static constexpr unsigned kRecordSize = 128;

  // A machine may run with no diskette at all; the firmware has its own path
  // for that (19828 seeks, polls INDEX, reports "neni disk" at 19848).
  enum class Media { kNone, kFolder, kRam };

  // Logical tracks, cylinder * 2 + side.  Both numbering schemes below index
  // the formatted map with this.
  static constexpr unsigned kLogicalTracks = kTracks;

  bool Mount(const std::filesystem::path& folder, std::wstring& error);
  // formatted false gives a diskette that has never been through a format:
  // every track reads back Record Not Found until the guest's own format
  // routine lays it down.  Only worth doing for a diskette that lives in
  // memory -- a host folder is a filesystem and always has one.
  void CreateRamDisk(bool formatted = true);
  // Takes the medium out.  Whatever was owed to the host folder has to have
  // been flushed already: this drops the image on the floor.
  void Eject();
  bool Flush(std::wstring& error);
  bool ExportTo(const std::filesystem::path& folder, std::wstring& error);

  // One track laid down by the guest's format routine (Write Track).  It
  // means something only on a diskette in memory: behind a host folder the
  // track image is discarded and the files are left alone (6.5), so there the
  // call succeeds and changes nothing.
  void FormatTrack(unsigned cylinder, unsigned side);
  bool TrackFormatted(unsigned cylinder, unsigned side) const;
  // False only for a diskette on which no track has been laid down at all.
  // The window puts this in the title: "nenaformátovaná" is the difference
  // between a diskette that is broken and one that is simply blank, and the
  // machine answers both with the same "vadny disk".
  bool has_format() const { return formatted_.any(); }

  // The write protect notch.  It belongs to the medium, not to the drive, so
  // every way of putting a diskette in clears it here and the host says what
  // the new one's notch is; a flag left standing would silently protect
  // whatever came next.  Remembering which diskettes are locked is the host's
  // job (Settings::disk_locked) -- this class knows only what is in now.  The
  // firmware side is documented, not guessed -- DEVICES.10 has bit 6 of
  // fdc_ctl_chkdsk as "Write protected", and SYSEQU.LIB folds that bit into
  // write_error_mask (11011110b) but leaves it out of read_error_mask
  // (10011110b), so a protected diskette still reads.
  void set_write_protected(bool protect) { write_protected_ = protect; }
  bool write_protected() const { return write_protected_; }

  bool ReadRecord(unsigned track, unsigned record, uint8_t* destination) const;
  bool WriteRecord(unsigned track, unsigned record, const uint8_t* source);
  bool ReadPhysicalSector(unsigned cylinder, unsigned side, unsigned sector,
                          uint8_t* destination) const;
  bool WritePhysicalSector(unsigned cylinder, unsigned side, unsigned sector,
                           const uint8_t* source);

  const std::filesystem::path& folder() const { return folder_; }
  bool dirty() const { return dirty_; }
  bool present() const { return media_ != Media::kNone; }
  Media media() const { return media_; }
  std::size_t imported_files() const { return imported_.size(); }
  std::size_t StoredFiles() const;

 private:
  struct SourceFile {
    std::filesystem::path path;
    std::uintmax_t size = 0;
  };

  struct ImportedFile {
    std::filesystem::path path;
    std::size_t exact_size = 0;
    uint64_t hash = 0;
  };

  struct Extent {
    unsigned number = 0;
    unsigned records = 0;
    std::array<uint16_t, 8> blocks{};
  };

  struct ExportedFile {
    std::string name;
    std::vector<Extent> extents;
  };

  static std::string MakeCpmName(const std::filesystem::path& path);
  static std::string UniqueCpmName(const std::string& requested,
                                   const std::unordered_map<std::string, bool>& used);
  static std::string DirectoryName(const uint8_t* entry);
  static uint64_t Hash(const uint8_t* data, std::size_t size);
  static bool IsTextType(const std::string& cpmName);
  static std::wstring DecodeCpmName(const std::string& cpmName);

  bool BuildImage(std::wstring& error);
  bool ScanFolder(std::vector<SourceFile>& files, std::wstring& error);
  bool CheckCapacity(const std::vector<SourceFile>& files, std::wstring& error) const;
  // writeBack distinguishes the two directions: updating the mounted folder in
  // place (host paths and deletions honoured, unchanged files left alone) from
  // copying the whole image out to a folder that knows nothing about it.
  bool ExportImage(const std::filesystem::path& destination, bool writeBack,
                   std::wstring& error);

  static bool ValidTrack(unsigned cylinder, unsigned side);

  std::filesystem::path folder_;
  std::array<uint8_t, kSize> image_{};
  // Which logical tracks carry a format.  Full for anything that came from a
  // host folder, and for a RAM diskette made formatted; empty for one that
  // was not, and filled in track by track as the guest formats it.
  std::bitset<kLogicalTracks> formatted_;
  std::unordered_map<std::string, ImportedFile> imported_;
  Media media_ = Media::kNone;
  bool dirty_ = false;
  bool write_protected_ = false;
};

#endif
