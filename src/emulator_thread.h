#ifndef EUREKA_EMULATOR_THREAD_H
#define EUREKA_EMULATOR_THREAD_H

// The machine runs on its own thread, and the window thread only ever posts to
// it.  This is not tidiness: a dropped-down menu, a modal dialog and a window
// being dragged each spin their own message loop inside Windows, and any of
// them would have frozen a main loop that carried the emulator with it.  At
// the 22 ms of audio latency this loop steers to, that means the speech cuts
// out -- and on this machine the speech is the entire user interface.
//
// Nothing here is locked around the machine, because nothing else touches it:
// the window thread hands over key events and commands through one queue, in
// order, and the worker owns EurekaMachine and AudioPlayer outright.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "disk_stash.h"
#include "machine.h"
#include "settings.h"

// How the host keyboard is presented to the machine.  The Eureka had exactly
// two keyboards -- the braille one built in and the optional PC one on the
// serial port -- and so has the emulator.
enum class InputMode {
  kBraille,  // the six dot keys, chorded
  kPc,       // the optional IBM PC keyboard on the serial port
};

const wchar_t* ModeName(InputMode mode);

// One host key event, in the shape the translation needs.  The modifier field
// keeps the console's own bit names (SHIFT_PRESSED and friends) even though it
// is now filled in from GetKeyState: the translation below was measured
// against those bits and reproducing them costs one function.
struct HostKeyEvent {
  bool down = false;
  WORD virtualKey = 0;
  uint8_t scanCode = 0;
  bool extended = false;
  // lParam bit 30: the key was already down, so this is typematic and not a
  // new press.  The console could not report this at all.
  bool autoRepeat = false;
  DWORD modifiers = 0;
};

// Builds a HostKeyEvent from a WM_KEYDOWN/WM_KEYUP style message.
HostKeyEvent KeyEventFromMessage(bool down, WPARAM wParam, LPARAM lParam);

// Builds one for a key nobody pressed, so that a menu command can hand the
// guest a key the host shortcuts took away from it -- F11 and F12, which the
// ROM does know (1DF05: scan codes 57h and 58h give CAh and CBh).
HostKeyEvent SyntheticKey(bool down, WORD virtualKey, bool alt);

// Messages the worker posts back to the window.  wParam carries the detail.
enum : UINT {
  WM_EMU_STATE = WM_APP + 1,   // mode or diagnostics changed; refresh the UI
  WM_EMU_POWERED_OFF = WM_APP + 2,
  WM_EMU_DISK_ERROR = WM_APP + 3,
  // A Save As finished.  It carries the new state as well as the outcome: a
  // diskette that had no home has one now, so the title stops saying
  // "neuložená" in the same breath as the box that says where it went.
  WM_EMU_SAVED = WM_APP + 4,
  // The sound device would not open.  On this machine that is not a degraded
  // experience, it is no user interface at all, so the window has to say so.
  WM_EMU_NO_AUDIO = WM_APP + 5,
  // A diskette went in or came out: the title, the menu and About all name it.
  WM_EMU_DISK_CHANGED = WM_APP + 6,
};

// How a diskette is named in the window title (short) and in About (long).
// In one place so that a diskette put in at start-up and one swapped in later
// cannot end up described two different ways.
struct DiskLabels {
  std::wstring name;
  std::wstring description;
};

// Everything the window needs to know about what is in the drive.  One struct
// rather than four parameters, because these four always travel together and
// four of them in a row is how one ends up passed in the wrong order.
struct DiskState {
  bool present = false;
  // The folder this diskette saves itself to, empty when it has none or the
  // drive is empty.  Kept apart from the labels because those are text for the
  // user, not data to parse back.
  std::wstring home;
  // What a quick-choice slot would have to hold to bring this diskette back:
  // the folder, or the marker for one with no home.  Empty for an empty
  // drive, which is nothing to remember.
  std::wstring slotValue;
  // The write protect notch.  Part of the state and not a separate flag on the
  // window, because it belongs to the medium the machine has right now:
  // another diskette comes in with its own answer, and a window keeping its
  // own copy would go on saying "locked" about a diskette that is not.  What
  // makes the lock outlast a swap is the settings file, not this.
  bool writeProtected = false;
  // True for a diskette with no home, with how many files are on it.  The
  // window needs both to know whether taking it out would destroy anything:
  // one that has never been saved exists nowhere else, and one with nothing
  // on it is nothing to lose.
  bool unsaved = false;
  std::size_t files = 0;
  // Which quick-choice slot it belongs to, 0 for none.  An unsaved diskette
  // with no slot has only one reference -- the drive -- so whatever pushes it
  // out has to ask the user first.
  int slot = 0;
  DiskLabels labels;
};

DiskState DescribeDisk(const VirtualDisk& disk);

// The outcome of a swap, picked up by the window when WM_EMU_DISK_CHANGED
// arrives.  A failed mount leaves the drive empty rather than half-loaded --
// VirtualDisk::Mount already unwinds itself -- so the labels are always the
// truth about what is in there now, error or not.
struct DiskChange {
  bool ok = true;
  // False when only the notch moved.  The window sounds a swap and a lock
  // differently, and a lock that announced itself as a diskette going in
  // would be telling the user something that did not happen.
  bool swapped = true;
  std::wstring error;
  // What is in the drive afterwards.  A refused mount leaves it empty --
  // VirtualDisk::Mount unwinds itself -- so this is always the truth about
  // now, error or not.
  DiskState state;
};

// The outcome of a Save As, picked up when WM_EMU_SAVED arrives.  The state
// travels with it because saving changes what the diskette *is* -- it has a
// home afterwards -- and reporting that through a second message would leave a
// window in which the box names a folder the title has not heard of.
struct SaveResult {
  bool ok = true;
  // The folder it went to, or why it did not go.
  std::wstring detail;
  DiskState state;
};

class EmulatorThread {
 public:
  EmulatorThread() = default;
  ~EmulatorThread();
  EmulatorThread(const EmulatorThread&) = delete;
  EmulatorThread& operator=(const EmulatorThread&) = delete;

  // Takes a machine that already has its ROM loaded and its disk mounted --
  // both can fail, and they must fail before there is a window to fail behind.
  void Start(std::unique_ptr<EurekaMachine> machine, HWND notify,
             InputMode startMode, bool diagnostics);
  // Asks the worker to finish, waits for it, and gives the machine back so the
  // caller can flush the disk and read the diagnostics.  Safe to call twice.
  std::unique_ptr<EurekaMachine> Stop();

  // The diskettes that are not in the drive.  Only safe once Stop() has
  // joined the worker -- until then it is the worker's alone.  The caller
  // needs it at exit: an unsaved diskette sitting in a slot stops existing
  // with the process, so it has to be offered for saving like the one in the
  // drive.
  DiskStash& stash() { return stash_; }

  void PostKey(const HostKeyEvent& event);
  void PostReset();
  void PostSetMode(InputMode mode);
  void PostToggleMode();
  void PostSetDiagnostics(bool on);
  void PostDumpDiagnostics();
  // The four cursor keys at once (8Fh, k_udlr): how the machine is switched
  // off for real, from the Main Menu.
  void PostPowerOff();
  // Everything the host believes is held goes up.  The window sends this on
  // WM_KILLFOCUS, where the real key releases are delivered to whoever took
  // the focus instead.
  void PostFocusLost();
  // Save As: writes the current diskette out to a folder and keeps that folder
  // as its home.  It has to be the worker that does it: the machine is the
  // worker's, and a save taken from another thread mid-update would see a
  // half-written directory.
  void PostSaveDiskAs(std::wstring folder);
  // Puts a different diskette in, or takes the current one out.  Both wait for
  // the drive to go quiet before they touch anything -- see the worker.
  // writeProtected travels with the mount rather than following it, so a
  // diskette from a locked slot is never writable for the moment in between.
  void PostMountDisk(std::wstring folder, bool writeProtected = false);
  // Puts in the diskette belonging to a quick-choice slot.  Not the same as
  // mounting its folder: a slot holding an unsaved diskette hands
  // back *that* diskette, with whatever the guest has written on it, because
  // the slot is where it was while it was out of the drive.  `value` is what
  // the settings hold for the slot -- a folder or the memory marker -- and is
  // used only when the slot has no diskette put away yet.
  void PostInsertSlot(int slot, std::wstring value, bool writeProtected);
  // Says that the diskette now in the drive belongs to this slot from now on.
  // Nothing is swapped: it gives an unsaved diskette a second reference, so
  // that taking it out puts it away instead of destroying it.
  void PostAssignSlot(int slot);
  void PostEjectDisk();
  // Moves the notch on the diskette that is already in.  Not a swap: nothing
  // is flushed and nothing waits for the drive to go quiet, because no image
  // changes hands -- and a lock the user has just asked for should not sit in
  // a queue behind a running write.
  void PostSetWriteProtect(bool writeProtected);
  // Moves the notch on the diskette belonging to a slot, wherever it is: on
  // the shelf, or in the drive if that slot's diskette is the one in it.  The
  // worker has to decide which, because it owns both.
  //
  // This is the other half of "a diskette is a thing": the lock is a member of
  // the diskette, so a diskette that is not in the drive can be locked as
  // readily as one that is.  What a diskette with no home cannot do is have
  // that lock written down -- Settings::SetDiskLocked keys the list by path --
  // and the slots dialog used to answer the first question with the second
  // (6.26).
  void PostSetSlotWriteProtect(int slot, bool writeProtected);
  // Makes sure an unsaved slot has its diskette, making an empty one if it has
  // none.  Nothing is swapped and nothing waits for the drive.
  //
  // Sent when the slot is set up and once per marked slot at start-up, so that
  // a slot the settings call "*pamat" really holds a diskette from the moment
  // it exists.  Deferring the making until the first insert left a slot that
  // named a diskette and had none -- which is why its lock could not be set,
  // and why the list had to explain that in words (6.26, 6.28).
  void PostEnsureSlotDiskette(int slot);
  // A blank diskette with no home folder.  Unformatted, no track
  // answers until the guest's own format routine has been over it.
  // slot is where the new diskette belongs (0 for none), so that taking it
  // out later puts it back where the user expects to find it.
  void PostCreateEmptyDisk(bool formatted, int slot = 0);

  // Whether any slot holds an unsaved diskette that has files on it.  Asked
  // when the emulator is closing, because those diskettes stop existing with
  // it.  Read from the window thread; written by the worker.
  bool stash_has_files() const {
    return stashHasFiles_.load(std::memory_order_relaxed);
  }

  // What the shelf holds, as two bitmasks with slot n in bit n.  The slots
  // dialog needs both: whether a slot has a diskette at all decides whether
  // its lock can be *set*, and what that diskette's notch is decides how the
  // box arrives.  Published rather than asked for, because the stash is the
  // worker's and the dialog runs on the window thread.
  //
  // A slot whose diskette is in the drive right now is not in here -- Take
  // emptied it -- so the caller has to fold in the drive's own state.  That is
  // DiskState::slot, which it already has.
  unsigned stash_holds() const {
    return stashHolds_.load(std::memory_order_relaxed);
  }
  unsigned stash_locked() const {
    return stashLocked_.load(std::memory_order_relaxed);
  }

  // Read from the window thread; written by the worker.
  InputMode mode() const { return mode_.load(std::memory_order_relaxed); }
  bool diagnostics() const { return diagnostics_.load(std::memory_order_relaxed); }
  bool running() const { return running_.load(std::memory_order_relaxed); }
  // Last disk error the worker reported, for the WM_EMU_DISK_ERROR handler.
  std::wstring TakeDiskError();
  // What the last Save As did, for the WM_EMU_SAVED handler.
  SaveResult TakeSaveResult();
  // What the last swap did, for the WM_EMU_DISK_CHANGED handler.
  DiskChange TakeDiskChange();

 private:
  struct Command {
    enum class Type {
      kKey, kReset, kSetMode, kToggleMode, kSetDiagnostics,
      kDumpDiagnostics, kPowerOff, kFocusLost, kSaveDiskAs,
      kMountDisk, kEjectDisk, kCreateEmptyDisk, kInsertSlot, kAssignSlot,
      kSetWriteProtect, kSetSlotWriteProtect, kEnsureSlotDisk, kQuit,
    } type = Type::kQuit;
    HostKeyEvent key{};
    InputMode mode = InputMode::kPc;
    bool flag = false;
    // Which quick-choice slot the diskette belongs to, 0 for none.  It rides
    // with the command because the worker owns the stash, and a diskette has
    // to go back to the slot it came from when the next one takes its place.
    int slot = 0;
    std::wstring path;
  };

  void Post(Command command);
  void PostType(Command::Type type);
  void Run();

  std::unique_ptr<EurekaMachine> machine_;
  std::thread thread_;
  HWND notify_ = nullptr;

  std::mutex mutex_;
  std::deque<Command> queue_;

  std::mutex errorMutex_;
  std::wstring diskError_;
  SaveResult saveResult_;
  DiskChange diskChange_;

  // Written by the worker, read by the owner after Stop().
  DiskStash stash_;

  std::atomic<InputMode> mode_{InputMode::kPc};
  std::atomic<bool> diagnostics_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stashHasFiles_{false};
  std::atomic<unsigned> stashHolds_{0};
  std::atomic<unsigned> stashLocked_{0};
};

#endif
