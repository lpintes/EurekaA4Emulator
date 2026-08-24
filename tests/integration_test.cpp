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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 4 || (std::wstring(argv[3]) != L"com" &&
                    std::wstring(argv[3]) != L"bas")) {
    std::wcerr << L"usage: integration_test ROM DISK_FOLDER com|bas\n";
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
