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
  Check(Near(out[At(1.22)], 6 * loop), "po 1,2 s krok na slucke", Show(out[At(1.22)], 6 * loop));
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
  Check(Near(out[At(3.27)], 6 * loop), "a krok az po roztoceni", Show(out[At(3.27)], 6 * loop));
}

// A seek over many cylinders is heard as the raking, for as long as its
// steps take: 40 steps at 6 ms.
void RakesAcrossCylinders() {
  DriveSound drive;
  std::string error;
  drive.LoadSamples(TestSet(), error);
  const auto out = Run(drive, {Seek(0, 40, 0.0)}, 1.6);
  const double loop = out[At(1.0)];
  Check(Near(out[At(1.3)], 5 * loop), "hrabanie pocas presunu", Show(out[At(1.3)], 5 * loop));
  Check(Near(out[At(1.5)], loop), "po 40 krokoch dozvoni", Show(out[At(1.5)], loop));
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
  RakesAcrossCylinders();
  FinishesWithoutTheMachine();
  SameEventsSameSound();
  std::cout << (failures == 0 ? "PASS" : "FAIL") << " mode=DRIVE kontrol=" << checks
            << " chyb=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
