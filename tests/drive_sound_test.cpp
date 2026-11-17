// The drive sound (HANDOFF 6.54): when the drive plays what, with no ROM and
// no sound device.
//
// Its failures are the quiet kind.  A drive that never waits for the DAC talks
// over the machine's voice; a motor that never stops hums under every prompt
// from then on; a set of the user's own sounds that fails to load is replaced
// by the built-in one and looks exactly like success.  None of that breaks
// anything a ROM test would notice, so it is held here.
//
// The recordings are not used.  The test builds its own set of WAVs, each one
// a constant level -- spin-up 8000, loop 4000, run-down 12000, raking 16000, a
// step 20000 -- so what comes out of the mixer says which of them is playing.
// Levels are compared with the loop's, not with numbers: the gain is a matter
// of taste (ea4-tvd.3) and must be free to change without this noticing.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "drive_sound.h"

namespace {

int checks = 0;
int failures = 0;

void Check(bool passed, const std::string& name, const std::string& detail = "") {
  ++checks;
  if (!passed) ++failures;
  std::cout << (passed ? "  ok   " : "  CHYBA ") << name;
  if (!passed && !detail.empty()) std::cout << ": " << detail;
  std::cout << "\n";
}

constexpr uint32_t kRate = EurekaMachine::kAudioHz;
constexpr uint64_t kCyclesPerSample = EurekaMachine::kCpuHz / EurekaMachine::kAudioHz;
// The test step is five times the loop, and a single step plays at the
// set's own level (kStepGain in drive_sound.cpp, 1 since the recordings of a
// real Eureka), so loop and step together stand at six loops.
constexpr double kLoopAndStep = 1 + 5 * 1.0;

std::size_t At(double seconds) { return static_cast<std::size_t>(seconds * kRate); }

// A mono 16-bit WAV of `seconds` at a constant `level`.
std::vector<uint8_t> Wav(double seconds, int16_t level, uint32_t rate = kRate) {
  const uint32_t samples = static_cast<uint32_t>(seconds * rate);
  const uint32_t bytes = samples * 2;
  std::vector<uint8_t> out;
  auto put = [&out](const void* data, std::size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
  };
  auto put32 = [&put](uint32_t v) { put(&v, 4); };
  auto put16 = [&put](uint16_t v) { put(&v, 2); };
  put("RIFF", 4);
  put32(36 + bytes);
  put("WAVEfmt ", 8);
  put32(16);
  put16(1);
  put16(1);
  put32(rate);
  put32(rate * 2);
  put16(2);
  put16(16);
  put("data", 4);
  put32(bytes);
  for (uint32_t i = 0; i < samples; ++i) put16(static_cast<uint16_t>(level));
  return out;
}

// A steady motor of `turns` revolutions of kTurn samples each: a 1 ms pulse at
// the start of every revolution, for its rhythm, then a level of its own,
// 1000 + 100 per revolution, so the mixer's output says which one is playing.
constexpr std::size_t kTurn = 9504;  // 198 ms, a whole number of 1 ms blocks
constexpr int16_t kPulse = 16000;

std::vector<uint8_t> Revolutions(std::size_t turns) {
  std::vector<uint8_t> out = Wav(static_cast<double>(turns * kTurn) / kRate, 0);
  for (std::size_t i = 0; i < turns * kTurn; ++i) {
    const int16_t level =
        i % kTurn < kRate / 1000 ? kPulse : static_cast<int16_t>(1000 + 100 * (i / kTurn));
    std::memcpy(&out[44 + 2 * i], &level, 2);
  }
  return out;
}

// The test set; `rate` lets one file be broken on purpose.
DriveSound::Fetch TestSet(uint32_t loopRate = kRate, bool withStep = true) {
  return [=](const std::string& stem, std::vector<uint8_t>& bytes) {
    if (stem == "motor-rozbeh") bytes = Wav(0.1, 8000);
    else if (stem == "motor-slucka") bytes = Wav(0.2, 4000, loopRate);
    else if (stem == "motor-dobeh") bytes = Wav(0.1, 12000);
    else if (stem == "presun") bytes = Wav(0.5, 16000);
    else if (stem == "krok-1" && withStep) bytes = Wav(0.05, 20000);
    else return false;
    return true;
  };
}

EurekaMachine::DriveEvent Seek(uint8_t from, uint8_t to, double at) {
  return {EurekaMachine::DriveEvent::Kind::kSeek,
          static_cast<uint64_t>(at * kRate) * kCyclesPerSample, from, to, 0, 0, 0};
}

// Runs `seconds` of audio starting with `input` (silence past its end) and
// the events, in one call.
std::vector<int16_t> Run(DriveSound& drive, const std::vector<EurekaMachine::DriveEvent>& events,
                         double seconds, const std::vector<int16_t>& input = {}) {
  std::vector<int16_t> audio(At(seconds), 0);
  for (std::size_t i = 0; i < input.size() && i < audio.size(); ++i) audio[i] = input[i];
  drive.Mix(events, audio, audio.size() * kCyclesPerSample);
  return audio;
}

// The mixer truncates to int16, so the loop's level comes out a hair low and
// a multiple of it a few counts lower still.  A relative margin keeps that out
// of the way while the levels told apart here differ by a whole loop's level.
bool Near(double value, double expected) {
  return std::abs(value - expected) <= 3.0 + 0.003 * std::abs(expected);
}

std::string Show(double value, double expected) {
  return std::to_string(static_cast<int>(value)) + " namiesto " +
         std::to_string(static_cast<int>(expected));
}

void LoadsAndRefuses() {
  {
    DriveSound drive;
    std::string error;
    Check(drive.LoadSamples(TestSet(), error) && drive.sampled(), "uplna sada sa nacita", error);
  }
  {
    DriveSound drive;
    std::string error;
    const bool loaded = drive.LoadSamples(TestSet(44100), error);
    Check(!loaded && !drive.sampled(), "sada s 44,1 kHz sa odmietne");
    Check(error.find("motor-slucka.wav") != std::string::npos, "dovod menuje subor", error);
  }
  {
    DriveSound drive;
    std::string error;
    const bool loaded = drive.LoadSamples(TestSet(kRate, false), error);
    Check(!loaded && !drive.sampled(), "sada bez kroku sa odmietne");
    Check(error.find("krok-1.wav") != std::string::npos, "dovod menuje krok-1.wav", error);
  }
}

// One step at the start, nothing talking: spin-up, loop, the step on top of
// the loop once the spindle is up (6 index pulses, 1.2 s), then the motor
// held 9 index pulses (1.8 s) after the last work and run down.
void SpinsUpStepsAndStops() {
  DriveSound drive;
  std::string error;
  drive.LoadSamples(TestSet(), error);
  const auto out = Run(drive, {Seek(0, 1, 0.0)}, 3.6);
  Check(out[At(0.01)] != 0, "motor sa rozbehne hned");
  const double loop = out[At(0.5)];
  Check(loop > 0, "slucka motora hra", std::to_string(loop));
  Check(Near(out[At(0.05)], 2 * loop), "najprv hra rozbeh", Show(out[At(0.05)], 2 * loop));
  Check(Near(out[At(1.15)], loop), "pred roztocenim krok nepadne", Show(out[At(1.15)], loop));
  Check(Near(out[At(1.22)], kLoopAndStep * loop), "po 1,2 s krok na slucke", Show(out[At(1.22)], kLoopAndStep * loop));
  Check(Near(out[At(2.9)], loop), "motor bezi este 1,8 s po kroku", Show(out[At(2.9)], loop));
  Check(Near(out[At(3.08)], 3 * loop), "potom dobeh", Show(out[At(3.08)], 3 * loop));
  Check(out[At(3.4)] == 0 && drive.Idle(), "a ticho, mechanika stoji");
}

// The machine talks for two seconds: nothing of the drive may be heard until
// it stops, and then the drive starts as from rest.
void WaitsForTheDac() {
  DriveSound drive;
  std::string error;
  drive.LoadSamples(TestSet(), error);
  const std::vector<int16_t> speech(At(2.0), 10000);
  const auto out = Run(drive, {Seek(0, 1, 0.0)}, 4.0, speech);
  bool untouched = true;
  for (std::size_t i = 0; i < At(2.0); ++i) untouched = untouched && out[i] == 10000;
  Check(untouched, "kym hra DAC, mechanika mlci");
  const double loop = out[At(2.6)];
  Check(loop > 0, "po reci sa motor rozbehne");
  Check(Near(out[At(2.1)], 2 * loop), "od rozbehu", Show(out[At(2.1)], 2 * loop));
  Check(Near(out[At(3.27)], kLoopAndStep * loop), "a krok az po roztoceni", Show(out[At(3.27)], kLoopAndStep * loop));
}

// A seek over many cylinders is heard as the raking, for as long as its
// steps take: 40 steps at 6 ms.
// The faithful drive (HANDOFF 6.54, step 4c): the events come from a timed
// controller, so they are played when they happened.  What tells it from the
// backdrop above is what the backdrop adds of its own: no 1.2 s spin-up in
// front of the first step (the controller has already waited it out) and no
// waiting for the DAC (the firmware does not touch the disk while it talks,
// so a step heard under speech is the machine's).  The motor still runs down
// 1.8 s after the last thing it did.
void PlaysInRealTime() {
  DriveSound drive;
  std::string error;
  drive.LoadSamples(TestSet(), error);
  drive.SetRealTime(true);
  std::vector<int16_t> speech(At(1.0), 10000);
  const auto out = Run(drive, {Seek(0, 1, 0.3)}, 3.0, speech);
  // The motor starts with the seek, so its loop is measured once the speech
  // is over; at the seek it is still in its spin-up, at twice the loop.
  const double loop = out[At(2.0)];
  Check(loop > 0, "motor bezi este po 1,7 s", std::to_string(loop));
  Check(out[At(0.2)] == 10000, "pred prvou udalostou mechanika mlci",
        std::to_string(out[At(0.2)]));
  const double spinUpAndStep = 2 * loop + (kLoopAndStep - 1) * loop;
  Check(Near(out[At(0.32)] - 10000, spinUpAndStep), "krok hned a aj pod recou",
        Show(out[At(0.32)] - 10000, spinUpAndStep));
  Check(out[At(2.4)] == 0, "a po 1,8 s a dobehu stoji", std::to_string(out[At(2.4)]));
}

void RakesAcrossCylinders() {
  DriveSound drive;
  std::string error;
  drive.LoadSamples(TestSet(), error);
  const auto out = Run(drive, {Seek(0, 40, 0.0)}, 1.6);
  const double loop = out[At(1.0)];
  Check(Near(out[At(1.3)], 5 * loop), "hrabanie pocas presunu", Show(out[At(1.3)], 5 * loop));
  Check(Near(out[At(1.5)], loop), "po 40 krokoch dozvoni", Show(out[At(1.5)], loop));
}

// A long enough steady motor is never looped (HANDOFF 6.54): it plays in
// random runs of 5 to 15 whole revolutions, and every seam sits on a
// revolution boundary.  A loop of any length was heard repeating, and a seam
// off the boundary would break the spindle's rhythm -- neither is anything a
// level check would see, so both are read off the pulses and levels here.
void ShufflesTheMotor() {
  const auto set = [](const std::string& stem, std::vector<uint8_t>& bytes) {
    if (stem == "motor-rozbeh") bytes = Wav(0.1, 8000);
    else if (stem == "motor-slucka") bytes = Revolutions(30);
    else if (stem == "motor-dobeh") bytes = Wav(0.1, 12000);
    else if (stem == "presun") bytes = Wav(0.5, 0);
    else if (stem == "krok-1") bytes = Wav(0.05, 0);  // silent, not to blur the levels
    else return false;
    return true;
  };
  std::vector<EurekaMachine::DriveEvent> events;
  // A step back and forth every second keeps the motor running throughout.
  for (int i = 0; i < 30; ++i)
    events.push_back(Seek(static_cast<uint8_t>(i % 2), static_cast<uint8_t>((i + 1) % 2), i));
  std::string error;
  DriveSound drive;
  drive.LoadSamples(set, error);
  const auto out = Run(drive, events, 30.0);

  const int threshold = kPulse / 8;  // the set's gain of a quarter, and half that
  std::vector<std::size_t> pulses;
  for (std::size_t i = At(0.5); i < At(29.0); ++i)
    if (out[i] >= threshold && out[i - 1] < threshold) pulses.push_back(i);
  bool even = pulses.size() > 100;
  for (std::size_t i = 1; i < pulses.size(); ++i)
    even = even && pulses[i] - pulses[i - 1] + 2 >= kTurn && pulses[i] - pulses[i - 1] <= kTurn + 2;
  Check(even, "rytmus otacok drzi aj cez svy", std::to_string(pulses.size()) + " pulzov");

  // Which revolution each one was, from its level half a revolution in --
  // past the crossfade -- and the runs of revolutions that follow on.
  std::vector<int> played;
  for (std::size_t p : pulses)
    played.push_back(static_cast<int>(std::lround((out[p + kTurn / 2] * 4.0 - 1000) / 100)));
  std::vector<int> runs{1};
  for (std::size_t i = 1; i < played.size(); ++i) {
    if (played[i] == played[i - 1] + 1) ++runs.back();
    else runs.push_back(1);
  }
  bool lengths = runs.size() > 10;
  for (std::size_t i = 1; i + 1 < runs.size(); ++i) lengths = lengths && runs[i] >= 5 && runs[i] <= 15;
  std::string shown;
  for (int r : runs) shown += std::to_string(r) + " ";
  Check(lengths, "motor sa neopakuje, hra behy 5 az 15 otacok", shown);

  DriveSound again;
  again.LoadSamples(set, error);
  Check(Run(again, events, 30.0) == out, "nahodne behy su pri rovnakych udalostiach rovnake");
}

// The motor stopping in the middle of a seam must not cut the run the seam
// is fading out, nor jump the one fading in to full strength (review of
// 16. 11. 2026).  With the whole motor at one level and the stop silent,
// every fade is a smooth slope and any cut is a step of hundreds; the stop
// is moved 10 ms at a time across a second and a half, so some of the stops
// land in a seam.
void StopsCleanlyMidSeam() {
  constexpr int16_t kLevel = 8000;
  const auto set = [](const std::string& stem, std::vector<uint8_t>& bytes) {
    if (stem == "motor-rozbeh") bytes = Wav(0.1, kLevel);
    else if (stem == "motor-slucka") bytes = Wav(30 * 0.198, kLevel);
    else if (stem == "motor-dobeh") bytes = Wav(0.1, 0);
    else if (stem == "presun") bytes = Wav(0.5, 0);
    else if (stem == "krok-1") bytes = Wav(0.05, 0);
    else return false;
    return true;
  };
  int worst = 0;
  double worstAt = 0;
  std::size_t worstSample = 0;
  for (int i = 0; i < 150; ++i) {
    const double second = 1.3 + 0.01 * i;
    DriveSound drive;
    std::string error;
    drive.LoadSamples(set, error);
    const auto out = Run(drive, {Seek(0, 1, 0.0), Seek(1, 0, second)}, second + 2.5);
    // From the second sample: the motor switched on from silence starts at
    // once, the spin-up's own fade in is in its recording.
    for (std::size_t s = 2; s < out.size(); ++s) {
      const int jump = std::abs(out[s] - out[s - 1]);
      if (jump > worst) {
        worst = jump;
        worstAt = second;
        worstSample = s;
      }
    }
  }
  Check(worst < 100, "zastavenie uprostred svu necvakne",
        "skok " + std::to_string(worst) + " pri druhom kroku v " + std::to_string(worstAt) + " s, vzorka " +
            std::to_string(worstSample));
}

// Switched off, the machine gives no samples; Continue has to let the drive
// finish on its own, or the motor would hum on into the next power-up.
void FinishesWithoutTheMachine() {
  DriveSound drive;
  std::string error;
  drive.LoadSamples(TestSet(), error);
  Run(drive, {Seek(0, 1, 0.0)}, 1.3);
  Check(!drive.Idle(), "uprostred prace mechanika nestoji");
  std::vector<int16_t> tail(At(3.0), 0);
  drive.Continue(tail);
  Check(drive.Idle(), "bez stroja dobehne az do ticha");
}

// The noise has a fixed seed, so a run can be compared with the next one --
// which is what tuning by ear depends on.
void SameEventsSameSound() {
  DriveSound first;
  DriveSound second;
  const auto a = Run(first, {Seek(0, 1, 0.0), Seek(1, 30, 0.5)}, 4.0);
  const auto b = Run(second, {Seek(0, 1, 0.0), Seek(1, 30, 0.5)}, 4.0);
  Check(a == b, "syntetizovany zvuk je pri rovnakych udalostiach rovnaky");
  bool any = false;
  for (int16_t s : a) any = any || s != 0;
  Check(any, "a vobec nieco hra");
}

}  // namespace

int main() {
  LoadsAndRefuses();
  SpinsUpStepsAndStops();
  WaitsForTheDac();
  PlaysInRealTime();
  RakesAcrossCylinders();
  ShufflesTheMotor();
  StopsCleanlyMidSeam();
  FinishesWithoutTheMachine();
  SameEventsSameSound();
  std::cout << (failures == 0 ? "PASS" : "FAIL") << " mode=DRIVE kontrol=" << checks
            << " chyb=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
