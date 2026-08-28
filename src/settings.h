#ifndef EUREKA_SETTINGS_H
#define EUREKA_SETTINGS_H

// What the emulator remembers between runs.  Until now it remembered nothing
// at all, so this is the first state that outlives a session; the reasoning is
// in HANDOFF 6.22.
//
// Three things here are decisions rather than detail:
//
// The file goes into a `config` folder beside the EXE when one exists, and
// into %APPDATA%\EurekaA4 otherwise.  A folder and not a file, because it has
// to be a deliberate act -- an unpacked archive can leave a stray file behind,
// and portable mode switching itself on unnoticed is exactly the sort of
// silent state this project keeps getting caught by.  It is never created
// here: creating it would turn the choice into an accident.
//
// The format is plain key=value, parsed by hand, and deliberately *not* the
// profile API.  WritePrivateProfileStringW writes ANSI into a file that has no
// UTF-16 BOM, so the first save of a path with diacritics
// (C:\Diskety\Príbehy) corrupts it -- and corrupts it silently, which is worse
// than failing.
//
// Nothing here reports anything.  A missing or unreadable file means defaults,
// because a first run has no file and must not complain about it (6.19); a
// failed save is returned to the caller, which reports it at the moment of the
// act that did not stick, never at start-up.

#include <array>
#include <filesystem>
#include <string>

class Settings {
 public:
  // Numbered 1..kSlots, the way the menu and Ctrl+digit name them.
  static constexpr int kSlots = 9;

  explicit Settings(std::filesystem::path file) : file_(std::move(file)) {}

  // Where the real settings file lives.  Empty when there is nowhere to put
  // it, which Save then reports.  Tests hand the constructor their own path
  // instead of calling this.
  static std::filesystem::path FindFile();

  // A line that makes no sense is skipped rather than fatal: this file is
  // meant to be editable by hand, and one bad line must not cost the user
  // every slot in it.
  void Load();
  bool Save(std::wstring& error) const;

  const std::filesystem::path& file() const { return file_; }

  // The diskette to put back in at the next start.  Empty means none.  Only a
  // folder-backed diskette is remembered: a RAM one has nothing to restore.
  const std::wstring& last_disk() const { return lastDisk_; }
  void SetLastDisk(std::wstring path);

  // An out-of-range number reads empty and writes nowhere, so a caller that
  // miscounts cannot corrupt the file or walk off the array.
  const std::wstring& slot(int number) const;
  void SetSlot(int number, std::wstring path);

 private:
  std::filesystem::path file_;
  std::wstring lastDisk_;
  std::array<std::wstring, kSlots> slots_;
};

// A slot holds either a host folder or this marker.  A Windows path can never
// begin with '*' -- the shell forbids it in a name -- so the two cannot be
// mistaken for each other, and the settings file stays readable:
// "slot3=*pamat" says what it does.
//
// The marker does not restore a diskette, it makes a fresh empty one.  That is
// the honest thing for a medium that lives only in memory: the one that was
// there is gone when the emulator closes, and pretending otherwise would be a
// slot that quietly hands back nothing.  Hence "nová" in the name.
//
// There is deliberately no marker for an unformatted diskette.  Unformatted is
// a state that lasts until the first Shift+F8, so a slot promising one would
// go on saying "nenaformátovaná" about a diskette that has long been
// formatted -- a label describing something that is no longer true.  That
// choice belongs in the New diskette dialog, where it is made once.
inline constexpr wchar_t kSlotRam[] = L"*pamat";

// True for the memory marker.  Anything else beginning with '*' is a marker
// from a newer version and is treated the same rather than being mounted as a
// folder, which is what a bare path test would have done with it.
bool SlotIsRam(const std::wstring& slot);

// What the menu and the dialogs call a slot.  In one place, so a slot cannot
// read one way in the menu and another way in the dialog that fills it.
std::wstring SlotDisplayName(const std::wstring& slot);

// Where the EXE lives.  Here rather than in main.cpp because the ROM search
// and the settings file ask the same question, and two answers to "where am I"
// would be one too many.
std::filesystem::path ExecutableDirectory();

#endif
