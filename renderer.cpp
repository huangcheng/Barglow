#include "renderer.h"

#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {

D2D1_COLOR_F Premul(const Rgb& c, float a) {
  a = (std::max)(0.f, (std::min)(1.f, a));
  return D2D1::ColorF(c.r * a, c.g * a, c.b * a, a);
}

}  // namespace

VisualizerRenderer::VisualizerRenderer() = default;

VisualizerRenderer::~VisualizerRenderer() {
  Release();
}

void VisualizerRenderer::Release() {
  if (rt_) {
    rt_->Release();
    rt_ = nullptr;
  }
  if (d2dFactory_) {
    d2dFactory_->Release();
    d2dFactory_ = nullptr;
  }
  if (dib_) {
    DeleteObject(dib_);
    dib_ = nullptr;
    bits_ = nullptr;
  }
  if (memDc_) {
    DeleteDC(memDc_);
    memDc_ = nullptr;
  }
  if (screenDc_) {
    ReleaseDC(nullptr, screenDc_);
    screenDc_ = nullptr;
  }
  width_ = height_ = 0;
}

bool VisualizerRenderer::CreateDevice() {
  if (d2dFactory_) return true;
  const HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
  return SUCCEEDED(hr) && d2dFactory_;
}

bool VisualizerRenderer::Resize(int width, int height) {
  if (width <= 0 || height <= 0) return false;
  if (width == width_ && height == height_ && dib_ && rt_) return true;

  if (rt_) {
    rt_->Release();
    rt_ = nullptr;
  }
  if (dib_) {
    DeleteObject(dib_);
    dib_ = nullptr;
    bits_ = nullptr;
  }
  if (!memDc_) {
    if (!screenDc_) screenDc_ = GetDC(nullptr);
    memDc_ = CreateCompatibleDC(screenDc_);
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  dib_ = CreateDIBSection(memDc_, &bmi, DIB_RGB_COLORS, &bits_, nullptr, 0);
  if (!dib_ || !bits_) return false;
  SelectObject(memDc_, dib_);

  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT);

  if (FAILED(d2dFactory_->CreateDCRenderTarget(&props, &rt_)) || !rt_) {
    return false;
  }

  const RECT bind{ 0, 0, width, height };
  if (FAILED(rt_->BindDC(memDc_, &bind))) {
    return false;
  }

  width_ = width;
  height_ = height;
  return true;
}

bool VisualizerRenderer::Ensure(HWND, int width, int height) {
  if (!CreateDevice()) return false;
  return Resize(width, height);
}

void VisualizerRenderer::Paint(const TaskbarInfo& info, const Palette& palette,
                               const SpectrumFrame& frame, bool occluded) {
  if (!rt_ || !bits_) return;

  rt_->BeginDraw();
  rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  rt_->Clear(D2D1::ColorF(0, 0, 0, 0));

  if (!occluded && frame.envelope > 0.01f) {
    ID2D1SolidColorBrush* brush = nullptr;
    rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &brush);
    if (!brush) {
      rt_->EndDraw();
      return;
    }

    const float W = static_cast<float>(width_);
    const float H = static_cast<float>(height_);
    const bool horizontalBar =
        info.edge == TaskbarEdge::Bottom || info.edge == TaskbarEdge::Top;
    const float shortAxis = horizontalBar ? H : W;
    const float longAxis = horizontalBar ? W : H;
    const float gain = (std::max)(0.85f, (std::min)(1.35f, palette.opacityCap / 0.22f));

    // Dense columns of flat LED segments, one hue per column.
    const float slot = (std::max)(5.f, shortAxis * 0.20f);
    const int count = (std::max)(32, static_cast<int>(longAxis / slot));
    const float gap = (std::max)(1.25f, slot * 0.28f);
    const float bw = (std::max)(1.5f, slot - gap);
    const float seg = (std::max)(2.2f, shortAxis * 0.055f);
    const float segGap = (std::max)(1.f, seg * 0.38f);
    const float step = seg + segGap;
    const float maxH = shortAxis * 0.78f;
    const int maxSeg = (std::max)(1, static_cast<int>(maxH / step));

    auto lift = [](Rgb c, float amount) {
      amount = (std::max)(0.f, (std::min)(1.f, amount));
      return Rgb{c.r + (1.f - c.r) * amount, c.g + (1.f - c.g) * amount,
                 c.b + (1.f - c.b) * amount};
    };

    for (int i = 0; i < count; ++i) {
      const float t = (count == 1) ? 0.f : static_cast<float>(i) / (count - 1);
      const int bandIndex = (std::min)(kBandCount - 1, static_cast<int>(t * (kBandCount - 1)));
      const float level = frame.bands[bandIndex];
      const float peak = frame.peaks[bandIndex];
      if (level < 0.02f && peak < 0.02f) continue;

      // Two passes of the spectrum, same as the reference image.
      const float hueT = t * 2.f - (t >= 0.5f ? 1.f : 0.f);
      const Rgb hue = LedStripRgb(hueT, palette.brightness);
      int lit = static_cast<int>((level * maxH + segGap) / step);
      lit = (std::max)(0, (std::min)(maxSeg, lit));
      int peakSeg = static_cast<int>((peak * maxH + segGap) / step) - 1;
      peakSeg = (std::max)(-1, (std::min)(maxSeg - 1, peakSeg));
      if (peakSeg < lit) peakSeg = lit - 1;

      auto drawSeg = [&](int s, bool hot) {
        if (s < 0) return;
        const Rgb c = lift(hue, hot ? 0.18f : 0.f);
        const float a = (hot ? 0.62f : 0.36f) * frame.envelope * gain;
        brush->SetColor(Premul(c, (std::min)(1.f, a)));
        const float along0 = i * slot + gap * 0.5f;
        const float dist = s * step;
        if (info.edge == TaskbarEdge::Bottom) {
          const float y1 = H - dist;
          rt_->FillRectangle(D2D1::RectF(along0, y1 - seg, along0 + bw, y1), brush);
        } else if (info.edge == TaskbarEdge::Top) {
          const float y0 = dist;
          rt_->FillRectangle(D2D1::RectF(along0, y0, along0 + bw, y0 + seg), brush);
        } else if (info.edge == TaskbarEdge::Left) {
          const float x0 = dist;
          rt_->FillRectangle(D2D1::RectF(x0, along0, x0 + seg, along0 + bw), brush);
        } else {
          const float x1 = W - dist;
          rt_->FillRectangle(D2D1::RectF(x1 - seg, along0, x1, along0 + bw), brush);
        }
      };

      for (int s = 0; s < lit; ++s) {
        drawSeg(s, s == lit - 1 && peakSeg == s);
      }
      if (peakSeg >= lit) drawSeg(peakSeg, true);
    }

    brush->Release();
  }

  const HRESULT hr = rt_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    // Caller will Ensure again next frame.
  }
}

bool VisualizerRenderer::Present(HWND hwnd, const TaskbarInfo& info, const Palette& palette,
                                 const SpectrumFrame& frame, bool occluded) {
  const int w = info.rect.right - info.rect.left;
  const int h = info.rect.bottom - info.rect.top;
  if (!Ensure(hwnd, w, h)) return false;

  Paint(info, palette, frame, occluded);

  SIZE size{ w, h };
  POINT src{ 0, 0 };
  POINT dst{ info.rect.left, info.rect.top };
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;

  RECT wr{};
  GetWindowRect(hwnd, &wr);
  dst.x = wr.left;
  dst.y = wr.top;

  return UpdateLayeredWindow(hwnd, screenDc_, &dst, &size, memDc_, &src, 0, &blend,
                             ULW_ALPHA) != FALSE;
}
