#include "audio_player.h"

#include <algorithm>
#include <cstring>

// Not in MinGW's audioclient.h.  With them the engine converts the
// emulator's mono 16-bit stream to its own mix format and rate itself, so
// nothing here has to know what the device runs at.
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace {
// The ceiling is a safety net for a device that stalls, not a working limit:
// with the main loop steering the queue it is never approached.  It is stated
// in milliseconds on purpose.  The old cap counted blocks, which silently
// meant 240 ms at twenty milliseconds a block -- and would have meant a
// thrashing 30 ms had the blocks ever been made small.
constexpr double kMaxQueueMs = 120.0;
// The engine's buffer is asked for with room above the ceiling, so it is
// never the buffer that refuses samples the ceiling would have let in.
constexpr double kBufferMs = 150.0;
// Under this, with sound still coming, the engine has run out or is about
// to: the main loop hands over samples every two milliseconds or so, so a
// queue this low means a pass came late enough to leave a gap.
constexpr double kDryMs = 1.0;
// Unplugging headphones or choosing another default device does not move a
// WASAPI stream on its own, so the default is looked up again this often --
// waveOut did that by itself.
constexpr auto kDeviceCheck = std::chrono::seconds(1);

template <typename T>
void SafeRelease(T*& pointer) {
  if (pointer) pointer->Release();
  pointer = nullptr;
}

std::wstring DeviceId(IMMDevice* device) {
  LPWSTR id = nullptr;
  std::wstring result;
  if (SUCCEEDED(device->GetId(&id)) && id) {
    result = id;
    CoTaskMemFree(id);
  }
  return result;
}

std::wstring DefaultDeviceId(IMMDeviceEnumerator* enumerator) {
  IMMDevice* device = nullptr;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
    return {};
  std::wstring result = DeviceId(device);
  device->Release();
  return result;
}
}  // namespace

AudioPlayer::~AudioPlayer() {
  Close();
}

bool AudioPlayer::Open(unsigned sampleRate, double prefillMs) {
  Close();
  prefillMs_ = prefillMs;
  // The emulator's own thread, which owns no window, so the free-threaded
  // apartment.  A thread that already chose the other one answers
  // RPC_E_CHANGED_MODE, and the objects work there too; only a call that
  // succeeded is undone in Close.
  comInitialised_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  sampleRate_ = sampleRate;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&enumerator_)))) {
    Close();
    return false;
  }
  // A device that is missing or held exclusively right now is reported, but
  // the enumerator stays: FollowDefaultDevice keeps trying.  Dropping it here
  // would leave a reset that hit such a moment silent until the next restart,
  // and without sound this machine has no user interface.
  return Start();
}

// Opens a stream on whatever the default device is now.
bool AudioPlayer::Start() {
  nextDeviceCheck_ = std::chrono::steady_clock::now() + kDeviceCheck;
  IMMDevice* device = nullptr;
  if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
    return false;
  // The id of the device opened, not of a second lookup: the default can move
  // in between, and then the move would never be followed.
  deviceId_ = DeviceId(device);
  const HRESULT activated =
      device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                       reinterpret_cast<void**>(&client_));
  device->Release();
  if (FAILED(activated)) {
    client_ = nullptr;
    return false;
  }
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = sampleRate_;
  format.wBitsPerSample = 16;
  format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  const auto duration = static_cast<REFERENCE_TIME>(kBufferMs * 10000.0);
  if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                     AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 duration, 0, &format, nullptr)) ||
      FAILED(client_->GetBufferSize(&bufferFrames_)) ||
      FAILED(client_->GetService(__uuidof(IAudioRenderClient),
                                 reinterpret_cast<void**>(&render_))) ||
      FAILED(client_->GetService(__uuidof(IAudioClock),
                                 reinterpret_cast<void**>(&clock_))) ||
      FAILED(clock_->GetFrequency(&clockFrequency_)) || clockFrequency_ == 0) {
    Release();
    return false;
  }
  // A new stream starts empty, and the main loop's two-percent slew would
  // take about a second to fill it to the target -- a second of the engine
  // running dry, every time the device changes.  Silence up front instead.
  const auto prefill = std::min(
      bufferFrames_, static_cast<UINT32>(prefillMs_ * sampleRate_ / 1000.0));
  BYTE* data = nullptr;
  if (prefill > 0 && SUCCEEDED(render_->GetBuffer(prefill, &data)))
    render_->ReleaseBuffer(prefill, AUDCLNT_BUFFERFLAGS_SILENT);
  if (FAILED(client_->Start())) {
    Release();
    return false;
  }
  return true;
}

// Drops the stream but keeps the enumerator, for opening the next one.
void AudioPlayer::Release() {
  if (client_) client_->Stop();
  SafeRelease(clock_);
  SafeRelease(render_);
  SafeRelease(client_);
  bufferFrames_ = 0;
  clockFrequency_ = 0;
  playing_ = false;
  dryNow_ = false;
  clockRunning_ = false;
}

bool AudioPlayer::Padding(UINT32& frames) const {
  frames = 0;
  return client_ && SUCCEEDED(client_->GetCurrentPadding(&frames));
}

double AudioPlayer::QueuedMs() const {
  if (!client_ || sampleRate_ == 0) return 0.0;
  UINT32 padding = 0;
  Padding(padding);
  return (static_cast<double>(padding) + staging_.size()) * 1000.0 / sampleRate_;
}

void AudioPlayer::FollowDefaultDevice() {
  const auto now = std::chrono::steady_clock::now();
  if (now < nextDeviceCheck_) return;
  nextDeviceCheck_ = now + kDeviceCheck;
  const std::wstring id = DefaultDeviceId(enumerator_);
  if (id.empty() || (client_ && id == deviceId_)) return;
  Release();
  staging_.clear();
  Start();
}

void AudioPlayer::Submit(std::vector<int16_t> samples) {
  if (!enumerator_) return;
  FollowDefaultDevice();
  UINT32 padding = 0;
  if (!Padding(padding)) {
    // The device went away (AUDCLNT_E_DEVICE_INVALIDATED and its kin), or
    // there is none: FollowDefaultDevice opens the default again once one is
    // there.  Until then the sound has nowhere to go.
    Release();
    staging_.clear();
    return;
  }
  const double device = padding * 1000.0 / sampleRate_;
  // A pass with nothing to hand over is a machine switched off; the queue it
  // lets drain is not a gap, so counting starts again with the next write.
  if (samples.empty()) {
    playing_ = false;
    dryNow_ = false;
    clockRunning_ = false;
  } else if (playing_) {
    if (statCount_ == 0 || device < statMin_) statMin_ = device;
    if (statCount_ == 0 || device > statMax_) statMax_ = device;
    statSum_ += device;
    ++statCount_;
    // Counted once per gap, not once per pass spent in it.
    const bool dry = device < kDryMs;
    if (dry && !dryNow_) ++statDry_;
    dryNow_ = dry;
    UINT64 position = 0;
    if (SUCCEEDED(clock_->GetPosition(&position, nullptr))) {
      const double played =
          static_cast<double>(position) * 1000.0 / static_cast<double>(clockFrequency_);
      const auto now = std::chrono::steady_clock::now();
      if (!clockRunning_) {
        clockRunning_ = true;
        clockStart_ = now;
        clockStartPlayedMs_ = played;
        lastMinDeficit_ = 0.0;
        haveWindowMin_ = false;
      }
      const double wall =
          std::chrono::duration<double, std::milli>(now - clockStart_).count();
      const double deficit = wall - (played - clockStartPlayedMs_);
      if (!haveWindowMin_ || deficit < windowMinDeficit_) windowMinDeficit_ = deficit;
      haveWindowMin_ = true;
    }
  }

  staging_.insert(staging_.end(), samples.begin(), samples.end());
  const UINT32 limit = std::min(
      bufferFrames_, static_cast<UINT32>(kMaxQueueMs * sampleRate_ / 1000.0));
  const UINT32 room = limit > padding ? limit - padding : 0;
  const auto count =
      static_cast<UINT32>(std::min<std::size_t>(staging_.size(), room));
  BYTE* data = nullptr;
  if (count > 0) {
    if (FAILED(render_->GetBuffer(count, &data))) {
      // Gone between the padding and the write: the same as a failed
      // padding, and not a skip over the ceiling, which the diagnostics
      // would otherwise report it as.
      Release();
      staging_.clear();
      return;
    }
    std::memcpy(data, staging_.data(), count * sizeof(int16_t));
    render_->ReleaseBuffer(count, 0);
    staging_.erase(staging_.begin(), staging_.begin() + count);
    playing_ = true;
  }
  // Over the ceiling the rest is dropped rather than waited out.  Waiting
  // would stall the whole main loop: on this machine sound is the entire user
  // interface, but so is the keyboard, and freezing both to protect a buffer
  // trades a glitch for an unresponsive machine.
  statDroppedSamples_ += staging_.size();
  staging_.clear();
}

AudioPlayer::Stats AudioPlayer::TakeStats() {
  Stats stats;
  stats.minQueueMs = statMin_;
  stats.maxQueueMs = statMax_;
  stats.meanQueueMs = statCount_ ? statSum_ / static_cast<double>(statCount_) : 0.0;
  stats.dry = statDry_;
  const uint64_t block = sampleRate_ / 200;
  stats.dropped = block ? (statDroppedSamples_ + block - 1) / block : 0;
  if (haveWindowMin_) {
    stats.gapMs = std::max(0.0, windowMinDeficit_ - lastMinDeficit_);
    lastMinDeficit_ = windowMinDeficit_;
    haveWindowMin_ = false;
  }
  statMin_ = statMax_ = statSum_ = 0.0;
  statCount_ = statDry_ = statDroppedSamples_ = 0;
  return stats;
}

void AudioPlayer::Close() {
  Release();
  SafeRelease(enumerator_);
  staging_.clear();
  deviceId_.clear();
  sampleRate_ = 0;
  if (comInitialised_) CoUninitialize();
  comInitialised_ = false;
}
