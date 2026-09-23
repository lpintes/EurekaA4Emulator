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
// "wav:SUBOR" to write those samples out as a WAV instead of counting them,
// "rychlost:N" and "hlasitost:N" to move the two sliders (sliders.h),
// "@TEXT" to time how long the console takes to show TEXT after the line
// typed just before it, or a literal string typed on the emulated PC keyboard.  A switched-off
// machine can wake itself on the alarm the same way it does on the hardware
// (HANDOFF 6.32): "cas:" reports it right on the token that crossed the
// alarm's minute, and every other token polls for it once before running, so
// a sequence of "." waits for it the way an interactive session would.  Left
// unanswered the machine rings, re-arms the alarm and goes back to sleep; a
// key answers it and keeps it in the Main Menu.  "." stops on the gaps
// between the rings, so waiting out the whole alarm takes "spin:", not dots.
// A separate family
// holds and releases one membrane key at a time, which "kXX" and "PressBraille"
// cannot say because both send a finished chord: "+b1".."+b6" and "-b1".."-b6"
// for the six dot keys, "+bs"/"-bs" for the space bar, "+bh"/"-bh" for shift,
// "+f1".."+f8"/"-f1".."-f8" for the function keys, "+ku"/"+kd"/"+kl"/"+kr"
// (and the matching "-...") for the four cursor keys, and "-b" on its own to
// let go of everything at once.  Between
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
#include "eureka_io.h"
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
  // The clock chip's own supply never cuts, so a switched-off machine can
  // still wake itself: checked once per call, the same poll the run loop
  // does every ~2ms, so a sequence of "." tokens (or "cas:" moving the RTC
  // across the alarm) sees it the way an interactive session would.
  if (machine.powered_off()) machine.WakeOnAlarm();
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
    // What the "+bN"/"-bN" family below has told HoldMembrane the fingers are
    // doing.  Kept here, across tokens, because each of those tokens only
    // flips one bit and HoldMembrane wants the whole row every time -- the
    // same reason rtcShift is kept here rather than read back from the
    // machine.
    uint8_t heldRow0 = 0;
    uint8_t heldRow1 = 0;
    uint8_t heldRow2 = 0;
    // Cycle count at which the last typed line went in, for "@text" to
    // measure from.  Set only by a line that is followed by "@": any other
    // token runs until the machine falls quiet, and that wait would be
    // counted as the program's own time.
    uint64_t sentAt = 0;
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
      if (token.size() > 1 && token[0] == L'@') {
        // How long a program takes to answer: wait until the console shows a
        // particular text and report the cycles since the line before it went
        // in.  The console, not the speech, because speech trails the work by
        // seconds.  "_" stands for a space so the token survives the shell.
        // Cycles spent in the program itself -- RAM below the common area --
        // are counted apart, since the rest (the ROM echoing the line aloud,
        // BDOS) is there on the hardware too but does not scale with the
        // program's work.  That split is what made the CHESS.COM levels
        // comparable with a stopwatch on the real machine (HANDOFF 6.46).
        std::string wanted;
        for (wchar_t ch : token.substr(1))
          wanted.push_back(static_cast<char>(ch == L'_' ? L' ' : ch));
        std::string seen;
        uint64_t programCycles = 0;
        const uint64_t deadline = machine->instructions() + budget;
        bool found = false;
        while (machine->instructions() < deadline && !found) {
          seen += Readable(machine->TakeConsoleOutput());
          found = seen.find(wanted) != std::string::npos;
          if (found) break;
          machine->TakeAudio();
          const bool inProgram = machine->pc() < 0xc000 &&
                                 machine->physical_pc() >= EurekaMachine::kRamBase;
          const uint64_t before = machine->cycles();
          if (!machine->Step() && machine->powered_off()) break;
          if (inProgram) programCycles += machine->cycles() - before;
        }
        const uint64_t elapsed = machine->cycles() - sentAt;
        std::printf("%-10ls -> [%llu cyklov = %.2f s, z toho v programe %.2f s] %s%s\n",
                    token.c_str(), static_cast<unsigned long long>(elapsed),
                    elapsed / double(EurekaMachine::kCpuHz),
                    programCycles / double(EurekaMachine::kCpuHz), seen.c_str(),
                    found ? "" : "  [NEDOCKAL SA]");
        machine->TakeSpeechInput();
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
        // A jump straight into the alarm's minute wakes the machine the same
        // tick the strobe would on the hardware; report it here rather than
        // silently, since a sequence measuring the alarm needs to know
        // whether this token was the one that crossed it.
        const bool woke = machine->powered_off() && machine->WakeOnAlarm();
        const auto now = machine->debug_rtc_now();
        std::printf("%-10ls -> [hodiny %02u.%02u.%02u %02u:%02u:%02u, posun %lld s%s]\n",
                    token.c_str(), unsigned(now[5]), unsigned(now[4]),
                    unsigned(now[6]), unsigned(now[1]), unsigned(now[2]),
                    unsigned(now[3]), static_cast<long long>(rtcShift),
                    woke ? ", zobudilo budikom" : "");
        continue;
      }
      if (token.starts_with(L"rychlost:") || token.starts_with(L"hlasitost:")) {
        // By the same positions the window uses, so a measurement names a
        // setting a user can actually reach.  The firmware looks at the rate
        // pot only while it speaks (001BB), so moving it in a silence changes
        // nothing until the next word.
        const int position =
            _wtoi(token.substr(token.find(L':') + 1).c_str());
        if (token.starts_with(L"rychlost:")) {
          machine->SetRatePot(sliders::RatePotLevel(position));
          std::printf("%-10ls -> [posuvnik rychlosti na %02Xh]\n", token.c_str(),
                      unsigned(sliders::RatePotLevel(position)));
        } else {
          machine->SetVolume(sliders::VolumeGain(position));
          std::printf("%-10ls -> [hlasitost x%.4f]\n", token.c_str(),
                      sliders::VolumeGain(position));
        }
        continue;
      }
      if (token.starts_with(L"dac:")) {
        // How the DAC is actually being driven, which is the question behind
        // every complaint about the sound.  Two histograms: how many cycles
        // pass between writes -- 540 while a tune plays, and a gap of twice
        // that means an interrupt went missing -- and how far the value moves
        // when it does, where a jump near 256 would be the firmware's sum of
        // four voices wrapping through the byte.
        const uint64_t steps = _wcstoui64(token.substr(4).c_str(), nullptr, 10);
        uint8_t last = machine->debug_dac();
        uint64_t lastAt = machine->cycles();
        uint64_t seen = machine->debug_dac_writes();
        uint64_t writes = 0;
        uint64_t gaps[8] = {};   // by multiple of the 540 cycle period
        uint64_t jumps[9] = {};  // by size, 32 apart
        uint64_t worst = 0;
        for (uint64_t step = 0; step < steps; ++step) {
          if (!machine->Step() && machine->powered_off()) break;
          if (machine->debug_dac_writes() == seen) continue;
          seen = machine->debug_dac_writes();
          const uint8_t now = machine->debug_dac();
          ++writes;
          const uint64_t span = machine->cycles() - lastAt;
          gaps[std::min<uint64_t>(span / 540, 7)]++;
          const unsigned jump =
              static_cast<unsigned>(std::abs(int(now) - int(last)));
          jumps[std::min<unsigned>(jump / 32, 8)]++;
          if (jump > worst) worst = jump;
          last = now;
          lastAt = machine->cycles();
        }
        std::printf("%-10ls -> [zapisov=%llu, rozostupy po 540 cykloch:",
                    token.c_str(), static_cast<unsigned long long>(writes));
        for (unsigned i = 0; i < 8; ++i)
          if (gaps[i]) std::printf(" %ux=%llu", i,
                                   static_cast<unsigned long long>(gaps[i]));
        std::printf(", skoky po 32:");
        for (unsigned i = 0; i < 9; ++i)
          if (jumps[i]) std::printf(" %ux=%llu", i,
                                    static_cast<unsigned long long>(jumps[i]));
        std::printf(", najvacsi %llu]\n",
                    static_cast<unsigned long long>(worst));
        continue;
      }
      if (token.starts_with(L"wav:")) {
        // The samples themselves, on disk.  "zvuk" answers whether the
        // loudspeaker moved at all, which is enough to tell sound from
        // silence and nothing else; a crackle is a shape, and a shape has to
        // be looked at.  This is the machine's own output at kAudioHz with
        // nothing of the host's in it -- no device, no queue, no clock being
        // steered -- so a defect that survives into the file is in the model,
        // and one that does not is in the real time path (HANDOFF 6.37).
        const std::vector<int16_t> samples = machine->TakeAudio();
        const std::wstring path = token.substr(4);
        FILE* out = _wfopen(path.c_str(), L"wb");
        if (!out) {
          std::printf("%-10ls -> [nepodarilo sa zapisat]\n", token.c_str());
          continue;
        }
        const uint32_t rate = EurekaMachine::kAudioHz;
        const uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
        auto put32 = [out](uint32_t value) { std::fwrite(&value, 4, 1, out); };
        auto put16 = [out](uint16_t value) { std::fwrite(&value, 2, 1, out); };
        std::fwrite("RIFF", 1, 4, out);
        put32(36 + bytes);
        std::fwrite("WAVEfmt ", 1, 8, out);
        put32(16);
        put16(1);
        put16(1);
        put32(rate);
        put32(rate * 2);
        put16(2);
        put16(16);
        std::fwrite("data", 1, 4, out);
        put32(bytes);
        std::fwrite(samples.data(), 1, bytes, out);
        std::fclose(out);
        std::printf("%-10ls -> [vzoriek=%zu, %.2f s pri %u Hz]\n", token.c_str(),
                    samples.size(),
                    double(samples.size()) / double(rate), unsigned(rate));
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
      // Fingers landing on, and lifting off, one key of the membrane
      // keyboard at a time -- what kXX and PressBraille cannot say, because
      // both of those are a finished chord.  "+b1".."+b6" and "-b1".."-b6"
      // hold or release one dot on row 89h, "+bs"/"-bs" the space bar
      // (89h bit 7), "+bh"/"-bh" shift (8Ch bit 6, through HoldShift --
      // shift is not part of HoldMembrane's own state, see machine.h),
      // "+f1".."+f8"/"-f1".."-f8" one function key on row 8Ah, and
      // "+ku"/"+kd"/"+kl"/"+kr" (and the matching "-...") one cursor bit on
      // row 8Ch.  "-b" on its own releases everything at once -- dots,
      // space, function keys, cursors and shift -- the way a hand lifting
      // off the whole keyboard would.  Falls through to the same
      // run-and-report tail as kXX below, so what the ROM says as the chord
      // is built is exactly what a real "seq" run hears.
      if (token.size() >= 2 &&
          (token[0] == L'+' || token[0] == L'-')) {
        const bool down = token[0] == L'+';
        const std::wstring rest = token.substr(1);
        bool matched = true;
        if (rest.size() == 2 && rest[0] == L'b' && rest[1] >= L'1' &&
            rest[1] <= L'6') {
          static const uint8_t kDots[] = {hw::kBkbDot1, hw::kBkbDot2,
                                          hw::kBkbDot3, hw::kBkbDot4,
                                          hw::kBkbDot5, hw::kBkbDot6};
          const uint8_t bit = kDots[rest[1] - L'1'];
          if (down) heldRow0 |= bit;
          else heldRow0 &= static_cast<uint8_t>(~bit);
        } else if (rest == L"bs") {
          if (down) heldRow0 |= hw::kBkbSpace;
          else heldRow0 &= static_cast<uint8_t>(~hw::kBkbSpace);
        } else if (rest == L"bh") {
          machine->HoldShift(down);
        } else if (rest.size() == 2 && rest[0] == L'f' && rest[1] >= L'1' &&
                  rest[1] <= L'8') {
          const uint8_t bit = static_cast<uint8_t>(1u << (rest[1] - L'1'));
          if (down) heldRow1 |= bit;
          else heldRow1 &= static_cast<uint8_t>(~bit);
        } else if (rest == L"ku" || rest == L"kd" || rest == L"kl" ||
                  rest == L"kr") {
          const uint8_t bit = rest == L"ku" ? 1 : rest == L"kd" ? 2
                                              : rest == L"kl"    ? 4
                                                                  : 8;
          if (down) heldRow2 |= bit;
          else heldRow2 &= static_cast<uint8_t>(~bit);
        } else if (!down && rest == L"b") {
          heldRow0 = heldRow1 = heldRow2 = 0;
          machine->HoldShift(false);
        } else {
          matched = false;
        }
        if (matched) {
          machine->HoldMembrane(heldRow0, heldRow1, heldRow2);
          const bool blocked = RunUntilPrompt(*machine, budget);
          std::printf("%-10ls -> %s%s\n", token.c_str(),
                      Readable(machine->TakeSpeechInput()).c_str(),
                      blocked ? "" : "  [nezastavil sa na vstupe]");
          continue;
        }
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
        // A line an "@" is about to time goes in and the clock starts at
        // once.  Waiting for quiet first would swallow the answer, or count
        // half a second of it twice.
        if (index + 1 < argc && argv[index + 1][0] == L'@') {
          machine->TakeConsoleOutput();
          sentAt = machine->cycles();
          std::printf("%-10ls -> [odoslane]\n", token.c_str());
          continue;
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
