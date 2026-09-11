#include "emulator_thread.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>

#include "audio_player.h"
#include "disk_stash.h"
#include "eureka_io.h"
#include "host_console.h"
#include "text_codec.h"

namespace {

// std::this_thread::sleep_for is no use for pacing this loop: on MinGW it
// always waits a whole 15.6 ms system tick whatever it is asked for, and
// timeBeginPeriod does not change that (it only fixes ::Sleep).  Measured, the
// two millisecond sleep below ran the main loop at 63 Hz, not 500 -- which set
// the keyboard poll rate and left the audio queue sitting at 55 ms.  A high
// resolution waitable timer honours the request without raising the timer
// resolution for every other process on the machine.
class PreciseTimer {
 public:
  PreciseTimer() {
    handle_ = CreateWaitableTimerExW(nullptr, nullptr,
                                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
    // Before Windows 10 1803 the flag is rejected; a plain timer then rounds
    // to the tick exactly as ::Sleep would, which is the old behaviour.
    if (!handle_)
      handle_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
  }
  ~PreciseTimer() { if (handle_) CloseHandle(handle_); }
  PreciseTimer(const PreciseTimer&) = delete;
  PreciseTimer& operator=(const PreciseTimer&) = delete;

  void Wait(double milliseconds) const {
    if (!handle_) {
      ::Sleep(static_cast<DWORD>(milliseconds));
      return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(milliseconds * 10000.0);
    if (!SetWaitableTimer(handle_, &due, 0, nullptr, nullptr, FALSE)) {
      ::Sleep(static_cast<DWORD>(milliseconds));
      return;
    }
    WaitForSingleObject(handle_, INFINITE);
  }

 private:
  HANDLE handle_ = nullptr;
};

// Sound is this machine's whole user interface, so the emulated clock is
// steered by the audio buffer rather than left at whatever depth the device
// happened to start with.  Twenty two is the lowest target the loop still
// tracks to within about two milliseconds; below twenty the driver's own
// buffering becomes the floor, the controller pushes against it and
// overshoots instead, and the spread measured 10-48 ms.  Measured here at 22:
// 24 ms average while speaking, never closer than 16 ms to running dry.
constexpr double kTargetLatencyMs = 22.0;

// Perkins entry on a QWERTY keyboard: the six dot keys under the fingers,
// pressed as a chord.  Nothing here turns dots into letters -- the machine
// does that itself from the pattern on its row 0, in whichever of its three
// tables is currently selected, so this only presses keys.
struct HostKeyboard {
  InputMode mode = InputMode::kPc;
  // Every one of the machine's nineteen non-shift membrane keys the host
  // currently believes is physically down, indexed by virtual key rather
  // than by the row bit it contributes: Left and Home both set row2's bit
  // 2 (see HoldableBit), and letting go of Home while Left is still held
  // must not clear that bit out from under it.  Recomputed from scratch on
  // every change for the same reason -- see RecomputeHeldMembrane.
  std::array<bool, 256> heldKeys{};
  ULONGLONG heldStamp = 0;  // host time of the last held-key event
  uint8_t mods = 0;         // modifiers the machine is being told are held
};

uint8_t SpecialKey(const HostKeyEvent& key) {
  const bool shift = (key.modifiers & SHIFT_PRESSED) != 0;
  const bool alt = (key.modifiers & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
  uint8_t code = 0;
  // Stops at F11, not at F10 where KB.H stops and not at F12 where the ROM's
  // PC table does.  This function serves the built-in twenty keys: eight
  // function keys on row 1, and F9, F10 and F11 made out of space-bar chords
  // (1D541).  F11 is the "d" chord, 9Ch, and it really is F11 -- measured, it
  // says "ROM operacniho systemu", the same as scan code 57h.  F12 is CBh and
  // has no chord at all, so mapping it here would only hand PressMembraneKey
  // a code it drops without a word; left at zero, the diagnostics say so.
  if (key.virtualKey >= VK_F1 && key.virtualKey <= VK_F11)
    code = static_cast<uint8_t>(hw::kKeyF1 + key.virtualKey - VK_F1);
  else {
    switch (key.virtualKey) {
      case VK_UP: code = hw::kKeyUp; break;
      case VK_DOWN: code = hw::kKeyDown; break;
      case VK_LEFT: code = hw::kKeyLeft; break;
      case VK_RIGHT: code = hw::kKeyRight; break;
      case VK_HOME: code = hw::kKeyHome; break;
      case VK_END: code = hw::kKeyEnd; break;
      case VK_PRIOR: code = hw::kKeyPgUp; break;
      case VK_NEXT: code = hw::kKeyPgDn; break;
      // KB.H and KB.LIB disagree about these two.  The ROM settles it: its
      // own table for the PC keyboard maps scan code 52h, Insert, to 8Dh and
      // 53h, Delete, to 8Eh (DF05), which is what KB.LIB says -- so these are
      // kKeyCtrlUp and kKeyCtrlDown by KB.H's naming, and Insert and Delete
      // by the machine's behaviour.
      case VK_INSERT: code = hw::kKeyCtrlUp; break;
      case VK_DELETE: code = hw::kKeyCtrlDown; break;
      default: return 0;
    }
  }
  if (shift) code |= hw::kKeyShift;
  if (alt) code |= hw::kKeyAlt;
  return code;
}

// The row bits run in Perkins key order, left to right, not in dot number
// order: bit 0 is dot 3 and bit 2 is dot 1.  Under the hands that is exactly
// the natural layout, F D S going outwards on the left and J K L on the right.
uint8_t BrailleBit(WORD virtualKey) {
  switch (virtualKey) {
    case 'F': return hw::kBkbDot1;
    case 'D': return hw::kBkbDot2;
    case 'S': return hw::kBkbDot3;
    case 'J': return hw::kBkbDot4;
    case 'K': return hw::kBkbDot5;
    case 'L': return hw::kBkbDot6;
    // The space bar is the same key as ALT on this keyboard, and with shift
    // it is Escape (1D52F).
    case VK_SPACE: return hw::kBkbSpace;
    default: return 0;
  }
}

// Which row of HoldMembrane's live matrix one physically held key of the
// cursor keypad or the eight function keys contributes to, and what bit.
// Dots and the space bar are not here -- BrailleBit already gives their row0
// bit directly and every call site below reaches for that first.
//
// The four cursor keys are one keypad, not four keys: the ROM reads them as a
// bit set on row 8Ch, so several held at once mean a chord of their own.  That
// is why Home is up plus left (85h) and why all four together are a command --
// 8Fh, k_udlr in KB.LIB, the one that switches the Eureka off from the Main
// Menu (CP 8Fh at 18154).  Home, End, PgUp, PgDn, Insert and Delete are each
// their own physical key on a PC keyboard, not a chord of arrows, but the
// nibble they need on row 8Ch is exactly the low nibble of the Eureka code
// KB.LIB already gives them (kKeyHome = 85h = up|left), so this reads it off
// those constants instead of inventing a second table that could disagree
// with SpecialKey's.
enum class MembraneRow { kNone, kRow1, kRow2 };
struct MembraneKeyBit {
  MembraneRow row = MembraneRow::kNone;
  uint8_t bit = 0;
};
MembraneKeyBit HoldableBit(WORD virtualKey) {
  if (virtualKey >= VK_F1 && virtualKey <= VK_F8)
    return {MembraneRow::kRow1,
            static_cast<uint8_t>(1u << (virtualKey - VK_F1))};
  switch (virtualKey) {
    case VK_UP: return {MembraneRow::kRow2, hw::kKeyUp & hw::kKeyNumberMask};
    case VK_DOWN: return {MembraneRow::kRow2, hw::kKeyDown & hw::kKeyNumberMask};
    case VK_LEFT: return {MembraneRow::kRow2, hw::kKeyLeft & hw::kKeyNumberMask};
    case VK_RIGHT: return {MembraneRow::kRow2, hw::kKeyRight & hw::kKeyNumberMask};
    case VK_HOME: return {MembraneRow::kRow2, hw::kKeyHome & hw::kKeyNumberMask};
    case VK_END: return {MembraneRow::kRow2, hw::kKeyEnd & hw::kKeyNumberMask};
    case VK_PRIOR: return {MembraneRow::kRow2, hw::kKeyPgUp & hw::kKeyNumberMask};
    case VK_NEXT: return {MembraneRow::kRow2, hw::kKeyPgDn & hw::kKeyNumberMask};
    case VK_INSERT: return {MembraneRow::kRow2, hw::kKeyCtrlUp & hw::kKeyNumberMask};
    case VK_DELETE: return {MembraneRow::kRow2, hw::kKeyCtrlDown & hw::kKeyNumberMask};
    default: return {};
  }
}

// Rebuilds the three rows HoldMembrane wants from scratch out of host.heldKeys
// and sends them.  Incremental add/remove of single bits cannot work here:
// Left alone and Home both set row2's bit 2, so letting go of Home while Left
// is still down must recompute to "just Left", not clear the bit Left is
// still holding.  Two hundred and fifty-six iterations on every key event is
// nothing next to the CPU cycles a single Step() burns; this is nowhere near
// the DAC-rate keyboard poll inside the machine itself.
void RecomputeHeldMembrane(EurekaMachine& machine, const HostKeyboard& host) {
  uint8_t row0 = 0;
  uint8_t row1 = 0;
  uint8_t row2 = 0;
  for (int vk = 0; vk < static_cast<int>(host.heldKeys.size()); ++vk) {
    if (!host.heldKeys[vk]) continue;
    if (const uint8_t dot = BrailleBit(static_cast<WORD>(vk))) {
      row0 |= dot;
      continue;
    }
    const MembraneKeyBit target = HoldableBit(static_cast<WORD>(vk));
    if (target.row == MembraneRow::kRow1) row1 |= target.bit;
    else if (target.row == MembraneRow::kRow2) row2 |= target.bit;
  }
  machine.HoldMembrane(row0, row1, row2);
}

// A key-up can go missing: let the window lose focus mid-press and the release
// is delivered to whoever took the focus.  A key left believed down would
// then jam whatever row it sits on for good -- and on this machine a key that
// does nothing is indistinguishable from a key that never arrived, because
// both are silence.  Windows repeats a held key about thirty times a second,
// so anything not heard from for two seconds is not under a finger; two
// seconds is also well above the longest first-repeat delay Windows offers, so
// a slowly assembled chord is never mistaken for a stale one.  One shared
// timestamp for all nineteen keys rather than one each: as long as any of
// them is still genuinely held, its own repeats keep refreshing it, so the
// slowest legitimate chord never goes stale just because a different key on
// the matrix happened to sit still.
//
// The window now also reports WM_KILLFOCUS, which clears this outright and is
// exact where the timer only guesses; the timer stays as the backstop for a
// release lost some other way.
void ForgetStaleHeld(HostKeyboard& host, EurekaMachine& machine) {
  const ULONGLONG now = GetTickCount64();
  if (now - host.heldStamp > 2000) {
    host.heldKeys.fill(false);
    RecomputeHeldMembrane(machine, host);
  }
  host.heldStamp = now;
}

// Which modifiers the ROM is currently being told are held, one bit each so
// the difference against what Windows reports is a single XOR.
enum : uint8_t {
  kModShift = 1,
  kModCtrl = 2,
  kModAlt = 4,    // left Alt
  kModAltGr = 8,  // right Alt, the one that selects the DF98 table
};

// XT set 1 scan codes for the modifier keys themselves.  Windows hands us the
// scan code of every other key in the message, so these are the only ones
// this file has to know.
constexpr uint8_t kScanLeftShift = 0x2a;
constexpr uint8_t kScanRightShift = 0x36;
constexpr uint8_t kScanCtrl = 0x1d;
constexpr uint8_t kScanAlt = 0x38;

// The modifiers are not forwarded as key events at all.  Windows reports the
// whole modifier state on every single event, and that report is the only
// thing here worth trusting: measured 26. 8. 2026, the release of AltGr
// arrives with the extended flag already clear, so a break built from the
// event alone goes out as a plain 38h -- which clears the *left* Alt bit in
// C670h and leaves the right one set for good.  The machine then reads every
// key through the AltGr table until it is reset.
//
// Reconciling against the reported state also survives a key-up lost to a
// focus change, which is the same trap ForgetStaleHeld exists for on the
// braille keyboard's own held keys.
uint8_t WantedModifiers(DWORD state) {
  uint8_t mods = 0;
  if ((state & SHIFT_PRESSED) != 0) mods |= kModShift;
  if ((state & LEFT_ALT_PRESSED) != 0) mods |= kModAlt;
  if ((state & RIGHT_ALT_PRESSED) != 0) mods |= kModAltGr;
  // Windows builds AltGr out of left Ctrl plus right Alt on every layout that
  // has one, and reports both.  A real PC keyboard sends only the right Alt,
  // so that Ctrl is a host artifact and must not reach the wire: with it held
  // the ROM masks every character above 3Fh to a control code at 1DE16, and
  // AltGr+2 arrives as 00h instead of '@'.  Measured both ways.
  if ((mods & kModAltGr) == 0 &&
      (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0)
    mods |= kModCtrl;
  return mods;
}

void SendModifier(EurekaMachine& machine, uint8_t mod, bool down) {
  uint8_t code = 0;
  switch (mod) {
    case kModShift: code = kScanLeftShift; break;
    case kModCtrl: code = kScanCtrl; break;
    case kModAlt: code = kScanAlt; break;
    // The right Alt is the one extended key among them, so it needs the E0
    // prefix on the break as much as on the make -- DFD6 lists both 38h and
    // B8h, and only behind an E0 does the decoder ever look there (1DD48).
    case kModAltGr:
      machine.QueueScanCode(hw::kScanExtended);
      code = kScanAlt;
      break;
    default: return;
  }
  machine.QueueScanCode(down ? code
                             : static_cast<uint8_t>(code | hw::kScanBreak));
}

// Brings the machine's idea of the modifiers in line with the host's.  Called
// on every key event in PC mode, and with nothing held on the way out of it:
// leaving a modifier down there would leave it down for good.
void SyncModifiers(EurekaMachine& machine, HostKeyboard& host, uint8_t wanted) {
  // Let go before pressing, so that on the way from one Alt to the other the
  // machine never has both of them down at once.
  for (uint8_t mod : {kModShift, kModCtrl, kModAlt, kModAltGr})
    if ((host.mods & mod) != 0 && (wanted & mod) == 0)
      SendModifier(machine, mod, false);
  for (uint8_t mod : {kModShift, kModCtrl, kModAlt, kModAltGr})
    if ((host.mods & mod) == 0 && (wanted & mod) != 0)
      SendModifier(machine, mod, true);
  host.mods = wanted;
}

// Forwards one host key event to the machine's serial keyboard.  Windows
// already hands us an XT set 1 scan code in the message, and the grey keys are
// the same code with the extended flag, which on the wire is an E0h prefix --
// so this is a wire, not a translation table.  Everything else, the Czech
// QWERTZ layout at DF05 included, is the ROM's own work.
void SendScanCode(EurekaMachine& machine, const HostKeyEvent& key) {
  const uint8_t code = key.scanCode;
  if (code == 0 || code >= hw::kScanBreak) return;
  // SyncModifiers owns these, from the state Windows reports rather than from
  // the event, so a make or break built here would fight it.
  if (code == kScanLeftShift || code == kScanRightShift || code == kScanCtrl ||
      code == kScanAlt)
    return;
  if (key.extended) machine.QueueScanCode(hw::kScanExtended);
  machine.QueueScanCode(key.down ? code
                                 : static_cast<uint8_t>(code | hw::kScanBreak));
}

// With diagnostics on, every key event is echoed with what the emulator made
// of it.  A keyboard that does nothing is otherwise impossible to tell apart
// from a key that never arrived: both are silence.
void TraceKey(const HostKeyEvent& key, InputMode mode, const wchar_t* took) {
  wchar_t line[220];
  swprintf(line, 220,
           L"[kláves %ls vk=%02X sc=%02X stav=%04X%ls režim=%ls -> %ls]\r\n",
           key.down ? L"dole" : L"hore",
           static_cast<unsigned>(key.virtualKey),
           static_cast<unsigned>(key.scanCode),
           static_cast<unsigned>(key.modifiers),
           key.autoRepeat ? L" opak." : L"", ModeName(mode), took);
  host::Print(line);
}

}  // namespace

const wchar_t* ModeName(InputMode mode) {
  return mode == InputMode::kBraille ? L"braillovská" : L"externá";
}

HostKeyEvent KeyEventFromMessage(bool down, WPARAM wParam, LPARAM lParam) {
  HostKeyEvent event;
  event.down = down;
  event.virtualKey = static_cast<WORD>(wParam);
  event.scanCode = static_cast<uint8_t>((lParam >> 16) & 0xff);
  event.extended = (lParam & (1 << 24)) != 0;
  event.autoRepeat = down && (lParam & (1 << 30)) != 0;
  // The console handed the whole modifier state with every record and the
  // translation below was measured against those bits; GetKeyState gives the
  // same picture, so asking it here keeps every consumer unchanged.
  DWORD state = 0;
  if (GetKeyState(VK_SHIFT) < 0) state |= SHIFT_PRESSED;
  if (GetKeyState(VK_LCONTROL) < 0) state |= LEFT_CTRL_PRESSED;
  if (GetKeyState(VK_RCONTROL) < 0) state |= RIGHT_CTRL_PRESSED;
  if (GetKeyState(VK_LMENU) < 0) state |= LEFT_ALT_PRESSED;
  if (GetKeyState(VK_RMENU) < 0) state |= RIGHT_ALT_PRESSED;
  if (event.extended) state |= ENHANCED_KEY;
  event.modifiers = state;
  return event;
}

HostKeyEvent SyntheticKey(bool down, WORD virtualKey, bool alt) {
  HostKeyEvent event;
  event.down = down;
  event.virtualKey = virtualKey;
  // Asked of Windows rather than written down here, because in PC mode the
  // scan code *is* the message -- SendScanCode is a wire, not a table -- and
  // this is the same source the real messages come from.
  event.scanCode =
      static_cast<uint8_t>(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC));
  // Alt is held for the press and let go for the release, which is what a
  // hand does.  In PC mode SyncModifiers then sends 38h before the key and
  // B8h after it, leaving nothing down -- a menu command that left a
  // modifier held would be the silent stuck-Alt bug all over again.  In
  // braille mode the release carries no Alt either, and that is fine: SameKey
  // masks the modifier bits precisely because a hand lets go in that order.
  event.modifiers = (alt && down) ? LEFT_ALT_PRESSED : 0;
  return event;
}

EmulatorThread::~EmulatorThread() { Stop(); }

void EmulatorThread::Start(std::unique_ptr<EurekaMachine> machine, HWND notify,
                           InputMode startMode, bool diagnostics) {
  machine_ = std::move(machine);
  notify_ = notify;
  mode_.store(startMode, std::memory_order_relaxed);
  diagnostics_.store(diagnostics, std::memory_order_relaxed);
  running_.store(true, std::memory_order_relaxed);
  poweredOff_.store(false, std::memory_order_relaxed);
  thread_ = std::thread([this] { Run(); });
}

std::unique_ptr<EurekaMachine> EmulatorThread::Stop() {
  if (thread_.joinable()) {
    PostType(Command::Type::kQuit);
    thread_.join();
  }
  running_.store(false, std::memory_order_relaxed);
  return std::move(machine_);
}

void EmulatorThread::Post(Command command) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(command);
}

void EmulatorThread::PostType(Command::Type type) {
  Command command;
  command.type = type;
  Post(std::move(command));
}

void EmulatorThread::PostKey(const HostKeyEvent& event) {
  Command command;
  command.type = Command::Type::kKey;
  command.key = event;
  Post(std::move(command));
}

void EmulatorThread::PostReset() { PostType(Command::Type::kReset); }

void EmulatorThread::PostSetMode(InputMode mode) {
  Command command;
  command.type = Command::Type::kSetMode;
  command.mode = mode;
  Post(std::move(command));
}

void EmulatorThread::PostToggleMode() { PostType(Command::Type::kToggleMode); }

void EmulatorThread::PostSetDiagnostics(bool on) {
  Command command;
  command.type = Command::Type::kSetDiagnostics;
  command.flag = on;
  Post(std::move(command));
}

void EmulatorThread::PostDumpDiagnostics() {
  PostType(Command::Type::kDumpDiagnostics);
}

void EmulatorThread::PostPowerOff() { PostType(Command::Type::kPowerOff); }

void EmulatorThread::PostPowerOn() { PostType(Command::Type::kPowerOn); }

void EmulatorThread::PostFocusLost() { PostType(Command::Type::kFocusLost); }

void EmulatorThread::PostSaveDiskAs(std::wstring folder) {
  Command command;
  command.type = Command::Type::kSaveDiskAs;
  command.path = std::move(folder);
  Post(std::move(command));
}

// The title carries the diskette's name and not its path: a screen reader
// reads the whole title on every Alt+Tab and on NVDA+T, and a path spelled out
// that often is noise.  The path stays in Pomocník -> O programe.
DiskState DescribeDisk(const VirtualDisk& disk) {
  DiskState state;
  state.present = disk.present();
  state.writeProtected = disk.write_protected();
  state.unsaved = disk.present() && !disk.has_home();
  state.files = disk.StoredFiles();
  if (!disk.present()) {
    state.labels = {L"žiadna", L"žiadna, mechanika je prázdna"};
  } else if (disk.has_home()) {
    state.home = disk.home().wstring();
    state.slotValue = state.home;
    state.labels = {SlotDisplayName(state.home), state.home};
  } else {
    // "Neuložená" and not "v pamäti", which is what this said for two years
    // and is not a difference at all: a mounted folder is read into an image
    // in memory too.  What sets this diskette apart is that it has nowhere to
    // be saved *yet* -- Ctrl+U gives it a folder and this name goes away,
    // which is exactly the promise the word has to make.
    //
    // Formatted or not is deliberately not in here.  It is a passing state of
    // the medium, not what the medium is, and a title that tracked it would be
    // telling the user about something they are in the middle of changing.
    state.slotValue = kSlotUnsaved;
    state.labels = {L"neuložená", L"neuložená disketa"};
  }
  return state;
}

void EmulatorThread::PostMountDisk(std::wstring folder, bool writeProtected) {
  Command command;
  command.type = Command::Type::kMountDisk;
  command.path = std::move(folder);
  command.flag = writeProtected;
  Post(std::move(command));
}

void EmulatorThread::PostSetWriteProtect(bool writeProtected) {
  Command command;
  command.type = Command::Type::kSetWriteProtect;
  command.flag = writeProtected;
  Post(std::move(command));
}

void EmulatorThread::PostInsertSlot(int slot, std::wstring value,
                                    bool writeProtected) {
  Command command;
  command.type = Command::Type::kInsertSlot;
  command.slot = slot;
  command.path = std::move(value);
  command.flag = writeProtected;
  Post(std::move(command));
}

void EmulatorThread::PostEnsureSlotDiskette(int slot) {
  Command command;
  command.type = Command::Type::kEnsureSlotDisk;
  command.slot = slot;
  Post(std::move(command));
}

void EmulatorThread::PostSetSlotWriteProtect(int slot, bool writeProtected) {
  Command command;
  command.type = Command::Type::kSetSlotWriteProtect;
  command.slot = slot;
  command.flag = writeProtected;
  Post(std::move(command));
}

void EmulatorThread::PostAssignSlot(int slot) {
  Command command;
  command.type = Command::Type::kAssignSlot;
  command.slot = slot;
  Post(std::move(command));
}

void EmulatorThread::PostEjectDisk() { PostType(Command::Type::kEjectDisk); }

void EmulatorThread::PostCreateEmptyDisk(bool formatted, int slot) {
  Command command;
  command.type = Command::Type::kCreateEmptyDisk;
  command.flag = formatted;
  command.slot = slot;
  Post(std::move(command));
}

DiskChange EmulatorThread::TakeDiskChange() {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return std::move(diskChange_);
}

std::wstring EmulatorThread::TakeDiskError() {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return std::move(diskError_);
}

SaveResult EmulatorThread::TakeSaveResult() {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return std::move(saveResult_);
}

void EmulatorThread::Run() {
  EurekaMachine& machine = *machine_;
  HostKeyboard host;
  host.mode = mode_.load(std::memory_order_relaxed);
  const HWND notify = notify_;

  AudioPlayer audio;
  // Told to the window rather than printed: there is no console unless the
  // user asked for one, and this is the least losable message the emulator
  // has -- without sound this machine has no user interface at all.
  if (!audio.Open(EurekaMachine::kAudioHz) && notify)
    PostMessageW(notify, WM_EMU_NO_AUDIO, 0, 0);

  using Clock = std::chrono::steady_clock;
  PreciseTimer timer;
  auto lastTick = Clock::now();
  // Guest cycles the wall clock has earned so far.  Fractional because the
  // rate is nudged by a couple of percent to steer the audio buffer.
  long double guestClock = 0.0L;
  std::wstring error;
  bool running = true;
  // What the worker last told the window about the power switch, so that only
  // the change is reported and not every pass of a two-millisecond loop.
  bool poweredOff = false;

  // One host key event, already off the queue.  Split out of the command loop
  // only because it is long; it runs on the worker like everything else.
  const auto applyKey = [&](const HostKeyEvent& key, bool trace) {
    if (!key.down) {
      if (host.mode == InputMode::kPc) {
        if (trace) TraceKey(key, host.mode, L"scan kód");
        SyncModifiers(machine, host, WantedModifiers(key.modifiers));
        SendScanCode(machine, key);
        return;
      }
      // Before the Ctrl guard below, and synced rather than toggled, because a
      // shift left held down would silently capitalise everything after it:
      // the accelerator table eats presses but never releases, so the guard
      // would swallow the one event that lets it go.
      machine.HoldShift((key.modifiers & SHIFT_PRESSED) != 0);
      // Ctrl is not a key on the braille keyboard, so nothing pressed with it
      // held ever entered the live matrix -- either the down path below
      // dropped it, or, while the host owns the keyboard, the accelerator
      // table ate the press outright.  Either way its release must not touch
      // held state that was never set.
      if ((key.modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0 &&
          (key.modifiers & RIGHT_ALT_PRESSED) == 0)
        return;
      // Letting go of a dot, the space bar, a function key or a cursor-pad
      // key: the matrix loses that bit and the ROM's own scan decides what,
      // if anything, that means.  There is no chord to finish here any more
      // -- HoldMembrane already showed the ROM every state on the way up.
      if (BrailleBit(key.virtualKey) != 0 ||
          HoldableBit(key.virtualKey).row != MembraneRow::kNone) {
        ForgetStaleHeld(host, machine);
        host.heldKeys[key.virtualKey] = false;
        RecomputeHeldMembrane(machine, host);
        return;
      }
      // Only F9, F10 and F11 reach here: they are chords of the space bar and
      // braille dots the ROM itself decodes as one key (1D541), not a state
      // to hold, so they stay on the frame queue like Escape does.
      if (const uint8_t special = SpecialKey(key)) machine.ReleaseKey(special);
      return;
    }

    // The emulator's own shortcuts are gone from here: they are entries in an
    // accelerator table now, so TranslateAccelerator eats those presses before
    // the window ever sees them.  One owner, one place to look.  Note that
    // Ctrl+letter does reach this function in the ordinary case -- the table
    // that holds those is only in force after F11 or Shift+F11 -- so in PC
    // mode the guest gets its control character as it always did.
    const uint8_t wanted = WantedModifiers(key.modifiers);
    const bool ctrl = (wanted & kModCtrl) != 0;
    const bool shift = (wanted & kModShift) != 0;
    if (trace) TraceKey(key, host.mode, L"do Eureky");

    if (host.mode == InputMode::kPc) {
      SyncModifiers(machine, host, wanted);
      SendScanCode(machine, key);
      return;
    }
    // Shift is the twentieth key of this keyboard and the machine watches it
    // as one, so the host holds it rather than only folding it into a chord.
    // That is what pausing continuous reading in the word processor needs: a
    // key going down where the shadow has no bit sets spabrt (C620h) and the
    // speech stops.  Measured -- with shift the utterance ended after 203k
    // cycles instead of 783k.  Held rather than tapped because that is what
    // the hardware does; the chord that follows carries the bit as well, so
    // capitals and Shift+Fn are unaffected, and space-bar chords like
    // Shift+F9 decode the same with it held (measured too).
    machine.HoldShift(shift);
    // Ctrl is not a key on this keyboard, so Ctrl+letter is not a dot either
    // -- the accelerator table would have eaten it before this ran anyway,
    // whenever the host owns the keyboard.  Auto-repeat needs no special
    // case any more: setting a bit that is already set is a no-op, and
    // ForgetStaleHeld still wants the timestamp refreshed so a key genuinely
    // held this long is never mistaken for one whose release went missing.
    if (ctrl ? 0 : BrailleBit(key.virtualKey)) {
      ForgetStaleHeld(host, machine);
      host.heldKeys[key.virtualKey] = true;
      RecomputeHeldMembrane(machine, host);
      return;
    }
    // Escape has no key of its own on the twenty-key keyboard: the machine
    // makes it out of shift and the bare space bar (1D52F), which is why
    // Shift+Space here already produces it.  The Esc key is the reflex of
    // every hand that learned on a PC, and there is nothing else it could
    // mean in this mode, so it presses that chord.  Sent on the press, not on
    // the release: it is one deliberate act, not a pattern of dots being
    // collected.  Auto-repeat is dropped because PressBraille holds nothing
    // down -- thirty taps a second would queue thirty chords the machine then
    // works through long after the finger came up.
    if (!ctrl && key.virtualKey == VK_ESCAPE) {
      if (!key.autoRepeat) machine.PressBraille(hw::kBkbSpace, true);
      return;
    }
    // Function keys F1-F8 and the whole cursor pad -- arrows, Home, End,
    // PgUp, PgDn, Insert, Delete -- are live matrix state now, exactly like
    // the dots: pressing one no longer sends a finished key by itself, it
    // sets a bit HoldMembrane presents to the ROM's own scan, which is what
    // lets it combine with the space bar (ea4-cb4, ea4-e9k) the way the
    // hardware does.  Not Ctrl-gated, the same as before this held layer
    // existed: only dots were ever masked by Ctrl.
    if (HoldableBit(key.virtualKey).row != MembraneRow::kNone) {
      ForgetStaleHeld(host, machine);
      host.heldKeys[key.virtualKey] = true;
      RecomputeHeldMembrane(machine, host);
      return;
    }
    // Only F9, F10 and F11 reach here now: chords of the space bar and
    // braille dots the ROM decodes as one key (1D541), sent whole rather than
    // held, exactly like Escape above.
    if (const uint8_t special = SpecialKey(key)) {
      machine.QueueKey(special);
      return;
    }
    // Nothing else on a braille keyboard is a key.  Text used to be handed
    // straight to the ROM's queue from here, which no machine could do; who
    // wants to write in this mode writes dots, as on the machine.  Saying so
    // matters more here than anywhere else: on this machine a key that does
    // nothing is indistinguishable from a key that never arrived, because both
    // are silence.
    if (trace)
      TraceKey(key, host.mode,
               L"ignorované, braillovská klávesnica text nepíše — píšte bodmi");
  };

  // A diskette change waits for the drive to go quiet instead of forcing
  // itself through: swapping mid-sector would tear the image, and what the
  // guest wrote has to reach the host folder first.  It is parked here rather
  // than spun on inside apply(), because spinning would stop feeding the sound
  // device -- and at this loop's 22 ms of latency that is a gap in the middle
  // of a word.
  std::optional<Command> pendingDisk;
  auto pendingSince = Clock::now();

  // The diskettes that are not in the drive, and which slot the one in the
  // drive belongs to.  An unsaved diskette exists nowhere but here,
  // so taking it out has to put it somewhere -- see disk_stash.h for what
  // the machine's own bulk copy does to a target that comes back blank.
  DiskStash& stash = stash_;
  // Zero: the diskette the emulator started with came from the command line or
  // from "posledna-disketa", neither of which is a slot.  It gets a slot only
  // when the user puts one in from the quick choice.
  int currentSlot = 0;

  // What the window is allowed to know about the shelf.  Published from here
  // and nowhere else, because everything that can change it goes through this
  // thread: a diskette put away or taken back, and a notch moved on one that
  // is sitting there.  The window needs it for the slots dialog, which has to
  // tell "this slot's diskette is locked" from "this slot has no diskette to
  // lock yet" -- and answering the second with the first is 6.26.
  const auto publishStash = [&] {
    unsigned holds = 0;
    unsigned locked = 0;
    for (int slot = 1; slot <= DiskStash::kSlots; ++slot) {
      if (!stash.holds(slot)) continue;
      holds |= 1u << slot;
      if (stash.WriteProtected(slot)) locked |= 1u << slot;
    }
    stashHolds_.store(holds, std::memory_order_relaxed);
    stashLocked_.store(locked, std::memory_order_relaxed);
    stashHasFiles_.store(stash.HoldsAnythingWritten(),
                         std::memory_order_relaxed);
  };

  const auto changeDisk = [&](const Command& command) {
    DiskChange result;
    // What the old diskette still owes its folder goes back first: after this
    // its image is gone, and a write that never reached the host would look
    // exactly like a file the user lost.
    std::wstring changeError;
    result.ok = machine.FlushDisk(changeError);
    if (result.ok) {
      // Out of the drive and onto the shelf, before anything replaces it.
      // Declined for a diskette with a home, which needs no copy.
      stash.Put(currentSlot, machine.disk());
      switch (command.type) {
        case Command::Type::kEjectDisk:
          machine.EjectDisk();
          currentSlot = 0;
          break;
        case Command::Type::kCreateEmptyDisk:
          machine.CreateEmptyDisk(command.flag);
          currentSlot = command.slot;
          break;
        case Command::Type::kInsertSlot: {
          // What the slot *names* decides where to look, and only then does
          // the shelf come into it.  The other way round -- shelf first,
          // whatever the slot names -- was a quiet trap: point a used memory
          // slot at a folder and Ctrl+3 went on handing back the diskette from
          // the shelf while the menu named the folder.  Same family as the
          // stale promise in 6.25, and eager creation would have made it easy
          // to meet.
          //
          // The shelved diskette is left where it is rather than dropped: it
          // may have files on it, and main.cpp offers to save every one of
          // them on the way out.  Unreachable for the rest of the session is
          // the user's own doing; destroyed without asking would not be.
          if (SlotIsUnsaved(command.path)) {
            // Its own diskette, or a fresh one the first time.  Since a marked
            // slot is given a diskette when it is set up, the second branch is
            // only for a slot whose diskette is somehow not there.
            if (auto disk = stash.Take(command.slot))
              machine.InsertDisk(*disk);
            else
              machine.CreateEmptyDisk(true);
          } else {
            result.ok =
                machine.MountDisk(command.path, changeError, &result.tooBig);
            if (result.ok) machine.SetDiskWriteProtected(command.flag);
          }
          currentSlot = command.slot;
          break;
        }
        default:
          result.ok =
              machine.MountDisk(command.path, changeError, &result.tooBig);
          // Set after the mount, which clears it: the notch belongs to the
          // medium, so Mount hands back an unprotected one and the slot's
          // wish is applied to the diskette that is now in.
          if (result.ok) machine.SetDiskWriteProtected(command.flag);
          currentSlot = 0;
          break;
      }
    }
    publishStash();
    if (!result.ok) result.error = changeError;
    // Only for the refusal the window can act on, so it cannot offer to split
    // a folder that was refused for some other reason entirely.
    if (result.tooBig) result.attempted = command.path;
    // Read after the change, so this is what is in the drive now.  A refused
    // mount leaves it empty, which the labels then say.
    result.state = DescribeDisk(machine.disk());
    // Which slot the diskette now in the drive belongs to.  The window needs
    // it to know whether anything else points at an unsaved diskette: one
    // with no slot has only this drive holding it.
    result.state.slot = currentSlot;
    {
      std::lock_guard<std::mutex> lock(errorMutex_);
      diskChange_ = std::move(result);
    }
    if (notify) PostMessageW(notify, WM_EMU_DISK_CHANGED, 0, 0);
  };

  // Everything from the window thread -- keys included -- comes through one
  // queue so the order the user produced is the order the machine sees: a mode
  // switch must never overtake the key typed after it.
  const auto apply = [&](const Command& command) {
    const bool trace = diagnostics_.load(std::memory_order_relaxed);
    switch (command.type) {
      case Command::Type::kKey:
        applyKey(command.key, trace);
        break;
      case Command::Type::kQuit:
        running = false;
        break;
      case Command::Type::kReset:
        machine.Reset();
        // The chosen writing mode is the user's, not the machine's, so it
        // survives; a key caught half-pressed does not.  Reset() already
        // zeroed the machine's own live layer; this just keeps the host's
        // bookkeeping from reintroducing a stale key on the next event.
        host.heldKeys.fill(false);
        host.mods = 0;
        audio.Close();
        audio.Open(EurekaMachine::kAudioHz);
        lastTick = Clock::now();
        guestClock = 0.0L;
        host::Print(L"\r\n[Eureka bola resetovaná]\r\n");
        break;
      case Command::Type::kSetMode:
      case Command::Type::kToggleMode: {
        const InputMode wanted =
            command.type == Command::Type::kSetMode
                ? command.mode
                : (host.mode == InputMode::kPc ? InputMode::kBraille
                                               : InputMode::kPc);
        if (wanted == host.mode) break;
        // Leaving a modifier held on the way out of PC mode would leave it
        // held for good: nothing in braille mode ever lets one go.
        if (host.mode == InputMode::kPc) SyncModifiers(machine, host, 0);
        host.mode = wanted;
        // A key held on the way out of braille mode would otherwise keep
        // sitting on the live matrix -- switching to PC mode does not stop
        // the ports reading it, only stops anything new from setting it.
        host.heldKeys.fill(false);
        RecomputeHeldMembrane(machine, host);
        // The same rule the other way round: in PC mode nothing ever lifts the
        // membrane's shift key, so it must not be left down on the way out.
        machine.HoldShift(false);
        mode_.store(wanted, std::memory_order_relaxed);
        if (notify) PostMessageW(notify, WM_EMU_STATE, 0, 0);
        break;
      }
      case Command::Type::kSetDiagnostics:
        diagnostics_.store(command.flag, std::memory_order_relaxed);
        machine.diagnostics().set_enabled(command.flag);
        if (notify) PostMessageW(notify, WM_EMU_STATE, 0, 0);
        break;
      case Command::Type::kDumpDiagnostics:
        host::Print(std::wstring(L"\r\n[Režim písania: ") +
                    ModeName(host.mode) + L"]\r\n");
        // Unconditional: the window checks whether diagnostics are on before
        // it posts this, because saying "they are off" is a message for the
        // user and would be lost in a console nobody has opened.
        host::Print(machine.diagnostics().Report());
        break;
      case Command::Type::kPowerOff:
        // Sent as a key rather than faked, so the firmware does its own
        // farewell.  The chord is built in machine.h, where the integration
        // test's power mode presses it too -- this used to be spelled out here
        // and the release that followed it silently disarmed the whole thing.
        machine.PressPowerOffChord();
        break;
      case Command::Type::kPowerOn:
        // Warm: the RAM, the clock and the eight bytes of alarm survive, so the
        // firmware comes up where the user was.  Nothing is done to the sound
        // device here, unlike Reset -- switching off drained it and waited for
        // it, and reopening would only throw away the pacing the latency
        // control has learned.  The cycle counter survives too, so guestClock
        // needs no correction: the debt ceiling in the run loop held it a
        // quarter second ahead of a clock that was not moving.
        machine.PowerOn();
        // The user's writing mode is theirs and survives; a key caught
        // half-pressed does not.  Same reasoning as Reset above.
        host.heldKeys.fill(false);
        host.mods = 0;
        host::Print(L"\r\n[Eureka bola zapnutá]\r\n");
        break;
      case Command::Type::kFocusLost:
        // Exact where ForgetStaleHeld only guesses: the releases for these
        // are about to be delivered to whoever took the focus.
        if (host.mode == InputMode::kPc) SyncModifiers(machine, host, 0);
        host.heldKeys.fill(false);
        RecomputeHeldMembrane(machine, host);
        machine.HoldShift(false);
        break;
      case Command::Type::kSaveDiskAs: {
        // Flushed first, so what lands in the folder is the image the guest
        // has actually finished writing rather than whatever was in it when
        // the menu was opened.  For a diskette that already had a home this
        // pays that one off before moving house, which is why the old folder
        // is left complete rather than one write short.
        std::wstring saveError;
        SaveResult result;
        result.ok = machine.FlushDisk(saveError) &&
                    machine.SaveDiskAs(command.path, saveError);
        result.detail = result.ok ? command.path : saveError;
        // Read after the save, so this says the diskette has a home now.
        result.state = DescribeDisk(machine.disk());
        result.state.slot = currentSlot;
        {
          std::lock_guard<std::mutex> lock(errorMutex_);
          saveResult_ = std::move(result);
        }
        if (notify) PostMessageW(notify, WM_EMU_SAVED, 0, 0);
        break;
      }
      case Command::Type::kEnsureSlotDisk:
        // Not while that slot's diskette is in the drive: it is not on the
        // shelf because the machine has it, and making a second one there
        // would leave two diskettes claiming one slot -- the one in the drive
        // would then be the one that loses, because putting it away overwrites.
        if (command.slot != currentSlot) {
          stash.CreateEmptyIfMissing(command.slot);
          publishStash();
        }
        break;
      case Command::Type::kSetSlotWriteProtect: {
        // The diskette this slot owns, wherever it is.  In the drive it is the
        // machine's and the window has to hear about it -- the title carries
        // the lock; on the shelf nothing visible changes, so nothing is
        // posted, only the published masks move.
        if (command.slot == currentSlot) {
          machine.SetDiskWriteProtected(command.flag);
          DiskChange result;
          result.swapped = false;
          result.state = DescribeDisk(machine.disk());
          result.state.slot = currentSlot;
          {
            std::lock_guard<std::mutex> lock(errorMutex_);
            diskChange_ = std::move(result);
          }
          if (notify) PostMessageW(notify, WM_EMU_DISK_CHANGED, 0, 0);
        } else {
          stash.SetWriteProtected(command.slot, command.flag);
        }
        publishStash();
        break;
      }
      case Command::Type::kAssignSlot: {
        // No swap and no waiting: only the bookkeeping changes.  Reported the
        // same way a lock is, so the window learns the diskette now has a
        // slot and stops warning about losing it.
        currentSlot = command.slot;
        DiskChange result;
        result.swapped = false;
        result.state = DescribeDisk(machine.disk());
        result.state.slot = currentSlot;
        {
          std::lock_guard<std::mutex> lock(errorMutex_);
          diskChange_ = std::move(result);
        }
        if (notify) PostMessageW(notify, WM_EMU_DISK_CHANGED, 0, 0);
        break;
      }
      case Command::Type::kSetWriteProtect: {
        // No flush and no waiting for the drive: nothing about the image
        // changes, only whether the controller will take a write.  Reported
        // through the same message as a swap so the title and the menu have
        // one road in, with swapped false to keep the tones apart.
        machine.SetDiskWriteProtected(command.flag);
        DiskChange result;
        result.swapped = false;
        result.state = DescribeDisk(machine.disk());
    // Which slot the diskette now in the drive belongs to.  The window needs
    // it to know whether anything else points at an unsaved diskette: one
    // with no slot has only this drive holding it.
    result.state.slot = currentSlot;
        {
          std::lock_guard<std::mutex> lock(errorMutex_);
          diskChange_ = std::move(result);
        }
        if (notify) PostMessageW(notify, WM_EMU_DISK_CHANGED, 0, 0);
        break;
      }
      case Command::Type::kMountDisk:
      case Command::Type::kEjectDisk:
      case Command::Type::kCreateEmptyDisk:
      case Command::Type::kInsertSlot:
        // Done straight away when the drive is already quiet, which it nearly
        // always is; otherwise it waits in the main loop below.  A second
        // request replaces the first: the user changed their mind, and doing
        // both would put in a diskette they no longer asked for.
        if (machine.DiskSwappable()) {
          changeDisk(command);
        } else {
          pendingDisk = command;
          pendingSince = Clock::now();
        }
        break;
    }
  };

  while (running) {
    for (;;) {
      Command command;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) break;
        command = queue_.front();
        queue_.pop_front();
      }
      apply(command);
      if (!running) break;
    }
    if (!running) break;

    const auto now = Clock::now();
    double delta = std::chrono::duration<double>(now - lastTick).count();
    lastTick = now;
    // A scheduling stall is the host's problem, not the guest's; letting one
    // through unclamped would turn into a sprint through the speech engine.
    if (delta > 0.25) delta = 0.25;

    // Slewing the emulated clock by at most two percent is how the audio
    // buffer is held at kTargetLatencyMs.  It is the right knob rather than
    // dropping or repeating samples: the DAC output stays coherent, and two
    // percent of 6.144 MHz is a third of a semitone nobody can hear.  Without
    // it nothing ever drains the queue the device started out with, because
    // producer and consumer both run at exactly real time.
    double rate = static_cast<double>(EurekaMachine::kCpuHz);
    if (audio.Ready()) {
      const double audioError = audio.QueuedMs() - kTargetLatencyMs;
      rate *= 1.0 - std::clamp(audioError * 0.002, -0.02, 0.02);
    }
    guestClock += static_cast<long double>(rate) * delta;

    const uint64_t target = static_cast<uint64_t>(guestClock);
    // The CPU is never parked, so it always runs the cycles the wall clock has
    // earned: the ROM waits for a key by spinning in its own dispatcher, and
    // the DAC and the filter behind it are fed by that spinning exactly as
    // they are on the hardware.
    unsigned steps = 0;
    while (machine.cycles() < target && steps++ < 200000)
      if (!machine.Step()) break;  // switched itself off
    // If the host could not keep up after all, forgive the debt rather than
    // carry it: catching up faster than real time would garble the speech.
    const uint64_t done = machine.cycles();
    const long double ceiling =
        static_cast<long double>(done) +
        static_cast<long double>(EurekaMachine::kCpuHz) / 4.0L;
    if (guestClock > ceiling) guestClock = ceiling;

    // Always taken, so the buffer cannot grow without bound, but only printed
    // with diagnostics on: the console exists only when it was asked for, and
    // the guest's console device is a diagnostic, not the machine's output --
    // its output is speech.
    auto output = machine.TakeConsoleOutput();
    if (!output.empty() && diagnostics_.load(std::memory_order_relaxed))
      host::Print(DecodeKamenicky(output.data(), output.size()));
    audio.Submit(machine.TakeAudio());

    // The machine switched itself off: the four cursor keys from the Main
    // Menu, or five minutes of nobody touching it.  It says "konec" first, and
    // that announcement is still inside the sound device when the strobe fires
    // -- AudioPlayer::Close() calls waveOutReset and would throw it away -- so
    // wait for it to play out.  Two seconds is a ceiling for a device that
    // stops reporting progress, not a pause.
    //
    // The loop does not end here, and that is the change: switching off is a
    // state the machine sits in, not the end of the run.  On the hardware the
    // RAM and the clock keep a supply of their own, so the worker stays alive
    // holding them and goes on serving commands -- kPowerOn starts it again
    // warm and kReset cold, and one day an alarm could do it too.  Nothing has
    // to stop the CPU on the way:
    // EurekaMachine::Step returns false while it is off, and the debt ceiling
    // above pins guestClock to a quarter second ahead of a clock that is no
    // longer moving, so switching on does not sprint through the time spent
    // off.
    if (machine.powered_off() != poweredOff) {
      poweredOff = machine.powered_off();
      poweredOff_.store(poweredOff, std::memory_order_relaxed);
      if (poweredOff) {
        const auto until = Clock::now() + std::chrono::seconds(2);
        while (audio.Ready() && audio.QueuedMs() > 1.0 && Clock::now() < until)
          timer.Wait(5.0);
      }
      if (notify)
        PostMessageW(notify, WM_EMU_POWERED_OFF, poweredOff ? 1 : 0, 0);
    }

    // Written back once the guest has finished with the disk, not on a clock:
    // an export taken mid-update would see a half-written directory.
    if (machine.DiskSettled() && !machine.FlushDisk(error)) {
      {
        std::lock_guard<std::mutex> lock(errorMutex_);
        diskError_ = error;
      }
      running = false;
      if (notify) PostMessageW(notify, WM_EMU_DISK_ERROR, 0, 0);
      break;
    }

    // A parked change goes through as soon as the controller is idle and the
    // flush above has cleaned the image.  The ceiling is not a limit on the
    // guest's work -- a settle costs one second of guest time and is reached
    // long before this -- it is for a controller left mid-transfer by firmware
    // that stopped feeding it, where waiting would be waiting for ever.
    if (pendingDisk) {
      if (machine.DiskSwappable()) {
        changeDisk(*pendingDisk);
        pendingDisk.reset();
      } else if (Clock::now() - pendingSince > std::chrono::seconds(5)) {
        DiskChange refused;
        refused.ok = false;
        refused.error =
            L"Disketu sa nepodarilo vymeniť: mechanika päť sekúnd "
            L"neprestala zapisovať.\r\n\r\nPôvodná disketa zostala v "
            L"mechanike. Skúste to znova, keď Eureka dopracuje.";
        refused.state = DescribeDisk(machine.disk());
        {
          std::lock_guard<std::mutex> lock(errorMutex_);
          diskChange_ = std::move(refused);
        }
        pendingDisk.reset();
        if (notify) PostMessageW(notify, WM_EMU_DISK_CHANGED, 0, 0);
      }
    }
    timer.Wait(2.0);
  }

  audio.Close();
  running_.store(false, std::memory_order_relaxed);
}
