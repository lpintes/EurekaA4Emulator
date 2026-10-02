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
  bool LoadSamples(const std::filesystem::path& folder, std::string& error);

  // Takes what the drive did and mixes the drive into `audio`, the machine's
  // own samples for the stretch that ended at `endCycle`.  Events may lie
  // before the stretch began; they then start at once.
  void Mix(const std::vector<EurekaMachine::DriveEvent>& events, std::vector<int16_t>& audio,
           uint64_t endCycle);
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
  };
  enum class Motor : uint8_t { kOff, kStart, kLoop, kStop };

  void Schedule(const EurekaMachine::DriveEvent& event);
  void Push(Job::Kind kind, uint64_t notBefore, uint32_t length, uint16_t steps = 0);
  void StartStep(const Job& job);
  float Noise();
  float Render();
  float RenderSampled();
  void SwitchMotor(Motor to);

  std::deque<Job> jobs_;
  uint64_t queued_ = 0;  // samples of work in jobs_, to bound the backlog
  bool afterSeek_ = true;
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
