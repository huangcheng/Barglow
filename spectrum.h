#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <vector>

constexpr int kFftSize = 2048;
constexpr int kBandCount = 64;

struct SpectrumFrame {
  std::array<float, kBandCount> bands{};
  std::array<float, kBandCount> peaks{};  // Winamp-style falling caps (0..1)
  float bass = 0.f;
  float envelope = 0.f;  // 0..1 master fade
  bool audible = false;
};

class SpectrumAnalyzer {
 public:
  SpectrumAnalyzer();

  void SetSampleRate(uint32_t sampleRate);
  void PushSamples(const float* samples, size_t count);
  SpectrumFrame Update(bool enabled, ULONGLONG lastPacketQpc, ULONGLONG qpcFreq);

 private:
  void EnsureTables();
  void ComputeFft();

  uint32_t sampleRate_ = 48000;
  std::vector<float> timeBuf_;
  size_t writePos_ = 0;
  std::vector<float> window_;
  std::vector<float> fftRe_;
  std::vector<float> fftIm_;
  std::array<float, kBandCount> smooth_{};
  std::array<float, kBandCount> peaks_{};
  std::array<float, kBandCount> peakHoldSec_{};
  float envelope_ = 0.f;
  ULONGLONG lastEnergyQpc_ = 0;
  ULONGLONG lastPeakQpc_ = 0;
  bool tablesReady_ = false;
};
