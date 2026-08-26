#include <algorithm>
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 4 ||
      (std::wstring(argv[3]) != L"com" && std::wstring(argv[3]) != L"bas" &&
       std::wstring(argv[3]) != L"kbd" && std::wstring(argv[3]) != L"power")) {
    std::wcerr
        << L"usage: integration_test ROM DISK_FOLDER com|bas|kbd|power\n";
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
