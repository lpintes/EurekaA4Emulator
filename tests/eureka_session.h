// EurekaSession: the machine as a test drives it -- "press, wait, what did it
// say" -- written once, for the probe and the tests alike (eureka.md).
//
// Before it the same waiting was written twice, in diag_probe's token loop and
// all over integration_test, a little differently each time, and nothing kept
// the two from drifting apart.  Waiting is where that matters: a key sent only
// to pass the time is an answer to a question the machine has not asked yet.
//
// Every instruction runs through Step() here, so nothing can take the speech
// or console output past the session.  The buffers below mirror the machine's
// own: they are drained by the Take* calls and nothing else, and cleared where
// the machine clears its own (PowerOn, and so Reset and a wake on the alarm).
// Audio stays in the machine until TakeAudio or a recording asks for it --
// pulling it every instruction would allocate on every sample for nothing,
// since no wait looks at it.
//
// A step that fails throws Failure.  A forgotten `if` on a return value lets a
// test carry on quietly; an exception cannot be overlooked.  The Try* forms
// return false instead, for the probe, which reports a missed wait and goes on.

#ifndef EUREKA_SESSION_H
#define EUREKA_SESSION_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "disk_stash.h"
#include "eureka_keys.h"
#include "machine.h"

namespace eureka {

using namespace std::chrono_literals;

// Kamenicky speech or console output as UTF-8 text, diacritics and all, for
// printing.  Control characters become "." so that one line of output stays
// one line.  The fold to plain ASCII it replaces had a table one character
// too long and shifted -- "Přeformátovat" came out as "Pseformatovat"
// (eureka.md, "Reč sa vypisuje s diakritikou").
std::string Readable(const std::vector<uint8_t>& bytes);
// Whether `bytes` contain `text` (UTF-8), with the diacritics taken off both
// sides first: "?vlož" and "?vloz" both find "vlož", and the commands written
// down before the speech was decoded go on working.
bool Contains(const std::vector<uint8_t>& bytes, std::string_view text);

// How long a step may run.  Emulated time for tests; a count of instructions
// only because that is what the probe's command line has always taken, and
// the commands in HANDOFF.md are written that way.
class Budget {
 public:
  template <class Rep, class Period>
  Budget(std::chrono::duration<Rep, Period> time)
      : instructions_(false),
        amount_(CyclesOf(std::chrono::duration_cast<std::chrono::microseconds>(time))) {}
  static Budget Instructions(uint64_t count) { return Budget(true, count); }

  bool counts_instructions() const { return instructions_; }
  uint64_t amount() const { return amount_; }

  // A millisecond is exactly 6144 cycles at kCpuHz.
  static uint64_t CyclesOf(std::chrono::microseconds time) {
    return static_cast<uint64_t>(time.count()) * EurekaMachine::kCpuHz / 1'000'000;
  }

 private:
  Budget(bool instructions, uint64_t amount)
      : instructions_(instructions), amount_(amount) {}
  bool instructions_;
  uint64_t amount_;
};

// How a wait decides that the machine is idle.
//
// kKeyPrompt asks the firmware: the machine is waiting for a key when the ROM
// keeps checking for one (WaitingForKey).  Silence could not tell that: after
// Escape out of the clock the machine says "ahoj", is silent for more than
// half a second in a delay loop, and only then plays the Main Menu's tones --
// a key sent in that gap is lost (eureka.md).
//
// kConsole is the probe's "." as it always was: half a second without new
// console output.  The speech trails it, so it can end before the machine
// has finished talking (after F2, between the hour and the minutes); kept
// because the probe's output must not change under the commands written
// down in HANDOFF.md.
enum class Quiet { kConsole, kKeyPrompt };

class Failure : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// What the machine said, showed and played between two points of a scenario.
struct Recording {
  std::vector<uint8_t> speech;   // Kamenicky, as the synthesiser got it
  std::vector<uint8_t> console;  // Kamenicky
  std::vector<int16_t> audio;    // kAudioHz, mono
  std::string Said() const { return Readable(speech); }
  bool Heard(std::string_view text) const { return Contains(speech, text); }
  bool Shown(std::string_view text) const { return Contains(console, text); }
};

// Where a recording starts: positions in the session's transcript.
struct RecordingMark {
  std::size_t speech;
  std::size_t console;
  std::size_t audio;
};

class Session {
 public:
  explicit Session(EurekaMachine& machine) : machine_(machine) {}

  EurekaMachine& machine() { return machine_; }

  // One instruction.  False once the machine has switched itself off.
  bool Step();

  // The power switch.  All three clear the machine's output buffers, and these
  // clear the session's copy of them the same way.
  void Reset();
  void PowerOn();
  bool WakeOnAlarm();
  // The off switch from the Main Menu, the chord and not a faked strobe: the
  // firmware says "konec" and writes C45Ah itself on the way out, which is
  // the state a warm start has to come up from.  Runs until the machine has
  // cut its own power; false if the budget ran out first.
  bool TryPowerOff(Budget budget);

  // --- Diskettes ---------------------------------------------------------
  //
  // A swap is two halves, the way the emulator's worker does it: first wait
  // for the drive to be swappable and flush (TrySettleForSwap), then change
  // the medium.  Mid-sector is the one moment a swap tears the image, and a
  // session that ignored it would be measuring a machine no user can
  // produce.  The medium calls below do not wait themselves, so that the
  // probe can report a drive that never settled and still carry on.

  bool TrySettleForSwap(Budget budget);
  bool Mount(const std::wstring& folder, std::wstring& error) {
    return machine_.MountDisk(folder, error);
  }
  void InsertBlank(bool formatted) { machine_.CreateEmptyDisk(formatted); }
  void Eject() { machine_.EjectDisk(); }
  void Protect(bool on) { machine_.SetDiskWriteProtected(on); }
  // The quick choice, stash and all: the diskette in the drive goes back to
  // the slot it came from, and the one kept in `slot` comes out -- the same
  // one, not a fresh copy, which is what the bulk copy turns on (HANDOFF
  // 6.24).  False if `slot` holds nothing yet; the drive is left as it was,
  // and what goes in instead is the caller's business.
  bool InsertFromSlot(int slot);
  int current_slot() const { return currentSlot_; }

  // --- Clock and sliders -------------------------------------------------

  // Moves the clock the RTC answers with, not the host's.  Shifts add up:
  // +7 days and then +1 hour lands a week and an hour on.  True if the move
  // woke a switched-off machine on its alarm -- a jump straight into the
  // alarm's minute wakes it the same tick the strobe would on the hardware.
  bool ShiftClock(std::chrono::seconds by);
  std::chrono::seconds clock_shift() const { return clockShift_; }
  // By the positions the window's sliders use (sliders.h).  The firmware looks
  // at the rate pot only while it speaks (001BB), so moving it in a silence
  // changes nothing until the next word.
  void SetRate(int position);
  void SetVolume(int position);

  // --- Input -------------------------------------------------------------

  void Press(keys::Key key) { machine_.QueueKey(key.code); }
  // Make and break, through the ROM's own delivery routine (1DDB0).
  void Pc(pc::Key key);
  // Text on the PC keyboard.  Throws if a character has no key in the ROM's
  // tables (DF05, DF5E, DF98); nothing is typed then, not even the rest.
  void Type(std::string_view text);
  bool TryType(std::string_view text, uint8_t* unmapped = nullptr);
  // Membrane keys held down, one or several at a time.  Unlike a chord these
  // stay down until released, so a partial chord can be said (ea4-91z).
  void Hold(membrane::Keys keys);
  void Release(membrane::Keys keys);
  void ReleaseAll();

  // --- Waiting -----------------------------------------------------------
  //
  // WaitIdle waits for the machine to finish talking, and some answers are
  // long: F11 reads out all five ROMs with their dates and outlasts the
  // default ten seconds (measured 24. 9. 2026).  Give such a step a bigger
  // budget, or wait for the one sentence it is about with WaitSaid.

  // Just runs for `budget`.  False if the machine switched itself off first.
  bool Run(Budget budget);

  // Until the machine is idle as `quiet` says.  A switched-off machine is
  // asked once whether its alarm wakes it, and counts as idle.
  bool TryWaitIdle(Budget budget, Quiet quiet = Quiet::kKeyPrompt);
  void WaitIdle(Budget budget = 10s, Quiet quiet = Quiet::kKeyPrompt);
  // As TryWaitIdle, and the loudspeaker has not moved either: the DAC's value,
  // not its writes, which the tone generator makes on every interrupt even in
  // a silence.
  bool TryWaitSilent(Budget budget, std::chrono::microseconds window = 500ms);
  void WaitSilent(Budget budget = 10s, std::chrono::microseconds window = 500ms);
  // Until the speech not yet taken contains `text` (compared by Contains).
  bool TryWaitSaid(std::string_view text, Budget budget);
  void WaitSaid(std::string_view text, Budget budget = 10s);
  // Until the console output not yet taken contains `text`.  The cycles spent
  // in the program itself -- RAM below the common area -- are added to
  // *programCycles, which is what made CHESS.COM comparable with a stopwatch
  // on the real machine (HANDOFF 6.46).
  bool TryWaitConsole(std::string_view text, Budget budget,
                      uint64_t* programCycles = nullptr);
  void WaitConsole(std::string_view text, Budget budget = 10s);

  // Until `done` returns true; asked before every instruction.  For a
  // firmware variable that marks the moment, e.g. the speech flag:
  //   eureka.WaitUntil([&] { return eureka.Peek(0xC621) != 0xFF; }, 5s);
  bool TryWaitUntil(const std::function<bool()>& done, Budget budget);
  void WaitUntil(const std::function<bool()>& done, Budget budget = 10s,
                 std::string_view what = "WaitUntil");

  // --- Memory ------------------------------------------------------------
  //
  // Addresses are logical, as the CPU sees them under the MMU at that moment
  // (debug_peek), which is how the firmware's own variables are named.

  uint8_t Peek(uint16_t address) const { return machine_.debug_peek(address); }
  // Whether the synthesiser is in the middle of saying something (C621h).
  bool Speaking() const;
  // Whether the machine is waiting for a key: for the last 20 ms the ROM has
  // been checking for one without a break, and none is pending.  Measured
  // 24. 9. 2026 (eureka.md, "Čakanie na kláves").
  bool WaitingForKey() const;
  // Calls `changed(before, now)` after every instruction that left a different
  // value at `address`.  Checked between instructions, so a value one
  // instruction writes and the next one puts back goes unseen -- the
  // firmware's variables do not do that; catching every write would take a
  // hook in WriteMemory, which the GUI runs on every store (eureka.md).
  // Returns a handle for Unwatch.
  int Watch(uint16_t address, std::function<void(uint8_t, uint8_t)> changed);
  void Unwatch(int handle);

  // --- Output ------------------------------------------------------------
  //
  // Two views of the same output.  The Take* calls hand over what has
  // arrived since the last Take and are what the probe prints from.  The
  // transcript keeps everything from the session's start, across resets,
  // and a recording is a stretch of it; the two do not disturb each other.

  std::vector<uint8_t> TakeSpeech();
  std::vector<uint8_t> TakeConsole();
  std::vector<int16_t> TakeAudio();

  RecordingMark StartRecording();
  Recording StopRecording(const RecordingMark& mark);
  // Runs `body` and returns what was said, shown and played meanwhile.
  template <class Body>
  Recording Record(Body&& body) {
    const RecordingMark mark = StartRecording();
    try {
      body();
    } catch (...) {
      StopRecording(mark);
      throw;
    }
    return StopRecording(mark);
  }
  // Everything said since the session started, readable.
  std::string Transcript() const { return Readable(speechLog_); }

 private:
  // Pulls what the last instruction produced into the buffers below.
  void Absorb();
  // Audio is not pulled every instruction -- that would allocate on every
  // sample, and no wait looks at it.  Only here, when someone asks for it.
  void AbsorbAudio();
  void CheckWatches();
  void ClearOutput();
  // Where a budget started, and whether it has run out.
  struct Deadline {
    bool instructions;
    uint64_t limit;
  };
  Deadline Start(Budget budget) const;
  bool Expired(const Deadline& deadline) const;
  [[noreturn]] void Fail(const std::string& what, uint64_t sinceCycles);

  EurekaMachine& machine_;
  std::vector<uint8_t> speech_;
  std::vector<uint8_t> console_;
  // Everything ever absorbed, for the waits: they notice what arrives without
  // taking it away from whoever prints it.
  uint64_t speechTotal_ = 0;
  uint64_t consoleTotal_ = 0;
  // The transcript: never cleared, so a recording's marks stay valid.
  std::vector<uint8_t> speechLog_;
  std::vector<uint8_t> consoleLog_;
  // Audio pulled from the machine and not yet taken; cleared with the
  // machine's own on a power-up.
  std::vector<int16_t> audio_;
  // Audio kept for recordings, and only while one is running -- 48000
  // samples a second is too much to keep for a whole run (eureka.md).
  // recordAudioBase_ is the count of samples dropped before it, so marks
  // are absolute and nested recordings work.
  std::vector<int16_t> recordAudio_;
  std::size_t recordAudioBase_ = 0;
  int activeRecordings_ = 0;
  struct Watcher {
    int handle;
    uint16_t address;
    uint8_t last;
    std::function<void(uint8_t, uint8_t)> changed;
  };
  std::vector<Watcher> watches_;
  int nextWatch_ = 1;
  membrane::Keys held_;
  DiskStash stash_;
  int currentSlot_ = 0;  // 0: the diskette in the drive belongs to no slot
  // The ROM's key check: when it last ran, and since when it has been running
  // without a break.  Cleared by a power-up.
  bool keyChecked_ = false;
  uint64_t keyCheckLast_ = 0;
  uint64_t keyCheckSince_ = 0;
  std::chrono::seconds clockShift_{0};
};

}  // namespace eureka

#endif
