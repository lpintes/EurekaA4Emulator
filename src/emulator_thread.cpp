#include "emulator_thread.h"

#include <algorithm>
#include <chrono>

#include "audio_player.h"
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
  uint8_t held = 0;   // dot keys physically down at this moment
  uint8_t chord = 0;  // every dot pressed since the current chord began
  bool chordShift = false;  // shift held at any point during that chord
  uint8_t arrows = 0;       // cursor keys physically down at this moment
  uint8_t arrowChord = 0;   // every cursor key pressed since the first went down
  ULONGLONG arrowStamp = 0; // host time of the last cursor key event
  uint8_t mods = 0;         // modifiers the machine is being told are held
};

uint8_t SpecialKey(const HostKeyEvent& key) {
  const bool shift = (key.modifiers & SHIFT_PRESSED) != 0;
  const bool alt = (key.modifiers & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
  uint8_t code = 0;
  // Stops at F10, and that is the machine and not KB.H saying so.  The ROM
  // does know CAh and CBh -- 1DF05 maps scan codes 57h and 58h to them, which
  // is where the Eureka's Alt+F11 "data ROM" comes from -- but only from the
  // PC keyboard.  This function serves the built-in twenty keys, which carry
  // eight function keys on row 1 and make F9 and F10 out of space-bar chords
  // (1D541); there is no eleventh key to press.  Claiming otherwise here
  // would only hand PressMembraneKey a code it drops in silence.
  if (key.virtualKey >= VK_F1 && key.virtualKey <= VK_F10)
    code = static_cast<uint8_t>(0xc0 + key.virtualKey - VK_F1);
  else {
    switch (key.virtualKey) {
      case VK_UP: code = 0x81; break;
      case VK_DOWN: code = 0x82; break;
      case VK_LEFT: code = 0x84; break;
      case VK_RIGHT: code = 0x88; break;
      case VK_HOME: code = 0x85; break;
      case VK_END: code = 0x86; break;
      case VK_PRIOR: code = 0x89; break;
      case VK_NEXT: code = 0x8a; break;
      // KB.H and KB.LIB disagree about these two.  The ROM settles it: its
      // own table for the PC keyboard maps scan code 52h, Insert, to 8Dh and
      // 53h, Delete, to 8Eh (DF05), which is what KB.LIB says.
      case VK_INSERT: code = 0x8d; break;
      case VK_DELETE: code = 0x8e; break;
      default: return 0;
    }
  }
  if (shift) code |= 0x10;
  if (alt) code |= 0x20;
  return code;
}

// The row bits run in Perkins key order, left to right, not in dot number
// order: bit 0 is dot 3 and bit 2 is dot 1.  Under the hands that is exactly
// the natural layout, F D S going outwards on the left and J K L on the right.
uint8_t BrailleBit(WORD virtualKey) {
  switch (virtualKey) {
    case 'F': return 0x04;       // dot 1
    case 'D': return 0x02;       // dot 2
    case 'S': return 0x01;       // dot 3
    case 'J': return 0x08;       // dot 4
    case 'K': return 0x10;       // dot 5
    case 'L': return 0x20;       // dot 6
    case VK_SPACE: return 0x80;  // the space bar, which is also the ALT key
    default: return 0;
  }
}

// The four cursor keys are one keypad, not four keys: the ROM reads them as a
// bit set on row 8Ch, so several held at once mean a chord of their own.  That
// is why Home is up plus left (85h) and why all four together are a command --
// 8Fh, k_udlr in KB.LIB, the one that switches the Eureka off from the Main
// Menu (CP 8Fh at 18154).
uint8_t ArrowBit(WORD virtualKey) {
  switch (virtualKey) {
    case VK_UP: return 1;
    case VK_DOWN: return 2;
    case VK_LEFT: return 4;
    case VK_RIGHT: return 8;
    default: return 0;
  }
}

// A key-up can go missing: let the window lose focus mid-press and the release
// is delivered to whoever took the focus.  A cursor key left believed down
// would then suppress every later single one -- and on this machine a key that
// does nothing is indistinguishable from a key that never arrived, because
// both are silence.  Windows repeats a held key about thirty times a second,
// so anything not heard from for two seconds is not under a finger; two
// seconds is also well above the longest first-repeat delay Windows offers, so
// a slowly assembled chord is never mistaken for a stale one.
//
// The window now also reports WM_KILLFOCUS, which clears this outright and is
// exact where the timer only guesses; the timer stays as the backstop for a
// release lost some other way.
void ForgetStaleArrows(HostKeyboard& host) {
  const ULONGLONG now = GetTickCount64();
  if (now - host.arrowStamp > 2000) host.arrows = host.arrowChord = 0;
  host.arrowStamp = now;
}

// Which modifiers the ROM is currently being told are held, one bit each so
// the difference against what Windows reports is a single XOR.
enum : uint8_t {
  kModShift = 1,
  kModCtrl = 2,
  kModAlt = 4,    // left Alt
  kModAltGr = 8,  // right Alt, the one that selects the DF98 table
};

// The modifiers are not forwarded as key events at all.  Windows reports the
// whole modifier state on every single event, and that report is the only
// thing here worth trusting: measured 26. 8. 2026, the release of AltGr
// arrives with the extended flag already clear, so a break built from the
// event alone goes out as a plain 38h -- which clears the *left* Alt bit in
// C670h and leaves the right one set for good.  The machine then reads every
// key through the AltGr table until it is reset.
//
// Reconciling against the reported state also survives a key-up lost to a
// focus change, which is the same trap ForgetStaleArrows exists for on the
// cursor keypad.
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
    case kModShift: code = 0x2a; break;
    case kModCtrl: code = 0x1d; break;
    case kModAlt: code = 0x38; break;
    // The right Alt is the one extended key among them, so it needs the E0
    // prefix on the break as much as on the make -- DFD6 lists both 38h and
    // B8h, and only behind an E0 does the decoder ever look there (1DD48).
    case kModAltGr: machine.QueueScanCode(0xe0); code = 0x38; break;
    default: return;
  }
  machine.QueueScanCode(down ? code : static_cast<uint8_t>(code | 0x80));
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
  if (code == 0 || code >= 0x80) return;
  // SyncModifiers owns these, from the state Windows reports rather than from
  // the event, so a make or break built here would fight it.
  if (code == 0x2a || code == 0x36 || code == 0x1d || code == 0x38) return;
  if (key.extended) machine.QueueScanCode(0xe0);
  machine.QueueScanCode(key.down ? code : static_cast<uint8_t>(code | 0x80));
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
  return mode == InputMode::kBraille ? L"braillovská" : L"externá PC";
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

void EmulatorThread::PostFocusLost() { PostType(Command::Type::kFocusLost); }

void EmulatorThread::PostExportDisk(std::wstring folder) {
  Command command;
  command.type = Command::Type::kExportDisk;
  command.path = std::move(folder);
  Post(std::move(command));
}

std::wstring EmulatorThread::TakeDiskError() {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return std::move(diskError_);
}

std::wstring EmulatorThread::TakeExportResult(bool& ok) {
  std::lock_guard<std::mutex> lock(errorMutex_);
  ok = exportOk_;
  return std::move(exportResult_);
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
      // Ctrl is not a key on the braille keyboard, so a release that arrives
      // with it held belongs to a host shortcut whose press the accelerator
      // table ate.  Letting it in would corrupt the chord being built.
      if ((key.modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0 &&
          (key.modifiers & RIGHT_ALT_PRESSED) == 0)
        return;
      // A braille chord is finished by letting go, not by pressing: the dots
      // go down one at a time and only the whole pattern means anything, so
      // it is sent when the last finger comes up.
      if (const uint8_t dot = BrailleBit(key.virtualKey)) {
        host.held &= static_cast<uint8_t>(~dot);
        // Shift belongs to the chord and is collected the same way the dots
        // are, on every event of it: let go of shift before the last dot comes
        // up and this final event no longer carries it.
        if ((key.modifiers & SHIFT_PRESSED) != 0) host.chordShift = true;
        if (host.held == 0 && host.chord != 0) {
          machine.PressBraille(host.chord, host.chordShift);
          host.chord = 0;
          host.chordShift = false;
        }
        return;
      }
      if (const uint8_t arrow = ArrowBit(key.virtualKey)) {
        ForgetStaleArrows(host);
        host.arrows &= static_cast<uint8_t>(~arrow);
        if (host.arrows != 0) return;  // fingers still on the keypad
        const uint8_t chord = host.arrowChord;
        host.arrowChord = 0;
        // chord == 0 means ForgetStaleArrows just wiped the set, so this is a
        // lone key however it got here.
        if (chord == arrow || chord == 0) {
          machine.ReleaseKey(SpecialKey(key));
        } else {
          // More than one cursor key was down: send the union, keeping only
          // the modifier bits of the code the single keys would have used.
          machine.QueueKey(
              static_cast<uint8_t>(0x80 | chord | (SpecialKey(key) & 0x30)));
        }
        return;
      }
      // Only the twenty-key keyboard has a released state worth reporting;
      // text goes into a queue and has nothing to let go of.
      if (const uint8_t special = SpecialKey(key)) machine.ReleaseKey(special);
      return;
    }

    // The emulator's own shortcuts are gone from here: they are entries in the
    // accelerator table now, so TranslateAccelerator eats those presses before
    // the window ever sees them.  One owner, one place to look.
    const uint8_t wanted = WantedModifiers(key.modifiers);
    const bool ctrl = (wanted & kModCtrl) != 0;
    const bool shift = (wanted & kModShift) != 0;
    if (trace) TraceKey(key, host.mode, L"do Eureky");

    if (host.mode == InputMode::kPc) {
      SyncModifiers(machine, host, wanted);
      SendScanCode(machine, key);
      return;
    }
    // Auto-repeat resends key-down without a key-up, so a dot already in the
    // chord must not count as a second finger.  Windows says so outright in
    // lParam bit 30, which the console could not report at all; the old code
    // had to infer it from the dot already being held.
    // Ctrl is not a key on this keyboard, so Ctrl+letter is not a dot either.
    if (const uint8_t dot = ctrl ? 0 : BrailleBit(key.virtualKey)) {
      if (key.autoRepeat) return;
      host.held |= dot;
      host.chord |= dot;
      if (shift) host.chordShift = true;
      return;
    }
    if (const uint8_t arrow = ArrowBit(key.virtualKey)) {
      ForgetStaleArrows(host);
      host.arrows |= arrow;
      host.arrowChord |= arrow;
      // One cursor key on its own goes down straight away, so navigation stays
      // immediate and the ROM's own typematic keeps repeating it.  A second
      // finger landing on top of it makes a chord, and a chord only means
      // something whole, so it waits for the last finger to come up -- the way
      // a braille chord does.  The single key that already went is not a bug:
      // fingers landing more than one scan apart look exactly like that to the
      // real machine too.
      if (host.arrows == arrow) machine.QueueKey(SpecialKey(key));
      return;
    }
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
        // survives; a chord caught half-pressed does not.
        host.held = host.chord = host.arrows = host.arrowChord = 0;
        host.chordShift = false;
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
        host.held = host.chord = host.arrows = host.arrowChord = 0;
        host.chordShift = false;
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
        host::Print(diagnostics_.load(std::memory_order_relaxed)
                        ? machine.diagnostics().Report()
                        : std::wstring(L"\r\nDiagnostika je vypnutá; zapnite "
                                       L"ju v Nastaveniach alebo cez "
                                       L"--diag.\r\n"));
        break;
      case Command::Type::kPowerOff:
        // Sent as a key rather than faked, so the firmware does its own
        // farewell.  The chord is built in machine.h, where the integration
        // test's power mode presses it too -- this used to be spelled out here
        // and the release that followed it silently disarmed the whole thing.
        machine.PressPowerOffChord();
        break;
      case Command::Type::kFocusLost:
        // Exact where ForgetStaleArrows only guesses: the releases for these
        // are about to be delivered to whoever took the focus.
        if (host.mode == InputMode::kPc) SyncModifiers(machine, host, 0);
        host.held = host.chord = host.arrows = host.arrowChord = 0;
        host.chordShift = false;
        break;
      case Command::Type::kExportDisk: {
        // Flushed first, so what lands in the folder is the image the guest
        // has actually finished writing rather than whatever was in it when
        // the menu was opened.
        std::wstring exportError;
        const bool ok = machine.FlushDisk(exportError) &&
                        machine.ExportDisk(command.path, exportError);
        {
          std::lock_guard<std::mutex> lock(errorMutex_);
          exportOk_ = ok;
          exportResult_ = ok ? command.path : exportError;
        }
        if (notify) PostMessageW(notify, WM_EMU_EXPORT_DONE, 0, 0);
        break;
      }
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
    // wait for it to play out before leaving the loop.  Two seconds is a
    // ceiling for a device that stops reporting progress, not a pause.
    if (machine.powered_off()) {
      const auto until = Clock::now() + std::chrono::seconds(2);
      while (audio.Ready() && audio.QueuedMs() > 1.0 && Clock::now() < until)
        timer.Wait(5.0);
      running = false;
      if (notify) PostMessageW(notify, WM_EMU_POWERED_OFF, 0, 0);
      break;
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
    timer.Wait(2.0);
  }

  audio.Close();
  running_.store(false, std::memory_order_relaxed);
}
