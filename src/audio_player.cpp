#include "audio_player.h"

#include <algorithm>

namespace {
// The ceiling is a safety net for a device that stalls, not a working limit:
// with the main loop steering the queue it is never approached.  It is stated
// in milliseconds on purpose.  The old cap counted blocks, which silently
// meant 240 ms at twenty milliseconds a block -- and would have meant a
// thrashing 30 ms had the blocks ever been made small.
constexpr double kMaxQueueMs = 120.0;
}  // namespace

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
  if (waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) !=
      MMSYSERR_NOERROR) {
    device_ = nullptr;
    return false;
  }
  sampleRate_ = sampleRate;
  submitted_ = 0;
  queued_ = 0;
  return true;
}

double AudioPlayer::QueuedMs() const {
  if (!device_ || sampleRate_ == 0) return 0.0;
  const double perSample = 1000.0 / static_cast<double>(sampleRate_);
  MMTIME time{};
  time.wType = TIME_SAMPLES;
  // waveOutGetPosition counts what has actually left, so it sees the driver's
  // own buffering as well as ours; the block bookkeeping only sees ours.
  if (waveOutGetPosition(device_, &time, sizeof(time)) == MMSYSERR_NOERROR &&
      time.wType == TIME_SAMPLES) {
    // The device reports 32 bits of sample position; mask our own count the
    // same way so the difference stays right across the wrap at 24 hours.
    const uint32_t handed = static_cast<uint32_t>(submitted_);
    const int32_t outstanding = static_cast<int32_t>(handed - time.u.sample);
    if (outstanding >= 0)
      return (static_cast<double>(outstanding) + staging_.size()) * perSample;
  }
  return static_cast<double>(queued_ + staging_.size()) * perSample;
}

void AudioPlayer::Reap() {
  while (!blocks_.empty() && (blocks_.front()->header.dwFlags & WHDR_DONE)) {
    queued_ -= blocks_.front()->samples.size();
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
  const std::size_t count = block->samples.size();
  if (waveOutWrite(device_, &block->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
    waveOutUnprepareHeader(device_, &block->header, sizeof(WAVEHDR));
    return;
  }
  submitted_ += count;
  queued_ += count;
  blocks_.push_back(std::move(block));
}

void AudioPlayer::Submit(std::vector<int16_t> samples) {
  if (!device_) return;
  Reap();
  staging_.insert(staging_.end(), samples.begin(), samples.end());
  const std::size_t block = BlockSamples();
  if (block == 0) return;
  const double perBlock = static_cast<double>(block) * 1000.0 / sampleRate_;
  double queued = QueuedMs();
  while (staging_.size() >= block) {
    std::vector<int16_t> chunk(staging_.begin(),
                               staging_.begin() + static_cast<long>(block));
    staging_.erase(staging_.begin(), staging_.begin() + static_cast<long>(block));
    // Over the ceiling the excess is dropped rather than waited out.  The old
    // code slept here, and that stalled the whole main loop: on this machine
    // sound is the entire user interface, but so is the keyboard, and freezing
    // both to protect a buffer trades a glitch for an unresponsive machine.
    if (queued > kMaxQueueMs) continue;
    WriteBlock(std::move(chunk));
    queued += perBlock;
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
  sampleRate_ = 0;
  submitted_ = 0;
  queued_ = 0;
}
