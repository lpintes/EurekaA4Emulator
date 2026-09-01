#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "machine.h"

namespace {

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

// Says "where am I" and returns how many steps the utterance lasted, pressing
// shift after `pressShiftAfter` steps of it if that is not zero.  C621h is FFh
// only while speech is playing -- armed at 002D5, cleared at 0055E -- so it is
// both the "is it speaking" flag and the value a new key press copies into
// spabrt (C620h) to stop it.
long SpeakUntilDone(EurekaMachine& machine, const EurekaMachine& booted,
                    long pressShiftAfter) {
  machine.CopyStateFrom(booted);
  machine.QueueKey(0xc9);  // F10, "where am I"
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
    if (pressShiftAfter != 0 && step == pressShiftAfter) machine.HoldShift(true);
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
  const long full = SpeakUntilDone(machine, booted, 0);
  if (full <= 0) return false;
  const long cut = SpeakUntilDone(machine, booted, full / 4);
  if (cut <= 0) return false;
  // Generous on purpose: the point is that speech stopped early, not where.
  if (cut < full / 2) return true;
  std::cout << "  shift rec nezastavil: cela " << full << " krokov, so shiftom "
            << cut << "\n";
  return false;
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
// the machine says "konec", stops executing, and leaves FFh in C45Ah, which is
// the marker its own boot code reads at 180CB to resume instead of initialise.
// Without this the strobe can go back to being a no-op and nothing would say
// so: an emulator that ignores it just spins in the two instructions after it,
// with interrupts off, which is silence and looks like any other hang.
bool CheckPowerOff(EurekaMachine& machine) {
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
bool CheckMusicStops(EurekaMachine& machine) {
  // Measured: the jingle runs about 6.3 s of guest time from the F7 press, so
  // pressing at 1 s and looking at 2.5-3.0 s is well inside it either way.
  const uint64_t kPressAt = EurekaMachine::kCpuHz;
  const uint64_t kWindowFrom = EurekaMachine::kCpuHz * 5 / 2;
  const uint64_t kWindowTo = EurekaMachine::kCpuHz * 3;

  double level[2] = {0, 0};
  for (int run = 0; run < 2; ++run) {
    const bool press = run == 0;
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
        machine.PressBraille(0x80);  // the space bar alone, row 89h bit 7
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
       std::wstring(argv[3]) != L"hlaseni")) {
    std::wcerr << L"usage: integration_test ROM DISK_FOLDER "
                  L"com|bas|kbd|power|dc|rtc|hudba|format|wp|hlaseni\n";
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
    const bool passed = CheckAlarm(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=RTC"
              << " maska=" << std::hex
              << static_cast<unsigned>(machine->debug_rtc_mask()) << std::dec
              << "\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"dc") {
    const bool passed = CheckSettlesToSilence(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=DC\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"power") {
    const bool passed = CheckPowerOff(*machine);
    std::cout << (passed ? "PASS" : "FAIL") << " mode=POWER"
              << " vypnute=" << (machine->powered_off() ? "ano" : "nie")
              << " C45A=" << std::hex
              << static_cast<unsigned>(machine->power_down_marker()) << std::dec
              << "\n";
    return passed ? 0 : 1;
  }

  if (std::wstring(argv[3]) == L"kbd") {
    const auto booted = BootedSnapshot(*machine);
    const bool keys = CheckKeyboard(*machine, *booted);
    const bool braille = CheckBraille(*machine, *booted) &&
                         CheckBrailleShiftSpace(*machine, *booted) &&
                         CheckBrailleShiftStopsSpeech(*machine, *booted);
    const bool pc = CheckPcKeyboard(*machine);
    const bool altgr = CheckAltGr(*machine, *booted);
    const bool passed = keys && braille && pc && altgr;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=KBD"
              << " klavesy=" << (keys ? "ok" : "chyba")
              << " braille=" << (braille ? "ok" : "chyba")
              << " pc=" << (pc ? "ok" : "chyba")
              << " altgr=" << (altgr ? "ok" : "chyba") << "\n";
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
