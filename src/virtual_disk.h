#ifndef EUREKA_VIRTUAL_DISK_H
#define EUREKA_VIRTUAL_DISK_H

#include <array>
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

  bool Mount(const std::filesystem::path& folder, std::wstring& error);
  void CreateRamDisk();
  // Takes the medium out.  Whatever was owed to the host folder has to have
  // been flushed already: this drops the image on the floor.
  void Eject();
  bool Flush(std::wstring& error);
  bool ExportTo(const std::filesystem::path& folder, std::wstring& error);

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

  std::filesystem::path folder_;
  std::array<uint8_t, kSize> image_{};
  std::unordered_map<std::string, ImportedFile> imported_;
  Media media_ = Media::kNone;
  bool dirty_ = false;
};

#endif
