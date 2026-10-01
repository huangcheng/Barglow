#include "audio_capture.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>

#include <avrt.h>
#include <cmath>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace {

constexpr REFERENCE_TIME kBufferDuration = 10000000;  // 1s

}  // namespace

SampleRing::SampleRing(size_t capacity) {
  size_t cap = 1;
  while (cap < capacity) cap <<= 1;
  buffer_.assign(cap, 0.f);
  mask_ = cap - 1;
}

void SampleRing::Write(const float* samples, size_t count) {
  size_t w = write_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < count; ++i) {
    buffer_[w & mask_] = samples[i];
    ++w;
  }
  write_.store(w, std::memory_order_release);
  // Drop oldest if overrun.
  size_t r = read_.load(std::memory_order_relaxed);
  if (w - r > mask_) {
    read_.store(w - mask_, std::memory_order_release);
  }
}

size_t SampleRing::Read(float* out, size_t maxCount) {
  const size_t w = write_.load(std::memory_order_acquire);
  size_t r = read_.load(std::memory_order_relaxed);
  size_t available = w - r;
  if (available > maxCount) available = maxCount;
  for (size_t i = 0; i < available; ++i) {
    out[i] = buffer_[(r + i) & mask_];
  }
  read_.store(r + available, std::memory_order_release);
  return available;
}

void SampleRing::Clear() {
  const size_t w = write_.load(std::memory_order_relaxed);
  read_.store(w, std::memory_order_release);
}

AudioCapture::AudioCapture() {
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

AudioCapture::~AudioCapture() {
  Stop();
  if (stopEvent_) {
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
  }
}

bool AudioCapture::Start() {
  if (running_.load()) return true;
  if (!stopEvent_) return false;
  ResetEvent(stopEvent_);
  running_.store(true);
  thread_ = CreateThread(nullptr, 0,
                         [](LPVOID p) -> DWORD {
                           static_cast<AudioCapture*>(p)->CaptureThread();
                           return 0;
                         },
                         this, 0, nullptr);
  return thread_ != nullptr;
}

void AudioCapture::Stop() {
  if (!running_.exchange(false)) return;
  if (stopEvent_) SetEvent(stopEvent_);
  if (thread_) {
    WaitForSingleObject(thread_, 5000);
    CloseHandle(thread_);
    thread_ = nullptr;
  }
}

void AudioCapture::CaptureThread() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  DWORD taskIndex = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

  while (running_.load()) {
    if (!OpenStream()) {
      hasDevice_.store(false);
      if (WaitForSingleObject(stopEvent_, 500) == WAIT_OBJECT_0) break;
      continue;
    }
    hasDevice_.store(true);

    // OpenStream runs until stop or failure; reopen on next loop.
    CloseStream();
    hasDevice_.store(false);
    if (!running_.load()) break;
    WaitForSingleObject(stopEvent_, 200);
  }

  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
  CoUninitialize();
}

bool AudioCapture::OpenStream() {
  IMMDeviceEnumerator* enumerator = nullptr;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator));
  if (FAILED(hr)) return false;

  IMMDevice* device = nullptr;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  enumerator->Release();
  if (FAILED(hr) || !device) return false;

  IAudioClient* client = nullptr;
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(&client));
  device->Release();
  if (FAILED(hr) || !client) return false;

  WAVEFORMATEX* mix = nullptr;
  hr = client->GetMixFormat(&mix);
  if (FAILED(hr) || !mix) {
    client->Release();
    return false;
  }

  sampleRate_ = mix->nSamplesPerSec;
  hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                          AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                          kBufferDuration, 0, mix, nullptr);
  if (FAILED(hr)) {
    CoTaskMemFree(mix);
    client->Release();
    return false;
  }

  HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!audioEvent) {
    CoTaskMemFree(mix);
    client->Release();
    return false;
  }
  client->SetEventHandle(audioEvent);

  IAudioCaptureClient* capture = nullptr;
  hr = client->GetService(__uuidof(IAudioCaptureClient),
                          reinterpret_cast<void**>(&capture));
  if (FAILED(hr) || !capture) {
    CloseHandle(audioEvent);
    CoTaskMemFree(mix);
    client->Release();
    return false;
  }

  client->Start();
  HANDLE waits[2] = { stopEvent_, audioEvent };
  std::vector<float> mono;
  mono.reserve(4096);

  while (running_.load()) {
    const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 200);
    if (wait == WAIT_OBJECT_0) break;  // stop
    if (wait == WAIT_TIMEOUT) {
      // No packets — wall-clock silence handled by UI via LastPacketQpc.
      continue;
    }

    UINT32 packet = 0;
    hr = capture->GetNextPacketSize(&packet);
    if (FAILED(hr)) break;

    while (packet > 0) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
      if (FAILED(hr)) break;

      if (frames > 0 && data) {
        const WORD channels = mix->nChannels;
        mono.resize(frames);
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
          std::fill(mono.begin(), mono.end(), 0.f);
        } else if (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                   (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->wBitsPerSample == 32)) {
          const float* src = reinterpret_cast<const float*>(data);
          for (UINT32 i = 0; i < frames; ++i) {
            float sum = 0.f;
            for (WORD c = 0; c < channels; ++c) sum += src[i * channels + c];
            mono[i] = sum / static_cast<float>(channels);
          }
        } else if (mix->wBitsPerSample == 16) {
          const int16_t* src = reinterpret_cast<const int16_t*>(data);
          for (UINT32 i = 0; i < frames; ++i) {
            float sum = 0.f;
            for (WORD c = 0; c < channels; ++c) {
              sum += src[i * channels + c] / 32768.f;
            }
            mono[i] = sum / static_cast<float>(channels);
          }
        } else {
          std::fill(mono.begin(), mono.end(), 0.f);
        }
        ring_.Write(mono.data(), mono.size());
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        lastPacketQpc_.store(static_cast<ULONGLONG>(qpc.QuadPart));
      }

      capture->ReleaseBuffer(frames);
      hr = capture->GetNextPacketSize(&packet);
      if (FAILED(hr)) break;
    }
  }

  client->Stop();
  capture->Release();
  CloseHandle(audioEvent);
  CoTaskMemFree(mix);
  client->Release();
  return true;
}

void AudioCapture::CloseStream() {
  // Stream lifetime is scoped inside OpenStream.
}
