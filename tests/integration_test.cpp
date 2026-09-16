#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "eureka_io.h"
#include "machine.h"
#include "md5.h"

namespace {

namespace fs = std::filesystem;

bool Contains(const std::vector<uint8_t>& data, const std::string& needle) {
  return std::search(data.begin(), data.end(), needle.begin(), needle.end()) !=
         data.end();
}

// Boots the machine once and hands back a copy of that state.  Every check
// that starts from a plain boot re-enters it instead of booting again, which
// is 8M instructions each.  CheckPcKeyboard is the exception and has to keep
// its own Reset -- see there.
std::unique_ptr<EurekaMachine> BootedSnapshot(EurekaMachine& machine) {
  machine.Reset();
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step()) break;
  auto snapshot = std::make_unique<EurekaMachine>();
  snapshot->CopyStateFrom(machine);
  return snapshot;
}

// Every key code the host keyboard can produce, pressed one at a time and
// checked against what the ROM made of it.  Nothing on the machine's ports
// carries a key code: it scans a twenty-key braille keyboard and works the
// code out at D4B0, so a row read from the wrong port or a press too short for
// the 75 Hz scan turns a cursor key into a braille letter with no error
// anywhere.  C638 is where the decoder leaves its answer (1D513).
bool CheckKeyboard(EurekaMachine& machine, const EurekaMachine& booted) {
  static const uint8_t kCodes[] = {
      0x81, 0x82, 0x84, 0x88,  // the four cursor keys
      0x85, 0x86, 0x89, 0x8a,  // home, end, page up, page down
      0x8b, 0x8c,              // insert, delete
      0x91, 0x92, 0x94, 0x98,  // the same with shift
      0xa1, 0xa2, 0xa4, 0xa8,  // with ALT, which is the space bar
      0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
      0xc8, 0xc9,              // F9 and F10, chords of space and braille dots
      0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
  };
  // Each key has to be pressed on a machine nothing else has touched, and
  // that is not caution for its own sake: these keys act.  Before the cursor
  // keys moved to port 8Ch they decoded as braille dots and the application
  // did what the dot meant -- left arrow was dot 3, which in the disk
  // directory is "leave the directory" (06d62c1).  One wrongly decoded key
  // therefore carried the machine somewhere the next key could not be decoded
  // at all, and a single fault came back as thirty-eight.  The isolation is
  // what keeps a regression readable.
  //
  // Getting it by rebooting cost 8M instructions per key, about 300M in all
  // and two thirds of the whole kbd run.  Re-entering one booted state does
  // the same thing: measured, all 38 keys decode identically either way.
  bool ok = true;
  for (uint8_t code : kCodes) {
    machine.CopyStateFrom(booted);
    machine.QueueKey(code);
    uint8_t seen = 0;
    for (int step = 0; step < 6'000'000 && seen != code; ++step) {
      if (!machine.Step() && machine.queued_keys() == 0) break;
      seen = machine.debug_peek(0xc638);
    }
    if (seen == code) continue;
    std::cout << "  klaves " << std::hex << static_cast<unsigned>(code)
              << " dekodovany ako " << static_cast<unsigned>(seen) << std::dec
              << "\n";
    ok = false;
  }
  return ok;
}

// Types a word on the braille dot keys and has the machine read it back.  The
// emulator sends only the six bits; the ROM does the translation (1D79C
// indexes table D7A0 with the row byte), so this checks the one thing the host
// can get wrong: the bit order, which runs in key order and not in dot number
// order.  Reading the line back afterwards is what proves the letters arrived
// as letters and in the right sequence.
//
// The first chord is shifted, which is why the word comes back capitalised.
// Shift is the keyboard's twentieth key and sits on row 8Ch, not beside the
// chord: the decoder reads it at 1D60C and sends the letter through D72D.  A
// host that presses only the dot row can never make a capital letter at all.
bool CheckBraille(EurekaMachine& machine, const EurekaMachine& booted) {
  static const uint8_t kAhoj[] = {
      0x04,  // dot 1       -> a
      0x16,  // dots 1,2,5  -> h
      0x15,  // dots 1,3,5  -> o
      0x1a,  // dots 2,4,5  -> j
  };
  machine.CopyStateFrom(booted);
  machine.QueueKey(0xd0);  // Shift+F1, the word processor
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  bool shifted = true;  // only the first chord, so the word comes back "Ahoj"
  for (uint8_t dots : kAhoj) {
    machine.PressBraille(dots, shifted);
    shifted = false;
    for (int step = 0; step < 8'000'000; ++step)
      if (!machine.Step() && machine.queued_keys() == 0) break;
  }
  machine.TakeSpeechInput();
  machine.QueueKey(0x85);  // Home, which speaks the line
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  const auto spoken = machine.TakeSpeechInput();
  if (Contains(spoken, "Ahoj")) return true;
  std::cout << "  braillovske akordy precitane ako \"";
  for (uint8_t byte : spoken)
    std::cout << (byte >= 0x20 && byte < 0x7f ? static_cast<char>(byte) : '.');
  std::cout << "\"\n";
  return false;
}

// Runs the machine a fixed budget of steps, enough for one HoldMembrane
// state to clear kHoldStepMs's debounce queue and for the ROM to scan and
// react to it.  Not "until quiet" the way diag_probe waits -- this is a
// model-level test, not a probe session, and a fixed budget keeps it fast.
void RunHeldState(EurekaMachine& machine) {
  for (int step = 0; step < 2'000'000; ++step) machine.Step();
}

// Whether the DAC moved since the last TakeAudio call.  Silence in this
// model is exactly zero, not a low hiss (RenderAudio's own coupling
// capacitor model settles a genuinely idle channel to nothing), so any
// nonzero sample is a real event -- matching what "zvuk" showed by hand
// while this bead's live-matrix layer was being measured (ea4-91z).
bool DacMoved(EurekaMachine& machine) {
  const auto samples = machine.TakeAudio();
  return std::any_of(samples.begin(), samples.end(),
                     [](int16_t sample) { return sample != 0; });
}

// Pins the owner's own two measurements of ea4-91z (HANDOFF, "Braillovská
// klávesnica"), driven through HoldMembrane rather than a finished
// PressBraille chord -- the entire point of the fix is that the ROM sees
// each state on the way up, not only the pattern the fingers end on.
//
// Case 1: hold dot 1 (the owner heard "a"), add dot 2 ("b"), add dot 4 ("f"),
// then let go of all three together.  Each addition has to produce a sound
// -- proof the ROM saw a new state, not proof of which letter, since the
// echo goes through .spchar and never reaches the transcript (HANDOFF
// section 3).
//
// Case 2: the same build-up, then let go of dot 4 alone while 1 and 2 stay
// down.  The matrix returns to a state already seen (dots 1 and 2), so
// nothing new is heard -- silence is as much the point here as sound was in
// case 1, the difference between "the ROM sees a change" and "the ROM sees a
// new dot".  What commits to the text once 1 and 2 also let go, measured
// here rather than assumed: the union of every dot reached during the one
// continuous press, "f" -- the same letter as case 1, not "b", which how
// this was first recalled by hand did not survive being pinned down with an
// instrument.  Released one at a time with a pause between each release in
// diag_probe's own measurement, "f" still came out no matter the order --
// see HANDOFF 6.18.
bool CheckChordBuilding(EurekaMachine& machine, const EurekaMachine& booted) {
  machine.CopyStateFrom(booted);
  machine.QueueKey(0xd0);  // Shift+F1, the word processor
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;

  bool ok = true;
  machine.TakeAudio();
  machine.HoldMembrane(hw::kBkbDot1, 0, 0);
  RunHeldState(machine);
  if (!DacMoved(machine)) {
    std::cout << "  bod 1 samotny nevyvolal ziadny zvuk\n";
    ok = false;
  }
  machine.HoldMembrane(hw::kBkbDot1 | hw::kBkbDot2, 0, 0);
  RunHeldState(machine);
  if (!DacMoved(machine)) {
    std::cout << "  pridanie bodu 2 nevyvolalo ziadny zvuk\n";
    ok = false;
  }
  machine.HoldMembrane(hw::kBkbDot1 | hw::kBkbDot2 | hw::kBkbDot4, 0, 0);
  RunHeldState(machine);
  if (!DacMoved(machine)) {
    std::cout << "  pridanie bodu 4 nevyvolalo ziadny zvuk\n";
    ok = false;
  }
  // Let go of dot 4 alone: back to a state already seen, so this must be
  // quiet.
  machine.HoldMembrane(hw::kBkbDot1 | hw::kBkbDot2, 0, 0);
  RunHeldState(machine);
  if (DacMoved(machine)) {
    std::cout << "  uvolnenie bodu 4 (1 a 2 drzane dalej) nemalo zniet\n";
    ok = false;
  }
  // Let go of the rest together.
  machine.HoldMembrane(0, 0, 0);
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;

  machine.TakeSpeechInput();
  machine.QueueKey(0x85);  // Home, which speaks the line back
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  const auto spoken = machine.TakeSpeechInput();
  if (!Contains(spoken, "f")) {
    std::cout << "  po akorde 1-2-4 s uvolnenym bodom 4 sa nenapisalo \"f\": \"";
    for (uint8_t byte : spoken)
      std::cout << (byte >= 0x20 && byte < 0x7f ? static_cast<char>(byte) : '.');
    std::cout << "\"\n";
    ok = false;
  }
  return ok;
}

// Shift with the bare space bar is Escape, decided at 1D52F: the decoder finds
// no dots on row 89h, looks at row 8Ch bit 6 and emits 1Bh instead of a space.
// It is the only key code on this machine that needs two rows held at once, so
// it is also the sharpest check that the host presses shift as a key and not
// as a flag it keeps to itself.
bool CheckBrailleShiftSpace(EurekaMachine& machine,
                            const EurekaMachine& booted) {
  machine.CopyStateFrom(booted);
  machine.PressBraille(0x80, true);
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  const uint8_t seen = machine.debug_peek(0xc638);
  if (seen == 0x1b) return true;
  std::cout << "  shift plus medzernik dekodovany ako " << std::hex
            << static_cast<unsigned>(seen) << std::dec << ", nie 1B\n";
  return false;
}

// What SpeakUntilDone does with shift while the machine speaks.
enum class ShiftFinger { kNone, kHeld, kTapped };

// Says "where am I" and returns how many steps the utterance lasted, putting a
// finger on shift `after` steps into it.  C621h is FFh only while speech is
// playing -- armed at 002D5, cleared at 0055E -- so it is both the "is it
// speaking" flag and the value a new key press copies into spabrt (C620h) to
// stop it.  With pcKeyboard the question is asked on the PC keyboard, F10 being
// scan code 44h there, so the check covers a machine that is being typed on
// with it rather than on the membrane.
long SpeakUntilDone(EurekaMachine& machine, const EurekaMachine& booted,
                    ShiftFinger finger, long after, bool pcKeyboard = false) {
  machine.CopyStateFrom(booted);
  if (pcKeyboard) {
    machine.QueueScanCode(0x44);
    machine.QueueScanCode(0xc4);
  } else {
    machine.QueueKey(0xc9);  // F10, "where am I"
  }
  bool armed = false;
  for (long step = 0; step < 6'000'000 && !armed; ++step) {
    machine.Step();
    armed = machine.debug_peek(0xc621) == 0xff;
  }
  if (!armed) {
    std::cout << "  rec sa vobec nerozbehla\n";
    return -1;
  }
  for (long step = 0; step < 8'000'000; ++step) {
    machine.Step();
    if (step == after && finger == ShiftFinger::kHeld) machine.HoldShift(true);
    if (step == after && finger == ShiftFinger::kTapped) machine.TapShift();
    if (machine.debug_peek(0xc621) != 0xff) {
      machine.HoldShift(false);
      return step;
    }
  }
  machine.HoldShift(false);
  std::cout << "  rec neskoncila do 8M krokov\n";
  return -1;
}

// Pressing shift on its own stops speech, which is how continuous reading in
// the word processor has always been paused; SYSJUMPS.11 names that use of
// spabrt outright.  Nothing here is a translation of ours: the sample loop at
// 0063D compares the three rows against their shadows on every DAC sample and
// aborts on any bit the shadow lacks, so this only checks that the host holds
// shift as a key.  It cannot be faked by a chord -- a chord's shift arrives
// together with dots the shadow is about to learn anyway.
bool CheckBrailleShiftStopsSpeech(EurekaMachine& machine,
                                  const EurekaMachine& booted) {
  const long full = SpeakUntilDone(machine, booted, ShiftFinger::kNone, 0);
  if (full <= 0) return false;
  const long cut =
      SpeakUntilDone(machine, booted, ShiftFinger::kHeld, full / 4);
  if (cut <= 0) return false;
  // Generous on purpose: the point is that speech stopped early, not where.
  if (cut < full / 2) return true;
  std::cout << "  shift rec nezastavil: cela " << full << " krokov, so shiftom "
            << cut << "\n";
  return false;
}

// The emulator's tap on Ctrl: shift pressed for a moment and let go by itself
// (TapShift, HANDOFF 6.35).  It has to stop speech on both keyboards -- the PC
// keyboard above all, which has no key of its own that does only that -- and
// it has to end.  A shift left down would be silent: nothing would say so
// until the next bare space bar came out as Escape (1D52F) and every letter as
// a capital, and that is what the second half looks at.
bool CheckShiftTap(EurekaMachine& machine, const EurekaMachine& booted) {
  bool ok = true;
  for (const bool pcKeyboard : {false, true}) {
    const long full = SpeakUntilDone(machine, booted, ShiftFinger::kNone, 0,
                                     pcKeyboard);
    if (full <= 0) return false;
    const long cut = SpeakUntilDone(machine, booted, ShiftFinger::kTapped,
                                    full / 4, pcKeyboard);
    if (cut <= 0) return false;
    if (cut >= full / 2) {
      std::cout << "  tuknutie na shift rec nezastavilo"
                << (pcKeyboard ? " (externa klavesnica)" : "") << ": cela "
                << full << " krokov, s tuknutim " << cut << "\n";
      ok = false;
    }
  }
  machine.CopyStateFrom(booted);
  machine.TapShift();
  const uint64_t until = machine.cycles() + EurekaMachine::kCpuHz / 2;
  while (machine.cycles() < until) machine.Step();
  machine.PressBraille(0x80);  // the bare space bar
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  // Only Escape is asked about.  A bare space bar leaves C638h at 00 here, so
  // there is no "right" value to expect -- but a shift still down makes it
  // 1Bh, exactly as CheckBrailleShiftSpace measures, and that is the failure
  // this is for.  Checked by making the tap last two seconds: it rang.
  const uint8_t seen = machine.debug_peek(0xc638);
  if (seen == 0x1b) {
    std::cout << "  medzernik pol sekundy po tuknuti dekodovany ako Escape"
                 " -- shift zostal dole\n";
    ok = false;
  }
  return ok;
}

// Types the same word on the optional IBM PC keyboard.  The emulator sends
// nothing but XT scan codes down the serial port; the ROM's own tables (DF05
// and its shifted and AltGr siblings) decide what letters they are, and its
// own table maps the PC's function keys and arrows onto Eureka key codes.  So
// this checks the wire and the interrupt path -- CNTR, TRDR, vector C18C ->
// CD62 -> DD2E -- not a translation of ours.
bool CheckPcKeyboard(EurekaMachine& machine) {
  // The layout the ROM expects is Czech QWERTZ, so these are the positions of
  // a, h, o, j on it; 47h is Home, which speaks the line back.
  static const uint8_t kAhoj[] = {0x1e, 0x23, 0x18, 0x24};
  // This is the one check that cannot re-enter the shared booted state: its
  // boot is the thing under test.  The two break codes below have to be
  // waiting before the first instruction runs, and by the time a snapshot
  // exists the ROM has long since decided whether a keyboard is there.
  machine.Reset();
  // Two break codes before the machine has run one instruction, which is what
  // really happens: the Enter that launched the emulator and the Escape before
  // it are released into a window that is already listening.  They must not
  // survive the ROM's Reset command -- when they did, the ROM read 9Ch instead
  // of the AAh, decided no keyboard was there and never listened again.
  machine.QueueScanCode(0x9c);
  machine.QueueScanCode(0x81);
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step()) break;
  machine.QueueKey(0xd0);  // Shift+F1, the word processor
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  for (uint8_t code : kAhoj) {
    machine.QueueScanCode(code);
    for (int step = 0; step < 4'000'000; ++step)
      if (!machine.Step() && machine.queued_keys() == 0) break;
    machine.QueueScanCode(static_cast<uint8_t>(code | 0x80));
    for (int step = 0; step < 4'000'000; ++step)
      if (!machine.Step() && machine.queued_keys() == 0) break;
  }
  machine.TakeSpeechInput();
  // Home the way a PC keyboard really sends it: E0 first, because the key is
  // an extended one.  The bare 47h works too and hid a bug for a while -- the
  // second byte of every extended key was being eaten, so letters typed fine
  // and not one arrow ever moved the cursor.
  machine.QueueScanCode(0xe0);
  machine.QueueScanCode(0x47);
  machine.QueueScanCode(0xe0);
  machine.QueueScanCode(0xc7);
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  const auto spoken = machine.TakeSpeechInput();
  if (Contains(spoken, "ahoj")) return true;
  std::cout << "  scan kody precitane ako \"";
  for (uint8_t byte : spoken)
    std::cout << (byte >= 0x20 && byte < 0x7f ? static_cast<char>(byte) : '.');
  std::cout << "\"\n";
  return false;
}

// AltGr, pressed and let go, must leave nothing behind.  The right Alt is the
// one modifier on this keyboard that lives behind an E0 prefix (DFD6 lists 38h
// and B8h, and 1DD48 only looks there when the previous byte was E0), so a
// break sent without that prefix clears the *left* Alt bit in C670h and leaves
// the right one set for good -- after which the machine reads every key
// through the AltGr table.  Windows makes that easy to do by accident: it
// reports ENHANCED_KEY on the AltGr press and not on its release.
//
// Typed here in BASIC, which echoes: AltGr with the "2" key of the Czech
// layout is '@', and the same key on its own is 'ě' (88h in Kamenicky).  So a
// stuck AltGr shows up as a second '@' where the letter should be.
bool CheckAltGr(EurekaMachine& machine, const EurekaMachine& booted) {
  machine.CopyStateFrom(booted);
  machine.QueueKey(0xc5);  // F6, Eureka BASIC
  for (int step = 0; step < 12'000'000; ++step) machine.Step();
  machine.TakeConsoleOutput();

  for (uint8_t code : {0xe0, 0x38, 0x03, 0x83, 0xe0, 0xb8})
    machine.QueueScanCode(code);
  for (int step = 0; step < 12'000'000; ++step) machine.Step();
  for (uint8_t code : {0x03, 0x83}) machine.QueueScanCode(code);
  for (int step = 0; step < 12'000'000; ++step) machine.Step();

  const auto echoed = machine.TakeConsoleOutput();
  if (echoed.size() == 2 && echoed[0] == 0x40 && echoed[1] == 0x88) return true;
  std::cout << "  po AltGr a nasledujucom klavese prislo";
  for (uint8_t byte : echoed)
    std::cout << " " << std::hex << static_cast<unsigned>(byte) << std::dec;
  std::cout << ", cakalo sa 40 88\n";
  return false;
}

// The four cursor keys at once (8Fh, k_udlr) are the Eureka's off switch, and
// the inactivity timeout takes the same road.  Both end at a single instruction
// -- IN A,(B8h) at 1D144 -- so the whole thing is checked here from the outside:
// the machine says "konec", stops executing, and leaves FFh in C45Ah, the
// marker its own boot code reads at 180CB to put itself back to sleep when an
// alarm wakes it -- not, as this said until 6 Sep 2026, to resume instead of
// initialise (HANDOFF 6.15).  The byte checked here is the same either way.
// Without this the strobe can go back to being a no-op and nothing would say
// so: an emulator that ignores it just spins in the two instructions after it,
// with interrupts off, which is silence and looks like any other hang.
// What the machine looked like the moment it switched itself off, for the
// mode's summary line.  The check goes on to switch it back on warm and then
// cold, so asking the machine afterwards would report the last start instead.
struct PowerOffState {
  bool off = false;
  uint8_t marker = 0;
};

bool CheckPowerOff(EurekaMachine& machine, PowerOffState& seen) {
  machine.Reset();
  const uint64_t kQuiet = EurekaMachine::kCpuHz / 2;
  uint64_t lastOut = EurekaMachine::kCpuHz * 3;
  bool pressed = false;
  std::vector<uint8_t> spoken;
  for (uint64_t step = 0; step < 40'000'000; ++step) {
    if (!machine.TakeConsoleOutput().empty()) lastOut = machine.cycles();
    const auto said = machine.TakeSpeechInput();
    spoken.insert(spoken.end(), said.begin(), said.end());
    if (!pressed && machine.cycles() > lastOut + kQuiet) {
      // The same call the emulator's "Vypnúť Eureku" command makes, so this
      // test covers the host's path and not just the machine's.
      machine.PressPowerOffChord();
      pressed = true;
    }
    if (!machine.Step()) break;
  }
  const auto said = machine.TakeSpeechInput();
  spoken.insert(spoken.end(), said.begin(), said.end());

  seen.off = machine.powered_off();
  seen.marker = machine.power_down_marker();

  bool ok = true;
  if (!pressed) {
    std::cout << "  stroj sa neustalil, klaves sa vobec nestlacil\n";
    ok = false;
  }
  if (!Contains(spoken, "konec")) {
    std::cout << "  nepovedal \"konec\"\n";
    ok = false;
  }
  if (!machine.powered_off()) {
    std::cout << "  stroj bezi dalej, pwr_stb sa neozval\n";
    ok = false;
  }
  if (machine.power_down_marker() != 0xff) {
    std::cout << "  C45Ah je " << std::hex
              << static_cast<unsigned>(machine.power_down_marker())
              << " namiesto ff\n" << std::dec;
    ok = false;
  }
  // The counters must be frozen: everything that drives this machine loops on
  // them, so a Step() that still burned a cycle would turn every such loop into
  // a spin instead of an end.
  const uint64_t cycles = machine.cycles();
  for (int step = 0; step < 1000; ++step) machine.Step();
  if (machine.cycles() != cycles) {
    std::cout << "  vypnuty stroj este tika\n";
    ok = false;
  }

  // Runs a start out and hands back everything the machine said on the way.
  const auto boot = [&machine] {
    uint64_t lastOut = machine.cycles() + EurekaMachine::kCpuHz * 3;
    std::vector<uint8_t> said;
    for (uint64_t step = 0; step < 40'000'000; ++step) {
      if (!machine.TakeConsoleOutput().empty()) lastOut = machine.cycles();
      const auto heard = machine.TakeSpeechInput();
      said.insert(said.end(), heard.begin(), heard.end());
      if (machine.cycles() > lastOut + kQuiet) break;
      if (!machine.Step()) break;
    }
    return said;
  };

  // Switching on again, warm.  RAM, the clock chip's eight bytes and the cycle
  // counter survive it, so the boot code finds magic 55AAh still at C45Bh,
  // skips the wipe of the permanent data at 1805E and comes up without saying
  // "inicializace eureky".  That line is the whole observable: it is the one
  // thing the firmware says when the user's state has been thrown away, so a
  // warm start that quietly turned into a cold one would be caught here and
  // nowhere else.  Measured 9 Sep 2026 with diag_probe's vypni/zapni tokens.
  machine.PowerOn();
  if (Contains(boot(), "inicializace")) {
    std::cout << "  teple zapnutie zmazalo RAM, povedalo \"inicializace\"\n";
    ok = false;
  }
  if (machine.powered_off()) {
    std::cout << "  po teplom zapnuti stroj nebezi\n";
    ok = false;
  }
  // The contrast, without which the check above would also pass on a machine
  // that never came up at all.
  machine.Reset();
  if (!Contains(boot(), "inicializace")) {
    std::cout << "  studeny start nepovedal \"inicializace\"\n";
    ok = false;
  }
  return ok;
}

// Defined below with the other typing helpers; CheckSnapshot arms an alarm the
// same way CheckAlarm does, and that code sits further down the file.
void Grind(EurekaMachine& machine, uint64_t instructions);
void TypeScan(EurekaMachine& machine, uint8_t code);
void TypeDigit(EurekaMachine& machine, int value);

// The RAM snapshot at the machine level.  SaveSnapshot writes RAM, the clock
// chip's eight bytes and an MD5 of the ROM to a file; a fresh machine loads it
// and PowerOn() comes up where the first one stopped instead of saying
// "inicializace eureky".  main.cpp only wires these two calls into start-up
// and exit -- the contract they keep is here.  HANDOFF 6.15.
bool CheckSnapshot(EurekaMachine& source, const wchar_t* romPath,
                   const wchar_t* diskPath) {
  bool ok = true;

  // The hash the ROM fingerprint rides on.  A broken MD5 would not fail any
  // round trip -- both sides compute it the same wrong way -- so it is pinned
  // here against RFC 1321 instead.
  const auto hex = [](const std::array<uint8_t, 16>& d) {
    std::string s;
    for (uint8_t b : d) {
      const char* k = "0123456789abcdef";
      s += k[b >> 4];
      s += k[b & 15];
    }
    return s;
  };
  if (hex(Md5("", 0)) != "d41d8cd98f00b204e9800998ecf8427e" ||
      hex(Md5("abc", 3)) != "900150983cd24fb0d6963f7d28e17f72") {
    std::cout << "  md5 nesedi s RFC 1321\n";
    ok = false;
  }

  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec);
  const fs::path file = dir / L"ea4_snimka.bin";
  const fs::path echo = dir / L"ea4_snimka_echo.bin";
  const fs::path badMagic = dir / L"ea4_snimka_zle.bin";
  const fs::path badRom = dir / L"ea4_snimka_rom.bin";
  const fs::path missing = dir / L"ea4_snimka_niet.bin";
  for (const fs::path& p : {file, echo, badMagic, badRom, missing})
    fs::remove(p, ec);

  // Cold boot, then arm an alarm so rtcRam_ carries something: it is the one
  // part of the snapshot that is not in memory_, and only zeros travelling
  // would not prove it moves.  This is the first half of CheckAlarm, stopping
  // before the alarm fires.
  source.Reset();
  Grind(source, 12'000'000);
  source.QueueKey(0xc1);   // F2, clock and calendar
  Grind(source, 8'000'000);
  source.QueueKey(0xd2);   // Shift+F3, "vlož čas buzení"
  Grind(source, 8'000'000);
  const std::time_t target = std::time(nullptr) + 120;
  std::tm local{};
  localtime_s(&local, &target);
  if (local.tm_hour >= 10) TypeDigit(source, local.tm_hour / 10);
  TypeDigit(source, local.tm_hour % 10);
  TypeScan(source, 0x39);  // space between the two numbers
  TypeDigit(source, local.tm_min / 10);
  TypeDigit(source, local.tm_min % 10);
  Grind(source, 8'000'000);
  TypeScan(source, 0x1c);  // Enter
  Grind(source, 20'000'000);

  std::array<uint8_t, 8> armed{};
  for (unsigned i = 0; i < 8; ++i) armed[i] = source.debug_rtc_ram(i);
  if (armed[1] == 0 && armed[2] == 0) {
    std::cout << "  budik sa nenastavil, rtcRam by v snimke bola sama nula\n";
    ok = false;
  }

  std::wstring err;
  if (!source.SaveSnapshot(file, err)) {
    std::wcout << L"  SaveSnapshot zlyhal: " << err << L"\n";
    return false;
  }
  if (!fs::exists(file, ec) ||
      fs::file_size(file, ec) <= EurekaMachine::kRamSnapshotBytes) {
    std::cout << "  snimka nie je vacsia nez samotna RAM\n";
    ok = false;
  }

  // A fresh machine loads it back.  On the heap: EurekaMachine is half a
  // megabyte of RAM array and two of them would overflow the stack.
  auto restoredHolder = std::make_unique<EurekaMachine>();
  EurekaMachine& restored = *restoredHolder;
  if (!restored.LoadRom(romPath, err) || !restored.MountDisk(diskPath, err)) {
    std::wcout << L"  druhy stroj sa nezostavil: " << err << L"\n";
    return false;
  }
  if (restored.LoadSnapshot(file, err) !=
      EurekaMachine::SnapshotResult::kOk) {
    std::wcout << L"  LoadSnapshot nevratil kOk: " << err << L"\n";
    return false;
  }
  for (unsigned i = 0; i < 8; ++i)
    if (restored.debug_rtc_ram(i) != armed[i]) {
      std::cout << "  rtcRam[" << i << "] sa neprenieslo\n";
      ok = false;
    }

  // Every byte the snapshot carries, checked without depending on the MMU
  // state: a second save from the restored machine has to reproduce the file
  // exactly -- same ROM MD5, same clock bytes, same RAM.
  if (!restored.SaveSnapshot(echo, err)) {
    std::wcout << L"  druhy SaveSnapshot zlyhal: " << err << L"\n";
    return false;
  }
  {
    std::ifstream a(file, std::ios::binary);
    std::ifstream b(echo, std::ios::binary);
    const std::vector<char> ba((std::istreambuf_iterator<char>(a)),
                               std::istreambuf_iterator<char>());
    const std::vector<char> bb((std::istreambuf_iterator<char>(b)),
                               std::istreambuf_iterator<char>());
    if (ba != bb) {
      std::cout << "  snimka sa po nacitani a znovuulozeni zmenila\n";
      ok = false;
    }
  }

  // Functional resume: PowerOn on the restored RAM must skip "inicializace",
  // the one thing the firmware says when the user's state is gone.  The
  // contrast is a cold Reset on the same machine, which must say it.
  const auto bootOut = [](EurekaMachine& m) {
    std::vector<uint8_t> said;
    uint64_t lastOut = m.cycles() + EurekaMachine::kCpuHz * 3;
    for (uint64_t step = 0; step < 40'000'000; ++step) {
      m.TakeConsoleOutput();
      const auto heard = m.TakeSpeechInput();
      said.insert(said.end(), heard.begin(), heard.end());
      if (m.cycles() > lastOut + EurekaMachine::kCpuHz / 2) break;
      if (!m.Step()) break;
    }
    return said;
  };
  restored.PowerOn();
  if (Contains(bootOut(restored), "inicializace")) {
    std::cout << "  obnovena RAM aj tak povedala \"inicializace\"\n";
    ok = false;
  }
  if (restored.powered_off()) {
    std::cout << "  obnoveny stroj po PowerOn nebezi\n";
    ok = false;
  }
  restored.Reset();
  if (!Contains(bootOut(restored), "inicializace")) {
    std::cout << "  studeny start druheho stroja nepovedal \"inicializace\"\n";
    ok = false;
  }

  // The three refusals.  A missing file is a first run; a short or
  // wrong-magic file is corruption; a good file whose ROM MD5 has been
  // altered is a snapshot from another firmware.
  auto probeHolder = std::make_unique<EurekaMachine>();
  EurekaMachine& probe = *probeHolder;
  if (!probe.LoadRom(romPath, err)) return false;
  if (probe.LoadSnapshot(missing, err) !=
      EurekaMachine::SnapshotResult::kMissing) {
    std::cout << "  chybajuci subor nevratil kMissing\n";
    ok = false;
  }
  { std::ofstream(badMagic, std::ios::binary) << "toto nie je snimka"; }
  if (probe.LoadSnapshot(badMagic, err) !=
      EurekaMachine::SnapshotResult::kCorrupt) {
    std::cout << "  poskodeny subor nevratil kCorrupt\n";
    ok = false;
  }
  {
    std::ifstream in(file, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    bytes.at(8) ^= 0xff;  // first byte of the ROM MD5 in the header
    std::ofstream(badRom, std::ios::binary)
        .write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  if (probe.LoadSnapshot(badRom, err) !=
      EurekaMachine::SnapshotResult::kRomMismatch) {
    std::cout << "  cudzia ROM nevratila kRomMismatch\n";
    ok = false;
  }

  for (const fs::path& p : {file, echo, badMagic, badRom}) fs::remove(p, ec);
  return ok;
}

// The output has to settle to silence once the machine stops talking, whatever
// the DAC is left holding.  It holds its last written value for as long as
// nothing writes it again, and after an utterance that is wherever the final
// sample happened to land -- so without the coupling capacitor modelled the
// stream carries a standing DC offset, and every interruption of it steps
// between that offset and zero.  Those steps are audible as clicking, which is
// the one kind of bug this machine cannot report: it is all sound.
//
// The check is worth nothing if every application happens to leave the DAC at
// mid scale, so that is reported rather than assumed.
bool CheckSettlesToSilence(EurekaMachine& machine) {
  // The recorder, the calculator, BASIC and "where am I": four different exits
  // from the speech engine, which is what varies the resting value.
  static const uint8_t kApps[] = {0xc0, 0xc2, 0xc5, 0xc9};
  const uint64_t kQuiet = EurekaMachine::kCpuHz / 2;
  bool ok = true;
  bool exercised = false;

  for (uint8_t app : kApps) {
    machine.Reset();
    uint64_t lastOut = EurekaMachine::kCpuHz * 3;
    bool pressed = false;
    for (uint64_t step = 0; step < 40'000'000; ++step) {
      if (!machine.TakeConsoleOutput().empty()) lastOut = machine.cycles();
      if (machine.cycles() > lastOut + kQuiet) {
        if (pressed) break;
        machine.QueueKey(app);
        pressed = true;
        lastOut = machine.cycles();
      }
      if (!machine.Step()) break;
    }
    // Console output stops well before the speech engine does -- the text goes
    // out in one go and is then rendered sample by sample -- so give the
    // announcement time to finish before looking at what is left behind.
    const uint64_t spoken = machine.cycles() + EurekaMachine::kCpuHz * 9;
    while (machine.cycles() < spoken && machine.Step()) {}
    machine.TakeAudio();

    // A second of guest time with nobody speaking: far longer than the filter's
    // 5.3 ms time constant, so anything still there is standing, not decaying.
    const uint64_t until = machine.cycles() + EurekaMachine::kCpuHz;
    while (machine.cycles() < until && machine.Step()) {}
    const auto tail = machine.TakeAudio();
    const uint8_t resting = machine.debug_io(0x88);
    if (resting != 0x80) exercised = true;

    int16_t worst = 0;
    for (std::size_t i = tail.size() / 2; i < tail.size(); ++i)
      if (std::abs(tail[i]) > std::abs(worst)) worst = tail[i];
    // One LSB of the DAC is 256 here, so 64 is a quarter of the smallest step
    // the hardware can make: comfortably below anything audible, and far below
    // the offsets measured without the filter (up to -4352).
    if (std::abs(worst) > 64) {
      std::cout << "  po klavese " << std::hex << static_cast<unsigned>(app)
                << std::dec << " zostava vychylka " << worst << " (DAC "
                << static_cast<unsigned>(resting) << ")\n";
      ok = false;
    }
  }
  if (!exercised) {
    std::cout << "  ziadna aplikacia nenechala DAC mimo stredu, test nic "
                 "neoveril\n";
    ok = false;
  }
  return ok;
}

// The DAC answers on all of 88h-8Fh, not on 88h alone (eureka_io.h,
// DecodedPort).  The EUROU demo writes its samples to 8Ch, and a model that
// ignored that played the whole demo as silence -- which nothing here noticed,
// because the ROM itself only ever writes 88h.
//
// Each alias gets a full-scale step after a second of quiet, and has to be
// heard.  A step on 8Fh with the filter and coupling capacitor in the way is
// still thousands of units, so "any nonzero sample" is not a lenient bar.
bool CheckDacDecodesWholeBlock(EurekaMachine& machine) {
  bool ok = true;
  uint8_t level = 0x80;
  for (uint16_t port = 0x88; port <= 0x8f; ++port) {
    const uint64_t quiet = machine.cycles() + EurekaMachine::kCpuHz;
    while (machine.cycles() < quiet && machine.Step()) {}
    machine.TakeAudio();
    level = level == 0xff ? 0x00 : 0xff;
    machine.debug_out(static_cast<uint16_t>(0x0700 | port), level);
    const uint64_t heard = machine.cycles() + EurekaMachine::kCpuHz / 50;
    while (machine.cycles() < heard && machine.Step()) {}
    if (!DacMoved(machine)) {
      std::cout << "  zapis na port " << std::hex << port << std::dec
                << " nebolo pocut\n";
      ok = false;
    }
  }
  return ok;
}

// The music composer's jingle, and what does and does not silence it.
//
// The player's own stop test is at 10F13 and 10F1C: it reads rows 8Ch and 89h
// straight off the keyboard and takes any bit it finds as "stop".  It never
// looks at the ROM's key queue, so a character handed to that queue -- which
// is what the emulator's default mode and the serial keyboard both do -- is
// invisible to it and the tune plays on.  A key that reaches the rows stops it.
//
// Two runs, because one proves nothing: the same window is measured with the
// space bar pressed on row 89h and with nothing pressed at all.  Without the
// second the check would pass just as happily on a tune that ended by itself.
// A third taps shift, as a tap on Ctrl does in the window: the player stops
// only on a bit its previous read of row 8Ch lacked (10F16, shadow refreshed at
// 10F1B), so a pulse too short to span one of its reads would miss.
bool CheckMusicStops(EurekaMachine& machine) {
  // Measured: the jingle runs about 6.3 s of guest time from the F7 press, so
  // pressing at 1 s and looking at 2.5-3.0 s is well inside it either way.
  const uint64_t kPressAt = EurekaMachine::kCpuHz;
  const uint64_t kWindowFrom = EurekaMachine::kCpuHz * 5 / 2;
  const uint64_t kWindowTo = EurekaMachine::kCpuHz * 3;

  double level[3] = {0, 0, 0};
  for (int run = 0; run < 3; ++run) {
    const bool press = run != 1;
    machine.Reset();
    for (int step = 0; step < 8'000'000; ++step)
      if (!machine.Step()) break;
    machine.TakeAudio();
    machine.QueueKey(0xc6);  // F7, the music composer, which plays on entry
    const uint64_t started = machine.cycles();
    bool pressed = false;
    double sum = 0;
    std::size_t samples = 0;
    while (machine.cycles() < started + kWindowTo) {
      if (press && !pressed && machine.cycles() > started + kPressAt) {
        if (run == 0)
          machine.PressBraille(0x80);  // the space bar alone, row 89h bit 7
        else
          machine.TapShift();  // row 8Ch bit 6, for a moment
        pressed = true;
      }
      const bool measuring = machine.cycles() >= started + kWindowFrom;
      for (int16_t sample : machine.TakeAudio()) {
        if (!measuring) continue;
        sum += static_cast<double>(sample) * sample;
        ++samples;
      }
      machine.TakeSpeechInput();
      machine.TakeConsoleOutput();
      if (!machine.Step() && machine.powered_off()) break;
    }
    level[run] = samples ? std::sqrt(sum / samples) : 0.0;
  }

  bool ok = true;
  // One LSB of the DAC is 256 in these units, so 64 is a quarter of the
  // smallest step the hardware can take; with the space bar it measures 0.
  if (level[0] > 64) {
    std::cout << "  medzernik na riadku 89h hranie nezastavil, RMS "
              << level[0] << "\n";
    ok = false;
  }
  if (level[2] > 64) {
    std::cout << "  tuknutie na shift hranie nezastavilo, RMS " << level[2]
              << "\n";
    ok = false;
  }
  // Measured without the press: around 5000.  A tune that is not playing here
  // would make the check above pass without proving anything.
  if (level[1] < 1000) {
    std::cout << "  znelka v okne nehrala ani bez klavesu, RMS " << level[1]
              << " -- test nic neoveril\n";
    ok = false;
  }
  return ok;
}

// Runs a fixed stretch of instructions, draining what the machine produces so
// the buffers cannot grow without bound.  Pacing on silence, the way the other
// checks here do, does not work in the clock application: it says nothing at
// all, so "quiet" is its normal state and the wait would end before it had
// finished opening a dialogue.
// Defined further down, next to the other typing helpers; needed here because
// formatting asks a question and waits for the answer to be typed.
bool Type(EurekaMachine& machine, const std::string& text);

// Formatting a diskette that has never been formatted.
//
// Until now the format routine only ever ran against a host folder, where it
// is a no-op on purpose -- the diskette is the user's own folder and the
// emulated machine formatting is no reason to delete their files (6.5).  So
// the routine was known to run to the end and say "formatovani dokonceno",
// but nothing it did was ever observable.  An unsaved diskette created unformatted
// is the first medium where it has to work for real: every track answers
// Record Not Found until Write Track has been over it.
//
// Shift+F8 is "formatovat disk" in the main menu.  The shifted function keys
// are D0h..D9h for F1..F10, so it is D7h: D8h is Shift+F9 and answers with the
// battery state, which is how this test first found out it was off by one.
bool CheckFormatsBlankDiskette(EurekaMachine& machine) {
  // Booted with the blank already in the drive, rather than re-entering the
  // shared snapshot: that one was booted with a real diskette, and the
  // firmware carries what it learned about it in RAM.
  machine.CreateEmptyDisk(false);
  machine.Reset();
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step()) break;
  if (machine.disk().TrackFormatted(0, 0) || machine.disk().has_format()) {
    std::cout << "  cerstva nenaformatovana disketa uz je naformatovana\n";
    return false;
  }

  machine.TakeSpeechInput();
  machine.QueueKey(0xd7);
  // It asks first -- "mam formatovat disk, ano nebo ne?" -- and waits.  Not a
  // detail worth skipping past: a format that started on a keystroke alone
  // would be a very expensive misprint.
  //
  // The answer is "y", not "a": the question is Czech but the key is the
  // English one, which is what GETYN.H in the technical manual is named after.
  // Answering "a" is taken as no and the machine says "prikaz zrusen".
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  if (!Type(machine, "y")) return false;
  // And it asks a second time -- "disk je uz naformatovan, preformatovat?" --
  // whatever is in the drive.  Measured: over the whole run the ROM issues
  // three FDC commands, all Type I seeks, and not one BIOS read, so it never
  // looks at the medium at all.  The line is a second confirmation of a
  // destructive act, not a finding about the diskette.
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  if (!Type(machine, "y")) return false;

  // Formatting walks 160 tracks and verifies each one, so this is long: the
  // ceiling is a stop for a machine that has hung, not a measure of the work.
  std::vector<uint8_t> spoken;
  bool finished = false;
  for (int step = 0; step < 120'000'000 && !finished; ++step) {
    const auto said = machine.TakeSpeechInput();
    spoken.insert(spoken.end(), said.begin(), said.end());
    machine.TakeConsoleOutput();
    machine.TakeAudio();
    if (!machine.Step() && machine.powered_off()) break;
    finished = machine.disk().TrackFormatted(79, 1);
  }
  if (!finished) {
    // What the machine said matters more than the bare failure: this routine
    // has its own refusals, and "vadny disk" and silence mean different
    // things.
    std::cout << "  posledna stopa (79/1) sa nenaformatovala, stroj povedal \"";
    for (uint8_t byte : spoken)
      std::cout << (byte >= 0x20 && byte < 0x7f ? static_cast<char>(byte) : '.');
    std::cout << "\"\n";
    return false;
  }

  // Every track, not just the last: a routine that skipped a cylinder in the
  // middle would leave a diskette that works until the day something is
  // written there.
  for (unsigned cylinder = 0; cylinder < 80; ++cylinder) {
    for (unsigned side = 0; side <= 1; ++side) {
      if (machine.disk().TrackFormatted(cylinder, side)) continue;
      std::cout << "  stopa " << cylinder << "/" << side
                << " zostala nenaformatovana\n";
      return false;
    }
  }
  // And the diskette is usable afterwards, through the controller and through
  // the BIOS stub alike.
  uint8_t sector[512]{};
  if (!machine.disk().ReadPhysicalSector(0, 0, 1, sector)) {
    std::cout << "  naformatovana disketa sa neda citat\n";
    return false;
  }
  // The flag the window title hangs on: "nenaformátovaná" has to stop being
  // true the moment the guest has laid a format down, or the title would go
  // on calling a working diskette blank.
  if (!machine.disk().has_format()) {
    std::cout << "  has_format() zostalo false aj po formatovani\n";
    return false;
  }
  return true;
}

// Runs for a fixed stretch and hands back everything the machine said in it.
// Waiting for silence instead would cut a question in half: the ROM emits a
// line in bursts with gaps between them, and half a second of quiet happens
// inside "disk je uz naformatovan, preformatovat" rather than after it.  The
// stretch has to be long enough for a whole question and is then spent idling
// at the prompt, which costs nothing.
std::vector<uint8_t> RunAndListen(EurekaMachine& machine, uint64_t budget) {
  std::vector<uint8_t> spoken;
  const uint64_t deadline = machine.instructions() + budget;
  while (machine.instructions() < deadline) {
    const auto said = machine.TakeSpeechInput();
    spoken.insert(spoken.end(), said.begin(), said.end());
    machine.TakeConsoleOutput();
    machine.TakeAudio();
    if (!machine.Step() && machine.powered_off()) break;
  }
  const auto said = machine.TakeSpeechInput();
  spoken.insert(spoken.end(), said.begin(), said.end());
  return spoken;
}

// The write protect notch.  Bit 6 of the controller status is the whole of the
// model's side of it (DEVICES.10 has it as bit 6 of fdc_ctl_chkdsk), so what
// this check is really about is that the firmware is the one enforcing it:
// nothing here refuses a write on the host's behalf and then claims the ROM
// did.  Measured with diag_probe before it was written -- Shift+F8 on a
// protected diskette says "disk je chranen proti zapisu" (13D5E) instead of
// asking the second question.
//
// Reading has to go on working.  SYSEQU.LIB draws that line itself:
// write_error_mask is 11011110b and includes bit 6, read_error_mask is
// 10011110b and does not.
bool CheckProtectedDiskStillReads(EurekaMachine& machine) {
  machine.SetDiskWriteProtected(true);
  machine.Reset();
  const uint64_t kQuiet = EurekaMachine::kCpuHz / 2;
  std::vector<uint8_t> console;
  uint64_t lastOut = EurekaMachine::kCpuHz * 3;  // nechaj stroj nabehnut
  unsigned fed = 0;
  while (machine.instructions() < 60'000'000) {
    auto chunk = machine.TakeConsoleOutput();
    if (!chunk.empty()) {
      console.insert(console.end(), chunk.begin(), chunk.end());
      lastOut = machine.cycles();
    }
    if (fed < 2 && machine.cycles() > lastOut + kQuiet) {
      lastOut = machine.cycles();
      // Shift+F7 is "spustit program z disku", then the program's name.
      if (fed == 0) machine.QueueKey(0xd6);
      else if (!Type(machine, "READ\r")) return false;
      ++fed;
    }
    if (!machine.Step() && machine.powered_off()) break;
    if (fed >= 2 && machine.cycles() > lastOut + 3 * kQuiet) break;
  }
  if (!Contains(console, "Read which file?")) {
    std::cout << "  READ.COM sa z chranenej diskety nespustil\n";
    return false;
  }
  // Loading a 16K program is what those reads are: a protected diskette that
  // answered the prompt without them would mean the file came from somewhere
  // other than the medium.
  if (machine.debug_bios_reads() < 150) {
    std::cout << "  chranena disketa dala len " << machine.debug_bios_reads()
              << " citani BIOSu\n";
    return false;
  }
  return true;
}

void Say(const char* label, const std::vector<uint8_t>& spoken) {
  std::cout << "  " << label << " \"";
  for (uint8_t byte : spoken)
    std::cout << (byte >= 0x20 && byte < 0x7f ? static_cast<char>(byte) : '.');
  std::cout << "\"\n";
}

// The other half: a write the firmware has to refuse.  Formatting is the
// cleanest one to ask for, because a refusal is observable in the model as
// well -- not one track may be laid down.
//
// The refusal comes at the end of the dialogue, not at the keystroke: the ROM
// asks "mam formatovat disk" and, on a diskette that has a format on it, "disk
// je uz naformatovan, preformatovat" as well, and the notch is answered for
// only once those are.  Both confirmations are therefore sent here, and the
// check is on the refusal arriving somewhere in what follows -- exactly when
// it falls inside that dialogue is the firmware's business, and pinning it
// would make this a test of the probe's key timing rather than of the notch.
bool CheckProtectedDiskRefusesFormat(EurekaMachine& machine) {
  machine.CreateEmptyDisk(false);
  machine.SetDiskWriteProtected(true);
  machine.Reset();
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step()) break;

  machine.TakeSpeechInput();
  machine.QueueKey(0xd7);  // Shift+F8, "formatovat disk"
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  if (!Type(machine, "y")) return false;
  std::vector<uint8_t> spoken = RunAndListen(machine, 12'000'000);
  if (!Type(machine, "y")) return false;
  const auto more = RunAndListen(machine, 12'000'000);
  spoken.insert(spoken.end(), more.begin(), more.end());

  // "chranen proti zapisu" with the diacritics kept out of the way: the speech
  // is Kamenicky, so only plain-ASCII stretches are safe to match on.
  if (!Contains(spoken, "n proti z")) {
    Say("na chranenej diskete stroj nepovedal, ze je chranena, povedal", spoken);
    return false;
  }
  if (machine.disk().has_format()) {
    std::cout << "  chranena disketa sa napriek odmietnutiu naformatovala\n";
    return false;
  }

  // And the lock is a lock, not a wall: taking it off lets the very same
  // keystrokes through.  Only the first track is waited for -- the whole
  // format is the format mode's job, this one is about the notch.
  machine.SetDiskWriteProtected(false);
  machine.TakeSpeechInput();
  machine.QueueKey(0xd7);
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  if (!Type(machine, "y")) return false;
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  if (!Type(machine, "y")) return false;
  for (int step = 0; step < 40'000'000; ++step) {
    machine.TakeSpeechInput();
    machine.TakeConsoleOutput();
    machine.TakeAudio();
    if (!machine.Step() && machine.powered_off()) break;
    if (machine.disk().TrackFormatted(0, 0)) return true;
  }
  std::cout << "  po zruseni ochrany sa nenaformatovala ani prva stopa\n";
  return false;
}

// What the machine says about a drive it cannot read, which is three different
// sentences and used to be one.
//
// "vadny disk" is BDOS speaking (1BB56), and it is the answer to every BIOS
// disk status the firmware does not recognise.  The ones it does recognise are
// 2, 3 and 4 (1BB45), and the ROM has its own questions in front of that: the
// disk functions ask fdc_ctl_disk_in and fdc_ctl_disk_test first, and those
// two tell an empty drive from an unformatted diskette by issuing one Seek
// with verify (14h at 1980A) and reading it three ways.  With the model
// answering every verify with success and every BIOS failure with 1, all of
// that collapsed into "vadny disk" -- the sentence that on a real machine
// means the diskette is damaged.
//
// So this is one check per sentence, plus the one that matters most: none of
// the three may be "vadny disk".
bool CheckSaysWhichDiskProblem(EurekaMachine& machine) {
  // The firmware keeps what it learned about the medium in RAM, so each case
  // gets its own boot with the medium already in the drive.
  struct Case {
    const char* what;      // how the drive is set up
    bool eject;            // empty drive, or an unformatted diskette
    uint8_t key;           // C7h is F8 "adresar disku", D5h is Shift+F6
    const char* expected;  // an ASCII stretch of the Kamenicky speech
    const char* sentence;  // the same, readably, for the failure line
  };
  const Case cases[] = {
      {"prazdna mechanika, adresar", true, 0xc7, "disk nen", "disk neni zalozen"},
      {"prazdna mechanika, diskove funkcie", true, 0xd5, "jednotce nen",
       "v jednotce neni disk"},
      {"nenaformatovana disketa, diskove funkcie", false, 0xd5, "naform",
       "disk neni naformatovan"},
  };

  bool passed = true;
  for (const Case& item : cases) {
    if (item.eject) machine.EjectDisk();
    else machine.CreateEmptyDisk(false);
    machine.Reset();
    for (int step = 0; step < 8'000'000; ++step)
      if (!machine.Step()) break;

    machine.TakeSpeechInput();
    machine.QueueKey(item.key);
    // Generously long: an empty drive is the slow case on purpose.  The
    // firmware finds out the drive is empty by timing the controller out --
    // it polls the status two thousand times (19A16) before it gives up --
    // and that wait is the shape of the answer, not overhead to be trimmed.
    const std::vector<uint8_t> spoken = RunAndListen(machine, 120'000'000);

    if (!Contains(spoken, item.expected)) {
      std::cout << "  " << item.what << ": necakal som \"" << item.sentence
                << "\"\n";
      Say("  stroj povedal", spoken);
      passed = false;
    } else if (Contains(spoken, "vadn")) {
      std::cout << "  " << item.what << ": stroj povedal aj \"vadny disk\","
                << " co znamena poskodenu disketu\n";
      passed = false;
    }
  }
  return passed;
}

void Grind(EurekaMachine& machine, uint64_t instructions) {
  const uint64_t deadline = machine.instructions() + instructions;
  while (machine.instructions() < deadline) {
    machine.TakeSpeechInput();
    machine.TakeConsoleOutput();
    machine.TakeAudio();
    if (!machine.Step() && machine.powered_off()) return;
  }
}

// XT set 1 make codes for the top digit row, 0 to 9.
const uint8_t kDigitScan[10] = {0x0b, 0x02, 0x03, 0x04, 0x05,
                                0x06, 0x07, 0x08, 0x09, 0x0a};

void TypeScan(EurekaMachine& machine, uint8_t code) {
  machine.QueueScanCode(code);
  machine.QueueScanCode(static_cast<uint8_t>(code | 0x80));
}

// The ROM's own keyboard table is Czech QWERTZ (DF05), where the unshifted top
// row is the accented letters -- typing the digits needs shift, and without it
// "10 33" arrives as "+e s s" and the alarm dialogue only beeps.
void TypeDigit(EurekaMachine& machine, int value) {
  machine.QueueScanCode(0x2a);
  TypeScan(machine, kDigitScan[value]);
  machine.QueueScanCode(0xaa);
}

// The clock and calendar application tells the time the moment it opens, in
// two sentences: the hours, and about a million instructions later, once the
// first has been said, the minutes.  Neither goes through .speak -- both are
// spoken from the conversion buffer by .spconv, and while the capture took
// .speak alone this looked exactly like an application that says nothing on
// entry.  Both sentences are required, in order, because the second is the one
// that hangs on timing.  The ROM declines the words ("1 HODINA", "2 HODINY",
// "5 MINUT"), so only the stems are matched.
//
// The clock is put at seven minutes and twenty seconds past the hour: at a
// full hour the minutes sentence is empty (the buffer holds a bare zero), and a
// check on the host's own clock would fail once an hour.
bool CheckAnnouncesTime(EurekaMachine& machine) {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  machine.Reset();
  machine.SetRtcOffset(
      static_cast<int64_t>((7 - local.tm_min) * 60 + (20 - local.tm_sec)));
  Grind(machine, 12'000'000);
  machine.QueueKey(0xc1);  // F2, clock and calendar
  const std::vector<uint8_t> spoken = RunAndListen(machine, 8'000'000);
  // The alarm check that follows types the host's own time.
  machine.SetRtcOffset(0);
  const std::string said(spoken.begin(), spoken.end());
  const std::size_t hours = said.find("HODIN");
  if (hours != std::string::npos &&
      said.find("MINUT", hours) != std::string::npos)
    return true;
  std::cout << "  F2 neohlasil cas, prepis \"";
  for (uint8_t byte : spoken)
    std::cout << (byte >= 0x20 && byte < 0x7f ? static_cast<char>(byte) : '.');
  std::cout << "\"\n";
  return false;
}

// The alarm, end to end: set one in the clock and calendar application and
// then let the clock reach it.
//
// This is the whole of section 6.13.  The RTC raises no interrupt on this
// machine -- its interrupt pin goes to the power switch, not to the CPU -- so
// an alarm is found only by polling rtc_status, which the heartbeat does at
// CF61 on every tick.  While that port answered a hard zero, the poll could
// never see anything and the alarm, the chime and the diary were all dead
// together.  The check would pass with the port still dead only if the ROM
// found the alarm some other way, and it has no other way.
bool CheckAlarm(EurekaMachine& machine) {
  machine.Reset();
  Grind(machine, 12'000'000);
  machine.QueueKey(0xc1);  // F2, clock and calendar
  Grind(machine, 8'000'000);
  machine.QueueKey(0xd2);  // Shift+F3, "vloz cas buzeni"
  Grind(machine, 8'000'000);

  // Two minutes ahead, so typing cannot spill over into the alarm's own minute.
  const std::time_t target = std::time(nullptr) + 120;
  std::tm local{};
  localtime_s(&local, &target);
  if (local.tm_hour >= 10) TypeDigit(machine, local.tm_hour / 10);
  TypeDigit(machine, local.tm_hour % 10);
  // A space, and it has to be one: the skip loop at E40D is CP 30h / RET NC,
  // so only a character below '0' separates the two numbers.  A colon is 3Ah,
  // which the loop hands straight to the digit test that then rejects it.
  TypeScan(machine, 0x39);
  TypeDigit(machine, local.tm_min / 10);
  TypeDigit(machine, local.tm_min % 10);
  Grind(machine, 8'000'000);
  TypeScan(machine, 0x1c);  // Enter
  Grind(machine, 20'000'000);

  std::array<uint8_t, 8> armed{};
  for (unsigned index = 0; index < 8; ++index)
    armed[index] = machine.debug_rtc_ram(index);

  bool ok = true;
  if (armed[1] != local.tm_hour || armed[2] != local.tm_min) {
    std::cout << "  budik sa nenastavil: 191h=" << std::hex
              << static_cast<unsigned>(armed[1]) << " 192h="
              << static_cast<unsigned>(armed[2]) << std::dec << " namiesto "
              << local.tm_hour << " a " << local.tm_min << "\n";
    ok = false;
  }
  // 80h in a field means "do not compare"; the alarm writer at 0DA5B puts it
  // in the hundredths, the seconds and the day of week.
  if ((armed[0] & 0x80) == 0 || (armed[3] & 0x80) == 0) {
    std::cout << "  sekundy alebo stotiny nie su oznacene ako lubovolne\n";
    ok = false;
  }
  if ((machine.debug_rtc_mask() & 0x01) == 0) {
    std::cout << "  rtc_mask nema povoleny budik\n";
    ok = false;
  }
  if (!ok) return false;

  // Ten seconds into the alarm's minute.  Waiting for it in real time would
  // cost two minutes per run; the clock is the host's, so it is moved instead.
  machine.SetRtcOffset(static_cast<int64_t>(target - target % 60 + 10 -
                                            std::time(nullptr)));
  for (int slice = 0; slice < 12; ++slice) {
    Grind(machine, 8'000'000);
    if (machine.debug_rtc_ram(5) != armed[5]) break;
  }
  // Servicing an alarm ends in .schedule_alarm, which arms the next one: for a
  // daily alarm that is the same time tomorrow, so the date register moves.
  // Nothing else in the ROM rewrites these registers on its own.
  std::array<uint8_t, 8> rearmed{};
  for (unsigned index = 0; index < 8; ++index)
    rearmed[index] = machine.debug_rtc_ram(index);
  if (rearmed == armed) {
    std::cout << "  budik nezazvonil: alarmove registre zostali nedotknute\n";
    ok = false;
  }
  return ok;
}

// The other half of the alarm: not a machine that is on and finds the time,
// but one that is off and has to switch itself back on to find it -- the
// whole point of HANDOFF 6.32.
bool CheckAlarmWake(EurekaMachine& machine) {
  machine.Reset();
  Grind(machine, 12'000'000);
  machine.QueueKey(0xc1);  // F2, clock and calendar
  Grind(machine, 8'000'000);
  machine.QueueKey(0xd2);  // Shift+F3, "vloz cas buzeni"
  Grind(machine, 8'000'000);

  // Two minutes ahead, same margin CheckAlarm uses and for the same reason:
  // typing must not spill into the alarm's own minute.
  const std::time_t target = std::time(nullptr) + 120;
  std::tm local{};
  localtime_s(&local, &target);
  if (local.tm_hour >= 10) TypeDigit(machine, local.tm_hour / 10);
  TypeDigit(machine, local.tm_hour % 10);
  TypeScan(machine, 0x39);
  TypeDigit(machine, local.tm_min / 10);
  TypeDigit(machine, local.tm_min % 10);
  Grind(machine, 8'000'000);
  TypeScan(machine, 0x1c);  // Enter
  Grind(machine, 20'000'000);

  std::array<uint8_t, 8> armed{};
  for (unsigned index = 0; index < 8; ++index)
    armed[index] = machine.debug_rtc_ram(index);
  if ((machine.debug_rtc_mask() & 0x01) == 0) {
    std::cout << "  budik sa nenastavil, rtc_mask nema povoleny budik\n";
    return false;
  }

  // Off the Main Menu, the only place 8Fh means anything (HANDOFF, "Vypinanie
  // stroja"): the alarm just armed is still whatever left the clock and
  // calendar application, so this is the same Escape CheckAlarm's own probe
  // measurement needed to reach it.
  machine.QueueScanCode(0x01);
  machine.QueueScanCode(0x81);
  Grind(machine, 8'000'000);
  machine.PressPowerOffChord();
  for (int slice = 0; slice < 20 && !machine.powered_off(); ++slice)
    Grind(machine, 4'000'000);
  if (!machine.powered_off()) {
    std::cout << "  stroj sa nevypol, chord 8Fh nezabral z hlavneho menu\n";
    return false;
  }

  // The alarm registers and the mask must have survived the power-down
  // untouched: PowerDown() does not touch rtcRam_/rtcMask_/rtcCommand_, on
  // the hardware because the clock chip's supply is separate (GLOSSARY.TXT).
  bool ok = true;
  std::array<uint8_t, 8> stillArmed{};
  for (unsigned index = 0; index < 8; ++index)
    stillArmed[index] = machine.debug_rtc_ram(index);
  if (stillArmed != armed) {
    std::cout << "  budik sa zmenil pocas vypnutia\n";
    ok = false;
  }
  if ((machine.debug_rtc_mask() & 0x01) == 0) {
    std::cout << "  rtc_mask nepreziva vypnutie\n";
    ok = false;
  }
  if (!ok) return false;

  // Ten seconds into the alarm's minute, exactly as CheckAlarm does it: the
  // clock is the host's, so reaching that instant is a jump, not a wait.
  machine.SetRtcOffset(static_cast<int64_t>(target - target % 60 + 10 -
                                            std::time(nullptr)));
  if (!machine.WakeOnAlarm()) {
    std::cout << "  WakeOnAlarm() nezobudilo stroj na nastaveny cas\n";
    return false;
  }
  if (machine.powered_off()) {
    std::cout << "  stroj zostal vypnuty aj po WakeOnAlarm()==true\n";
    return false;
  }

  // The mask must still be armed the instant the machine comes back --
  // that is the bug this test exists to pin: PowerOn() used to zero
  // rtc_mask/rtc_command on every call, which this path never re-arms the
  // way a hand switch-on's schedule_alarm does, silently disarming the
  // alarm after its first ring (HANDOFF 6.32).
  if ((machine.debug_rtc_mask() & 0x01) == 0) {
    std::cout << "  rtc_mask bolo vynulovane pri zobudeni budikom\n";
    return false;
  }

  // Woken is heard, not just seen: the samples are what the test asserts on,
  // the transcript is not proof of speech (HANDOFF section 3, "zvuk").
  // Collected on the way rather than taken after Grind, which throws the
  // samples away on every step.
  machine.TakeAudio();
  int32_t low = 0;
  int32_t high = 0;
  const uint64_t listenUntil = machine.instructions() + 8'000'000;
  while (machine.instructions() < listenUntil) {
    machine.TakeSpeechInput();
    machine.TakeConsoleOutput();
    for (int16_t sample : machine.TakeAudio()) {
      if (sample < low) low = sample;
      if (sample > high) high = sample;
    }
    if (!machine.Step() && machine.powered_off()) break;
  }
  if (high - low < 1000) {
    std::cout << "  budik po zobudeni nebolo pocut, rozkmit len " << (high - low)
              << "\n";
    return false;
  }

  // No key is sent from here, and that is the case this pins: an alarm
  // nobody answers.  service_alarm1 (CALL CFEBh at 180C8) rings, re-arms the
  // alarm for its next occurrence and returns with C45Ah still FFh, so
  // 180CB-180CF takes JP NZ,CFD9h back to .go_to_sleep.  Measured with the
  // probe: off again within 150M instructions.  An answered alarm is the
  // other branch -- the key clears C45Ah and the machine stays in the Main
  // Menu -- which the owner confirmed by hand (HANDOFF 6.32).
  for (int slice = 0; slice < 40 && !machine.powered_off(); ++slice)
    Grind(machine, 8'000'000);
  std::array<uint8_t, 8> rearmed{};
  for (unsigned index = 0; index < 8; ++index)
    rearmed[index] = machine.debug_rtc_ram(index);
  bool served = true;
  if (rearmed == armed) {
    std::cout << "  budik po zobudeni sa neprestavil na dalsi vyskyt\n";
    served = false;
  }
  if (!machine.powered_off()) {
    std::cout << "  neodkliknuty budik stroj znovu neuspal\n";
    served = false;
  }
  if (machine.power_down_marker() != 0xff) {
    std::cout << "  C45Ah po uspati je " << std::hex
              << static_cast<unsigned>(machine.power_down_marker())
              << " namiesto ff\n" << std::dec;
    served = false;
  }
  if ((machine.debug_rtc_mask() & 0x01) == 0) {
    std::cout << "  po uspati nie je budik ozbrojeny na dalsi vyskyt\n";
    served = false;
  }
  return served;
}

// Typing goes on the keys the ROM's own tables put the characters on, so a
// character that is on none of them cannot be typed at all.  That has to be
// said out loud: dropping it silently, or worse typing whatever is near it,
// would leave the machine holding a command nobody wrote, and the failure
// would then surface as a puzzling wrong answer three steps later.
bool Type(EurekaMachine& machine, const std::string& text) {
  uint8_t unmapped = 0;
  if (machine.QueueText(text, &unmapped)) return true;
  std::cout << "  znak " << std::hex << static_cast<unsigned>(unmapped)
            << std::dec << "h";
  if (unmapped >= 0x20 && unmapped < 0x7f)
    std::cout << " ('" << static_cast<char>(unmapped) << "')";
  std::cout << " nie je v ziadnej z tabuliek DF05, DF5E a DF98, takze \""
            << text << "\" sa neda napisat" << std::endl;
  return false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 4 ||
      (std::wstring(argv[3]) != L"com" && std::wstring(argv[3]) != L"bas" &&
       std::wstring(argv[3]) != L"kbd" && std::wstring(argv[3]) != L"power" &&
       std::wstring(argv[3]) != L"dc" && std::wstring(argv[3]) != L"rtc" &&
       std::wstring(argv[3]) != L"hudba" &&
       std::wstring(argv[3]) != L"format" && std::wstring(argv[3]) != L"wp" &&
       std::wstring(argv[3]) != L"hlaseni" &&
       std::wstring(argv[3]) != L"snimka" &&
       std::wstring(argv[3]) != L"akord" &&
       std::wstring(argv[3]) != L"budik")) {
    std::wcerr << L"usage: integration_test ROM DISK_FOLDER "
                  L"com|bas|kbd|power|dc|rtc|hudba|format|wp|hlaseni|snimka|"
                  L"akord|budik\n";
    return 2;
  }
  const bool basic = std::wstring(argv[3]) == L"bas";
  auto machine = std::make_unique<EurekaMachine>();
  std::wstring error;
  if (!machine->LoadRom(argv[1], error) || !machine->MountDisk(argv[2], error)) {
    std::wcerr << error << L"\n";
    return 2;
  }
  machine->Reset();

  if (std::wstring(argv[3]) == L"hudba") {
    const bool passed = CheckMusicStops(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=HUDBA\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"wp") {
    // Reading first, while the host folder is still the medium: the refusal
    // check needs a blank in memory and cannot give it back.
    const bool reads = CheckProtectedDiskStillReads(*machine);
    const bool refuses = CheckProtectedDiskRefusesFormat(*machine);
    const bool passed = reads && refuses;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=WP"
              << " citanie=" << (reads ? "ok" : "chyba")
              << " odmietnutie=" << (refuses ? "ok" : "chyba") << "\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"hlaseni") {
    const bool passed = CheckSaysWhichDiskProblem(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=HLASENI\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"format") {
    const bool passed = CheckFormatsBlankDiskette(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=FORMAT\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"rtc") {
    const bool announces = CheckAnnouncesTime(*machine);
    const bool alarm = CheckAlarm(*machine);
    const bool passed = announces && alarm;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=RTC"
              << " cas=" << (announces ? "ok" : "chyba")
              << " budik=" << (alarm ? "ok" : "chyba")
              << " maska=" << std::hex
              << static_cast<unsigned>(machine->debug_rtc_mask()) << std::dec
              << "\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"budik") {
    const bool passed = CheckAlarmWake(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=BUDIK\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"dc") {
    const bool settles = CheckSettlesToSilence(*machine);
    const bool aliases = CheckDacDecodesWholeBlock(*machine);
    const bool passed = settles && aliases;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=DC"
              << " ticho=" << (settles ? "ok" : "chyba")
              << " porty=" << (aliases ? "ok" : "chyba") << "\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"power") {
    PowerOffState seen;
    const bool passed = CheckPowerOff(*machine, seen);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=POWER"
              << " vypnute=" << (seen.off ? "ano" : "nie") << " C45A=" << std::hex
              << static_cast<unsigned>(seen.marker) << std::dec << "\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"snimka") {
    const bool passed = CheckSnapshot(*machine, argv[1], argv[2]);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=SNIMKA\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"kbd") {
    const auto booted = BootedSnapshot(*machine);
    const bool keys = CheckKeyboard(*machine, *booted);
    const bool braille = CheckBraille(*machine, *booted) &&
                         CheckBrailleShiftSpace(*machine, *booted) &&
                         CheckBrailleShiftStopsSpeech(*machine, *booted);
    const bool tap = CheckShiftTap(*machine, *booted);
    const bool pc = CheckPcKeyboard(*machine);
    const bool altgr = CheckAltGr(*machine, *booted);
    const bool passed = keys && braille && tap && pc && altgr;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=KBD"
              << " klavesy=" << (keys ? "ok" : "chyba")
              << " braille=" << (braille ? "ok" : "chyba")
              << " tuknutie=" << (tap ? "ok" : "chyba")
              << " pc=" << (pc ? "ok" : "chyba")
              << " altgr=" << (altgr ? "ok" : "chyba") << "\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"akord") {
    const auto booted = BootedSnapshot(*machine);
    const bool passed = CheckChordBuilding(*machine, *booted);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=AKORD"
              << " skladanie=" << (passed ? "ok" : "chyba") << "\n";
    return passed ? 0 : 1;
  }

  // Emulator uz neparkuje -- ROM na klaves caka tocenim ako skutocny stroj --
  // takze "Step() vratil false" uz nie je signal, ze si stroj pyta vstup.
  // Nahradou je ticho: dalsi klaves ide az ked stroj pol sekundy nic nevypisal.
  const uint64_t kQuiet = EurekaMachine::kCpuHz / 2;
  const unsigned kSteps = basic ? 3u : 2u;
  std::vector<uint8_t> console;
  uint64_t lastOut = EurekaMachine::kCpuHz * 3;  // nechaj stroj nabehnut
  unsigned fed = 0;
  bool typed = true;
  uint64_t runStarted = 0;
  constexpr uint64_t kLimit = 60'000'000;
  while (machine->instructions() < kLimit) {
    auto chunk = machine->TakeConsoleOutput();
    if (!chunk.empty()) {
      console.insert(console.end(), chunk.begin(), chunk.end());
      lastOut = machine->cycles();
    }
    if (fed < kSteps && machine->cycles() > lastOut + kQuiet) {
      lastOut = machine->cycles();
      if (fed == 0) machine->QueueKey(basic ? 0xc5 : 0xd6);
      else if (fed == 1)
        typed = Type(*machine, basic ? "LOAD \"BEEP\"\r" : "READ\r");
      else {
        typed = Type(*machine, "RUN\r");
        runStarted = machine->instructions();
      }
      if (!typed) break;
      ++fed;
    }
    // A machine that has switched itself off never executes again, so the
    // instruction and cycle counters stop moving: without this the loop's own
    // deadlines can never come due and the test hangs instead of failing.
    if (!machine->Step() && machine->powered_off()) break;
    if (basic && runStarted && machine->instructions() > runStarted + 5'000'000)
      break;
    if (!basic && fed >= kSteps && machine->cycles() > lastOut + 3 * kQuiet) break;
  }

  const auto speech = machine->TakeSpeechInput();
  const auto audio = machine->TakeAudio();
  // The console is checked by content, not by length.  A byte count passed
  // happily while every character was being emitted twice ("hhoottoovvoo").
  const bool passed = typed && (basic
      ? fed >= 3 && machine->debug_bios_reads() >= 60 &&
            Contains(speech, "hotovo") && Contains(console, "hotovo") &&
            Contains(console, "RUN") && runStarted != 0 && audio.size() > 200000
      : fed >= 2 && machine->debug_bios_reads() >= 150 &&
            Contains(speech, "Read which file?") &&
            Contains(console, "Read which file?"));
  std::cout << (passed ? "PASS" : "FAIL")
            << " mode=" << (basic ? "BAS" : "COM")
            << " instructions=" << machine->instructions()
            << " bios_reads=" << machine->debug_bios_reads()
            << " console=" << console.size()
            << " speech=" << speech.size()
            << " audio=" << audio.size() << "\n";
  return passed ? 0 : 1;
}
