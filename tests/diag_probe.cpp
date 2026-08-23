// Boots the ROM with diagnostics on, optionally drives it, and prints the
// report.
//
// The point is not to test applications but to answer questions the ROM image
// cannot answer on its own: whether kRamBase is placed correctly, which I/O
// ports the machine model still does not implement, and what the three
// write-only control latches are actually driven with.
//
//   diag_probe ROM DISK_FOLDER boot
//   diag_probe ROM DISK_FOLDER sweep [budget]
//   diag_probe ROM DISK_FOLDER seq [budget] TOKEN...
//
// A sequence TOKEN is either "kXX" (one key code in hex, e.g. kD7 for
// Shift+F8) or a literal string typed as text.  The machine is run until the
// BIOS blocks on console input before each token, so the sequence follows the
// ROM's own pacing instead of a guessed instruction count.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "machine.h"

namespace {

std::string Utf8(const std::wstring& text) {
  std::string out;
  for (wchar_t ch : text) {
    if (ch < 0x80) {
      out.push_back(static_cast<char>(ch));
    } else if (ch < 0x800) {
      out.push_back(static_cast<char>(0xc0 | (ch >> 6)));
      out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xe0 | (ch >> 12)));
      out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
    }
  }
  return out;
}

// Speech text is Kamenicky; fold it to plain ASCII so the report stays legible
// whatever the console codepage happens to be.
std::string Readable(const std::vector<uint8_t>& bytes) {
  static const char* kHigh =
      "CueDaDTcePILlrAAEzZooOuUyOUSLYRtaiouna UOsrrR";
  std::string out;
  for (uint8_t byte : bytes) {
    if (byte >= 0x20 && byte < 0x7f) out.push_back(static_cast<char>(byte));
    else if (byte >= 0x80 && byte < 0xac) out.push_back(kHigh[byte - 0x80]);
    else out.push_back('.');
  }
  return out;
}

// Runs until the BIOS blocks on console input or the budget is spent.
// Returns true if it stopped because input was wanted.
bool RunUntilPrompt(EurekaMachine& machine, uint64_t budget) {
  const uint64_t deadline = machine.instructions() + budget;
  while (machine.instructions() < deadline)
    if (!machine.Step()) return true;
  return false;
}

unsigned HexValue(wchar_t ch) {
  if (ch >= L'0' && ch <= L'9') return static_cast<unsigned>(ch - L'0');
  if (ch >= L'a' && ch <= L'f') return static_cast<unsigned>(ch - L'a' + 10);
  if (ch >= L'A' && ch <= L'F') return static_cast<unsigned>(ch - L'A' + 10);
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 4) {
    std::wcerr << L"usage: diag_probe ROM DISK_FOLDER boot|sweep|seq "
                  L"[budget] [TOKEN...]\n";
    return 2;
  }
  const std::wstring mode = argv[3];
  const uint64_t budget = argc > 4 ? _wcstoui64(argv[4], nullptr, 10) : 4'000'000;

  auto machine = std::make_unique<EurekaMachine>();
  std::wstring error;
  if (!machine->LoadRom(argv[1], error)) {
    std::wcerr << L"ROM: " << error << L"\n";
    return 2;
  }
  if (!machine->MountDisk(argv[2], error)) {
    std::wcerr << L"disk: " << error << L"\n";
    return 2;
  }
  machine->diagnostics().set_enabled(true);

  if (mode == L"seq") {
    machine->Reset();
    if (!RunUntilPrompt(*machine, 8'000'000)) {
      std::printf("nedosiel na prompt\n");
      return 1;
    }
    std::printf("start: %s\n", Readable(machine->TakeSpeechInput()).c_str());
    for (int index = 5; index < argc; ++index) {
      const std::wstring token = argv[index];
      if (token.size() == 3 && (token[0] == L'k' || token[0] == L'K')) {
        machine->QueueKey(static_cast<uint8_t>(HexValue(token[1]) * 16 +
                                               HexValue(token[2])));
      } else {
        std::string text;
        for (wchar_t ch : token)
          text.push_back(ch == L'~' ? '\r' : static_cast<char>(ch));
        machine->QueueText(text);
      }
      const bool blocked = RunUntilPrompt(*machine, budget);
      std::printf("%-10ls -> %s%s\n", token.c_str(),
                  Readable(machine->TakeSpeechInput()).c_str(),
                  blocked ? "" : "  [nezastavil sa na vstupe]");
    }
  } else if (mode == L"sweep") {
    std::vector<uint8_t> keys;
    for (uint8_t code = 0xc0; code <= 0xc9; ++code) keys.push_back(code);
    for (uint8_t code = 0xd0; code <= 0xd9; ++code) keys.push_back(code);
    for (uint8_t key : keys) {
      machine->Reset();
      if (!RunUntilPrompt(*machine, 8'000'000)) {
        std::printf("klaves %02X: nedosiel na prompt\n", key);
        continue;
      }
      machine->TakeSpeechInput();
      machine->QueueKey(key);
      for (int round = 0; round < 3; ++round) {
        if (!RunUntilPrompt(*machine, budget)) break;
        machine->QueueKey(0x1b);
      }
      std::printf("klaves %02X: %s\n", key,
                  Readable(machine->TakeSpeechInput()).c_str());
    }
  } else if (mode == L"trace") {
    // Records every access to the floppy controller, the status port and the
    // three control latches, then stops the moment the ROM speaks the phrase
    // we are chasing, so the ring buffer ends on the decision itself instead
    // of on whatever the machine did afterwards.
    for (uint8_t port : {0x80, 0x98, 0x99, 0x9a, 0x9b, 0xa0, 0xa8, 0xb0})
      machine->diagnostics().set_trace(port, true);

    std::string needle = "jednotce";
    if (argc > 5) {
      needle.clear();
      for (wchar_t ch : std::wstring(argv[5]))
        needle.push_back(static_cast<char>(ch));
    }

    machine->Reset();
    RunUntilPrompt(*machine, 8'000'000);
    machine->TakeSpeechInput();
    machine->QueueKey(0xd7);          // Shift+F8, format disk
    RunUntilPrompt(*machine, budget);
    std::string spoken = Readable(machine->TakeSpeechInput());
    machine->QueueText("Y");

    bool found = false;
    for (int slice = 0; slice < 20000 && !found; ++slice) {
      const uint64_t deadline = machine->instructions() + 5000;
      while (machine->instructions() < deadline) {
        if (!machine->Step()) {
          machine->QueueKey(0x1b);
          break;
        }
      }
      spoken += Readable(machine->TakeSpeechInput());
      found = spoken.find(needle) != std::string::npos;
    }
    std::printf("rec: %s\nhladane=\"%s\" najdene=%s\n", spoken.c_str(),
                needle.c_str(), found ? "ano" : "NIE");
  } else {
    machine->Reset();
    RunUntilPrompt(*machine, 8'000'000);
    std::printf("start: %s\n", Readable(machine->TakeSpeechInput()).c_str());
  }

  std::printf("\ninstrukcii=%llu\n",
              static_cast<unsigned long long>(machine->instructions()));
  const std::string report = Utf8(machine->diagnostics().Report());
  std::fwrite(report.data(), 1, report.size(), stdout);
  return 0;
}
