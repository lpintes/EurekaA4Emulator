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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 4 ||
      (std::wstring(argv[3]) != L"com" && std::wstring(argv[3]) != L"bas" &&
       std::wstring(argv[3]) != L"kbd")) {
    std::wcerr << L"usage: integration_test ROM DISK_FOLDER com|bas|kbd\n";
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

  if (std::wstring(argv[3]) == L"kbd") {
    const bool keys = CheckKeyboard(*machine);
    const bool braille = CheckBraille(*machine);
    const bool passed = keys && braille;
    std::cout << (passed ? "PASS" : "FAIL") << " mode=KBD"
              << " klavesy=" << (keys ? "ok" : "chyba")
              << " braille=" << (braille ? "ok" : "chyba") << "\n";
    return passed ? 0 : 1;
  }

  unsigned prompts = 0;
  uint64_t runStarted = 0;
  constexpr uint64_t kLimit = 20'000'000;
  while (machine->instructions() < kLimit) {
    if (!machine->Step()) {
      ++prompts;
      if (prompts == 1) {
        machine->QueueKey(basic ? 0xc5 : 0xd6);  // F6 BASIC / Shift+F7 COM
      } else if (prompts == 2) {
        machine->QueueText(basic ? "LOAD \"BEEP\"\r" : "READ\r");
      } else if (basic && prompts == 3) {
        machine->QueueText("RUN\r");
        runStarted = machine->instructions();
      } else {
        break;
      }
    }
    if (basic && runStarted && machine->instructions() > runStarted + 3'000'000)
      break;
  }

  const auto speech = machine->TakeSpeechInput();
  const auto console = machine->TakeConsoleOutput();
  const auto audio = machine->TakeAudio();
  // The console is checked by content, not by length.  A byte count passed
  // happily while every character was being emitted twice ("hhoottoovvoo").
  const bool passed = basic
      ? prompts >= 3 && machine->debug_bios_reads() >= 60 &&
            Contains(speech, "hotovo") && Contains(console, "hotovo") &&
            Contains(console, "RUN") && runStarted != 0 && audio.size() > 200000
      : prompts >= 3 && machine->debug_bios_reads() >= 150 &&
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
