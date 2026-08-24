#ifndef EUREKA_AUDIO_PLAYER_H
#define EUREKA_AUDIO_PLAYER_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

class AudioPlayer {
 public:
  AudioPlayer() = default;
  ~AudioPlayer();

  bool Open(unsigned sampleRate);
  void Submit(std::vector<int16_t> samples);
  void Close();

  bool Ready() const { return device_ != nullptr; }
  // Milliseconds of sound handed over but not yet played, staging included.
  // This is the latency the listener hears, and it is what the main loop
  // steers the emulated clock by, so it is asked of the device itself rather
  // than inferred from how many blocks happen to be outstanding.
  double QueuedMs() const;

 private:
  struct Block {
    WAVEHDR header{};
    std::vector<int16_t> samples;
  };

  // Five milliseconds.  Twenty was the old figure and it put a whole block of
  // latency into staging_ before anything reached the device at all.
  std::size_t BlockSamples() const { return sampleRate_ / 200; }

  void Reap();
  void WriteBlock(std::vector<int16_t> samples);

  HWAVEOUT device_ = nullptr;
  unsigned sampleRate_ = 0;
  uint64_t submitted_ = 0;
  uint64_t queued_ = 0;
  std::deque<std::unique_ptr<Block>> blocks_;
  std::vector<int16_t> staging_;
};

#endif
