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
// Back to 1 with the recordings of a real Eureka (24. 10. 2026): they are one
// drive in its own proportions, a quiet motor under a clear step, and the
// owner remembers it exactly so.  Kept as a knob for a set that needs one.
constexpr float kStepGain = 1.0f;
constexpr uint32_t kCrossfade = Ms(10);
constexpr uint32_t kSeekRelease = Ms(15);

// The steady motor, never repeating.  A 0.75 s loop and then an 8.7 s one
// were both heard repeating by Luboš Krejča, who made the recordings and is a
// musician (November 2026); any loop is heard once the motor runs long enough,
// and formatting runs it for over a minute.  So the recording is played in
// random runs of 5 to 15 whole revolutions (1 to 3 s), each joined to the
// next at a revolution boundary so the spindle's once-a-revolution rhythm
// carries on across the seam.  A sketch of exactly this delighted him.
constexpr std::size_t kMinRun = 5;
constexpr std::size_t kMaxRun = 15;
// A run never starts this close to where the last one ended, so the same
// stretch does not come straight back.
constexpr std::size_t kNearRun = 3;
constexpr uint32_t kSeamFade = Ms(50);

// Equal-power crossfade, `left` samples before its end: two stretches of
// motor are not in phase sample by sample, so a linear one would dip.
float FadeIn(std::size_t left, std::size_t length) {
  return static_cast<float>(std::sin((1.0 - static_cast<double>(left) / length) * kPi / 2));
}
float FadeOut(std::size_t left, std::size_t length) {
  return static_cast<float>(std::cos((1.0 - static_cast<double>(left) / length) * kPi / 2));
}

// Revolution boundaries of a steady motor recording.  The pattern of one
// revolution recurs about 200 ms later -- 196 to 199 ms in the Eureka's, and
// not the same each time, so no fixed period would stay in phase -- and each
// boundary is put where the last revolution's pattern recurs best, on a 1 ms
// envelope.  Every seam then sits at the same phase on both sides.
std::vector<std::size_t> FindTurns(const std::vector<float>& sound) {
  constexpr std::size_t kBlock = kRate / 1000;
  constexpr std::size_t kWindow = 199;
  constexpr std::size_t kMinLag = 195;
  constexpr std::size_t kMaxLag = 203;
  std::vector<float> env(sound.size() / kBlock);
  for (std::size_t i = 0; i < env.size(); ++i) {
    float sum = 0.0f;
    for (std::size_t j = 0; j < kBlock; ++j) sum += std::abs(sound[i * kBlock + j]);
    env[i] = sum;
  }
  std::vector<std::size_t> turns{0};
  while (turns.back() + kMaxLag + kWindow <= env.size()) {
    const std::size_t at = turns.back();
    std::size_t best = kMinLag;
    double bestMatch = -1.0;
    for (std::size_t lag = kMinLag; lag <= kMaxLag; ++lag) {
      double match = 0.0;
      for (std::size_t i = 0; i < kWindow; ++i) match += env[at + i] * env[at + lag + i];
      if (match > bestMatch) {
        bestMatch = match;
        best = lag;
      }
    }
    turns.push_back(at + best);
  }
  for (std::size_t& turn : turns) turn *= kBlock;
  // A seam lets the run it leaves play on for the crossfade.
  while (!turns.empty() && turns.back() + kSeamFade > sound.size()) turns.pop_back();
  // Too short to choose from: the loop just loops.
  if (turns.size() < kMaxRun + kNearRun + 2) turns.clear();
  return turns;
}

}  // namespace

DriveSound::DriveSound() : seed_(0x2545f491u), shuffleSeed_(0x9e3779b9u) {
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
           seamFade_ == 0 && heads_.empty();
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
  // The leaving voice fades from where it stands -- mid-seam it is only
  // part way in -- and a seam in progress fades out on its own.
  motorOut_ = motorVoice_;
  motorOut_.gain = MotorGain();
  motorFade_ = motor_ == Motor::kOff ? 0 : kCrossfade;
  seamIn_ = 0;
  motor_ = to;
  motorVoice_ = Voice{};
  switch (to) {
    case Motor::kStart: motorVoice_.sound = &motorStart_; break;
    case Motor::kLoop: StartRun(0); break;
    case Motor::kStop: motorVoice_.sound = &motorStop_; break;
    case Motor::kOff: break;
  }
}

// Its own xorshift32, apart from Noise(): the synthesized sound stays the
// same whatever the shuffle draws, and the same events still give the same
// WAV.
uint32_t DriveSound::Shuffle() {
  shuffleSeed_ ^= shuffleSeed_ << 13;
  shuffleSeed_ ^= shuffleSeed_ >> 17;
  shuffleSeed_ ^= shuffleSeed_ << 5;
  return shuffleSeed_;
}

float DriveSound::MotorGain() const {
  float gain = 1.0f;
  if (motorFade_ > 0) gain *= FadeIn(motorFade_, kCrossfade);
  if (seamIn_ > 0) gain *= FadeIn(seamIn_, kSeamFade);
  return gain;
}

void DriveSound::StartRun(std::size_t turn) {
  if (turns_.empty()) {
    motorVoice_ = Voice{&motorLoop_, 0, SIZE_MAX, 0, true};
    return;
  }
  const std::size_t last = turns_.size() - 1;
  const std::size_t run = std::min(kMinRun + Shuffle() % (kMaxRun - kMinRun + 1), last - turn);
  motorVoice_ = Voice{&motorLoop_, turns_[turn]};
  runEnd_ = turn + run;
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

  // The motor: spin-up runs straight into the steady motor, which starts
  // where the spin-up's recording left off; anything else is crossfaded so
  // the cut cannot click.  The crossfade keeps the power, not the amplitude:
  // two stretches of motor are not in phase sample by sample, and a linear
  // one would dip in the middle of every seam.
  float value = 0.0f;
  if (motor_ == Motor::kLoop && !turns_.empty() && motorFade_ == 0 && seamFade_ == 0 &&
      motorVoice_.position >= turns_[runEnd_]) {
    // The run is over: the next starts anywhere but close to where this one
    // ended, and this one plays on under the crossfade.
    seamOut_ = motorVoice_;
    seamFade_ = seamIn_ = kSeamFade;
    const std::size_t ended = runEnd_;
    const std::size_t last = turns_.size() - 1;
    std::size_t next = Shuffle() % (last - kMinRun + 1);
    for (std::size_t tries = 0; tries <= last && next + kNearRun >= ended && next <= ended + kNearRun;
         ++tries)
      next = (next + 1) % (last - kMinRun + 1);
    StartRun(next);
  }
  if (motor_ != Motor::kOff) {
    if (Advance(motorVoice_.sound, motorVoice_.position, motorVoice_.loop, value)) {
      out += MotorGain() * value;
    } else if (motor_ == Motor::kStart) {
      // Straight on in this very sample: one left silent is a click.
      motor_ = Motor::kLoop;
      StartRun(0);
      if (Advance(motorVoice_.sound, motorVoice_.position, motorVoice_.loop, value))
        out += MotorGain() * value;
    } else {
      motor_ = Motor::kOff;
    }
  }
  if (seamIn_ > 0) --seamIn_;
  if (motorFade_ > 0) {
    if (Advance(motorOut_.sound, motorOut_.position, motorOut_.loop, value))
      out += motorOut_.gain * FadeOut(motorFade_, kCrossfade) * value;
    --motorFade_;
  }
  if (seamFade_ > 0) {
    if (Advance(seamOut_.sound, seamOut_.position, seamOut_.loop, value))
      out += FadeOut(seamFade_, kSeamFade) * value;
    --seamFade_;
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
  turns_ = FindTurns(motorLoop_);
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
