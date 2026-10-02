#include "taskbar_host.h"

#include <shellapi.h>
#include <shobjidl.h>

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

bool IsShellExperienceProcess(HWND hwnd) {
  if (!hwnd) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!pid) return false;

  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) return false;

  wchar_t path[MAX_PATH]{};
  DWORD n = MAX_PATH;
  const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &n);
  CloseHandle(proc);
  if (!ok || n == 0) return false;

  const wchar_t* base = path;
  for (const wchar_t* p = path; *p; ++p) {
    if (*p == L'\\' || *p == L'/') base = p + 1;
  }
  return lstrcmpiW(base, L"StartMenuExperienceHost.exe") == 0 ||
         lstrcmpiW(base, L"SearchHost.exe") == 0 ||
         lstrcmpiW(base, L"ShellExperienceHost.exe") == 0 ||
         lstrcmpiW(base, L"ShellHost.exe") == 0 ||
         lstrcmpiW(base, L"TextInputHost.exe") == 0;
}

bool IsFullscreenAppCoveringTaskbar(const TaskbarInfo& info) {
  if (!info.valid) return true;

  // Exclusive / D3D / presentation fullscreen.
  QUERY_USER_NOTIFICATION_STATE notify = QUNS_ACCEPTS_NOTIFICATIONS;
  if (SUCCEEDED(SHQueryUserNotificationState(&notify))) {
    if (notify == QUNS_BUSY || notify == QUNS_RUNNING_D3D_FULL_SCREEN ||
        notify == QUNS_PRESENTATION_MODE) {
      return true;
    }
  }

  // Borderless fullscreen: foreground window covers the monitor (including taskbar strip).
  HWND fg = GetForegroundWindow();
  if (!fg || !IsWindowVisible(fg)) return false;
  if (fg == GetShellWindow()) return false;
  // Start / Search / shell flyouts can be large or flip QUNS_APP — never treat as game fullscreen.
  if (IsShellExperienceProcess(fg)) return false;

  wchar_t cls[256]{};
  GetClassNameW(fg, cls, 256);
  if (lstrcmpiW(cls, L"Shell_TrayWnd") == 0 || lstrcmpiW(cls, L"Shell_SecondaryTrayWnd") == 0 ||
      lstrcmpiW(cls, L"Progman") == 0 || lstrcmpiW(cls, L"WorkerW") == 0) {
    return false;
  }

  HMONITOR mon = MonitorFromRect(&info.rect, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{ sizeof(mi) };
  if (!GetMonitorInfoW(mon, &mi)) return false;

  RECT wr{};
  if (!GetWindowRect(fg, &wr)) return false;

  // Nearly full monitor coverage (allow a few px for borders).
  const int mw = mi.rcMonitor.right - mi.rcMonitor.left;
  const int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
  const int ww = wr.right - wr.left;
  const int wh = wr.bottom - wr.top;
  if (ww >= mw - 4 && wh >= mh - 4) {
    RECT cover{};
    if (IntersectRect(&cover, &wr, &info.rect)) {
      const int coverArea = (cover.right - cover.left) * (cover.bottom - cover.top);
      const int barArea = (info.rect.right - info.rect.left) * (info.rect.bottom - info.rect.top);
      if (barArea > 0 && coverArea * 2 > barArea) return true;
    }
  }

  // Taskbar strip actually covered by another top-level window.
  POINT samples[3] = {
      {(info.rect.left + info.rect.right) / 2, (info.rect.top + info.rect.bottom) / 2},
      {info.rect.left + 8, (info.rect.top + info.rect.bottom) / 2},
      {info.rect.right - 8, (info.rect.top + info.rect.bottom) / 2},
  };
  int covered = 0;
  for (POINT pt : samples) {
    HWND hit = WindowFromPoint(pt);
    if (!hit) continue;
    HWND root = GetAncestor(hit, GA_ROOT);
    if (!root) root = hit;
    if (root == info.tray) continue;
    if (info.tray && IsChild(info.tray, hit)) continue;
    if (IsShellExperienceProcess(root)) continue;
    // Ignore our own overlay host.
    wchar_t hitCls[128]{};
    GetClassNameW(root, hitCls, 128);
    if (lstrcmpiW(hitCls, L"BarglowTaskbarHost") == 0) continue;
    ++covered;
  }
  return covered >= 2;
}

bool TaskbarLikelyOccluded(const TaskbarInfo& info) {
  if (!info.valid || !info.tray) return true;
  if (!IsWindowVisible(info.tray)) return true;
  if (IsFullscreenAppCoveringTaskbar(info)) return true;

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
  const UINT showFlag = visible_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;

  if (mode_ == HostMode::Child) {
    POINT pt{ info_.rect.left, info_.rect.top };
    ScreenToClient(info_.tray, &pt);
    SetWindowPos(hwnd_, HWND_TOP, pt.x, pt.y, w, h,
                 SWP_NOACTIVATE | showFlag | SWP_NOSENDCHANGING);
  } else {
    SetWindowPos(hwnd_, HWND_TOPMOST, info_.rect.left, info_.rect.top, w, h,
                 SWP_NOACTIVATE | showFlag);
  }
  return true;
}

void TaskbarHost::SetVisible(bool visible) {
  if (!hwnd_) {
    visible_ = visible;
    return;
  }
  if (visible_ == visible && !!IsWindowVisible(hwnd_) == visible) return;
  visible_ = visible;
  ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
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
  // Own the popup to Shell_TrayWnd (WS_POPUP + owner, not WS_CHILD). Owned windows
  // stay above their owner in z-order, which survives Start/Search on Win11; a
  // free-floating HWND_TOPMOST overlay gets permanently demoted behind the bar.
  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
      kHostClass, L"", WS_POPUP,
      info_.rect.left, info_.rect.top, w, h,
      info_.tray, nullptr, instance_, userData_);
  if (!hwnd_) return false;
  SetWindowPos(hwnd_, HWND_TOPMOST, info_.rect.left, info_.rect.top, w, h,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  return true;
}
