#include "settings_dlg.h"

#include "Resource.h"
#include "i18n.h"
#include "settings.h"

#include <shellapi.h>

namespace {

HWND g_settingsHwnd = nullptr;
AppSettings* g_settings = nullptr;
bool* g_dirty = nullptr;
HINSTANCE g_instance = nullptr;

void LocalizeSettingsDialog(HWND hDlg) {
  SetWindowTextW(hDlg, Tr(StrId::SettingsTitle));
  SetDlgItemTextW(hDlg, IDC_GRP_STRENGTH, Tr(StrId::Strength));
  SetDlgItemTextW(hDlg, IDC_STRENGTH_QUIET, Tr(StrId::Quiet));
  SetDlgItemTextW(hDlg, IDC_STRENGTH_PRESENT, Tr(StrId::Present));
  SetDlgItemTextW(hDlg, IDC_STRENGTH_LOUD, Tr(StrId::Loud));
  SetDlgItemTextW(hDlg, IDC_GRP_THEME, Tr(StrId::Theme));
  SetDlgItemTextW(hDlg, IDC_THEME_FOLLOW, Tr(StrId::FollowWindows));
  SetDlgItemTextW(hDlg, IDC_THEME_DARK, Tr(StrId::ForceDark));
  SetDlgItemTextW(hDlg, IDC_THEME_LIGHT, Tr(StrId::ForceLight));
  SetDlgItemTextW(hDlg, IDC_GRP_LANGUAGE, Tr(StrId::Language));
  SetDlgItemTextW(hDlg, IDC_LANG_FOLLOW, Tr(StrId::LangFollowSystem));
  SetDlgItemTextW(hDlg, IDC_LANG_EN, Tr(StrId::LangEnglish));
  SetDlgItemTextW(hDlg, IDC_LANG_ZH, Tr(StrId::LangZhHans));
  SetDlgItemTextW(hDlg, IDC_ENABLE, Tr(StrId::EnableVisualizer));
  SetDlgItemTextW(hDlg, IDC_AUTOSTART, Tr(StrId::StartWithWindows));
  SetDlgItemTextW(hDlg, IDOK, Tr(StrId::Close));
}

void SyncControls(HWND hDlg) {
  if (!g_settings) return;
  CheckRadioButton(hDlg, IDC_STRENGTH_QUIET, IDC_STRENGTH_LOUD,
                   IDC_STRENGTH_QUIET + static_cast<int>(g_settings->strength));
  CheckRadioButton(hDlg, IDC_THEME_FOLLOW, IDC_THEME_LIGHT,
                   IDC_THEME_FOLLOW + static_cast<int>(g_settings->themeMode));
  CheckRadioButton(hDlg, IDC_LANG_FOLLOW, IDC_LANG_ZH,
                   IDC_LANG_FOLLOW + static_cast<int>(g_settings->language));
  CheckDlgButton(hDlg, IDC_ENABLE, g_settings->enabled ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hDlg, IDC_AUTOSTART,
                 g_settings->startWithWindows ? BST_CHECKED : BST_UNCHECKED);
}

void MarkDirty() {
  if (g_dirty) *g_dirty = true;
  if (g_settings) SettingsSave(*g_settings);
}

INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
  switch (msg) {
    case WM_INITDIALOG:
      ApplyDialogIcon(hDlg, g_instance);
      LocalizeSettingsDialog(hDlg);
      SyncControls(hDlg);
      CenterDialogOnDesktop(hDlg);
      return TRUE;
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      if (!g_settings) break;

      if (id == IDC_STRENGTH_QUIET || id == IDC_STRENGTH_PRESENT ||
          id == IDC_STRENGTH_LOUD) {
        g_settings->strength = static_cast<StrengthId>(id - IDC_STRENGTH_QUIET);
        MarkDirty();
      } else if (id == IDC_THEME_FOLLOW || id == IDC_THEME_DARK || id == IDC_THEME_LIGHT) {
        g_settings->themeMode = static_cast<ThemeMode>(id - IDC_THEME_FOLLOW);
        MarkDirty();
      } else if (id == IDC_LANG_FOLLOW || id == IDC_LANG_EN || id == IDC_LANG_ZH) {
        g_settings->language = static_cast<LanguagePreference>(id - IDC_LANG_FOLLOW);
        ApplyLanguagePreference(g_settings->language);
        LocalizeSettingsDialog(hDlg);
        // Refresh tray tip language.
        NOTIFYICONDATAW tip{};
        tip.cbSize = sizeof(tip);
        tip.hWnd = FindWindowW(L"BarglowHiddenMain", L"Barglow");
        tip.uID = 1;
        tip.uFlags = NIF_TIP;
        lstrcpynW(tip.szTip, Tr(StrId::TrayTip), ARRAYSIZE(tip.szTip));
        Shell_NotifyIconW(NIM_MODIFY, &tip);
        MarkDirty();
      } else if (id == IDC_ENABLE) {
        g_settings->enabled = IsDlgButtonChecked(hDlg, IDC_ENABLE) == BST_CHECKED;
        MarkDirty();
      } else if (id == IDC_AUTOSTART) {
        g_settings->startWithWindows =
            IsDlgButtonChecked(hDlg, IDC_AUTOSTART) == BST_CHECKED;
        MarkDirty();
      } else if (id == IDOK || id == IDCANCEL) {
        DestroyWindow(hDlg);
        return TRUE;
      }
      break;
    }
    case WM_CLOSE:
      DestroyWindow(hDlg);
      return TRUE;
    case WM_DESTROY:
      g_settingsHwnd = nullptr;
      return TRUE;
  }
  return FALSE;
}

}  // namespace

void CenterDialogOnDesktop(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return;

  RECT wr{};
  GetWindowRect(hwnd, &wr);
  const int w = wr.right - wr.left;
  const int h = wr.bottom - wr.top;

  POINT cursor{};
  GetCursorPos(&cursor);
  HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{ sizeof(mi) };
  if (!GetMonitorInfoW(mon, &mi)) return;

  const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
  const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
  SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool SettingsDialogIsOpen() {
  return g_settingsHwnd && IsWindow(g_settingsHwnd);
}

HWND SettingsDialogHwnd() {
  return SettingsDialogIsOpen() ? g_settingsHwnd : nullptr;
}

void SettingsDialogFocus() {
  if (SettingsDialogIsOpen()) {
    ShowWindow(g_settingsHwnd, SW_SHOW);
    SetForegroundWindow(g_settingsHwnd);
  }
}

void SettingsDialogShow(HINSTANCE instance, HWND owner, AppSettings& settings,
                        bool* dirtyOut) {
  g_instance = instance;
  g_settings = &settings;
  g_dirty = dirtyOut;
  if (SettingsDialogIsOpen()) {
    SettingsDialogFocus();
    SyncControls(g_settingsHwnd);
    return;
  }
  g_settingsHwnd = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner,
                                      SettingsProc, 0);
  if (g_settingsHwnd) {
    ShowWindow(g_settingsHwnd, SW_SHOW);
  }
}
