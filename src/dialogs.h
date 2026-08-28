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

// Making a diskette.  Four kinds, and the unformatted one is not a joke: it
// is the only medium on which the firmware's own format routine has anything
// to do (6.5 leaves a host folder alone), so it is also the only way to try
// Shift+F8 and see it work.
class NewDiskDialog : public win::Dialog {
 public:
  enum class Kind { kFolder, kEmptyFolder, kRam, kUnformattedRam };

  Kind kind() const { return kind_; }
  // The chosen folder, for kFolder and kEmptyFolder.  Empty otherwise.
  const std::wstring& folder() const { return folder_; }

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
};

// Editing the nine quick-choice slots.  It works on a copy and hands it back
// only on OK, so Zrušiť really does leave the settings alone -- the dialog
// never touches the file itself.
class SlotsDialog : public win::Dialog {
 public:
  using Slots = std::array<std::wstring, Settings::kSlots>;

  // currentDisk is the folder in the drive right now, empty when there is no
  // folder-backed diskette in it; that is what "Sem vloženú disketu" assigns.
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
