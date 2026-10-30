#ifndef EUREKA_AUDIO_PLAYER_H
#define EUREKA_AUDIO_PLAYER_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Shared-mode WASAPI on the default output device.  It replaced waveOut, which
// on Windows 10 is a wrapper over the same engine with a layer of its own in
// between: it reported its position coarsely and fed the engine in pieces of
// its own, so the 12 ms it showed queued did not keep the engine fed, and the
// sound broke up below 30-50 ms depending on the driver (HANDOFF 6.61).  Here
// the queue is the engine's own buffer, which GetCurrentPadding counts
// exactly.  No thread and no events: the main loop already hands over samples
// every two milliseconds, and each pass writes what fits.
class AudioPlayer {
 public:
  AudioPlayer() = default;
  ~AudioPlayer();
  AudioPlayer(const AudioPlayer&) = delete;
  AudioPlayer& operator=(const AudioPlayer&) = delete;

  // prefillMs of silence goes into every new stream, so that it starts at
  // the depth the main loop steers to rather than empty.  False means no
  // sound for now; Submit goes on looking for a device unless the sound
  // system itself was not there.
  bool Open(unsigned sampleRate, double prefillMs);
  void Submit(std::vector<int16_t> samples);
  void Close();

  bool Ready() const { return client_ != nullptr; }
  // Milliseconds of sound handed over but not yet played, staging included.
  // This is the latency the listener hears, and it is what the main loop
  // steers the emulated clock by, so it is asked of the engine itself rather
  // than inferred from what was written.
  double QueuedMs() const;

  // How the queue fared since the last call, for the diagnostics dump: an
  // audible gap is a queue that ran dry with sound still coming, and nothing
  // else in the program can tell one from silence the guest chose.
  struct Stats {
    // What the engine itself held, without samples not yet written to it.
    double minQueueMs = 0.0;
    double meanQueueMs = 0.0;
    double maxQueueMs = 0.0;
    // Times the engine ran out while there were samples to play.
    uint64_t dry = 0;
    // How much less the device played than the wall clock ran: the length of
    // the gaps, however short each one was.
    double gapMs = 0.0;
    // Sound thrown away over the ceiling, in 5 ms blocks -- a skip.
    uint64_t dropped = 0;
  };
  Stats TakeStats();

 private:
  bool Start();
  void Release();
  // Frames still waiting in the engine's buffer; false once the device is
  // gone, which Submit answers by opening the default device again.
  bool Padding(UINT32& frames) const;
  void FollowDefaultDevice();

  bool comInitialised_ = false;
  IMMDeviceEnumerator* enumerator_ = nullptr;
  IAudioClient* client_ = nullptr;
  IAudioRenderClient* render_ = nullptr;
  IAudioClock* clock_ = nullptr;
  UINT64 clockFrequency_ = 0;
  UINT32 bufferFrames_ = 0;
  unsigned sampleRate_ = 0;
  double prefillMs_ = 0.0;
  std::wstring deviceId_;
  std::chrono::steady_clock::time_point nextDeviceCheck_;
  std::vector<int16_t> staging_;

  // Accumulated for TakeStats.  Counting starts with the first write: an
  // empty buffer before anything was ever written is not a gap.
  bool playing_ = false;
  bool dryNow_ = false;
  double statMin_ = 0.0;
  double statMax_ = 0.0;
  double statSum_ = 0.0;
  uint64_t statCount_ = 0;
  uint64_t statDry_ = 0;
  uint64_t statDroppedSamples_ = 0;
  // The gap clock: wall time against the device's own position since
  // playing started.  Their difference only grows while the device starves;
  // the minimum over each TakeStats window takes out the jitter of reading
  // both at slightly different moments.
  bool clockRunning_ = false;
  std::chrono::steady_clock::time_point clockStart_;
  double clockStartPlayedMs_ = 0.0;
  double lastMinDeficit_ = 0.0;
  double windowMinDeficit_ = 0.0;
  bool haveWindowMin_ = false;
};

#endif
