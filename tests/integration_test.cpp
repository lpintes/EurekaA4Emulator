#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
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

// Every key code the host keyboard can produce, pressed one at a time and
// checked against what the ROM made of it.  Nothing on the machine's ports
// carries a key code: it scans a twenty-key braille keyboard and works the
// code out at D4B0, so a row read from the wrong port or a press too short for
// the 75 Hz scan turns a cursor key into a braille letter with no error
// anywhere.  C638 is where the decoder leaves its answer (1D513).
bool CheckKeyboard(EurekaMachine& machine) {
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
  bool ok = true;
  for (uint8_t code : kCodes) {
    machine.Reset();
    for (int step = 0; step < 8'000'000; ++step)
      if (!machine.Step()) break;
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
bool CheckBraille(EurekaMachine& machine) {
  static const uint8_t kAhoj[] = {
      0x04,  // dot 1       -> a
      0x16,  // dots 1,2,5  -> h
      0x15,  // dots 1,3,5  -> o
      0x1a,  // dots 2,4,5  -> j
  };
  machine.Reset();
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step()) break;
  machine.QueueKey(0xd0);  // Shift+F1, the word processor
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  for (uint8_t dots : kAhoj) {
    machine.PressBraille(dots);
    for (int step = 0; step < 8'000'000; ++step)
      if (!machine.Step() && machine.queued_keys() == 0) break;
  }
  machine.TakeSpeechInput();
  machine.QueueKey(0x85);  // Home, which speaks the line
  for (int step = 0; step < 8'000'000; ++step)
    if (!machine.Step() && machine.queued_keys() == 0) break;
  const auto spoken = machine.TakeSpeechInput();
  if (Contains(spoken, "ahoj")) return true;
  std::cout << "  braillovske akordy precitane ako \"";
  for (uint8_t byte : spoken)
    std::cout << (byte >= 0x20 && byte < 0x7f ? static_cast<char>(byte) : '.');
  std::cout << "\"\n";
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
      machine.QueueKey(0x8f);
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 4 ||
      (std::wstring(argv[3]) != L"com" && std::wstring(argv[3]) != L"bas" &&
       std::wstring(argv[3]) != L"kbd" && std::wstring(argv[3]) != L"power" &&
       std::wstring(argv[3]) != L"dc" && std::wstring(argv[3]) != L"rtc" &&
       std::wstring(argv[3]) != L"hudba")) {
    std::wcerr << L"usage: integration_test ROM DISK_FOLDER "
                  L"com|bas|kbd|power|dc|rtc|hudba\n";
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
    const bool keys = CheckKeyboard(*machine);
    const bool braille = CheckBraille(*machine);
    const bool pc = CheckPcKeyboard(*machine);
    const bool passed = keys && braille && pc;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=KBD"
              << " klavesy=" << (keys ? "ok" : "chyba")
              << " braille=" << (braille ? "ok" : "chyba")
              << " pc=" << (pc ? "ok" : "chyba") << "\n";
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
      else if (fed == 1) machine->QueueText(basic ? "LOAD \"BEEP\"\r" : "READ\r");
      else { machine->QueueText("RUN\r"); runStarted = machine->instructions(); }
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
  const bool passed = basic
      ? fed >= 3 && machine->debug_bios_reads() >= 60 &&
            Contains(speech, "hotovo") && Contains(console, "hotovo") &&
            Contains(console, "RUN") && runStarted != 0 && audio.size() > 200000
      : fed >= 2 && machine->debug_bios_reads() >= 150 &&
            Contains(speech, "Read which file?") &&
            Contains(console, "Read which file?");
  std::cout << (passed ? "PASS" : "FAIL")
            << " mode=" << (basic ? "BAS" : "COM")
            << " instructions=" << machine->instructions()
            << " bios_reads=" << machine->debug_bios_reads()
            << " console=" << console.size()
            << " speech=" << speech.size()
            << " audio=" << audio.size() << "\n";
  return passed ? 0 : 1;
}
