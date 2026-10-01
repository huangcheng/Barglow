#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

class SampleRing {
 public:
  explicit SampleRing(size_t capacity = 1 << 15);

  void Write(const float* samples, size_t count);
  size_t Read(float* out, size_t maxCount);
  void Clear();

 private:
  std::vector<float> buffer_;
  std::atomic<size_t> write_{0};
  std::atomic<size_t> read_{0};
  size_t mask_ = 0;
};

class AudioCapture {
 public:
  AudioCapture();
  ~AudioCapture();

  bool Start();
  void Stop();
  SampleRing& Ring() { return ring_; }
  uint32_t SampleRate() const { return sampleRate_; }
  ULONGLONG LastPacketQpc() const { return lastPacketQpc_.load(); }
  bool HasDevice() const { return hasDevice_.load(); }

 private:
  void CaptureThread();
  bool OpenStream();
  void CloseStream();

  SampleRing ring_;
  std::atomic<bool> running_{false};
  std::atomic<bool> hasDevice_{false};
  std::atomic<ULONGLONG> lastPacketQpc_{0};
  HANDLE thread_ = nullptr;
  HANDLE stopEvent_ = nullptr;
  uint32_t sampleRate_ = 48000;
};
