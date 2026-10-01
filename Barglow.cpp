#include "framework.h"
#include "Barglow.h"

#include "audio_capture.h"
#include "renderer.h"
#include "settings.h"
#include "settings_dlg.h"
#include "spectrum.h"
#include "taskbar_host.h"
#include "theme.h"

#include <shellapi.h>

#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_APP_FOCUS = WM_APP + 2;
constexpr UINT_PTR kTimerVisual = 1;
constexpr UINT_PTR kTimerGeometry = 2;

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid{};
UINT g_taskbarCreated = 0;
HANDLE g_mutex = nullptr;

AppSettings g_settings{};
bool g_settingsDirty = false;
AudioCapture g_audio;
SpectrumAnalyzer g_spectrum;
VisualizerRenderer g_renderer;
TaskbarHost g_host;
LARGE_INTEGER g_qpcFreq{};

void ShowContextMenu(HWND hwnd) {
  POINT pt{};
  GetCursorPos(&pt);
  HMENU menu = LoadMenuW(g_instance, MAKEINTRESOURCEW(IDC_BARGLOW));
  if (!menu) return;
  HMENU popup = GetSubMenu(menu, 0);
  SetForegroundWindow(hwnd);
  TrackPopupMenu(popup, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

void AddTrayIcon(HWND hwnd) {
  ZeroMemory(&g_nid, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAY;
  g_nid.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_BARGLOW));
  lstrcpynW(g_nid.szTip, L"Barglow", ARRAYSIZE(g_nid.szTip));
  Shell_NotifyIconW(NIM_ADD, &g_nid);
  g_nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

void RemoveTrayIcon() {
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

INT_PTR CALLBACK AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
  switch (message) {
    case WM_INITDIALOG:
      return TRUE;
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, LOWORD(wParam));
        return TRUE;
      }
      break;
  }
  return FALSE;
}

void OpenSettings() {
  SettingsDialogShow(g_instance, g_hwnd, g_settings, &g_settingsDirty);
}

bool EnsureHost() {
  if (g_host.Hwnd()) {
    g_host.SyncGeometry();
    return true;
  }
  return g_host.Create(
      g_instance,
      [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_NCHITTEST) return HTTRANSPARENT;
        if (m == WM_NCCREATE) {
          auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
          SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        return DefWindowProcW(h, m, w, l);
      },
      nullptr);
}

void TickVisual() {
  if (!EnsureHost()) return;

  const TaskbarInfo info = g_host.Info();
  const bool occluded = TaskbarLikelyOccluded(info) || !g_settings.enabled;

  std::vector<float> chunk(4096);
  const size_t n = g_audio.Ring().Read(chunk.data(), chunk.size());
  if (n > 0) {
    g_spectrum.SetSampleRate(g_audio.SampleRate());
    g_spectrum.PushSamples(chunk.data(), n);
  }

  const SpectrumFrame frame =
      g_spectrum.Update(g_settings.enabled && !occluded, g_audio.LastPacketQpc(),
                        static_cast<ULONGLONG>(g_qpcFreq.QuadPart));
  const Palette palette = ResolvePalette(g_settings);
  g_renderer.Present(g_host.Hwnd(), info, palette, frame, occluded);
}

void ShutdownApp() {
  KillTimer(g_hwnd, kTimerVisual);
  KillTimer(g_hwnd, kTimerGeometry);
  RemoveTrayIcon();
  g_audio.Stop();
  g_renderer.Release();
  g_host.Destroy();
  if (g_hwnd) {
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
  }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == g_taskbarCreated) {
    AddTrayIcon(hwnd);
    g_host.Recreate();
    return 0;
  }

  switch (msg) {
    case WM_CREATE:
      AddTrayIcon(hwnd);
      SetTimer(hwnd, kTimerVisual, 33, nullptr);
      SetTimer(hwnd, kTimerGeometry, 1000, nullptr);
      g_audio.Start();
      EnsureHost();
      return 0;

    case WM_TRAY:
      if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP) {
        ShowContextMenu(hwnd);
      } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == NIN_SELECT) {
        OpenSettings();
      }
      return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case IDM_SETTINGS:
          OpenSettings();
          break;
        case IDM_ABOUT:
          DialogBoxW(g_instance, MAKEINTRESOURCEW(IDD_ABOUTBOX), hwnd, AboutProc);
          break;
        case IDM_EXIT:
          ShutdownApp();
          PostQuitMessage(0);
          break;
      }
      return 0;

    case WM_TIMER:
      if (wParam == kTimerVisual) {
        TickVisual();
      } else if (wParam == kTimerGeometry) {
        g_host.SyncGeometry();
      }
      return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
      g_host.SyncGeometry();
      return 0;

    case WM_SETTINGCHANGE:
      if (lParam &&
          (lstrcmpiW(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0 ||
           lstrcmpiW(reinterpret_cast<LPCWSTR>(lParam), L"TraySettings") == 0)) {
        g_settingsDirty = true;
        g_host.SyncGeometry();
      }
      return 0;

    case WM_APP_FOCUS:
      OpenSettings();
      return 0;

    case WM_ENDSESSION:
      if (wParam) {
        SettingsSave(g_settings);
        RemoveTrayIcon();
        g_audio.Stop();
      }
      return 0;

    case WM_DESTROY:
      RemoveTrayIcon();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool CreateMainWindow() {
  const wchar_t* cls = L"BarglowHiddenMain";
  WNDCLASSEXW wc{ sizeof(wc) };
  wc.lpfnWndProc = MainWndProc;
  wc.hInstance = g_instance;
  wc.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_BARGLOW));
  wc.hIconSm = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_SMALL));
  wc.lpszClassName = cls;
  RegisterClassExW(&wc);

  g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, cls, L"Barglow", WS_POPUP, 0, 0, 0, 0,
                           nullptr, nullptr, g_instance, nullptr);
  return g_hwnd != nullptr;
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR,
                      _In_ int) {
  g_instance = hInstance;
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  QueryPerformanceFrequency(&g_qpcFreq);

  g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\Barglow.SingleInstance");
  if (!g_mutex) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = FindWindowW(L"BarglowHiddenMain", L"Barglow");
    if (existing) PostMessageW(existing, WM_APP_FOCUS, 0, 0);
    return 0;
  }

  INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
  InitCommonControlsEx(&icc);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  SettingsLoad(g_settings);
  if (!CreateMainWindow()) {
    CoUninitialize();
    ReleaseMutex(g_mutex);
    CloseHandle(g_mutex);
    return 1;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (HWND settings = SettingsDialogHwnd()) {
      if (IsDialogMessageW(settings, &msg)) continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  SettingsSave(g_settings);
  g_audio.Stop();
  g_renderer.Release();
  g_host.Destroy();
  CoUninitialize();
  ReleaseMutex(g_mutex);
  CloseHandle(g_mutex);
  return static_cast<int>(msg.wParam);
}
