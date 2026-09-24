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

namespace {

// FFh while the synthesiser is speaking and only then: set at 002D5 when a
// batch starts, cleared at 0055E when it has been said (HANDOFF, spabrt;
// SYSRAM.A has the word spabrt at C620h, this is its high byte).  New speech
// bytes arrive a whole sentence at a time, so without this a second of
// talking looked like a second of silence and a wait ended between the
// hour and the minutes of the clock's announcement.
constexpr uint16_t kSpeaking = 0xc621;

// The ROM's "is there a key?" check (logical D675h, slot 2 of the check table
// at EBDDh).  Both ways the firmware waits for input run through it: the
// blocking event dispatcher at 19B41-19B6C and the non-blocking poll at
// 19B6D-19B94 that the clock application calls from its own loop instead.
// While the machine waits it runs every 0.3-0.5 ms; while it speaks, formats,
// boots, or sits in the delay loop at 19CD9-19CEC after "ahoj", it does not
// run at all.  The PC alone cannot say this -- the clock never enters the
// dispatcher.  Measured 24. 9. 2026 by sending keys: F10 at the moment this
// holds after Escape out of the clock says "hlavní menu"; 300 ms after "ahoj"
// it is lost without a word (eureka.md).
constexpr uint32_t kKeyCheck = 0x18675;
// The key event the check looks at; non-zero while a key waits to be taken.
// SYSRAM.A does not name it; D675h reads it.
constexpr uint16_t kKeyPending = 0xc677;
// Longest gap between two checks in a real wait was 0.54 ms (the clock), and
// the longest run of checks outside one 4.6 ms (a pass through the dispatcher
// right after Escape): hence 2 ms and 20 ms.
constexpr std::chrono::microseconds kKeyCheckGap = 2ms;
constexpr std::chrono::microseconds kKeyCheckRun = 20ms;

}  // namespace

bool Session::Speaking() const { return Peek(kSpeaking) == 0xff; }

bool Session::WaitingForKey() const {
  // A key still in the machine's queues counts as pending too.  A safeguard,
  // not a measured need: a press lasts 120 ms (kPressMs), but the ROM has the
  // key in C677h well within the 20 ms below, and a typed line goes in faster
  // than that too -- with this line taken out, the session mode still passes,
  // typing included (24. 9. 2026).  Kept because it costs nothing and a
  // slower delivery path would otherwise end a wait before its key landed.
  if (machine_.queued_keys() != 0) return false;
  if (!keyChecked_ || Peek(kKeyPending) != 0) return false;
  const uint64_t now = machine_.cycles();
  return now - keyCheckLast_ <= Budget::CyclesOf(kKeyCheckGap) &&
         keyCheckLast_ - keyCheckSince_ >= Budget::CyclesOf(kKeyCheckRun);
}

bool Session::Step() {
  if (machine_.physical_pc() == kKeyCheck) {
    const uint64_t now = machine_.cycles();
    if (!keyChecked_ || now - keyCheckLast_ > Budget::CyclesOf(kKeyCheckGap))
      keyCheckSince_ = now;
    keyChecked_ = true;
    keyCheckLast_ = now;
  }
  const bool running = machine_.Step();
  Absorb();
  if (!watches_.empty()) CheckWatches();
  return running;
}

void Session::Absorb() {
  const auto said = machine_.TakeSpeechInput();
  if (!said.empty()) {
    speech_.insert(speech_.end(), said.begin(), said.end());
    speechLog_.insert(speechLog_.end(), said.begin(), said.end());
    speechTotal_ += said.size();
  }
  const auto shown = machine_.TakeConsoleOutput();
  if (!shown.empty()) {
    console_.insert(console_.end(), shown.begin(), shown.end());
    consoleLog_.insert(consoleLog_.end(), shown.begin(), shown.end());
    consoleTotal_ += shown.size();
  }
}

void Session::AbsorbAudio() {
  const auto played = machine_.TakeAudio();
  if (played.empty()) return;
  audio_.insert(audio_.end(), played.begin(), played.end());
  if (activeRecordings_ > 0)
    recordAudio_.insert(recordAudio_.end(), played.begin(), played.end());
}

// The machine clears its own buffers on a power-up; what was not taken yet is
// gone there, and so here.  The audio is pulled first so that a recording
// running across the reset keeps what was played before it.
void Session::ClearOutput() {
  speech_.clear();
  console_.clear();
  audio_.clear();
  keyChecked_ = false;
}

void Session::Reset() {
  AbsorbAudio();
  machine_.Reset();
  ClearOutput();
}

void Session::PowerOn() {
  AbsorbAudio();
  machine_.PowerOn();
  ClearOutput();
}

bool Session::WakeOnAlarm() {
  AbsorbAudio();
  if (!machine_.WakeOnAlarm()) return false;
  ClearOutput();
  return true;
}

void Session::CheckWatches() {
  // By index: a callback may add or remove a watch.
  for (std::size_t index = 0; index < watches_.size(); ++index) {
    const uint8_t now = Peek(watches_[index].address);
    if (now == watches_[index].last) continue;
    const uint8_t before = watches_[index].last;
    watches_[index].last = now;
    const auto changed = watches_[index].changed;
    changed(before, now);
  }
}

int Session::Watch(uint16_t address, std::function<void(uint8_t, uint8_t)> changed) {
  const int handle = nextWatch_++;
  watches_.push_back({handle, address, Peek(address), std::move(changed)});
  return handle;
}

void Session::Unwatch(int handle) {
  std::erase_if(watches_, [handle](const Watcher& w) { return w.handle == handle; });
}

bool Session::Run(Budget budget) {
  const Deadline deadline = Start(budget);
  while (!Expired(deadline))
    if (!Step() && machine_.powered_off()) return false;
  return true;
}

bool Session::TryWaitUntil(const std::function<bool()>& done, Budget budget) {
  const Deadline deadline = Start(budget);
  while (!Expired(deadline)) {
    if (done()) return true;
    if (!Step() && machine_.powered_off()) break;
  }
  return done();
}

void Session::WaitUntil(const std::function<bool()>& done, Budget budget,
                        std::string_view what) {
  const uint64_t since = machine_.cycles();
  if (!TryWaitUntil(done, budget)) Fail(std::string(what), since);
}

std::vector<int16_t> Session::TakeAudio() {
  AbsorbAudio();
  std::vector<int16_t> out;
  out.swap(audio_);
  return out;
}

RecordingMark Session::StartRecording() {
  AbsorbAudio();
  ++activeRecordings_;
  return {speechLog_.size(), consoleLog_.size(),
          recordAudioBase_ + recordAudio_.size()};
}

Recording Session::StopRecording(const RecordingMark& mark) {
  AbsorbAudio();
  Recording out;
  out.speech.assign(speechLog_.begin() + mark.speech, speechLog_.end());
  out.console.assign(consoleLog_.begin() + mark.console, consoleLog_.end());
  const std::size_t from = mark.audio - recordAudioBase_;
  out.audio.assign(recordAudio_.begin() + from, recordAudio_.end());
  if (--activeRecordings_ == 0) {
    recordAudioBase_ += recordAudio_.size();
    recordAudio_.clear();
  }
  return out;
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

bool Session::TryWaitIdle(Budget budget, Quiet quiet) {
  const Deadline deadline = Start(budget);
  // The clock chip's own supply never cuts, so a switched-off machine can
  // still wake itself: checked once per call, the same poll the run loop does
  // every ~2ms.
  if (machine_.powered_off()) WakeOnAlarm();
  if (quiet == Quiet::kKeyPrompt) {
    // Whatever the check saw before this call is history: the key that was
    // just queued has not been looked at yet.
    keyChecked_ = false;
    while (!Expired(deadline)) {
      if (WaitingForKey()) return true;
      if (!Step() && machine_.powered_off()) return true;
    }
    return false;
  }
  // The probe's RunUntilPrompt, word for word in what it does: the probe's
  // output must not change (eureka.md, step 1).  The first three seconds after
  // a reset do not count as quiet -- the machine is silent while it boots.
  const uint64_t still = EurekaMachine::kCpuHz / 2;
  const uint64_t booted = EurekaMachine::kCpuHz * 3;
  uint64_t lastOut = machine_.cycles() > booted ? machine_.cycles() : booted;
  uint64_t console = consoleTotal_;
  while (!Expired(deadline)) {
    if (consoleTotal_ != console) lastOut = machine_.cycles();
    console = consoleTotal_;
    if (machine_.cycles() > lastOut + still) return true;
    // A machine that touched pwr_stb has stopped for good: the counters
    // freeze, so the budget would never run out.
    if (!Step() && machine_.powered_off()) return true;
  }
  return false;
}

void Session::WaitIdle(Budget budget, Quiet quiet) {
  const uint64_t since = machine_.cycles();
  if (!TryWaitIdle(budget, quiet)) Fail("WaitIdle", since);
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
    if (consoleTotal_ != console || speechTotal_ != speech || Speaking() ||
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
