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
// Audio stays in the machine until TakeAudio -- pulling it every instruction
// would allocate on every sample for nothing, since no wait looks at it.
//
// A step that fails throws Failure.  A forgotten `if` on a return value lets a
// test carry on quietly; an exception cannot be overlooked.  The Try* forms
// return false instead, for the probe, which reports a missed wait and goes on.

#ifndef EUREKA_SESSION_H
#define EUREKA_SESSION_H

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "disk_stash.h"
#include "eureka_keys.h"
#include "machine.h"

namespace eureka {

using namespace std::chrono_literals;

// Kamenicky speech folded to plain ASCII, one byte per byte.  Kept exactly as
// the probe has always had it, table fault and all (eureka.md, "Reč sa
// vypisuje s diakritikou"): step 1 must not change a byte of the probe's
// output, and step 1b replaces it.
std::string Readable(const std::vector<uint8_t>& bytes);

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

// What a wait for the machine to be idle listens to.  The probe's "." has
// always watched the console alone, and keeps doing so; the speech trails the
// work by seconds, though, so a wait that ignores it can end before the
// machine has finished talking (eureka.md: after F2, "." ends between the hour
// and the minutes).
enum class Quiet { kConsole, kConsoleAndSpeech };

class Failure : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
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

  // Until nothing new has appeared for `window`.  The first three seconds
  // after a reset do not count as quiet: the machine is silent while it boots.
  // A switched-off machine is asked once whether its alarm wakes it.
  bool TryWaitIdle(Budget budget, Quiet quiet = Quiet::kConsoleAndSpeech,
                   std::chrono::microseconds window = 500ms);
  void WaitIdle(Budget budget = 10s, Quiet quiet = Quiet::kConsoleAndSpeech,
                std::chrono::microseconds window = 500ms);
  // As TryWaitIdle, and the loudspeaker has not moved either: the DAC's value,
  // not its writes, which the tone generator makes on every interrupt even in
  // a silence.
  bool TryWaitSilent(Budget budget, std::chrono::microseconds window = 500ms);
  void WaitSilent(Budget budget = 10s, std::chrono::microseconds window = 500ms);
  // Until the speech not yet taken contains `text` (compared on Readable).
  bool TryWaitSaid(std::string_view text, Budget budget);
  void WaitSaid(std::string_view text, Budget budget = 10s);
  // Until the console output not yet taken contains `text`.  The cycles spent
  // in the program itself -- RAM below the common area -- are added to
  // *programCycles, which is what made CHESS.COM comparable with a stopwatch
  // on the real machine (HANDOFF 6.46).
  bool TryWaitConsole(std::string_view text, Budget budget,
                      uint64_t* programCycles = nullptr);
  void WaitConsole(std::string_view text, Budget budget = 10s);

  // --- Output ------------------------------------------------------------

  std::vector<uint8_t> TakeSpeech();
  std::vector<uint8_t> TakeConsole();
  std::vector<int16_t> TakeAudio() { return machine_.TakeAudio(); }

 private:
  // Pulls what the last instruction produced into the buffers below.
  void Absorb();
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
  membrane::Keys held_;
  DiskStash stash_;
  int currentSlot_ = 0;  // 0: the diskette in the drive belongs to no slot
  std::chrono::seconds clockShift_{0};
};

}  // namespace eureka

#endif
