#include "eureka_session.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

#include "sliders.h"
#include "text_codec.h"

namespace eureka {

namespace {

std::wstring Decoded(const std::vector<uint8_t>& bytes) {
  std::wstring text = DecodeKamenicky(bytes.data(), bytes.size());
  for (wchar_t& ch : text)
    if (ch < 0x20 || ch == 0x7f) ch = L'.';
  return text;
}

std::string ToUtf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring FromUtf8(std::string_view text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size);
  return out;
}

// Each character split into its base letter and marks, the marks dropped.
// Windows does the splitting, as it does for EncodeKamenicky, so there is no
// table here to go out of step.
std::wstring WithoutDiacritics(std::wstring_view text) {
  std::wstring out;
  for (wchar_t ch : text) {
    if (ch < 0x80) {
      out.push_back(ch);
      continue;
    }
    wchar_t parts[16]{};
    const int count = NormalizeString(NormalizationD, &ch, 1, parts,
                                      static_cast<int>(std::size(parts)));
    if (count <= 0) {
      out.push_back(ch);
      continue;
    }
    for (int index = 0; index < count; ++index) {
      WORD type = 0;
      GetStringTypeW(CT_CTYPE3, &parts[index], 1, &type);
      if ((type & (C3_NONSPACING | C3_DIACRITIC)) == 0) out.push_back(parts[index]);
    }
  }
  return out;
}

}  // namespace

std::string Readable(const std::vector<uint8_t>& bytes) {
  return ToUtf8(Decoded(bytes));
}

bool Contains(const std::vector<uint8_t>& bytes, std::string_view text) {
  return WithoutDiacritics(Decoded(bytes)).find(WithoutDiacritics(FromUtf8(text))) !=
         std::wstring::npos;
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

bool Session::TryPowerOff(Budget budget) {
  machine_.PressPowerOffChord();
  const Deadline deadline = Start(budget);
  while (!Expired(deadline) && !machine_.powered_off()) Step();
  return machine_.powered_off();
}

bool Session::TrySettleForSwap(Budget budget) {
  const Deadline deadline = Start(budget);
  bool settled = false;
  while (!Expired(deadline)) {
    if (machine_.DiskSwappable()) {
      settled = true;
      break;
    }
    if (!Step() && machine_.powered_off()) break;
  }
  // Flushed whether or not the drive settled, as the probe always did: a
  // swap that goes ahead anyway should at least not lose what was written.
  std::wstring error;
  machine_.FlushDisk(error);
  return settled;
}

bool Session::InsertFromSlot(int slot) {
  stash_.Put(currentSlot_, machine_.disk());
  currentSlot_ = slot;
  auto kept = stash_.Take(slot);
  if (!kept) return false;
  machine_.InsertDisk(*kept);
  return true;
}

bool Session::ShiftClock(std::chrono::seconds by) {
  clockShift_ += by;
  machine_.SetRtcOffset(clockShift_.count());
  return machine_.powered_off() && WakeOnAlarm();
}

void Session::SetRate(int position) {
  machine_.SetRatePot(sliders::RatePotLevel(position));
}

void Session::SetVolume(int position) {
  machine_.SetVolume(sliders::VolumeGain(position));
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
  const auto heard = [&] { return Contains(speech_, text); };
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
  const auto shown = [&] { return Contains(console_, text); };
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
