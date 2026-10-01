#pragma once

#include "theme.h"

#include <windows.h>

struct TaskbarInfo {
  HWND tray = nullptr;
  RECT rect{};
  TaskbarEdge edge = TaskbarEdge::Bottom;
  bool valid = false;
};

TaskbarInfo QueryPrimaryTaskbar();
bool TaskbarLikelyOccluded(const TaskbarInfo& info);

enum class HostMode {
  None,
  Child,
  Overlay,
};

class TaskbarHost {
 public:
  TaskbarHost() = default;
  ~TaskbarHost();

  bool Create(HINSTANCE instance, WNDPROC proc, void* userData);
  void Destroy();
  bool Recreate();
  bool SyncGeometry();

  HWND Hwnd() const { return hwnd_; }
  HostMode Mode() const { return mode_; }
  TaskbarInfo Info() const { return info_; }

 private:
  bool CreateChild();
  bool CreateOverlay();

  HINSTANCE instance_ = nullptr;
  WNDPROC proc_ = nullptr;
  void* userData_ = nullptr;
  HWND hwnd_ = nullptr;
  HostMode mode_ = HostMode::None;
  TaskbarInfo info_{};
  ATOM atom_ = 0;
};
