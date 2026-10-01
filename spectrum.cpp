#include "spectrum.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

void FftRadix2(std::vector<float>& re, std::vector<float>& im) {
  const int n = static_cast<int>(re.size());
  int j = 0;
  for (int i = 1; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float ang = -2.f * kPi / static_cast<float>(len);
    const float wlenRe = std::cos(ang);
    const float wlenIm = std::sin(ang);
    for (int i = 0; i < n; i += len) {
      float wRe = 1.f;
      float wIm = 0.f;
      for (int k = 0; k < len / 2; ++k) {
        const float uRe = re[i + k];
        const float uIm = im[i + k];
        const float vRe = re[i + k + len / 2] * wRe - im[i + k + len / 2] * wIm;
        const float vIm = re[i + k + len / 2] * wIm + im[i + k + len / 2] * wRe;
        re[i + k] = uRe + vRe;
        im[i + k] = uIm + vIm;
        re[i + k + len / 2] = uRe - vRe;
        im[i + k + len / 2] = uIm - vIm;
        const float nextWRe = wRe * wlenRe - wIm * wlenIm;
        wIm = wRe * wlenIm + wIm * wlenRe;
        wRe = nextWRe;
      }
    }
  }
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer() {
  timeBuf_.assign(kFftSize, 0.f);
  fftRe_.assign(kFftSize, 0.f);
  fftIm_.assign(kFftSize, 0.f);
  EnsureTables();
}

void SpectrumAnalyzer::SetSampleRate(uint32_t sampleRate) {
  if (sampleRate == 0) return;
  sampleRate_ = sampleRate;
}

void SpectrumAnalyzer::EnsureTables() {
  if (tablesReady_) return;
  window_.resize(kFftSize);
  for (int i = 0; i < kFftSize; ++i) {
    window_[i] = 0.5f * (1.f - std::cos(2.f * kPi * i / (kFftSize - 1)));
  }
  tablesReady_ = true;
}

void SpectrumAnalyzer::PushSamples(const float* samples, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    timeBuf_[writePos_] = samples[i];
    writePos_ = (writePos_ + 1) % kFftSize;
  }
}

void SpectrumAnalyzer::ComputeFft() {
  EnsureTables();
  for (int i = 0; i < kFftSize; ++i) {
    const size_t idx = (writePos_ + static_cast<size_t>(i)) % kFftSize;
    fftRe_[i] = timeBuf_[idx] * window_[i];
    fftIm_[i] = 0.f;
  }
  FftRadix2(fftRe_, fftIm_);
}

SpectrumFrame SpectrumAnalyzer::Update(bool enabled, ULONGLONG lastPacketQpc,
                                       ULONGLONG qpcFreq) {
  SpectrumFrame frame;
  if (!enabled) {
    envelope_ *= 0.85f;
    if (envelope_ < 0.01f) envelope_ = 0.f;
    frame.envelope = envelope_;
    for (int i = 0; i < kBandCount; ++i) {
      peaks_[i] *= envelope_;
      peakHoldSec_[i] = 0.f;
      frame.peaks[i] = peaks_[i];
    }
    return frame;
  }

  ComputeFft();

  const float nyquist = sampleRate_ * 0.5f;
  const float minHz = 30.f;
  const float maxHz = (std::min)(16000.f, nyquist - 1.f);
  float rms = 0.f;

  for (int b = 0; b < kBandCount; ++b) {
    const float t0 = static_cast<float>(b) / kBandCount;
    const float t1 = static_cast<float>(b + 1) / kBandCount;
    const float f0 = minHz * std::pow(maxHz / minHz, t0);
    const float f1 = minHz * std::pow(maxHz / minHz, t1);
    const int i0 = (std::max)(1, static_cast<int>(f0 / nyquist * (kFftSize / 2)));
    const int i1 = (std::min)(kFftSize / 2 - 1,
                              static_cast<int>(f1 / nyquist * (kFftSize / 2)));
    float mag = 0.f;
    int n = 0;
    for (int i = i0; i <= i1; ++i) {
      const float re = fftRe_[i];
      const float im = fftIm_[i];
      mag += std::sqrt(re * re + im * im);
      ++n;
    }
    if (n > 0) mag /= static_cast<float>(n);
    mag = std::log1p(mag * 8.f) / 4.f;
    mag = (std::min)(1.f, mag);
    const float k = mag > smooth_[b] ? 0.45f : 0.12f;
    smooth_[b] += (mag - smooth_[b]) * k;
    frame.bands[b] = smooth_[b];
    rms += smooth_[b] * smooth_[b];
  }

  float bass = 0.f;
  const int bassN = (std::max)(1, kBandCount * 18 / 100);
  for (int i = 0; i < bassN; ++i) bass += smooth_[i];
  frame.bass = bass / static_cast<float>(bassN);
  rms = std::sqrt(rms / kBandCount);

  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  const ULONGLONG nowQpc = static_cast<ULONGLONG>(now.QuadPart);
  if (rms > 0.04f) {
    lastEnergyQpc_ = nowQpc;
  }

  const double silentMs = (lastEnergyQpc_ == 0 || qpcFreq == 0)
                              ? 1e9
                              : (nowQpc - lastEnergyQpc_) * 1000.0 / qpcFreq;
  const double packetAgeMs =
      (lastPacketQpc == 0 || qpcFreq == 0)
          ? 1e9
          : (nowQpc - lastPacketQpc) * 1000.0 / qpcFreq;

  const bool audible = silentMs < 500.0 && packetAgeMs < 1500.0 && rms > 0.02f;
  frame.audible = audible;
  const float goal = audible ? 1.f : 0.f;
  envelope_ += (goal - envelope_) * (audible ? 0.35f : 0.12f);
  if (!audible && envelope_ < 0.01f) envelope_ = 0.f;
  frame.envelope = envelope_;

  for (int i = 0; i < kBandCount; ++i) {
    frame.bands[i] *= envelope_;
  }
  frame.bass *= envelope_;

  // Winamp-style peak caps: snap up with the bar, brief hold, then fall slower.
  double dt = 1.0 / 60.0;
  if (lastPeakQpc_ != 0 && qpcFreq != 0) {
    dt = (nowQpc - lastPeakQpc_) * 1.0 / static_cast<double>(qpcFreq);
    dt = (std::max)(0.0, (std::min)(0.05, dt));
  }
  lastPeakQpc_ = nowQpc;

  constexpr float kPeakHoldSec = 0.35f;
  constexpr float kPeakFallPerSec = 0.9f;  // slower than bar decay

  for (int i = 0; i < kBandCount; ++i) {
    const float bar = frame.bands[i];
    if (bar >= peaks_[i]) {
      peaks_[i] = bar;
      peakHoldSec_[i] = kPeakHoldSec;
    } else if (peakHoldSec_[i] > 0.f) {
      peakHoldSec_[i] -= static_cast<float>(dt);
      if (peakHoldSec_[i] < 0.f) peakHoldSec_[i] = 0.f;
    } else {
      peaks_[i] -= kPeakFallPerSec * static_cast<float>(dt);
      if (peaks_[i] < bar) peaks_[i] = bar;
      if (peaks_[i] < 0.f) peaks_[i] = 0.f;
    }
    frame.peaks[i] = peaks_[i];
  }

  return frame;
}
