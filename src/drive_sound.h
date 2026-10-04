// The diskette drive, heard: a synthesized sound mixed under the machine's own
// output (HANDOFF 6.54).  No recordings -- third-party samples have no place in
// a published tree -- so the head's steps and the spindle are made here.
//
// It is a backdrop that follows the machine, not part of it.  The model does a
// disk operation in no time at all, so this keeps its own timeline: what the
// drive was told to do is queued and played out at the drive's own pace,
// however far behind the machine that falls.  Real timing in the controller is
// a later step (ea4-tvd.4); until then a format is heard for about a minute
// after the machine has said it is done.
//
// Plain C++ with no host in it: the probe renders it into a WAV, the emulator
// thread into the sound device, and both have to hear the same thing.

#ifndef DRIVE_SOUND_H
#define DRIVE_SOUND_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "machine.h"

class DriveSound {
 public:
  DriveSound();

  // Recorded sounds in place of the synthesized ones, from a folder of 48 kHz
  // mono 16-bit WAVs: motor-rozbeh, motor-slucka and motor-dobeh (spin-up,
  // loop, run-down), krok-1, krok-2 and on (takes of one head step, played in
  // turn) and presun (the head raking across many cylinders).  An experiment
  // for now (HANDOFF 6.54): the synthesis never satisfied the owner; the set
  // he chose is a motor sampled by Shiru (CC-BY), steps cut from a Wikimedia
  // Commons recording and the raking from BigSoundBank (both CC0).  False
  // with a reason if anything is missing; the synthesized sound then stays.
  // The reason is meant for the user and is UTF-8.
  bool LoadSamples(const std::filesystem::path& folder, std::string& error);
  // The same set from anywhere: `fetch` is given a sound's stem
  // ("motor-rozbeh", "krok-2") and fills `bytes` with its WAV file, or
  // returns false when there is no such sound.  The emulator reads its
  // built-in set out of the EXE's resources this way, and this class stays
  // free of the host.
  using Fetch = std::function<bool(const std::string& stem, std::vector<uint8_t>& bytes)>;
  bool LoadSamples(const Fetch& fetch, std::string& error);
  bool sampled() const { return sampled_; }

  // Real time: the events come from a timed controller (the BIOS bypass off,
  // HANDOFF 6.54 step 4) and carry the moments the drive acts.  The drive is
  // then played as it happens -- no backlog behind the machine, no waiting
  // for the DAC to fall quiet and no spin-up of its own, since the machine
  // does all three itself.  Off, the backdrop of step 2 plays as before.
  void SetRealTime(bool on) { realTime_ = on; }

  // Takes what the drive did and mixes the drive into `audio`, the machine's
  // own samples for the stretch that ended at `endCycle`.  Events may lie
  // before the stretch began; they then start at once.
  void Mix(const std::vector<EurekaMachine::DriveEvent>& events, std::vector<int16_t>& audio,
           uint64_t endCycle);
  // Runs the drive on its own timeline over `audio` with no machine behind
  // it.  A switched-off machine produces no samples at all, and the drive
  // has to finish what it was doing -- the motor runs down on the hardware
  // too -- rather than freeze in the middle of a step.
  void Continue(std::vector<int16_t>& audio);
  // Nothing queued, nothing playing and the spindle at rest.
  bool Idle() const;

 private:
  struct Job {
    enum class Kind : uint8_t { kStep, kTurn } kind;
    uint64_t notBefore;  // sample index
    uint32_t length;     // samples
    // On the first step of a seek, how many steps it has (0 on the rest): a
    // recording plays a whole seek as one sound.
    uint16_t steps = 0;
  };
  struct Click {
    uint32_t position;
    float gain;
  };
  // One recording being played.  `stopAt` cuts it short with a fade, and a
  // looping one starts over until it is cut.
  struct Voice {
    const std::vector<float>* sound = nullptr;
    std::size_t position = 0;
    std::size_t stopAt = SIZE_MAX;
    std::size_t fading = 0;
    bool loop = false;
    float gain = 1.0f;
  };
  enum class Motor : uint8_t { kOff, kStart, kLoop, kStop };

  void Schedule(const EurekaMachine::DriveEvent& event);
  void Push(Job::Kind kind, uint64_t notBefore, uint32_t length, uint16_t steps = 0);
  void StartStep(const Job& job);
  void Tick(int16_t& sample);
  float Noise();
  float Render();
  float RenderSampled();
  void SwitchMotor(Motor to);

  std::deque<Job> jobs_;
  uint64_t queued_ = 0;  // samples of work in jobs_, to bound the backlog
  bool afterSeek_ = true;
  bool realTime_ = false;
  bool running_ = false;
  uint32_t jobLeft_ = 0;

  uint64_t now_ = 0;  // sample index of the next sample rendered
  uint64_t lastDac_ = 0;
  bool heardDac_ = false;

  bool motorOn_ = false;
  uint64_t motorUntil_ = 0;
  uint64_t spunUpAt_ = 0;
  float speed_ = 0.0f;  // spindle speed, 0 at rest and 1 at 300 rpm
  double rotation_ = 0.0;
  double growlPhase_ = 0.0;
  double roughPhase_ = 0.0;

  std::vector<float> click_;
  std::vector<Click> clicks_;

  // Band-pass state for the spindle's rub.
  float b0_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
  float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
  uint32_t seed_;

  // The recorded set, when LoadSamples succeeded.
  bool sampled_ = false;
  std::vector<float> motorStart_, motorLoop_, motorStop_, seek_;
  std::vector<std::vector<float>> steps_;
  std::size_t stepTake_ = 0;
  Motor motor_ = Motor::kOff;
  Voice motorVoice_, motorOut_;
  std::size_t motorFade_ = 0;
  bool motorWasOn_ = false;
  std::vector<Voice> heads_;
};

#endif
