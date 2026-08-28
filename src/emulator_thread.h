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

#include "machine.h"

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
  WM_EMU_EXPORT_DONE = WM_APP + 4,
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
DiskLabels DescribeDisk(const VirtualDisk& disk);

// The outcome of a swap, picked up by the window when WM_EMU_DISK_CHANGED
// arrives.  A failed mount leaves the drive empty rather than half-loaded --
// VirtualDisk::Mount already unwinds itself -- so the labels are always the
// truth about what is in there now, error or not.
struct DiskChange {
  bool ok = true;
  // True when a diskette actually went in or came out, false when the same
  // medium merely changed underfoot -- the guest formatting a blank one is
  // the case.  The window sounds its tone only for a real swap: a tone for
  // something the user did not do would say the wrong thing.
  bool swapped = true;
  // Whether there is a diskette in the drive afterwards.  Its own field and
  // not something read back out of the labels: the labels are user-facing
  // text, and a swap that decides what it did by comparing them would come
  // apart the day one of them is reworded.
  bool present = false;
  // The host folder behind it, empty for a RAM diskette or an empty drive.
  // Carried separately from the labels for the same reason as present: the
  // labels are text for the user, not data to parse back.
  std::wstring folder;
  std::wstring error;
  DiskLabels labels;
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
  // Writes the current diskette out to a folder without disturbing the guest.
  // It has to be the worker that does it: the machine is the worker's, and an
  // export taken from another thread mid-update would see a half-written
  // directory.
  void PostExportDisk(std::wstring folder);
  // Puts a different diskette in, or takes the current one out.  Both wait for
  // the drive to go quiet before they touch anything -- see the worker.
  void PostMountDisk(std::wstring folder);
  void PostEjectDisk();
  // A blank diskette that lives only in memory.  Unformatted, no track
  // answers until the guest's own format routine has been over it.
  void PostCreateRamDisk(bool formatted);

  // Read from the window thread; written by the worker.
  InputMode mode() const { return mode_.load(std::memory_order_relaxed); }
  bool diagnostics() const { return diagnostics_.load(std::memory_order_relaxed); }
  bool running() const { return running_.load(std::memory_order_relaxed); }
  // True while the diskette lives only in memory and has been written to, so
  // taking it out would throw the guest's work away with no host folder behind
  // it.  Read from the window thread before a swap: the machine belongs to the
  // worker and cannot be asked directly, and this is one bool rather than a
  // round trip.
  bool ram_disk_dirty() const {
    return ramDiskDirty_.load(std::memory_order_relaxed);
  }
  // Last disk error the worker reported, for the WM_EMU_DISK_ERROR handler.
  std::wstring TakeDiskError();
  // Empty when the last export succeeded, otherwise why it did not.
  std::wstring TakeExportResult(bool& ok);
  // What the last swap did, for the WM_EMU_DISK_CHANGED handler.
  DiskChange TakeDiskChange();

 private:
  struct Command {
    enum class Type {
      kKey, kReset, kSetMode, kToggleMode, kSetDiagnostics,
      kDumpDiagnostics, kPowerOff, kFocusLost, kExportDisk,
      kMountDisk, kEjectDisk, kCreateRamDisk, kQuit,
    } type = Type::kQuit;
    HostKeyEvent key{};
    InputMode mode = InputMode::kPc;
    bool flag = false;
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
  std::wstring exportResult_;
  bool exportOk_ = false;
  DiskChange diskChange_;

  std::atomic<InputMode> mode_{InputMode::kPc};
  std::atomic<bool> diagnostics_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> ramDiskDirty_{false};
};

#endif
