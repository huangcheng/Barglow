#include "settings_dlg.h"

#include "Resource.h"
#include "settings.h"

namespace {

HWND g_settingsHwnd = nullptr;
AppSettings* g_settings = nullptr;
bool* g_dirty = nullptr;
HINSTANCE g_instance = nullptr;

void SyncControls(HWND hDlg) {
  if (!g_settings) return;
  CheckRadioButton(hDlg, IDC_STRENGTH_QUIET, IDC_STRENGTH_LOUD,
                   IDC_STRENGTH_QUIET + static_cast<int>(g_settings->strength));
  CheckRadioButton(hDlg, IDC_THEME_FOLLOW, IDC_THEME_LIGHT,
                   IDC_THEME_FOLLOW + static_cast<int>(g_settings->themeMode));
  CheckDlgButton(hDlg, IDC_ENABLE, g_settings->enabled ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hDlg, IDC_AUTOSTART,
                 g_settings->startWithWindows ? BST_CHECKED : BST_UNCHECKED);
}

void MarkDirty() {
  if (g_dirty) *g_dirty = true;
  if (g_settings) SettingsSave(*g_settings);
}

INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_INITDIALOG:
      SyncControls(hDlg);
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
