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

 private:
  struct Block {
    WAVEHDR header{};
    std::vector<int16_t> samples;
  };

  void Reap();
  void WriteBlock(std::vector<int16_t> samples);

  HWAVEOUT device_ = nullptr;
  std::deque<std::unique_ptr<Block>> blocks_;
  std::vector<int16_t> staging_;
};

#endif
