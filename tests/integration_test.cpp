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
// A bare core with eight bytes of memory and a port that only remembers which
// address it was asked for.  Nothing here needs the ROM or the machine: the
// question is what the instruction puts on A8-A15, and that is the core's
// business alone.
struct BarePorts {
  uint8_t code[8] = {};
  uint16_t asked = 0xffff;
  uint8_t gives = 0x00;  // what a read of any port hands back
  uint8_t sent = 0x00;   // what the last write put on the bus
};

uint8_t BareRead(void* context, uint16_t address) {
  const auto* probe = static_cast<const BarePorts*>(context);
  return address < sizeof probe->code ? probe->code[address] : 0x00;
}

void BareWrite(void* context, uint16_t address, uint8_t value) {}

uint8_t BareIn(z80* cpu, uint16_t port) {
  auto* probe = static_cast<BarePorts*>(cpu->userdata);
  probe->asked = port;
  return probe->gives;
}

void BareOut(z80* cpu, uint16_t port, uint8_t value) {
  auto* probe = static_cast<BarePorts*>(cpu->userdata);
  probe->asked = port;
  probe->sent = value;
}

// TSTIO, IN0, OUT0 and the block-I/O instructions all put 00h on A8-A15
// instead of B (UM005004, Table 46; HANDOFF 6.33 and 6.40).  IN and OUT (C)
// are the ones that really do use B, and they are here as the control: a core
// that forced every address to 00h would pass the rest of this on its own.
//
// It matters because this machine decodes the upper byte.  With a non-zero B
// the address an on-chip register lands on is one nobody answers at all
// (hw::IsUndecodedIo), so the read would come back as FFh instead of the
// register.  The ROM has TSTIO twice, at 185FC and 1E0A2, and neither runs in
// any test here -- measured 22 September 2026 -- so nothing else covers this.
bool CheckZ180IoAddressHighByte() {
  struct Case {
    const char* name;
    std::vector<uint8_t> code;
    uint16_t expected;
  };
  // B is E1h throughout, so a leak is loud rather than subtle.
  const std::vector<Case> cases = {
      {"tstio 04h", {0xed, 0x74, 0x04}, 0x0004},
      {"in0 a,(04h)", {0xed, 0x38, 0x04}, 0x0004},
      {"out0 (04h),a", {0xed, 0x39, 0x04}, 0x0004},
      {"otim", {0xed, 0x83}, 0x0004},
      {"in a,(c)", {0xed, 0x78}, 0xe104},
      {"out (c),a", {0xed, 0x79}, 0xe104},
  };
  bool ok = true;
  for (const auto& item : cases) {
    BarePorts probe;
    std::copy(item.code.begin(), item.code.end(), probe.code);
    z80 cpu;
    z80_init(&cpu);
    cpu.read_byte = BareRead;
    cpu.write_byte = BareWrite;
    cpu.port_in = BareIn;
    cpu.port_out = BareOut;
    cpu.userdata = &probe;
    cpu.b = 0xe1;
    cpu.c = 0x04;
    z80_step(&cpu);
    if (probe.asked != item.expected) {
      std::cout << "  " << item.name << " siahol na " << std::hex
                << probe.asked << " namiesto " << item.expected << std::dec
                << "\n";
      ok = false;
    }
  }
  return ok;
}

// MWI, the other half of DCNTL: bits 7-6 put 0 to 3 wait states into every
// memory cycle, the opcode fetch included (UM005004 Table 4).  The firmware
// writes 38h once at 00012 and never touches it again, so on this machine MWI
// is always 0 -- but the built-in BASIC can change it, and `OUT 50,240` is
// exactly that write (HANDOFF 6.39, 6.43).
//
// Charged per memory **access**, not per instruction, which is why the three
// cases below differ: NOP fetches and nothing more, while LD A,(HL) and
// LD (HL),A each touch memory twice.  An implementation that added the
// penalty once per instruction would pass the first case and fail the others.
bool CheckMemoryWaitStates(EurekaMachine& machine) {
  constexpr uint16_t kScratch = 0x8000;
  struct Case {
    const char* name;
    uint8_t opcode;
    unsigned accesses;
  };
  const std::vector<Case> cases = {
      {"nop", 0x00, 1},
      {"ld a,(hl)", 0x7e, 2},
      {"ld (hl),a", 0x77, 2},
  };
  bool ok = true;
  const uint8_t settled = machine.debug_in(hw::kDcntl);

  for (const auto& item : cases) {
    unsigned long long cost[2] = {0, 0};
    for (int pass = 0; pass < 2; ++pass) {
      // Pass 0 leaves MWI at 0, pass 1 sets it to 3 -- the F0h that BASIC
      // writes, with IWI left where the firmware had it.
      const uint8_t mwi = pass == 0 ? 0x00 : hw::kDcntlMwi;
      machine.debug_out(hw::kDcntl,
                        static_cast<uint8_t>((settled & ~hw::kDcntlMwi) | mwi));
      machine.debug_poke(kScratch, item.opcode);
      machine.debug_set_pc(kScratch);
      // HL into scratch RAM as well, so the memory operand is somewhere
      // harmless rather than wherever HL happened to point.
      machine.debug_set_hl(kScratch + 1);
      const uint64_t before = machine.cycles();
      machine.Step();
      cost[pass] = machine.cycles() - before;
    }
    const unsigned long long added = cost[1] - cost[0];
    const unsigned long long want = 3ull * item.accesses;
    if (added != want) {
      std::cout << "  " << item.name << ": MWI=3 pridalo " << added
                << " T namiesto " << want << " (" << cost[0] << " -> "
                << cost[1] << ")\n";
      ok = false;
    }
  }

  machine.debug_out(hw::kDcntl, settled);
  return ok;
}

// DRAM refresh as BASIC's `OUT 54,252` switches it on (ea4-e2m).  A thousand
// NOPs take P clocks of program; refresh has to add its share of wall time on
// top, counted with the refresh cycles inside the interval.  FCh is 3 in every
// 10, so the program gets 7 of them and refresh adds P * 3/7.  83h is 2 in
// every 80: P * 2/78.  7Ch has every bit but REFE and must add nothing.  The
// other reading of the interval -- 3 after every 10 of program -- would add
// P * 3/10 for FCh, which is what the first case is there to catch.
bool CheckRefreshCycles(EurekaMachine& machine) {
  constexpr uint16_t kScratch = 0x8000;
  constexpr unsigned kNops = 1000;
  struct Case {
    uint8_t rcr;
    unsigned length, interval;  // refresh clocks per interval, 0 when off
  };
  const Case cases[] = {{0xfc, 3, 10}, {0x83, 2, 80}, {0x7c, 0, 10}};
  const uint8_t settled = machine.debug_in(hw::kRcr);
  const bool interrupts = machine.debug_iff1();
  // No interrupt may land in the middle and bring its handler's clocks along.
  machine.debug_set_iff1(false);
  for (unsigned i = 0; i < kNops; ++i) machine.debug_poke(kScratch + i, 0x00);

  auto run = [&](uint8_t rcr) {
    machine.debug_out(hw::kRcr, rcr);
    machine.debug_set_pc(kScratch);
    const uint64_t before = machine.cycles();
    for (unsigned i = 0; i < kNops; ++i) machine.Step();
    return static_cast<unsigned long long>(machine.cycles() - before);
  };
  bool ok = true;
  const unsigned long long plain = run(0x00);
  for (const auto& item : cases) {
    const unsigned long long added = run(item.rcr) - plain;
    const unsigned long long want =
        plain * item.length / (item.interval - item.length);
    // Refresh lands per instruction, so the last few clocks may fall either
    // side of the end of the run.
    if (added + 3 < want || added > want + 3) {
      std::cout << "  RCR=" << std::hex << static_cast<unsigned>(item.rcr)
                << std::dec << ": obnovovanie pridalo " << added
                << " T namiesto " << want << " (bez neho " << plain << ")\n";
      ok = false;
    }
  }
  machine.debug_out(hw::kRcr, settled);
  machine.debug_set_iff1(interrupts);
  return ok;
}

// A Z180 samples its interrupt inputs at the **end** of an instruction
// (UM005004 Table 47, note 7).  Step used to decide the request before the
// instruction, so an instruction that switched a source off could still take
// an interrupt from it -- three times in a sweep, always from timer 0 behind
// the OUT0 (TCR),A at 0027C (HANDOFF 6.33).
//
// The situation is *built*, not waited for, and that is the point.  Watching
// the firmware reach 0027C looks like the obvious test and is worthless: it
// was tried, and it passed with the bug put back, because the collision needs
// timer 0 to expire during the AND at 0027A and that never happens while a
// tune plays.  Three times in 542 824 interrupts is not something to sample.
//
// So: one ED-prefixed instruction in RAM, timer 0 pending and enabled, and A
// holding EEh.  With OUT0 (TCR),A the interrupt must not be taken, because
// that instruction is what forbids it.  With IN0 A,(TCR) -- same state, same
// two-byte length, but it disables nothing -- it must be taken.  The second
// half is not decoration: without it a machine that never took an interrupt
// at all would pass the first.
bool CheckInterruptSampledAtEndOfInstruction(EurekaMachine& machine) {
  // Somewhere in RAM, out of the way of anything the firmware is in the
  // middle of.  This is the last check in the mode, so the machine is not
  // needed afterwards.
  constexpr uint16_t kScratch = 0x8000;
  bool ok = true;

  machine.Reset();
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step()) break;
  if (!machine.debug_iff1()) {
    std::cout << "  stroj ma po nabehu zakazane prerusenia, scenar by nemeral "
                 "nic\n";
    return false;
  }

  // Both halves start from the same armed state: timer 0 counting, its
  // interrupt enabled and its flag already up, and A holding EEh -- the value
  // the firmware's own AND at 0027A leaves there, which clears TDE0 and TIE0.
  const auto arm = [&machine](uint8_t second) {
    machine.debug_poke(kScratch + 0, 0xed);
    machine.debug_poke(kScratch + 1, second);
    machine.debug_poke(kScratch + 2, hw::kTcr);
    machine.debug_out(hw::kTcr, hw::kTcrTde0 | hw::kTcrTie0);
    machine.debug_make_timer0_pending();
    machine.debug_set_a(0xee);
    machine.debug_set_pc(kScratch);
    // The halves have to be independent: if the first one does accept an
    // interrupt -- which is the failure it is there to catch -- that clears
    // IFF1, and the second would then report "not armed" about a state the
    // first one broke.
    machine.debug_set_iff1(true);
  };

  if (machine.debug_poke(kScratch, 0xed), machine.debug_peek(kScratch) != 0xed) {
    std::cout << "  do 8000h sa neda zapisat, tam RAM nie je\n";
    return false;
  }

  // OUT0 (TCR),A with A=EEh switches timer 0's interrupt off, so the
  // instruction that forbids the source must not be the one that takes an
  // interrupt from it.  Sampling after the instruction is what makes that
  // true; sampling before it does not.
  arm(0x39);  // out0 (m),a
  machine.Step();
  if (machine.pc() != kScratch + 3) {
    std::cout << "  prerusenie prislo za OUT0 (TCR),A: PC=" << std::hex
              << machine.pc() << " namiesto " << (kScratch + 3) << std::dec
              << "\n";
    ok = false;
  }

  // The control, and the whole reason the check above means anything: the
  // same armed state in front of an instruction that leaves TIE0 alone has to
  // be taken.  Without it a machine that never accepted the interrupt at all
  // would sail through, and that is exactly how the earlier attempt at this
  // test passed while the bug was back in place.
  arm(0x38);  // in0 a,(m)
  machine.Step();
  if (machine.pc() == kScratch + 3) {
    std::cout << "  prerusenie neprislo ani za IN0 A,(TCR), scenar nie je "
                 "nabity\n";
    ok = false;
  }
  return ok;
}

// Flags of the block-I/O instructions, from UM005004 Table 46 and its two
// notes: (5) Z is 1 when B-1 is 0, (6) **N is the top bit of the byte
// transferred** -- not the constant 1 this core wrote for years.  Hitachi's
// manual matches Zilog's symbol for symbol.
//
// OTIMR and OTDMR are the exception in the group: their flags are fixed
// (S=0, Z=1, H=0, P/V=1, C=0) rather than computed, because they cannot
// finish with B at anything but zero.
//
// Nothing else can hold this.  The ROM has OTIMR six times and reads the
// flags after none of them, INI and OUTI never run in any test here, and
// ZEXDOC has no I/O instructions at all -- so zex_test is blind to it too.
bool CheckBlockIoFlags() {
  struct Case {
    const char* name;
    uint8_t opcode;  // second byte of the ED prefix
    uint8_t data;    // byte at (HL) for the OUT forms, or what the port gives
    uint8_t b;
    bool expectN;
    bool expectZ;
  };
  // 80h and 7Fh differ in exactly the bit note (6) is about; B of 1 empties
  // the counter and B of 2 does not, which is what note (5) is about.
  const std::vector<Case> cases = {
      {"outi data=80h", 0xa3, 0x80, 2, true, false},
      {"outi data=7Fh", 0xa3, 0x7f, 1, false, true},
      {"ini data=80h", 0xa2, 0x80, 2, true, false},
      {"ini data=7Fh", 0xa2, 0x7f, 1, false, true},
      {"otim data=80h", 0x83, 0x80, 2, true, false},
      {"otdm data=7Fh", 0x8b, 0x7f, 1, false, true},
  };
  bool ok = true;
  for (const auto& item : cases) {
    BarePorts probe;
    probe.code[0] = 0xed;
    probe.code[1] = item.opcode;
    probe.code[4] = item.data;  // where HL points, for the OUT forms
    probe.gives = item.data;    // and what the port hands back, for INI
    z80 cpu;
    z80_init(&cpu);
    cpu.read_byte = BareRead;
    cpu.write_byte = BareWrite;
    cpu.port_in = BareIn;
    cpu.port_out = BareOut;
    cpu.userdata = &probe;
    cpu.b = item.b;
    cpu.c = 0x40;
    cpu.h = 0;
    cpu.l = 4;
    z80_step(&cpu);
    if (static_cast<bool>(cpu.nf) != item.expectN) {
      std::cout << "  " << item.name << ": N=" << int(cpu.nf) << " namiesto "
                << int(item.expectN) << "\n";
      ok = false;
    }
    if (static_cast<bool>(cpu.zf) != item.expectZ) {
      std::cout << "  " << item.name << ": Z=" << int(cpu.zf) << " namiesto "
                << int(item.expectZ) << "\n";
      ok = false;
    }
  }

  // OTIMR with B=1 finishes in one step, so what it leaves behind is what the
  // table prescribes.  Every flag starts at the opposite value, or "set to 0"
  // would pass on a core that simply never touched them.
  {
    BarePorts probe;
    probe.code[0] = 0xed;
    probe.code[1] = 0x93;  // otimr
    probe.code[4] = 0x80;
    z80 cpu;
    z80_init(&cpu);
    cpu.read_byte = BareRead;
    cpu.write_byte = BareWrite;
    cpu.port_in = BareIn;
    cpu.port_out = BareOut;
    cpu.userdata = &probe;
    cpu.b = 1;
    cpu.c = 0x40;
    cpu.h = 0;
    cpu.l = 4;
    cpu.sf = 1;
    cpu.zf = 0;
    cpu.hf = 1;
    cpu.pf = 0;
    cpu.cf = 1;
    z80_step(&cpu);
    if (cpu.sf != 0 || cpu.zf != 1 || cpu.hf != 0 || cpu.pf != 1 ||
        cpu.cf != 0 || cpu.nf != 1) {
      std::cout << "  otimr nechal S=" << int(cpu.sf) << " Z=" << int(cpu.zf)
                << " H=" << int(cpu.hf) << " P=" << int(cpu.pf)
                << " N=" << int(cpu.nf) << " C=" << int(cpu.cf)
                << ", cakalo sa S=0 Z=1 H=0 P=1 N=1 C=0\n";
      ok = false;
    }
  }

  // And the other half of the decision: S, H, P/V and C are "x -- Undefined"
  // for INI and OUTI (Table 36), so the core leaves them as they were instead
  // of inventing the undocumented Z80 rule.  This pins that choice down --
  // if somebody later fills them in, this is what will ask them to read the
  // comment in z80.c first.
  {
    BarePorts probe;
    probe.code[0] = 0xed;
    probe.code[1] = 0xa3;  // outi
    probe.code[4] = 0x80;
    z80 cpu;
    z80_init(&cpu);
    cpu.read_byte = BareRead;
    cpu.write_byte = BareWrite;
    cpu.port_in = BareIn;
    cpu.port_out = BareOut;
    cpu.userdata = &probe;
    cpu.b = 2;
    cpu.c = 0x40;
    cpu.h = 0;
    cpu.l = 4;
    cpu.sf = 1;
    cpu.hf = 1;
    cpu.pf = 1;
    cpu.cf = 1;
    z80_step(&cpu);
    if (cpu.sf != 1 || cpu.hf != 1 || cpu.pf != 1 || cpu.cf != 1) {
      std::cout << "  outi siahol na nedefinovany priznak: S=" << int(cpu.sf)
                << " H=" << int(cpu.hf) << " P=" << int(cpu.pf)
                << " C=" << int(cpu.cf) << ", vsetky mali zostat 1\n";
      ok = false;
    }
  }
  return ok;
}

// The other half of port decoding, and the one the firmware cannot test for
// us.  The 64180 answers on its on-chip registers only when the top eight bits
// of the I/O address are zero (TECHMAN1/64180.4, "Programming the Input/Output
// Ports"), and no Eureka peripheral sits below 40h, so an address like 0132h
// reaches nobody at all.
//
// The firmware never gets there: it uses OUT0, which forces those bits to
// zero.  The built-in BASIC does, though -- it compiles OUT to OUT (C),A with
// the whole port in BC (0B5C1) and INP to IN A,(C) (0B5B7), so OUT 306,240 is
// a line somebody can type.  DCNTL is the register to check it on because it
// is the one the story is about: OUT 50,240 reaches it and would slow the real
// machine down (ea4-vii), and OUT 306,240 must not reach it at all.
//
// Both directions are checked on purpose.  Rejecting the high-byte address
// alone would also pass on a model that had quietly stopped decoding DCNTL.
bool CheckInternalRegistersNeedZeroHighByte(EurekaMachine& machine) {
  bool ok = true;
  const uint16_t decoded = hw::kDcntl;
  const uint16_t shadowed = static_cast<uint16_t>(0x0100 | hw::kDcntl);
  const uint8_t settled = machine.debug_in(decoded);

  if (machine.debug_in(shadowed) != hw::kUndecodedIoRead) {
    std::cout << "  citanie z 0132h nevratilo prazdnu zbernicu\n";
    ok = false;
  }
  // A value the firmware would never leave in DCNTL, so finding it there means
  // the write landed where nothing should have answered.
  const uint8_t intruder = static_cast<uint8_t>(~settled);
  machine.debug_out(shadowed, intruder);
  if (machine.debug_in(decoded) != settled) {
    std::cout << "  zapis na 0132h sa dostal do DCNTL\n";
    ok = false;
  }
  machine.debug_out(decoded, intruder);
  if (machine.debug_in(decoded) != intruder) {
    std::cout << "  zapis na 0032h sa do DCNTL nedostal\n";
    ok = false;
  }
  // DCNTL drives the DMA and the I/O wait states, so it does not stay poked.
  machine.debug_out(decoded, settled);
  return ok;
}

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
// What the output stage does to a waveform the test chose itself.
//
// The DAC steps every 540 cycles while a tune plays, which is 4.22 output
// samples at 48 kHz -- not a whole number, and that is the whole point.  Read
// at the instant each output sample falls, one step comes out four samples
// wide and the next five, so the edge jitters by up to a full sample period.
// The jitter is not in the machine, it is in the act of reading it, and it
// lands as noise spread under the signal: measured 43.3 dB below the
// fundamental sampled, 63.5 dB integrated (HANDOFF 6.38).  Music is where it
// was heard, because music is where the DAC runs fastest.
//
// Nothing here involves the firmware.  A 440 Hz sine is fed to the pin at the
// tune's own DAC rate and the output is asked one question: how much energy
// sits away from the fundamental and its harmonics.  A return to point
// sampling drops that by twenty decibels, so the threshold has ten to spare
// in both directions and does not need retuning when the filters move.
bool CheckDacReconstruction(EurekaMachine& machine) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kDacPeriodCycles = 540.0;  // RLDR0 = 26, HANDOFF 6.10
  const double dacHz = EurekaMachine::kCpuHz / kDacPeriodCycles;
  constexpr double kToneHz = 440.0;
  constexpr std::size_t kWanted = 1 << 15;

  machine.SetVolume(1.0);
  machine.TakeAudio();
  std::vector<int16_t> samples;
  for (uint64_t step = 0; samples.size() < kWanted; ++step) {
    const double t = static_cast<double>(step) / dacHz;
    const double v = 128.0 + 127.0 * std::sin(2.0 * kPi * kToneHz * t);
    machine.debug_feed_dac(
        static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)),
        static_cast<uint32_t>(kDacPeriodCycles));
    for (int16_t sample : machine.TakeAudio()) samples.push_back(sample);
  }
  samples.resize(kWanted);

  // Hann window, then one Goertzel per frequency asked about.  A full FFT
  // would answer questions nobody has: only the fundamental and a sparse comb
  // across the audible band are needed.
  std::vector<double> windowed(kWanted);
  for (std::size_t i = 0; i < kWanted; ++i)
    windowed[i] = samples[i] *
                  (0.5 - 0.5 * std::cos(2.0 * kPi * i / kWanted));
  const double binHz = static_cast<double>(EurekaMachine::kAudioHz) / kWanted;
  auto magnitude = [&](double hz) {
    const double w = 2.0 * kPi * hz / EurekaMachine::kAudioHz;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (double x : windowed) {
      const double s0 = x + coeff * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
  };

  double fundamental = 0.0;
  for (int offset = -1; offset <= 1; ++offset)
    fundamental = std::max(fundamental, magnitude(kToneHz + offset * binHz));
  double noise = 0.0;
  for (double hz = 100.0; hz < 8000.0; hz += 50.0) {
    bool harmonic = false;
    for (int h = 1; h <= 18; ++h)
      if (std::abs(hz - h * kToneHz) < 40.0) harmonic = true;
    if (harmonic) continue;
    const double m = magnitude(hz);
    noise += m * m;
  }
  noise = std::sqrt(noise);

  const double db = 20.0 * std::log10(fundamental / noise);
  const bool ok = db > 55.0;
  if (!ok)
    std::cout << "  rekonstrukcia DAC: odstup sumu " << db
              << " dB, ocakava sa nad 55 (bodove vzorkovanie dava 43)\n";
  return ok;
}

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
// them may be "vadny disk" -- except F8 on an unformatted diskette, where the
// real machine says exactly that (HANDOFF 6.30, measured 7. 9. 2026).
//
// The cases are the owner's table from the real machine, entry point by entry
// point, and three things in it are easy to get wrong without noticing: one
// medium says different sentences depending on where it is reached from, the
// unformatted diskette too, and formatting asks before it looks.  That last
// one is why a case is a sequence of steps, each with what must and what must
// not be heard by then -- the question has to come, and "v jednotce neni disk"
// must not, until the answer is given.
//
// Two lines of the table differ from the model in words, and the ROM sides
// with the model on both: the word processor says "<name> neni ulozen" (the
// only string it has, 75534) where the owner remembers "nebyl ulozen", and
// "zadny soubor nebyl precten" where the owner remembers "nacten".  The
// checks follow the ROM.
bool CheckSaysWhichDiskProblem(EurekaMachine& machine) {
  struct Step {
    uint8_t key;       // a key code, or 0 to type `text` instead
    const char* text;  // "\r" is Enter
    // Stretches of the Kamenicky speech that have to be heard by the end of
    // this step, and ones that must not have been.  A step listens until the
    // case so far has said all of `expected`, and then a little longer; one
    // with something forbidden listens its whole budget, because what it
    // checks is a silence.
    std::vector<const char*> expected;
    std::vector<const char*> forbidden;
  };
  struct Case {
    const char* what;   // how the drive is set up and what is asked of it
    bool eject;         // empty drive, or an unformatted diskette
    bool damaged;       // "vadny disk" is the right answer here
    std::vector<Step> steps;
  };
  const uint8_t kF6 = 0xc5, kF8 = 0xc7, kShiftF1 = 0xd0, kShiftF6 = 0xd5,
                kShiftF8 = 0xd7;
  // Shift+F1 inside the word processor asks "vloz souborovy prikaz"; r reads
  // a file and s saves one (the ROM's own help text, 73752).
  const Case cases[] = {
      {"prazdna mechanika, adresar", true, false,
       {{kF8, nullptr, {"disk nen"}, {}}}},
      {"prazdna mechanika, diskove funkcie", true, false,
       {{kShiftF6, nullptr, {"jednotce nen"}, {}}}},
      {"prazdna mechanika, textovy procesor, citanie", true, false,
       {{kShiftF1, nullptr, {}, {}},
        {kShiftF1, nullptr, {}, {}},
        {0, "r", {}, {}},
        {0, "X\r", {"disk nen", "nebyl p"}, {}}}},
      {"prazdna mechanika, textovy procesor, ukladanie", true, false,
       {{kShiftF1, nullptr, {}, {}},
        {kShiftF1, nullptr, {}, {}},
        {0, "s", {}, {}},
        {0, "X\r", {"disk nen", "X nen", "ulo"}, {}}}},
      {"prazdna mechanika, BASIC, LOAD", true, false,
       {{kF6, nullptr, {}, {}},
        {0, "LOAD \"X\"\r", {"disk nen", "chyba 21"}, {}}}},
      {"prazdna mechanika, BASIC, SAVE", true, false,
       {{kF6, nullptr, {}, {}},
        {0, "SAVE \"X\"\r", {"disk nen", "chyba 21"}, {}}}},
      {"prazdna mechanika, formatovanie", true, false,
       {{kShiftF8, nullptr, {"nebo ne"}, {"jednotce nen"}},
        {0, "y", {"jednotce nen"}, {}}}},
      {"nenaformatovana disketa, adresar", false, true,
       {{kF8, nullptr, {"vadn"}, {}}}},
      {"nenaformatovana disketa, diskove funkcie", false, false,
       {{kShiftF6, nullptr, {"naform"}, {}}}},
  };

  bool passed = true;
  for (const Case& item : cases) {
    // The firmware keeps what it learned about the medium in RAM, so each case
    // gets its own boot with the medium already in the drive.
    if (item.eject) machine.EjectDisk();
    else machine.CreateEmptyDisk(false);
    machine.Reset();
    for (int step = 0; step < 8'000'000; ++step)
      if (!machine.Step()) break;
    machine.TakeSpeechInput();

    std::vector<uint8_t> spoken;
    bool failed = false;
    for (const Step& step : item.steps) {
      if (step.key) machine.QueueKey(step.key);
      else if (!Type(machine, step.text)) return false;

      auto heard = [&] {
        for (const char* stretch : step.expected)
          if (!Contains(spoken, stretch)) return false;
        return true;
      };
      // An empty drive is the slow case on purpose: the firmware finds out by
      // timing the controller out -- it polls the status two thousand times
      // (19A16) before it gives up -- and that wait is the shape of the
      // answer.  Measured, every expected sentence is out within 5M
      // instructions of the key, so 120M is only a ceiling for a model gone
      // slow.  A silence is listened to for 30M, fifteen times what the
      // format dialogue takes to find the drive empty once answered; a step
      // that only moves the dialogue on gets 12M.
      const uint64_t budget = !step.forbidden.empty() ? 30'000'000
                              : !step.expected.empty() ? 120'000'000
                                                       : 12'000'000;
      const uint64_t deadline = machine.instructions() + budget;
      uint64_t tail = 0;
      while (machine.instructions() < deadline) {
        const auto chunk = RunAndListen(machine, 1'000'000);
        spoken.insert(spoken.end(), chunk.begin(), chunk.end());
        if (machine.powered_off()) break;
        // What comes after the expected sentence is still listened to: a
        // "vadny disk" said right after it is the very regression this is for.
        if (!step.expected.empty() && step.forbidden.empty() && heard() &&
            ++tail > 10)
          break;
      }

      for (const char* stretch : step.expected)
        if (!Contains(spoken, stretch)) {
          std::cout << "  " << item.what << ": necakal som \"" << stretch
                    << "\"\n";
          failed = true;
        }
      for (const char* stretch : step.forbidden)
        if (Contains(spoken, stretch)) {
          std::cout << "  " << item.what << ": \"" << stretch
                    << "\" zaznelo priskoro\n";
          failed = true;
        }
      if (failed) break;
    }
    if (!failed && !item.damaged && Contains(spoken, "vadn")) {
      std::cout << "  " << item.what << ": stroj povedal aj \"vadny disk\","
                << " co znamena poskodenu disketu\n";
      failed = true;
    }
    if (failed) {
      Say("  stroj povedal", spoken);
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

// An undefined op code has to trap, not run as a Z80 would.  TRAP.COM says
// "pred", executes ED 77, which a Z180 does not know, and says "po"; the owner
// ran these exact bytes on a real Eureka on 18.9.2026 and heard "pred", then
// "ahoj" and the Main Menu (HANDOFF 6.33, point 5).  Without the trap the
// emulator said "pred po ahoj".  The program gets a folder of its own because
// the shared test diskette is mounted by every mode at once.
bool CheckUndefinedOpcodeTraps(EurekaMachine& machine) {
  static constexpr uint8_t kTrapCom[] = {
      0x11, 0x15, 0x01, 0x0e, 0x09, 0xcd, 0x05, 0x00,  // ld de,pred / bdos 9
      0xed, 0x77,                                      // undefined on a Z180
      0x11, 0x1c, 0x01, 0x0e, 0x09, 0xcd, 0x05, 0x00,  // ld de,po / bdos 9
      0xc3, 0x00, 0x00,                                // jp 0
      'p', 'r', 'e', 'd', '\r', '\n', '$', 'p', 'o', '\r', '\n', '$'};
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / L"ea4_trap";
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  {
    std::ofstream out(dir / L"TRAP.COM", std::ios::binary);
    out.write(reinterpret_cast<const char*>(kTrapCom), sizeof kTrapCom);
    if (!out) {
      std::cout << "  TRAP.COM sa do " << dir.string() << " nezapisal\n";
      return false;
    }
  }
  std::wstring err;
  if (!machine.MountDisk(dir.wstring(), err)) {
    std::wcout << L"  priecinok s TRAP.COM sa nepripojil: " << err << L"\n";
    fs::remove_all(dir, ec);
    return false;
  }
  machine.Reset();

  const uint64_t kQuiet = EurekaMachine::kCpuHz / 2;
  std::vector<uint8_t> spoken;
  uint64_t lastOut = EurekaMachine::kCpuHz * 3;  // nechaj stroj nabehnut
  unsigned fed = 0;
  bool typed = true;
  while (machine.instructions() < 40'000'000) {
    const auto said = machine.TakeSpeechInput();
    if (!said.empty()) {
      spoken.insert(spoken.end(), said.begin(), said.end());
      lastOut = machine.cycles();
    }
    machine.TakeConsoleOutput();
    machine.TakeAudio();
    if (fed < 2 && machine.cycles() > lastOut + kQuiet) {
      lastOut = machine.cycles();
      // Shift+F7 is "spustit program z disku", then the program's name.
      if (fed == 0) machine.QueueKey(0xd6);
      else typed = Type(machine, "TRAP\r");
      if (!typed) break;
      ++fed;
    }
    if (!machine.Step() && machine.powered_off()) break;
    if (fed >= 2 && machine.cycles() > lastOut + 3 * kQuiet) break;
  }
  fs::remove_all(dir, ec);
  if (!typed) return false;

  // "po" is short enough to hide in other words, so only the stretch between
  // the program's first line and the machine's goodbye is searched for it.
  const std::string pred = "pred", ahoj = "ahoj", po = "po";
  const auto start = std::search(spoken.begin(), spoken.end(), pred.begin(),
                                 pred.end());
  const auto end = start == spoken.end()
      ? spoken.end()
      : std::search(start + pred.size(), spoken.end(), ahoj.begin(), ahoj.end());
  const bool passed =
      end != spoken.end() &&
      std::search(start, end, po.begin(), po.end()) == end;
  if (!passed) Say("TRAP.COM povedal", spoken);
  return passed;
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
       std::wstring(argv[3]) != L"zvuk" &&
       std::wstring(argv[3]) != L"format" && std::wstring(argv[3]) != L"wp" &&
       std::wstring(argv[3]) != L"hlaseni" &&
       std::wstring(argv[3]) != L"snimka" &&
       std::wstring(argv[3]) != L"akord" &&
       std::wstring(argv[3]) != L"budik" &&
       std::wstring(argv[3]) != L"trap")) {
    std::wcerr << L"usage: integration_test ROM DISK_FOLDER "
                  L"com|bas|kbd|power|dc|rtc|hudba|zvuk|format|wp|hlaseni|snimka|"
                  L"akord|budik|trap\n";
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

  if (std::wstring(argv[3]) == L"zvuk") {
    const bool passed = CheckDacReconstruction(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=ZVUK\n";
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

  if (std::wstring(argv[3]) == L"trap") {
    const bool passed = CheckUndefinedOpcodeTraps(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=TRAP\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"dc") {
    const bool settles = CheckSettlesToSilence(*machine);
    const bool aliases = CheckDacDecodesWholeBlock(*machine);
    const bool internal = CheckInternalRegistersNeedZeroHighByte(*machine);
    const bool highByte = CheckZ180IoAddressHighByte();
    const bool blockFlags = CheckBlockIoFlags();
    const bool waits = CheckMemoryWaitStates(*machine);
    const bool refresh = CheckRefreshCycles(*machine);
    const bool sampling = CheckInterruptSampledAtEndOfInstruction(*machine);
    const bool passed = settles && aliases && internal && highByte &&
                        blockFlags && waits && refresh && sampling;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=DC"
              << " ticho=" << (settles ? "ok" : "chyba")
              << " porty=" << (aliases ? "ok" : "chyba")
              << " interne=" << (internal ? "ok" : "chyba")
              << " hornybajt=" << (highByte ? "ok" : "chyba")
              << " priznaky=" << (blockFlags ? "ok" : "chyba")
              << " cakacie=" << (waits ? "ok" : "chyba")
              << " obnovovanie=" << (refresh ? "ok" : "chyba")
              << " vzorkovanie=" << (sampling ? "ok" : "chyba") << "\n";
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
