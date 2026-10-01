#pragma once

#include "spectrum.h"
#include "taskbar_host.h"
#include "theme.h"

#include <d2d1.h>
#include <wincodec.h>

class VisualizerRenderer {
 public:
  VisualizerRenderer();
  ~VisualizerRenderer();

  bool Ensure(HWND hwnd, int width, int height);
  void Release();
  bool Present(HWND hwnd, const TaskbarInfo& info, const Palette& palette,
               const SpectrumFrame& frame, bool occluded);

 private:
  bool CreateDevice();
  bool Resize(int width, int height);
  void Paint(const TaskbarInfo& info, const Palette& palette,
             const SpectrumFrame& frame, bool occluded);

  int width_ = 0;
  int height_ = 0;
  HDC screenDc_ = nullptr;
  HDC memDc_ = nullptr;
  HBITMAP dib_ = nullptr;
  void* bits_ = nullptr;
  ID2D1Factory* d2dFactory_ = nullptr;
  ID2D1DCRenderTarget* rt_ = nullptr;
};
