#include "taskbar_host.h"

#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kHostClass[] = L"BarglowTaskbarHost";

TaskbarEdge EdgeFromRect(const RECT& rc, const RECT& monitor) {
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (w >= h) {
    const int midY = (rc.top + rc.bottom) / 2;
    const int monMid = (monitor.top + monitor.bottom) / 2;
    return midY < monMid ? TaskbarEdge::Top : TaskbarEdge::Bottom;
  }
  const int midX = (rc.left + rc.right) / 2;
  const int monMid = (monitor.left + monitor.right) / 2;
  return midX < monMid ? TaskbarEdge::Left : TaskbarEdge::Right;
}

}  // namespace

TaskbarInfo QueryPrimaryTaskbar() {
  TaskbarInfo info;
  info.tray = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (!info.tray || !IsWindow(info.tray)) return info;

  APPBARDATA abd{};
  abd.cbSize = sizeof(abd);
  abd.hWnd = info.tray;
  if (SHAppBarMessage(ABM_GETTASKBARPOS, &abd)) {
    info.rect = abd.rc;
  } else if (!GetWindowRect(info.tray, &info.rect)) {
    return info;
  }

  HMONITOR mon = MonitorFromWindow(info.tray, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{ sizeof(mi) };
  GetMonitorInfoW(mon, &mi);
  info.edge = EdgeFromRect(info.rect, mi.rcMonitor);
  info.valid = (info.rect.right > info.rect.left && info.rect.bottom > info.rect.top);
  return info;
}

bool TaskbarLikelyOccluded(const TaskbarInfo& info) {
  if (!info.valid || !info.tray) return true;
  if (!IsWindowVisible(info.tray)) return true;

  APPBARDATA abd{};
  abd.cbSize = sizeof(abd);
  const UINT state = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
  if (state & ABS_AUTOHIDE) {
    HMONITOR mon = MonitorFromRect(&info.rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    RECT isect{};
    if (!IntersectRect(&isect, &info.rect, &mi.rcMonitor)) return true;
    const int area = (isect.right - isect.left) * (isect.bottom - isect.top);
    const int full = (info.rect.right - info.rect.left) * (info.rect.bottom - info.rect.top);
    if (full > 0 && area * 4 < full) return true;
  }
  return false;
}

TaskbarHost::~TaskbarHost() {
  Destroy();
}

bool TaskbarHost::Create(HINSTANCE instance, WNDPROC proc, void* userData) {
  instance_ = instance;
  proc_ = proc;
  userData_ = userData;

  WNDCLASSEXW wc{ sizeof(wc) };
  wc.lpfnWndProc = proc_;
  wc.hInstance = instance_;
  wc.lpszClassName = kHostClass;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  atom_ = RegisterClassExW(&wc);

  info_ = QueryPrimaryTaskbar();
  if (!info_.valid) return false;

  // Win11 taskbar icons + acrylic live in one XAML composition surface. A child HWND
  // under that surface is fully covered, so Overlay is the only visible host path.
  if (CreateOverlay()) {
    mode_ = HostMode::Overlay;
    return true;
  }
  if (CreateChild()) {
    mode_ = HostMode::Child;
    return true;
  }
  mode_ = HostMode::None;
  return false;
}

void TaskbarHost::Destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  mode_ = HostMode::None;
}

bool TaskbarHost::Recreate() {
  Destroy();
  return Create(instance_, proc_, userData_);
}

bool TaskbarHost::SyncGeometry() {
  info_ = QueryPrimaryTaskbar();
  if (!info_.valid || !hwnd_) return false;

  const int w = info_.rect.right - info_.rect.left;
  const int h = info_.rect.bottom - info_.rect.top;

  if (mode_ == HostMode::Child) {
    POINT pt{ info_.rect.left, info_.rect.top };
    ScreenToClient(info_.tray, &pt);
    SetWindowPos(hwnd_, HWND_TOP, pt.x, pt.y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
  } else {
    SetWindowPos(hwnd_, HWND_TOPMOST, info_.rect.left, info_.rect.top, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  return true;
}

bool TaskbarHost::CreateChild() {
  if (!info_.tray) return false;
  const int w = info_.rect.right - info_.rect.left;
  const int h = info_.rect.bottom - info_.rect.top;

  hwnd_ = CreateWindowExW(
      WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED,
      kHostClass, L"", WS_POPUP,
      info_.rect.left, info_.rect.top, w, h,
      nullptr, nullptr, instance_, userData_);
  if (!hwnd_) return false;

  SetWindowLongPtrW(hwnd_, GWL_STYLE,
                    (GetWindowLongPtrW(hwnd_, GWL_STYLE) & ~WS_POPUP) | WS_CHILD | WS_VISIBLE);
  SetParent(hwnd_, info_.tray);

  POINT pt{ info_.rect.left, info_.rect.top };
  ScreenToClient(info_.tray, &pt);
  SetWindowPos(hwnd_, HWND_TOP, pt.x, pt.y, w, h,
               SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
  return true;
}

bool TaskbarHost::CreateOverlay() {
  const int w = info_.rect.right - info_.rect.left;
  const int h = info_.rect.bottom - info_.rect.top;
  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
      kHostClass, L"", WS_POPUP,
      info_.rect.left, info_.rect.top, w, h,
      nullptr, nullptr, instance_, userData_);
  if (!hwnd_) return false;
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  return true;
}
