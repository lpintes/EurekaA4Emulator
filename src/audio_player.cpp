#include "audio_player.h"

#include <algorithm>

AudioPlayer::~AudioPlayer() {
  Close();
}

bool AudioPlayer::Open(unsigned sampleRate) {
  Close();
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = sampleRate;
  format.wBitsPerSample = 16;
  format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  return waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0,
                     CALLBACK_NULL) == MMSYSERR_NOERROR;
}

void AudioPlayer::Reap() {
  while (!blocks_.empty() && (blocks_.front()->header.dwFlags & WHDR_DONE)) {
    waveOutUnprepareHeader(device_, &blocks_.front()->header, sizeof(WAVEHDR));
    blocks_.pop_front();
  }
}

void AudioPlayer::WriteBlock(std::vector<int16_t> samples) {
  if (!device_ || samples.empty()) return;
  auto block = std::make_unique<Block>();
  block->samples = std::move(samples);
  block->header.lpData = reinterpret_cast<LPSTR>(block->samples.data());
  block->header.dwBufferLength =
      static_cast<DWORD>(block->samples.size() * sizeof(int16_t));
  if (waveOutPrepareHeader(device_, &block->header, sizeof(WAVEHDR)) !=
      MMSYSERR_NOERROR) return;
  if (waveOutWrite(device_, &block->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
    waveOutUnprepareHeader(device_, &block->header, sizeof(WAVEHDR));
    return;
  }
  blocks_.push_back(std::move(block));
}

void AudioPlayer::Submit(std::vector<int16_t> samples) {
  if (!device_) return;
  Reap();
  staging_.insert(staging_.end(), samples.begin(), samples.end());
  constexpr std::size_t kBlockSamples = 960;  // 20 ms at 48 kHz
  while (staging_.size() >= kBlockSamples) {
    std::vector<int16_t> block(staging_.begin(), staging_.begin() + kBlockSamples);
    staging_.erase(staging_.begin(), staging_.begin() + kBlockSamples);
    WriteBlock(std::move(block));
  }
  // Bound latency if the host cannot consume audio as fast as it is produced.
  while (blocks_.size() > 12) {
    Sleep(1);
    Reap();
  }
}

void AudioPlayer::Close() {
  if (!device_) return;
  if (!staging_.empty()) {
    WriteBlock(std::move(staging_));
    staging_.clear();
  }
  waveOutReset(device_);
  for (auto& block : blocks_)
    waveOutUnprepareHeader(device_, &block->header, sizeof(WAVEHDR));
  blocks_.clear();
  waveOutClose(device_);
  device_ = nullptr;
}
