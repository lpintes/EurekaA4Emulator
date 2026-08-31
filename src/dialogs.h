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

// Making a diskette.  Three kinds, and the unformatted one is not a joke: it
// is the only medium on which the firmware's own format routine has anything
// to do (6.5 leaves a host folder alone), so it is also the only way to try
// Shift+F8 and see it work.
//
// Making, not inserting.  A fourth kind, kFolder, mounted a folder that was
// already there -- the same deed as ID_DISK_INSERT, down to the identical
// PostMountDisk call, under a second name in a dialog called "Nová disketa".
// Putting an existing diskette in is Ctrl+I and always was.
class NewDiskDialog : public win::Dialog {
 public:
  enum class Kind { kNewFolder, kUnsaved, kUnformatted };

  // slots is what the nine hold now, so the picker can name them and so the
  // user can see they are about to overwrite one.
  explicit NewDiskDialog(SlotList slots) : slots_(std::move(slots)) {}

  Kind kind() const { return kind_; }
  // The chosen folder, for kNewFolder.  Empty otherwise.
  const std::wstring& folder() const { return folder_; }
  // 0 when the diskette is not to be put in a slot, otherwise 1..kSlots.
  int slot() const { return slot_; }

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;
  bool OnOk() override;

 private:
  // The path field and Prehľadávať are only meaningful for the permanent
  // kind; greying them out for the unsaved ones says so where a screen reader
  // reads it, rather than leaving a control that does nothing.
  void RefreshEnabled();
  Kind SelectedKind() const;

  Kind kind_ = Kind::kNewFolder;
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
  using Locks = std::array<bool, Settings::kSlots>;
  // Whether each slot has a diskette at all.  Passed in rather than worked out
  // from the slot's text, and that is the whole repair of 6.26: a folder slot
  // names a diskette that exists on disk, but an unsaved slot names one only
  // if the worker is holding it or it is in the drive.  Deriving it from the
  // string made "cannot be remembered" and "does not exist" one answer.
  using Present = std::array<bool, Settings::kSlots>;

  // currentDisk is what a slot would have to hold to bring back the diskette
  // in the drive right now -- a folder, or the marker for an unsaved one.
  // Empty when the drive is empty; that is what "Sem vloženú disketu" assigns.
  // currentLocked is that diskette's notch, so pointing a slot at it arrives
  // with the truth rather than with what the settings could remember.
  // settings is read, never written: the dialog works on a copy and hands it
  // back only on OK.  It is here because the lock belongs to the folder, so
  // pointing a slot at a folder that is already locked has to arrive ticked
  // -- otherwise OK would quietly take that lock off.
  SlotsDialog(Slots slots, Locks locks, Present present,
              std::wstring currentDisk, bool currentLocked,
              const Settings& settings)
      : slots_(std::move(slots)),
        locks_(locks),
        present_(present),
        currentDisk_(std::move(currentDisk)),
        currentLocked_(currentLocked),
        settings_(&settings) {}

  const Slots& slots() const { return slots_; }
  const Locks& locks() const { return locks_; }
  // Which slot the diskette now in the drive was put into, 0 for none.  For a
  // an unsaved diskette that is not bookkeeping: it is the difference
  // between the slot holding it and the slot making a new empty one.
  int assigned_current() const { return assignedCurrent_; }

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
  // The lock belongs to the selected slot, so the box has to follow the
  // selection.  A box that stayed where it was would be reporting one slot's
  // state while the list names another.
  void RefreshLock(int index);

  Slots slots_;
  Locks locks_{};
  Present present_{};
  int assignedCurrent_ = 0;
  std::wstring currentDisk_;
  bool currentLocked_ = false;
  const Settings* settings_ = nullptr;
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
