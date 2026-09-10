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
// Shift+F8), "sXX" (one PC scan code in hex, e.g. s3C for F2, taken through
// the keyboard's own delivery path), "+wp"/"-wp" to set or clear the diskette's write protect notch,
// "nova" for an unformatted diskette, "vysun" for an empty drive,
// "vypni"/"zapni"/"studeno" for the power switch and the two ways back on,
// "cas:+7d" to move the clock the RTC answers with, "budik" to print it
// beside the alarm the firmware armed, "zvuk" for what the loudspeaker got,
// or a literal string typed on the emulated PC keyboard.  Between
// tokens the machine is run until it has been quiet for half a second, which
// is what "ready for the next key" looks like now that the CPU is never parked
// at console input, so the sequence follows the ROM's own pacing instead of a
// guessed instruction count.  A long silent job -- a disk format -- outlasts
// that, so seq stops before one finishes; see tests/README.md.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "disk_stash.h"
#include "machine.h"
#include "virtual_disk.h"

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

// Runs until the machine goes quiet or the budget is spent.  It used to run
// until the BIOS blocked on console input, but the CPU is not parked there
// any more -- the ROM waits for a key by spinning, as the hardware does --
// so silence on the console is what "ready for the next key" looks like now.
// Returns true if it stopped because the machine went quiet.
bool RunUntilPrompt(EurekaMachine& machine, uint64_t budget) {
  const uint64_t deadline = machine.instructions() + budget;
  const uint64_t quiet = EurekaMachine::kCpuHz / 2;
  // The machine is silent while it boots, so the first call must not take
  // that for a prompt.
  const uint64_t booted = EurekaMachine::kCpuHz * 3;
  uint64_t lastOut = machine.cycles() > booted ? machine.cycles() : booted;
  while (machine.instructions() < deadline) {
    if (!machine.TakeConsoleOutput().empty()) lastOut = machine.cycles();
    if (machine.cycles() > lastOut + quiet) return true;
    // A machine that touched pwr_stb has stopped for good: the counters freeze,
    // so the budget below would never run out and the probe would spin.
    if (!machine.Step() && machine.powered_off()) return true;
  }
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
    // The probe keeps diskettes the way the emulator does, so a sequence can
    // put the same one back in rather than a fresh copy of it -- which is the
    // difference the bulk copy turns on.
    DiskStash probeStash;
    int currentSlot = 0;
    // How far the sequence has pushed the clock, in seconds.  It is kept here
    // rather than read back from the machine because "cas:" tokens add up:
    // "cas:+7d cas:+1h" has to land a week and an hour on, not an hour on.
    int64_t rtcShift = 0;
    for (int index = 5; index < argc; ++index) {
      const std::wstring token = argv[index];
      if (token == L".") {
        // Wait without pressing anything: some answers arrive long after the
        // machine has gone quiet, and a key sent to fill the gap would be an
        // answer to a question that has not been asked yet.
        const bool waited = RunUntilPrompt(*machine, budget);
        std::printf("%-10ls -> %s%s\n", token.c_str(),
                    Readable(machine->TakeSpeechInput()).c_str(),
                    waited ? "" : "  [nezastavil sa na vstupe]");
        continue;
      }
      if (token.size() > 1 && token[0] == L'?') {
        // Wait until the machine says a particular thing, then carry on.  A
        // fixed wait is not good enough for a job the machine drives: the
        // speech arrives well after the work, so a diskette swapped on a
        // timer lands in the middle of a step rather than at the prompt that
        // asked for it.
        std::string wanted;
        for (wchar_t ch : token.substr(1))
          wanted.push_back(static_cast<char>(ch));
        std::string heard;
        const uint64_t deadline = machine->instructions() + budget;
        bool found = false;
        while (machine->instructions() < deadline && !found) {
          heard += Readable(machine->TakeSpeechInput());
          found = heard.find(wanted) != std::string::npos;
          if (!found && !machine->Step() && machine->powered_off()) break;
        }
        heard += Readable(machine->TakeSpeechInput());
        std::printf("%-10ls -> %s%s\n", token.c_str(), heard.c_str(),
                    found ? "" : "  [NEDOCKAL SA]");
        continue;
      }
      if (token.starts_with(L"spin:")) {
        // Where the machine is spending its time.  A histogram of the physical
        // PC is how a hang gets a location instead of a guess: the top few
        // addresses are the loop, and the ROM listing says what it is waiting
        // for.
        const uint64_t steps = _wcstoui64(token.substr(5).c_str(), nullptr, 10);
        std::vector<std::pair<uint32_t, uint64_t>> seen;
        const uint64_t before = machine->debug_bios_reads();
        for (uint64_t step = 0; step < steps; ++step) {
          const uint32_t at = machine->physical_pc();
          bool found = false;
          for (auto& entry : seen)
            if (entry.first == at) { ++entry.second; found = true; break; }
          if (!found && seen.size() < 4096) seen.push_back({at, 1});
          machine->TakeConsoleOutput();
          machine->TakeAudio();
          if (!machine->Step() && machine->powered_off()) break;
        }
        std::sort(seen.begin(), seen.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::printf("%-10ls -> [citani BIOSu +%llu, najcastejsie PC:",
                    token.c_str(),
                    static_cast<unsigned long long>(machine->debug_bios_reads() - before));
        for (std::size_t i = 0; i < seen.size() && i < 6; ++i)
          std::printf(" %05X(%llu)", seen[i].first,
                      static_cast<unsigned long long>(seen[i].second));
        std::printf("]\n");
        continue;
      }
      if (token == L"trace") {
        // The controller and the latches from here on.  The ring buffer keeps
        // the last of them, so switching it on mid-sequence is what makes the
        // report end on the step being investigated rather than on the boot.
        // Only the controller: the status port at A8h is polled by the
        // idle loop and would flood the ring buffer with the machine doing
        // nothing.
        for (uint8_t port : {0x98, 0x99, 0x9a, 0x9b})
          machine->diagnostics().set_trace(port, true);
        std::printf("%-10ls -> [zapnute sledovanie diskovych portov]\n",
                    token.c_str());
        continue;
      }
      if (token == L"vypni" || token == L"zapni" || token == L"studeno") {
        if (token == L"vypni") {
          // The chord from the Main Menu, not a faked strobe: the firmware
          // says "konec" and writes C45Ah itself on the way out, and that is
          // the state a warm start has to come up from.
          machine->PressPowerOffChord();
          const uint64_t deadline = machine->instructions() + budget;
          while (machine->instructions() < deadline && !machine->powered_off())
            machine->Step();
        } else if (token == L"zapni") {
          machine->PowerOn();
        } else {
          machine->Reset();
        }
        const bool blocked = RunUntilPrompt(*machine, budget);
        std::printf("%-10ls -> [%s, C45Ah=%02X] %s%s\n", token.c_str(),
                    machine->powered_off() ? "vypnute" : "bezi",
                    machine->power_down_marker(),
                    Readable(machine->TakeSpeechInput()).c_str(),
                    blocked ? "" : "  [nezastavil sa na vstupe]");
        continue;
      }
      if (token == L"stav") {
        // What is actually on the diskette in the drive.  The speech says what
        // the machine believes; this says what reached the medium, which is
        // the difference the owner's bulk-copy report turns on.
        const VirtualDisk& disk = machine->disk();
        std::printf("%-10ls -> [%s, suborov=%u, %s]\n", token.c_str(),
                    !disk.present()      ? "prazdna mechanika"
                    : disk.has_home() ? "priecinok"
                                      : "neulozena",
                    static_cast<unsigned>(disk.StoredFiles()),
                    disk.write_protected() ? "zamknuta" : "odomknuta");
        continue;
      }
      if (token.starts_with(L"cas:")) {
        // Moves the clock the RTC answers with, not the host's.  An alarm that
        // fell due while the machine was off cannot be reached any other way:
        // waiting out a week is not a measurement, and setting the alarm into
        // the past is a different thing -- the firmware would then never have
        // armed it for a time it believed was still ahead.
        std::wstring spec = token.substr(4);
        int64_t scale = 1;
        if (!spec.empty()) {
          switch (spec.back()) {
            case L'd': scale = 86400; break;
            case L'h': scale = 3600; break;
            case L'm': scale = 60; break;
            case L's': scale = 1; break;
            default: scale = 0; break;  // no unit: the number is seconds
          }
          if (scale != 0) spec.pop_back();
          else scale = 1;
        }
        rtcShift += _wtoi64(spec.c_str()) * scale;
        machine->SetRtcOffset(rtcShift);
        const auto now = machine->debug_rtc_now();
        std::printf("%-10ls -> [hodiny %02u.%02u.%02u %02u:%02u:%02u, posun %lld s]\n",
                    token.c_str(), unsigned(now[5]), unsigned(now[4]),
                    unsigned(now[6]), unsigned(now[1]), unsigned(now[2]),
                    unsigned(now[3]), static_cast<long long>(rtcShift));
        continue;
      }
      if (token == L"zvuk") {
        // Whether the loudspeaker moved at all since the last time this was
        // asked.  The transcript takes .speak and .spconv but leaves out the
        // clicks and key echo of .spchar, and while it took .speak alone the
        // clock's whole announcement was missing from it; the samples cannot
        // be fooled that way.
        const std::vector<int16_t> samples = machine->TakeAudio();
        int32_t low = 0;
        int32_t high = 0;
        for (int16_t sample : samples) {
          if (sample < low) low = sample;
          if (sample > high) high = sample;
        }
        std::printf("%-10ls -> [vzoriek=%zu, rozkmit %d..%d]\n", token.c_str(),
                    samples.size(), low, high);
        continue;
      }
      if (token == L"budik") {
        // Both sides of the alarm at once: what the clock shows and what the
        // firmware last armed.  80h in an alarm register means "do not
        // compare" (0DA5B), so an alarm reads as a time with holes in it.
        const auto now = machine->debug_rtc_now();
        std::printf("%-10ls -> [hodiny %02u.%02u.%02u %02u:%02u:%02u, budik ",
                    token.c_str(), unsigned(now[5]), unsigned(now[4]),
                    unsigned(now[6]), unsigned(now[1]), unsigned(now[2]),
                    unsigned(now[3]));
        static const char* kNames[8] = {"100", "hod", "min", "sek",
                                        "mes", "den", "rok", "dtyz"};
        for (unsigned reg = 0; reg < 8; ++reg)
          std::printf("%s%s=%02X", reg != 0 ? " " : "", kNames[reg],
                      unsigned(machine->debug_rtc_ram(reg)));
        std::printf(", mask=%02X, status=%02X]\n",
                    unsigned(machine->debug_rtc_mask()),
                    unsigned(machine->debug_rtc_status()));
        continue;
      }
      // Waits for the drive the way the emulator's worker does before it
      // swaps: mid-sector is the one moment a swap tears the image, and a
      // probe that ignored that would be measuring a machine the user can
      // never produce.
      const auto settleForSwap = [&machine, budget] {
        const uint64_t deadline = machine->instructions() + budget;
        while (machine->instructions() < deadline) {
          if (machine->DiskSwappable()) return true;
          if (!machine->Step() && machine->powered_off()) return false;
        }
        return false;
      };
      if (token.starts_with(L"mount:")) {
        // Any folder, not just the one the run started with: the bulk copy
        // needs a target as well as a source.
        std::wstring swapError;
        const bool ok = machine->MountDisk(token.substr(6), swapError);
        std::printf("%-10ls -> [vymena diskety: %s]\n", token.c_str(),
                    ok ? "priecinok" : "ZLYHALA");
        continue;
      }
      if (token == L"slot1" || token == L"slot2") {
        // The quick choice, stash and all -- the emulator's own path, not a
        // shortcut through it.  Slot 1 is the folder this run started with,
        // locked as the owner's scenario has it; slot 2 is a diskette living
        // unsaved, and putting it back has to hand back the same one.
        const int slot = token == L"slot1" ? 1 : 2;
        std::wstring swapError;
        const bool settled = settleForSwap();
        machine->FlushDisk(swapError);
        probeStash.Put(currentSlot, machine->disk());
        bool ok = true;
        if (auto kept = probeStash.Take(slot)) {
          machine->InsertDisk(*kept);
        } else if (slot == 2) {
          machine->CreateEmptyDisk(true);
        } else {
          ok = machine->MountDisk(argv[2], swapError);
          if (ok) machine->SetDiskWriteProtected(true);
        }
        currentSlot = slot;
        std::printf("%-10ls -> [vlozeny slot %d: %s%s]\n", token.c_str(), slot,
                    ok ? (machine->disk().has_home() ? "priecinok" : "neulozena")
                       : "ZLYHALO",
                    settled ? "" : ", DISK SA NEUSTALIL");
        continue;
      }
      if (token == L"nova" || token == L"vysun") {
        // The two media the quick choice cannot produce and the firmware
        // answers differently: an empty drive and a diskette that never was
        // formatted.  Both look the same to a read -- nothing comes back --
        // and the ROM still tells them apart, so a probe that could not
        // produce them could not measure the difference either.
        const bool settled = settleForSwap();
        std::wstring swapError;
        machine->FlushDisk(swapError);
        if (token == L"nova") machine->CreateEmptyDisk(false);
        else machine->EjectDisk();
        std::printf("%-10ls -> [%s%s]\n", token.c_str(),
                    token == L"nova" ? "vlozena nenaformatovana disketa"
                                     : "mechanika vysunuta",
                    settled ? "" : ", DISK SA NEUSTALIL");
        continue;
      }
      if (token == L"ram" || token == L"folder") {
        // Swapping the diskette the way the window does, so a sequence can
        // follow the machine through a job that asks for it -- the ROM's bulk
        // copy sends the user back and forth between source and target.
        // "ram" is the quick-choice memory slot: a fresh empty diskette.
        std::wstring swapError;
        bool ok = true;
        const bool settled = settleForSwap();
        machine->FlushDisk(swapError);
        if (token == L"ram") {
          machine->CreateEmptyDisk(true);
        } else {
          ok = machine->MountDisk(argv[2], swapError);
          // The source diskette in the owner's scenario is locked, and the
          // ROM's bulk copy insists on that -- Mount clears the notch, so it
          // goes back on here.
          if (ok) machine->SetDiskWriteProtected(true);
        }
        std::printf("%-10ls -> [vymena diskety: %s%s]\n", token.c_str(),
                    ok ? (token == L"ram" ? "prazdna v pamati"
                                          : "priecinok, zamknuty")
                       : "ZLYHALA",
                    settled ? "" : ", DISK SA NEUSTALIL");
        continue;
      }
      if (token == L"+wp" || token == L"-wp") {
        // Flipping the notch mid-sequence lets one run ask the firmware the
        // same question protected and unprotected, from the same state.
        machine->SetDiskWriteProtected(token[0] == L'+');
        std::printf("%-10ls -> [zamok proti zapisu %s]\n", token.c_str(),
                    token[0] == L'+' ? "zapnuty" : "vypnuty");
        continue;
      }
      if (token.size() == 3 && (token[0] == L'k' || token[0] == L'K')) {
        machine->QueueKey(static_cast<uint8_t>(HexValue(token[1]) * 16 +
                                               HexValue(token[2])));
      } else if (token.size() == 3 && (token[0] == L's' || token[0] == L'S')) {
        // The same key over the PC keyboard's own wire, make and break.  It is
        // not the same thing as kXX: that one drops a finished key code into
        // the queue, while a scan code goes through the delivery routine at
        // 1DDB0/1DE47, which is also where speech is aborted (hardware-map).
        // Anything that behaves differently for a real keyboard than for an
        // injected code shows up as the difference between the two tokens.
        const uint8_t code = static_cast<uint8_t>(HexValue(token[1]) * 16 +
                                                  HexValue(token[2]));
        machine->QueueScanCode(code);
        machine->QueueScanCode(static_cast<uint8_t>(code | 0x80));
      } else {
        std::string text;
        for (wchar_t ch : token)
          text.push_back(ch == L'~' ? '\r' : static_cast<char>(ch));
        // Say it out loud when a character has no key on the keyboard the ROM
        // expects.  Typing on regardless would put a different line into the
        // machine than the one the sequence asked for, and the answer printed
        // below would then be an answer to a question nobody posed.
        uint8_t unmapped = 0;
        if (!machine->QueueText(text, &unmapped)) {
          std::printf("%-10ls -> [znak %02Xh nie je v tabulkach DF05, DF5E "
                      "ani DF98, nedal sa napisat]\n",
                      token.c_str(), unmapped);
          return 1;
        }
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
        machine->QueueText("\x1b");
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
    if (!machine->QueueText("Y")) {
      std::printf("nedalo sa napisat \"Y\"\n");
      return 1;
    }

    bool found = false;
    for (int slice = 0; slice < 20000 && !found && !machine->powered_off();
         ++slice) {
      const uint64_t deadline = machine->instructions() + 5000;
      while (machine->instructions() < deadline) {
        if (!machine->Step()) {
          machine->QueueText("\x1b");
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
