#include "drive_sound.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "eureka_io.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kRate = EurekaMachine::kAudioHz;
// Exactly 128, so a cycle maps onto a sample with no drift to correct.
constexpr uint32_t kCyclesPerSample = EurekaMachine::kCpuHz / EurekaMachine::kAudioHz;
static_assert(EurekaMachine::kCpuHz % EurekaMachine::kAudioHz == 0);

constexpr uint32_t Ms(double ms) { return static_cast<uint32_t>(ms * kRate / 1000.0); }

// 300 rpm.  One revolution carries hw::kRawTrackBytes, so a transfer lasts its
// share of one.
constexpr uint32_t kRevolution = Ms(200);
// The WD1772's own timings (data sheet): the motor goes off after 9 index
// pulses with no command, and with the h flag clear -- every command the ROM
// issues (SYSEQU.LIB) -- it waits 6 index pulses for the spindle to come up
// before it does anything.
constexpr uint32_t kMotorHold = 9 * kRevolution;
constexpr uint32_t kSpinUp = 6 * kRevolution;
// Step times for r1r0 on the 1772 -- not the 1770, where 00 is 6 ms too but the
// others differ.
constexpr uint32_t kStepMs[4] = {6, 12, 2, 3};

// The owner remembers it from the real machine (1. 10. 2026): while the Eureka
// played anything on the DAC, speech included, the disk stopped working and
// only the motor ran on; if the speech ended soon enough, or was cut short with
// Shift, the disk carried on before the motor had stopped.  One CPU feeds both,
// so this is what the drive does when the machine is talking.  The threshold
// is well above the coupling filter's settling and well below a click.
constexpr int kDacAudible = 64;
constexpr uint32_t kDacHold = Ms(50);

// Past this much queued work new events are dropped rather than queued: a
// program that hammers the disk while it talks would otherwise build a
// backlog the drive plays for minutes after the machine went quiet.
constexpr uint64_t kMaxBacklog = 120ull * kRate;

// Levels, as fractions of full scale.  To be tuned by ear (ea4-tvd.3).
constexpr float kClickLevel = 0.22f;
// The spindle is a growl, not a hiss.  The owner on the first two tries
// (1. 10. 2026): a loud rub with a deep swell once a revolution sounded like a
// train, and quieter it still sounded like a big hard disk or an eight-inch
// drive with the cover off.  What was always there in the background was the
// motor's low growl; the hiss of the diskette in its jacket was heard only on
// a damaged one or with a loose shutter.  So the rub is all but gone and the
// motor is a buzzy low tone whose pitch follows the spindle's speed -- it
// growls up on a spin-up and down as it coasts.
constexpr float kRubLevel = 0.002f;
constexpr float kRubSwell = 0.2f;
// After the fourth try: the motor was plainly audible on the real machine --
// no ear to the drive needed -- and it growled, roughly.  So another 6 dB, a
// lower and buzzier tone, and a fast flutter: amplitude moving 20-70 times a
// second is what the ear hears as roughness rather than as a beat.
constexpr float kGrowlLevel = 0.0072f;
constexpr double kGrowlHz = 110.0;
constexpr int kGrowlHarmonics = 10;
constexpr float kGrowlSwell = 0.15f;
constexpr double kGrowlRoughHz = 31.0;
constexpr float kGrowlRough = 0.35f;
constexpr double kRubHz = 380.0;
constexpr double kRubQ = 0.8;

// Spindle inertia: up in a few hundred milliseconds, down a little slower.
const float kSpeedUp = static_cast<float>(1.0 - std::exp(-1.0 / (0.15 * kRate)));
const float kSpeedDown = static_cast<float>(1.0 - std::exp(-1.0 / (0.40 * kRate)));

// The recorded set.  One gain for all of it keeps the recordings in the
// proportions they were made in; a quarter puts the motor's loop near where
// the last synthesized growl stood.  To be tuned by ear (ea4-tvd.3).
constexpr float kSampleGain = 0.25f;
// A single step on its own, against the motor.  The step recordings were
// cleaned of the drive they were made on, so at the set's own proportions a
// step stood some 20 dB over the loop and the spindle dropped out from under
// it for a moment -- the owner heard it as a recording cut in (4. 10. 2026).
// Of 1, 0.5 and 0.3 he chose 0.3: the step still stands out, and at 0.5 it
// already seemed to cover the motor.  The raking keeps the set's level.
constexpr float kStepGain = 0.3f;
constexpr uint32_t kCrossfade = Ms(10);
constexpr uint32_t kSeekRelease = Ms(15);

}  // namespace

DriveSound::DriveSound() : seed_(0x2545f491u) {
  // One step of the head: a few damped resonances of the carriage and the
  // frame, struck by a short burst of noise.  Rendered once; every step plays
  // it back at a slightly different strength.
  //
  // Low and woody on purpose.  The first version rang at 1800 and 3900 Hz and
  // the owner heard a hard disk's actuator ticking in it, not a diskette
  // (1. 10. 2026); a stepper's clack sits much lower and rings longer.
  click_.resize(Ms(20));
  float peak = 0.0f;
  for (std::size_t n = 0; n < click_.size(); ++n) {
    const double t = static_cast<double>(n) / kRate;
    const double v = 1.0 * std::exp(-t / 0.0050) * std::sin(2 * kPi * 420 * t) +
                     0.7 * std::exp(-t / 0.0025) * std::sin(2 * kPi * 1100 * t) +
                     0.35 * std::exp(-t / 0.0008) * std::sin(2 * kPi * 2600 * t) +
                     0.4 * std::exp(-t / 0.0003) * Noise();
    click_[n] = static_cast<float>(v);
    peak = std::max(peak, std::abs(click_[n]));
  }
  for (float& sample : click_) sample /= peak;

  // RBJ band-pass, 0 dB at the centre.
  const double w0 = 2 * kPi * kRubHz / kRate;
  const double alpha = std::sin(w0) / (2 * kRubQ);
  const double a0 = 1 + alpha;
  b0_ = static_cast<float>(alpha / a0);
  b2_ = static_cast<float>(-alpha / a0);
  a1_ = static_cast<float>(-2 * std::cos(w0) / a0);
  a2_ = static_cast<float>((1 - alpha) / a0);
}

// xorshift32 with a fixed seed: the same token sequence gives the same WAV,
// which is what makes tuning by ear comparable from one try to the next.
float DriveSound::Noise() {
  seed_ ^= seed_ << 13;
  seed_ ^= seed_ >> 17;
  seed_ ^= seed_ << 5;
  return static_cast<float>(seed_) / 2147483648.0f - 1.0f;
}

void DriveSound::Push(Job::Kind kind, uint64_t notBefore, uint32_t length, uint16_t steps) {
  jobs_.push_back(Job{kind, notBefore, std::max<uint32_t>(length, 1), steps});
  queued_ += jobs_.back().length;
}

void DriveSound::Schedule(const EurekaMachine::DriveEvent& event) {
  if (queued_ > kMaxBacklog) return;
  using Kind = EurekaMachine::DriveEvent::Kind;
  const uint64_t at = event.cycle / kCyclesPerSample;
  // In real time every event already sits where the drive does it: the
  // steps of a seek go one after another from its start, and a spin-up or a
  // transfer only keeps the motor running from that moment.
  if (realTime_) {
    if (event.kind == Kind::kSeek) {
      const int steps = std::abs(static_cast<int>(event.to) - static_cast<int>(event.from));
      for (int i = 0; i < steps; ++i)
        Push(Job::Kind::kStep, at, Ms(kStepMs[event.stepRate & 3]),
             i == 0 ? static_cast<uint16_t>(steps) : 0);
    } else {
      Push(Job::Kind::kTurn, at, 1);
    }
    return;
  }
  // Only a timed controller reports it, and the backdrop has its own.
  if (event.kind == Kind::kSpinUp) return;
  if (event.kind == Kind::kSeek) {
    const int steps = std::abs(static_cast<int>(event.to) - static_cast<int>(event.from));
    for (int i = 0; i < steps; ++i)
      Push(Job::Kind::kStep, at, Ms(kStepMs[event.stepRate & 3]),
           i == 0 ? static_cast<uint16_t>(steps) : 0);
    afterSeek_ = true;
    return;
  }
  // Fresh on a track, the head waits for the sector to come round: half a
  // revolution on average.  Write Track waits for the index hole instead,
  // which on average is the same.
  if (afterSeek_) Push(Job::Kind::kTurn, at, kRevolution / 2);
  afterSeek_ = false;
  const uint64_t length = static_cast<uint64_t>(kRevolution) * event.bytes / hw::kRawTrackBytes;
  Push(Job::Kind::kTurn, at, static_cast<uint32_t>(std::min<uint64_t>(length, kRevolution)));
}

float DriveSound::Render() {
  // The spindle: the motor's growl, with a faint trace of the diskette in its
  // jacket, both swelling a little once a revolution and following the speed.
  float out = 0.0f;
  if (speed_ > 1e-4f) {
    rotation_ += speed_ * 5.0 / kRate;
    if (rotation_ >= 1.0) rotation_ -= 1.0;
    growlPhase_ += speed_ * kGrowlHz / kRate;
    if (growlPhase_ >= 1.0) growlPhase_ -= 1.0;
    roughPhase_ += speed_ * kGrowlRoughHz / kRate;
    if (roughPhase_ >= 1.0) roughPhase_ -= 1.0;
    const float x = Noise();
    const float y = b0_ * x + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    // A sawtooth cut off after a few harmonics: buzzy, but with nothing high
    // enough to hiss.
    double growl = 0.0;
    for (int n = 1; n <= kGrowlHarmonics; ++n) growl += std::sin(2 * kPi * n * growlPhase_) / n;
    const float turn = static_cast<float>(0.5 + 0.5 * std::sin(2 * kPi * rotation_));
    const float flutter = static_cast<float>(0.5 + 0.5 * std::sin(2 * kPi * roughPhase_));
    out += speed_ * (kRubLevel * y * (1.0f - kRubSwell + kRubSwell * turn) +
                     kGrowlLevel * static_cast<float>(growl) *
                         (1.0f - kGrowlSwell + kGrowlSwell * turn) *
                         (1.0f - kGrowlRough + kGrowlRough * flutter));
  }
  for (Click& click : clicks_) out += kClickLevel * click.gain * click_[click.position++];
  std::erase_if(clicks_, [this](const Click& c) { return c.position >= click_.size(); });
  return out;
}

void DriveSound::Mix(const std::vector<EurekaMachine::DriveEvent>& events,
                     std::vector<int16_t>& audio, uint64_t endCycle) {
  for (const auto& event : events) Schedule(event);
  const uint64_t end = endCycle / kCyclesPerSample;
  // Never backwards: Continue may have carried the drive past a machine that
  // was switched off, and its clock did not move meanwhile.
  now_ = std::max(now_, end >= audio.size() ? end - audio.size() : 0);
  for (int16_t& sample : audio) Tick(sample);
}

void DriveSound::Continue(std::vector<int16_t>& audio) {
  for (int16_t& sample : audio) Tick(sample);
}

void DriveSound::Tick(int16_t& sample) {
  if (std::abs(static_cast<int>(sample)) > kDacAudible) {
    lastDac_ = now_;
    heardDac_ = true;
  }
  // In real time the machine itself does not touch the disk while it talks,
  // so the rule is the firmware's, not this class's.
  const bool dacQuiet = realTime_ || !heardDac_ || now_ - lastDac_ > kDacHold;

  if (!running_ && !jobs_.empty() && jobs_.front().notBefore <= now_ && dacQuiet) {
    // The controller has a command: the motor runs, and a spindle that was
    // off has to come up first.
    if (!motorOn_) {
      motorOn_ = true;
      // In real time the controller has done the waiting already: the
      // spin-up event came six revolutions before the first step.
      spunUpAt_ = now_ + (realTime_ ? 0 : kSpinUp);
    }
    motorUntil_ = now_ + kMotorHold;
    if (now_ >= spunUpAt_) {
      running_ = true;
      jobLeft_ = jobs_.front().length;
      if (jobs_.front().kind == Job::Kind::kStep) StartStep(jobs_.front());
    }
  }
  if (running_ && --jobLeft_ == 0) {
    queued_ -= jobs_.front().length;
    jobs_.pop_front();
    running_ = false;
    motorUntil_ = now_ + kMotorHold;
  }
  if (motorOn_ && !running_ && now_ >= motorUntil_) motorOn_ = false;
  speed_ += motorOn_ ? (1.0f - speed_) * kSpeedUp : -speed_ * kSpeedDown;
  if (sampled_ && motorOn_ != motorWasOn_) {
    // A spindle still coasting is caught again without a second spin-up.
    if (motorOn_) SwitchMotor(motor_ == Motor::kOff ? Motor::kStart : Motor::kLoop);
    else SwitchMotor(Motor::kStop);
    motorWasOn_ = motorOn_;
  }

  const float drive = sampled_ ? RenderSampled() : Render();
  const float mixed = static_cast<float>(sample) + drive * 32767.0f;
  sample = static_cast<int16_t>(std::clamp(mixed, -32768.0f, 32767.0f));
  ++now_;
}

bool DriveSound::Idle() const {
  if (sampled_)
    return jobs_.empty() && !running_ && !motorOn_ && motor_ == Motor::kOff && motorFade_ == 0 &&
           heads_.empty();
  return jobs_.empty() && !running_ && !motorOn_ && speed_ < 1e-3f && clicks_.empty();
}

void DriveSound::StartStep(const Job& job) {
  if (!sampled_) {
    clicks_.push_back(Click{0, 0.85f + 0.15f * (0.5f + 0.5f * Noise())});
    return;
  }
  // A seek is heard as one sound, so the steps after its first are silent.
  if (job.steps == 0) return;
  if (job.steps == 1) {
    Voice step{&steps_[stepTake_++ % steps_.size()]};
    step.gain = kStepGain;
    heads_.push_back(step);
    return;
  }
  // The raking runs for as long as the steps take and then dies away; the
  // recording repeats under a seek longer than itself.
  Voice rake{&seek_};
  rake.stopAt = static_cast<std::size_t>(job.steps) * job.length;
  rake.loop = true;
  heads_.push_back(rake);
}

void DriveSound::SwitchMotor(Motor to) {
  motorOut_ = motorVoice_;
  motorFade_ = motor_ == Motor::kOff ? 0 : kCrossfade;
  motor_ = to;
  motorVoice_ = Voice{};
  switch (to) {
    case Motor::kStart: motorVoice_.sound = &motorStart_; break;
    case Motor::kLoop: motorVoice_.sound = &motorLoop_; motorVoice_.loop = true; break;
    case Motor::kStop: motorVoice_.sound = &motorStop_; break;
    case Motor::kOff: break;
  }
}

// One voice one sample further.  False once it has nothing more to give.
static bool Advance(const std::vector<float>* sound, std::size_t& position, bool loop,
                    float& out) {
  if (!sound || sound->empty()) return false;
  if (position >= sound->size()) {
    if (!loop) return false;
    position = 0;
  }
  out = (*sound)[position++];
  return true;
}

float DriveSound::RenderSampled() {
  float out = 0.0f;

  // The motor: spin-up runs straight into the loop, as it does in the
  // recording, and the loop into the run-down; a change anywhere else is
  // crossfaded so the cut cannot click.
  float value = 0.0f;
  if (motor_ != Motor::kOff) {
    if (Advance(motorVoice_.sound, motorVoice_.position, motorVoice_.loop, value)) {
      float gain = 1.0f;
      if (motorFade_ > 0) gain = 1.0f - static_cast<float>(motorFade_) / kCrossfade;
      out += gain * value;
    } else if (motor_ == Motor::kStart) {
      motor_ = Motor::kLoop;
      motorVoice_ = Voice{&motorLoop_, 0, SIZE_MAX, 0, true};
    } else {
      motor_ = Motor::kOff;
    }
  }
  if (motorFade_ > 0) {
    if (Advance(motorOut_.sound, motorOut_.position, motorOut_.loop, value))
      out += static_cast<float>(motorFade_) / kCrossfade * value;
    --motorFade_;
  }

  for (Voice& head : heads_) {
    if (!Advance(head.sound, head.position, head.loop, value)) {
      head.sound = nullptr;
      continue;
    }
    float gain = head.gain;
    if (head.stopAt != SIZE_MAX) {
      if (head.stopAt > 0) --head.stopAt;
      if (head.stopAt == 0) {
        if (head.fading == 0) head.fading = kSeekRelease;
        if (--head.fading == 0) head.sound = nullptr;
        gain = static_cast<float>(head.fading) / kSeekRelease;
      }
    }
    out += gain * value;
  }
  std::erase_if(heads_, [](const Voice& v) { return v.sound == nullptr; });
  return out * kSampleGain;
}

namespace {

// The few WAVs this reads are the ones prepared for it: RIFF, PCM, mono,
// 16-bit, 48 kHz.  Anything else is refused rather than resampled -- a set at
// the wrong rate would play at the wrong pitch and nobody would know why.
//
// The messages are for the user: a folder of their own sounds that fails is
// reported at start-up (main.cpp), in UTF-8.
bool ParseWav(const std::string& stem, const std::vector<uint8_t>& data, std::vector<float>& out,
              std::string& error) {
  const std::string name = stem + ".wav";
  auto u16 = [&](std::size_t at) { return static_cast<uint32_t>(data[at] | data[at + 1] << 8); };
  auto u32 = [&](std::size_t at) { return u16(at) | u16(at + 2) << 16; };
  if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
      std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
    error = "súbor " + name + " nie je WAV";
    return false;
  }
  bool formatOk = false;
  for (std::size_t at = 12; at + 8 <= data.size();) {
    const uint32_t size = u32(at + 4);
    const std::size_t body = at + 8;
    if (body + size > data.size()) break;
    if (std::memcmp(data.data() + at, "fmt ", 4) == 0 && size >= 16)
      formatOk = u16(body) == 1 && u16(body + 2) == 1 && u32(body + 4) == kRate &&
                 u16(body + 14) == 16;
    if (std::memcmp(data.data() + at, "data", 4) == 0) {
      if (!formatOk) {
        error = "súbor " + name + " nie je 48 kHz, mono, 16 bitov";
        return false;
      }
      out.resize(size / 2);
      for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<int16_t>(u16(body + 2 * i)) / 32768.0f;
      return true;
    }
    at = body + size + (size & 1);
  }
  error = "súbor " + name + " nemá zvukové dáta";
  return false;
}

}  // namespace

bool DriveSound::LoadSamples(const Fetch& fetch, std::string& error) {
  std::vector<uint8_t> bytes;
  auto load = [&](const std::string& stem, std::vector<float>& out) {
    bytes.clear();
    if (!fetch(stem, bytes)) {
      error = "chýba súbor " + stem + ".wav";
      return false;
    }
    return ParseWav(stem, bytes, out, error);
  };
  std::vector<float> start, loop, stop, seek;
  std::vector<std::vector<float>> steps;
  if (!load("motor-rozbeh", start) || !load("motor-slucka", loop) ||
      !load("motor-dobeh", stop) || !load("presun", seek))
    return false;
  // krok-1, krok-2 and on for as long as they go: a few takes of the same
  // step, played in turn so that a walk across the disk does not sound
  // stamped out of one recording.
  for (unsigned i = 1;; ++i) {
    const std::string stem = "krok-" + std::to_string(i);
    bytes.clear();
    if (!fetch(stem, bytes)) break;
    steps.emplace_back();
    if (!ParseWav(stem, bytes, steps.back(), error)) return false;
  }
  if (steps.empty()) {
    error = "chýba súbor krok-1.wav";
    return false;
  }
  motorStart_ = std::move(start);
  motorLoop_ = std::move(loop);
  motorStop_ = std::move(stop);
  seek_ = std::move(seek);
  steps_ = std::move(steps);
  sampled_ = true;
  return true;
}

bool DriveSound::LoadSamples(const std::filesystem::path& folder, std::string& error) {
  return LoadSamples(
      [&folder](const std::string& stem, std::vector<uint8_t>& bytes) {
        std::ifstream file(folder / (stem + ".wav"), std::ios::binary);
        if (!file) return false;
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
      },
      error);
}
