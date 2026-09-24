#include "eureka_session.h"

#include <cstdio>

namespace eureka {

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

bool Session::Step() {
  const bool running = machine_.Step();
  Absorb();
  return running;
}

void Session::Absorb() {
  const auto said = machine_.TakeSpeechInput();
  if (!said.empty()) {
    speech_.insert(speech_.end(), said.begin(), said.end());
    speechTotal_ += said.size();
  }
  const auto shown = machine_.TakeConsoleOutput();
  if (!shown.empty()) {
    console_.insert(console_.end(), shown.begin(), shown.end());
    consoleTotal_ += shown.size();
  }
}

void Session::ClearOutput() {
  speech_.clear();
  console_.clear();
}

void Session::Reset() {
  machine_.Reset();
  ClearOutput();
}

void Session::PowerOn() {
  machine_.PowerOn();
  ClearOutput();
}

bool Session::WakeOnAlarm() {
  if (!machine_.WakeOnAlarm()) return false;
  ClearOutput();
  return true;
}

void Session::Pc(pc::Key key) {
  machine_.QueueScanCode(key.make);
  machine_.QueueScanCode(static_cast<uint8_t>(key.make | hw::kScanBreak));
}

bool Session::TryType(std::string_view text, uint8_t* unmapped) {
  return machine_.QueueText(std::string(text), unmapped);
}

void Session::Type(std::string_view text) {
  uint8_t unmapped = 0;
  if (TryType(text, &unmapped)) return;
  char code[8];
  std::snprintf(code, sizeof code, "%02Xh", unsigned(unmapped));
  throw Failure("Type(\"" + std::string(text) + "\"): znak " + code +
                " nie je v tabulkach DF05, DF5E ani DF98, nenapisalo sa nic");
}

void Session::Hold(membrane::Keys keys) {
  held_ = held_ | keys;
  if (keys.shift) machine_.HoldShift(true);
  machine_.HoldMembrane(held_.dots, held_.fn, held_.cursor);
}

void Session::Release(membrane::Keys keys) {
  held_.dots &= static_cast<uint8_t>(~keys.dots);
  held_.fn &= static_cast<uint8_t>(~keys.fn);
  held_.cursor &= static_cast<uint8_t>(~keys.cursor);
  if (keys.shift) {
    held_.shift = false;
    machine_.HoldShift(false);
  }
  machine_.HoldMembrane(held_.dots, held_.fn, held_.cursor);
}

void Session::ReleaseAll() {
  held_ = membrane::Keys{};
  machine_.HoldShift(false);
  machine_.HoldMembrane(0, 0, 0);
}

Session::Deadline Session::Start(Budget budget) const {
  if (budget.counts_instructions())
    return {true, machine_.instructions() + budget.amount()};
  return {false, machine_.cycles() + budget.amount()};
}

bool Session::Expired(const Deadline& deadline) const {
  return deadline.instructions ? machine_.instructions() >= deadline.limit
                               : machine_.cycles() >= deadline.limit;
}

// The probe's RunUntilPrompt, which this replaces, word for word in what it
// does: step 1 must leave the probe's output unchanged (eureka.md).
bool Session::TryWaitIdle(Budget budget, Quiet quiet,
                          std::chrono::microseconds window) {
  const Deadline deadline = Start(budget);
  const uint64_t still = Budget::CyclesOf(window);
  const uint64_t booted = EurekaMachine::kCpuHz * 3;
  uint64_t lastOut = machine_.cycles() > booted ? machine_.cycles() : booted;
  // The clock chip's own supply never cuts, so a switched-off machine can
  // still wake itself: checked once per call, the same poll the run loop does
  // every ~2ms.
  if (machine_.powered_off()) WakeOnAlarm();
  uint64_t console = consoleTotal_;
  uint64_t speech = speechTotal_;
  while (!Expired(deadline)) {
    const bool spoke = quiet == Quiet::kConsoleAndSpeech && speechTotal_ != speech;
    if (consoleTotal_ != console || spoke) lastOut = machine_.cycles();
    console = consoleTotal_;
    speech = speechTotal_;
    if (machine_.cycles() > lastOut + still) return true;
    // A machine that touched pwr_stb has stopped for good: the counters
    // freeze, so the budget would never run out.
    if (!Step() && machine_.powered_off()) return true;
  }
  return false;
}

void Session::WaitIdle(Budget budget, Quiet quiet, std::chrono::microseconds window) {
  const uint64_t since = machine_.cycles();
  if (!TryWaitIdle(budget, quiet, window)) Fail("WaitIdle", since);
}

bool Session::TryWaitSilent(Budget budget, std::chrono::microseconds window) {
  const Deadline deadline = Start(budget);
  const uint64_t still = Budget::CyclesOf(window);
  const uint64_t booted = EurekaMachine::kCpuHz * 3;
  uint64_t lastOut = machine_.cycles() > booted ? machine_.cycles() : booted;
  if (machine_.powered_off()) WakeOnAlarm();
  uint64_t console = consoleTotal_;
  uint64_t speech = speechTotal_;
  uint8_t dac = machine_.debug_dac();
  while (!Expired(deadline)) {
    if (consoleTotal_ != console || speechTotal_ != speech ||
        machine_.debug_dac() != dac)
      lastOut = machine_.cycles();
    console = consoleTotal_;
    speech = speechTotal_;
    dac = machine_.debug_dac();
    if (machine_.cycles() > lastOut + still) return true;
    if (!Step() && machine_.powered_off()) return true;
  }
  return false;
}

void Session::WaitSilent(Budget budget, std::chrono::microseconds window) {
  const uint64_t since = machine_.cycles();
  if (!TryWaitSilent(budget, window)) Fail("WaitSilent", since);
}

// Looked at again only when something new has arrived; the buffer is short,
// but this runs once per instruction.
bool Session::TryWaitSaid(std::string_view text, Budget budget) {
  const Deadline deadline = Start(budget);
  const auto heard = [&] { return Readable(speech_).find(text) != std::string::npos; };
  bool found = heard();
  while (!found && !Expired(deadline)) {
    const uint64_t before = speechTotal_;
    if (!Step() && machine_.powered_off()) break;
    if (speechTotal_ != before) found = heard();
  }
  return found;
}

void Session::WaitSaid(std::string_view text, Budget budget) {
  const uint64_t since = machine_.cycles();
  if (!TryWaitSaid(text, budget))
    Fail("WaitSaid(\"" + std::string(text) + "\")", since);
}

bool Session::TryWaitConsole(std::string_view text, Budget budget,
                             uint64_t* programCycles) {
  const Deadline deadline = Start(budget);
  const auto shown = [&] { return Readable(console_).find(text) != std::string::npos; };
  bool found = shown();
  while (!found && !Expired(deadline)) {
    const bool inProgram = machine_.pc() < 0xc000 &&
                           machine_.physical_pc() >= EurekaMachine::kRamBase;
    const uint64_t before = machine_.cycles();
    const uint64_t already = consoleTotal_;
    if (!Step() && machine_.powered_off()) break;
    if (inProgram && programCycles != nullptr)
      *programCycles += machine_.cycles() - before;
    if (consoleTotal_ != already) found = shown();
  }
  return found;
}

void Session::WaitConsole(std::string_view text, Budget budget) {
  const uint64_t since = machine_.cycles();
  if (!TryWaitConsole(text, budget))
    Fail("WaitConsole(\"" + std::string(text) + "\")", since);
}

std::vector<uint8_t> Session::TakeSpeech() {
  std::vector<uint8_t> out;
  out.swap(speech_);
  return out;
}

std::vector<uint8_t> Session::TakeConsole() {
  std::vector<uint8_t> out;
  out.swap(console_);
  return out;
}

// Everything needed to understand the failure without running it again.
void Session::Fail(const std::string& what, uint64_t sinceCycles) {
  char where[160];
  std::snprintf(where, sizeof where,
                "po %.2f s emulovaneho casu, PC %05X, stroj %s",
                (machine_.cycles() - sinceCycles) / double(EurekaMachine::kCpuHz),
                unsigned(machine_.physical_pc()),
                machine_.powered_off() ? "vypnuty" : "bezi");
  throw Failure(what + " sa nedockal " + where + "\n  rec: \"" +
                Readable(speech_) + "\"\n  konzola: \"" + Readable(console_) +
                "\"");
}

}  // namespace eureka
