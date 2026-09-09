#ifndef EUREKA_MAIN_WINDOW_H
#define EUREKA_MAIN_WINDOW_H

#include <string>

#include "dialogs.h"
#include "emulator_thread.h"
#include "settings.h"
#include "win/window.h"

// The emulator's window.  It owns no machine state: every key and every menu
// command is posted to the emulator thread, and everything it displays comes
// back from there.  The client area is deliberately empty -- the machine's
// output is speech, and the host console keeps the text for diagnostics.
class MainWindow : public win::Window {
 public:
  // diskDescription is the long form for O programe, diskName the short one
  // the title carries -- see RefreshTitle.
  MainWindow(EmulatorThread& emulator, Settings& settings, std::wstring romPath,
             DiskState disk);

  bool Create();
  HACCEL accelerators() const { return accelerators_; }
  HACCEL hostAccelerators() const { return hostAccelerators_; }

  // True while the keyboard belongs to the host, so the Ctrl shortcuts apply.
  // The rest of the time they are Eureka's keys and the window never sees
  // them: that is the whole reason there are two tables.
  bool HostShortcutsActive() const { return released_ || passOnce_; }

  // The diskette can be changed while the machine runs, so this is not fixed
  // at construction any more.  The title is read out on every Alt+Tab and
  // NVDA+T, so it has to say what is in the drive now.
  void SetDiskState(DiskState disk);

 protected:
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

 private:
  void RegisterCommands();
  void RefreshTitle() const;
  // Puts the released state where another process can read it.  The NVDA
  // add-on is the only reader; see kKeyboardReleasedProp.
  void PublishKeyboardState() const;
  void RefreshMenu() const;
  // Puts the "F11, " prefix on the Ctrl shortcuts, or takes it off once the
  // keyboard is released and they no longer need it.
  void RefreshShortcutText(HMENU menu) const;
  void ForwardKey(bool down, WPARAM wParam, LPARAM lParam) const;
  void SendGuestKey(WORD virtualKey, bool alt) const;
  std::wstring AboutText() const;

  // True when this key event belongs to the host rather than to Eureka.  Also
  // spends the one-shot, which is why it is not const.
  bool HostKeepsKey(WPARAM virtualKey, bool down);
  // quiet is for the one caller that has already said what happened: the power
  // switch.  Off and released are one state, so the power tone is the whole
  // announcement and the keyboard pair on top of it would be a second answer
  // to one event -- and two falling shapes in a row are harder to tell apart
  // than either is on its own.
  void SetReleased(bool released, bool quiet = false);
  void SetPassOnce(bool armed);

  // Remembers the diskette a swap put in, so the next start finds it.  Saved
  // at the moment of the swap and not at exit, because a failed save has to be
  // reported while the user can still do something about it.
  void RememberDisk(const std::wstring& folder);
  // The nine slots as the dialogs want them.
  SlotList CurrentSlots() const;
  // What each slot's diskette is, as the slots dialog needs it: whether there
  // is one at all, and whether it is locked.  A folder slot is answered from
  // the settings, an unsaved one from the worker's shelf -- and the drive
  // overrides both for the slot whose diskette is in it, because that is where
  // that diskette is right now.
  SlotsDialog::Locks CurrentLocks() const;
  SlotsDialog::Present CurrentPresent() const;
  void RememberLock(const DiskState& disk);
  // False when the user backed out of losing a diskette that lives only in
  // memory; the caller then does nothing.  See the definition.
  bool ConfirmLosingDiskette();
  void SaveSettings();
  void SaveSlot(int number, std::wstring value);
  // Puts the diskette from one of the nine slots in.  Numbered 1..9 the way
  // the menu and Ctrl+digit name them.
  void InsertSlot(int number);
  // Splits a collection too big for one diskette into diskettes.  `source`
  // pre-fills the field: from the menu that is whatever is in the drive, and
  // from the box refusing an oversized folder it is that folder -- which is
  // what makes this the deed replacing the advice, rather than a menu item
  // the user has to find and then name the folder to a second time (6.22).
  void SplitCollection(const std::wstring& source);
  // Rewrites the nine menu items from the settings, so the menu names what is
  // actually in each slot rather than what the .rc guessed.
  void RefreshSlotItems(HMENU menu) const;

  EmulatorThread& emulator_;
  Settings& settings_;
  std::wstring romPath_;
  // What is in the drive: its labels for the title and About, the folder
  // behind it, and what a slot would have to hold to bring it back.
  DiskState disk_;
  HACCEL accelerators_ = nullptr;
  HACCEL hostAccelerators_ = nullptr;

  // Shift+F11: the keyboard is let go of altogether and the window behaves
  // like any other Windows window until it is taken back.
  bool released_ = false;
  // F11: the next key goes to Windows instead of to Eureka, once.
  bool passOnce_ = false;
  // Whether the press of that key has been seen yet.  See HostKeepsKey: the
  // release of F11 itself must not be mistaken for it.
  bool passOnceUsed_ = false;
  // The machine has switched itself off and is sitting there switched off with
  // the window still open.  The worker's copy is the truth; this one is here
  // so that the title and the menu can be built on the window thread without
  // asking across.
  bool poweredOff_ = false;
};

#endif
