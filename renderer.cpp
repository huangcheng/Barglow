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

    // Solid flat bars with peak caps; hue sweeps the saturated EQ spectrum twice.
    const float slot = (std::max)(6.5f, shortAxis * 0.30f);
    const int count = (std::max)(28, static_cast<int>(longAxis / slot));
    const float gap = (std::max)(1.5f, slot * 0.26f);
    const float bw = (std::max)(1.5f, slot - gap);
    const float capThick = (std::max)(2.f, (std::min)(2.8f, bw * 0.30f));
    const float capGap = 1.4f;
    const float maxH = shortAxis * 0.82f;

    for (int i = 0; i < count; ++i) {
      const float t = (count == 1) ? 0.f : static_cast<float>(i) / (count - 1);
      const int bandIndex = (std::min)(kBandCount - 1, static_cast<int>(t * (kBandCount - 1)));
      const float level = frame.bands[bandIndex];
      const float peak = frame.peaks[bandIndex];
      const float bh = level * maxH;
      float ph = peak * maxH;
      if (ph > bh + 0.4f) ph = (std::max)(ph, bh + capGap + capThick);
      ph = (std::max)(capThick, ph);
      if (bh < 1.f && peak < 0.02f) continue;

      const float hueT = t * 2.f - (t >= 0.5f ? 1.f : 0.f);
      const Rgb c = LedStripRgb(hueT, palette.brightness);
      const float bodyA = (0.28f + level * 0.34f) * frame.envelope * gain;
      const float tipA = (std::min)(1.f, (0.72f + peak * 0.18f) * frame.envelope * gain);

      auto fillBar = [&](float x0, float y0, float x1, float y1, D2D1_POINT_2F tip,
                         D2D1_POINT_2F base) {
        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP gs[3]{};
        gs[0].position = 0.f;
        gs[1].position = 0.4f;
        gs[2].position = 1.f;
        gs[0].color = Premul(c, 1.f);
        gs[1].color = Premul(c, 0.55f);
        gs[2].color = Premul(c, 0.08f);
        rt_->CreateGradientStopCollection(gs, 3, &stops);
        if (!stops) return;
        ID2D1LinearGradientBrush* fill = nullptr;
        rt_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(tip, base), stops,
                                       &fill);
        if (fill) {
          fill->SetOpacity(bodyA);
          rt_->FillRectangle(D2D1::RectF(x0, y0, x1, y1), fill);
          fill->Release();
        }
        stops->Release();
      };

      if (info.edge == TaskbarEdge::Bottom) {
        const float x = i * slot + gap * 0.5f;
        if (bh > 1.f) {
          fillBar(x, H - bh, x + bw, H, D2D1::Point2F(x, H - bh), D2D1::Point2F(x, H));
        }
        if (peak > 0.02f) {
          brush->SetColor(Premul(c, tipA));
          const float top = H - ph;
          rt_->FillRectangle(D2D1::RectF(x, top, x + bw, top + capThick), brush);
        }
      } else if (info.edge == TaskbarEdge::Top) {
        const float x = i * slot + gap * 0.5f;
        if (bh > 1.f) {
          fillBar(x, 0, x + bw, bh, D2D1::Point2F(x, bh), D2D1::Point2F(x, 0));
        }
        if (peak > 0.02f) {
          brush->SetColor(Premul(c, tipA));
          rt_->FillRectangle(D2D1::RectF(x, ph - capThick, x + bw, ph), brush);
        }
      } else if (info.edge == TaskbarEdge::Left) {
        const float y = i * slot + gap * 0.5f;
        if (bh > 1.f) {
          fillBar(0, y, bh, y + bw, D2D1::Point2F(bh, y), D2D1::Point2F(0, y));
        }
        if (peak > 0.02f) {
          brush->SetColor(Premul(c, tipA));
          rt_->FillRectangle(D2D1::RectF(ph - capThick, y, ph, y + bw), brush);
        }
      } else {
        const float y = i * slot + gap * 0.5f;
        if (bh > 1.f) {
          fillBar(W - bh, y, W, y + bw, D2D1::Point2F(W - bh, y), D2D1::Point2F(W, y));
        }
        if (peak > 0.02f) {
          brush->SetColor(Premul(c, tipA));
          const float tip = W - ph;
          rt_->FillRectangle(D2D1::RectF(tip, y, tip + capThick, y + bw), brush);
        }
      }
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
