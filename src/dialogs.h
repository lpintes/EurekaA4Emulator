#ifndef EUREKA_DIALOGS_H
#define EUREKA_DIALOGS_H

// The emulator's own dialogs.  They are real dialogs from resource templates
// -- see win/dialog.h for why that matters and not merely how it looks.

#include <array>
#include <string>

#include "emulator_thread.h"
#include "settings.h"
#include "win/dialog.h"

class SettingsDialog : public win::Dialog {
 public:
  SettingsDialog(InputMode mode, bool diagnostics)
      : mode_(mode), diagnostics_(diagnostics) {}

  InputMode mode() const { return mode_; }
  bool diagnostics() const { return diagnostics_; }

 protected:
  bool OnInit() override;
  bool OnOk() override;

 private:
  InputMode mode_;
  bool diagnostics_;
};

// The nine quick-choice slots as the dialogs pass them around.
using SlotList = std::array<std::wstring, Settings::kSlots>;

// Making a diskette.  Four kinds, and the unformatted one is not a joke: it
// is the only medium on which the firmware's own format routine has anything
// to do (6.5 leaves a host folder alone), so it is also the only way to try
// Shift+F8 and see it work.
class NewDiskDialog : public win::Dialog {
 public:
  enum class Kind { kFolder, kEmptyFolder, kRam, kUnformattedRam };

  // slots is what the nine hold now, so the picker can name them and so the
  // user can see they are about to overwrite one.
  explicit NewDiskDialog(SlotList slots) : slots_(std::move(slots)) {}

  Kind kind() const { return kind_; }
  // The chosen folder, for kFolder and kEmptyFolder.  Empty otherwise.
  const std::wstring& folder() const { return folder_; }
  // 0 when the diskette is not to be put in a slot, otherwise 1..kSlots.
  int slot() const { return slot_; }

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;
  bool OnOk() override;

 private:
  // The path field and Prehľadávať are only meaningful for the two folder
  // kinds; greying them out for the RAM ones says so where a screen reader
  // reads it, rather than leaving a control that does nothing.
  void RefreshEnabled();
  Kind SelectedKind() const;

  Kind kind_ = Kind::kFolder;
  std::wstring folder_;
  int slot_ = 0;
  SlotList slots_;
};

// Editing the nine quick-choice slots.  It works on a copy and hands it back
// only on OK, so Zrušiť really does leave the settings alone -- the dialog
// never touches the file itself.
class SlotsDialog : public win::Dialog {
 public:
  using Slots = SlotList;

  // currentDisk is what a slot would have to hold to bring back the diskette
  // in the drive right now -- a folder, or a marker for one in memory.  Empty
  // when the drive is empty; that is what "Sem vloženú disketu" assigns.
  SlotsDialog(Slots slots, std::wstring currentDisk)
      : slots_(std::move(slots)), currentDisk_(std::move(currentDisk)) {}

  const Slots& slots() const { return slots_; }

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;

 private:
  // Rebuilds the list and puts the selection back where it was: the buttons
  // change one line, and a list that jumped to the top after every change
  // would make a screen reader lose the place the user was working at.
  void FillList(int select);
  int Selected() const;
  void SetSlotAndRefresh(int index, std::wstring path);

  Slots slots_;
  std::wstring currentDisk_;
};

class AboutDialog : public win::Dialog {
 public:
  explicit AboutDialog(std::wstring body) : body_(std::move(body)) {}

 protected:
  bool OnInit() override;

 private:
  std::wstring body_;
};

#endif
