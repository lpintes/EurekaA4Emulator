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

#include "cpm_disk.h"

class VirtualDisk {
 public:
  static constexpr std::size_t kSize = 800u * 1024u;
  static constexpr unsigned kTracks = 160;
  static constexpr unsigned kRecordsPerTrack = 40;
  // The record the guest reads and writes.  One number, one place: cpm_disk.h
  // is where the diskette's geometry is written down.
  static constexpr unsigned kRecordSize = cpm::kRecordSize;

  // There is one kind of diskette and it always lives in this process's
  // memory: mounting a host folder reads it into the image below and Flush
  // writes it back, so the guest never touches the folder.  What differs is
  // whether a diskette has a *home* -- a folder it loads from and saves
  // itself to -- or none, in which case it exists nowhere else.
  //
  // That distinction used to be an enum, Media::kFolder against Media::kRam,
  // and the name was the bug: "lives in RAM" was read as "cannot be locked"
  // when what it really means is "its lock cannot be written down" (6.26).
  // Both are in RAM.  Only one has somewhere to be saved.

  // Logical tracks, cylinder * 2 + side.  Both numbering schemes below index
  // the formatted map with this.
  static constexpr unsigned kLogicalTracks = kTracks;

  // Reads a host folder in and keeps it as this diskette's home.
  //
  // tooBig, when given, is set for the one refusal that has something to
  // offer: the folder is a fine folder and simply will not fit on 792 KiB or
  // in 256 directory entries.  The host turns that into the splitter (6.22),
  // and it has to be able to tell it apart from a file it could not read
  // without reading the message -- a message is text for the user, not a
  // value to parse back.
  bool Mount(const std::filesystem::path& folder, std::wstring& error,
             bool* tooBig = nullptr);
  // A diskette with no home.  formatted false gives one that has never been
  // through a format: every track reads back Record Not Found until the
  // guest's own format routine lays it down.  Only worth doing here -- a
  // diskette with a home is a filesystem and always carries a format.
  void CreateEmpty(bool formatted = true);
  // Takes the medium out.  Whatever was owed to the home folder has to have
  // been flushed already: this drops the image on the floor.
  void Eject();
  // Pays the image back to the home folder, if there is one and anything is
  // owed.  Nothing to do for a diskette that has none.
  bool Flush(std::wstring& error);
  // Writes the whole diskette out to a folder and keeps that folder as its
  // home from then on -- Save As, not Save a copy.  For a diskette that had
  // no home this is the act that gives it one; for a diskette that had a
  // different one it moves house, and the old folder keeps what it had.
  //
  // One rule for both, because two would need the user to know which one they
  // were in: an editor's Save As does exactly this, and a "save" that left the
  // document unsaved afterwards is the sort of half-act this program has been
  // caught wording before (6.25, 6.26).
  bool SaveAs(const std::filesystem::path& folder, std::wstring& error);

  // Whether laying a track down really erases it.  Its own question and its
  // own name on purpose: it happens to have the same answer as has_home(),
  // but for an unrelated reason -- a home folder is a filesystem, not a
  // magnetic surface, so the guest's format routine reports success and
  // deletes nothing (6.5).  Sharing one predicate between two reasons is how
  // 6.26 happened.
  bool formatting_erases() const { return present_ && !has_home(); }

  // One track laid down by the guest's format routine (Write Track).  See
  // formatting_erases: on a diskette with a home the track image is discarded
  // and the files are left alone, so the call succeeds and changes nothing.
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

  // The folder this diskette loads from and saves itself back to, empty when
  // it has none.  Also its name: the settings remember a lock by this path,
  // which is the whole of what a diskette with no home cannot have.
  const std::filesystem::path& home() const { return home_; }
  bool has_home() const { return !home_.empty(); }
  // True while the image owes something to the home folder.  Not "modified":
  // a diskette with no home never pays this off, because there is nothing to
  // pay it to.  Nothing outside Flush may read it as a user-visible state.
  bool dirty() const { return dirty_; }
  bool present() const { return present_; }
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

  static std::string DirectoryName(const uint8_t* entry);
  static uint64_t Hash(const uint8_t* data, std::size_t size);
  static bool IsTextType(const std::string& cpmName);
  static std::wstring DecodeCpmName(const std::string& cpmName);

  using ImportedFiles = std::unordered_map<std::string, ImportedFile>;

  bool BuildImage(std::wstring& error, bool* tooBig = nullptr);
  bool ScanFolder(std::vector<SourceFile>& files, std::wstring& error);
  bool CheckCapacity(const std::vector<SourceFile>& files, std::wstring& error) const;
  // writeBack distinguishes the two directions: updating the home folder in
  // place (host paths and deletions honoured, unchanged files left alone) from
  // copying the whole image out to a folder that knows nothing about it.
  //
  // adopted, when given, collects what was written where, so SaveAs can take
  // the destination as the new home with a file list that matches it.  Without
  // that the first Flush afterwards would find imported_ empty and leave every
  // file the guest had deleted lying in the folder.
  bool ExportImage(const std::filesystem::path& destination, bool writeBack,
                   std::wstring& error, ImportedFiles* adopted = nullptr);

  static bool ValidTrack(unsigned cylinder, unsigned side);

  std::filesystem::path home_;
  std::array<uint8_t, kSize> image_{};
  // Which logical tracks carry a format.  Full for anything that came from a
  // host folder, and for a homeless diskette made formatted; empty for one
  // that was not, and filled in track by track as the guest formats it.
  std::bitset<kLogicalTracks> formatted_;
  ImportedFiles imported_;
  // Whether there is a diskette in at all.  The firmware has its own path for
  // an empty drive (19828 seeks, polls INDEX, reports "neni disk" at 19848).
  bool present_ = false;
  bool dirty_ = false;
  bool write_protected_ = false;
};

#endif
