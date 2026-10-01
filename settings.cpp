#include "settings.h"

#include <shlobj.h>

namespace {

constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Barglow";

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

int ClampInt(int v, int lo, int hi) {
  return (v < lo) ? lo : (v > hi ? hi : v);
}

}  // namespace

std::wstring SettingsDir() {
  wchar_t* appData = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) ||
      !appData) {
    return {};
  }
  std::wstring dir = Join(appData, L"Barglow");
  CoTaskMemFree(appData);
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

bool SettingsLoad(AppSettings& out) {
  out = AppSettings{};
  const std::wstring path = Join(SettingsDir(), L"settings.ini");
  if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    out.startWithWindows = AutostartIsEnabled();
    return false;
  }

  out.strength = static_cast<StrengthId>(ClampInt(
      GetPrivateProfileIntW(L"Barglow", L"Strength", 1, path.c_str()), 0, 2));
  out.themeMode = static_cast<ThemeMode>(ClampInt(
      GetPrivateProfileIntW(L"Barglow", L"ThemeMode", 0, path.c_str()), 0, 2));
  out.enabled =
      GetPrivateProfileIntW(L"Barglow", L"Enabled", 1, path.c_str()) != 0;
  out.startWithWindows = AutostartIsEnabled();
  return true;
}

bool SettingsSave(const AppSettings& settings) {
  const std::wstring dir = SettingsDir();
  if (dir.empty()) return false;
  const std::wstring finalPath = Join(dir, L"settings.ini");
  const std::wstring tempPath = Join(dir, L"settings.ini.tmp");

  auto writeInt = [&](const wchar_t* key, int value) {
    wchar_t buf[32];
    wsprintfW(buf, L"%d", value);
    WritePrivateProfileStringW(L"Barglow", key, buf, tempPath.c_str());
  };

  writeInt(L"Strength", static_cast<int>(settings.strength));
  writeInt(L"ThemeMode", static_cast<int>(settings.themeMode));
  writeInt(L"Enabled", settings.enabled ? 1 : 0);

  if (!MoveFileExW(tempPath.c_str(), finalPath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(finalPath.c_str());
    if (!MoveFileW(tempPath.c_str(), finalPath.c_str())) {
      DeleteFileW(tempPath.c_str());
      return false;
    }
  }
  AutostartSet(settings.startWithWindows);
  return true;
}

bool AutostartIsEnabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t value[MAX_PATH * 2] = {};
  DWORD type = 0;
  DWORD size = sizeof(value);
  const LONG rc = RegQueryValueExW(key, kRunValue, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(value), &size);
  RegCloseKey(key);
  return rc == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0';
}

bool AutostartSet(bool enabled) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }
  bool ok = false;
  if (!enabled) {
    ok = RegDeleteValueW(key, kRunValue) == ERROR_SUCCESS ||
         GetLastError() == ERROR_FILE_NOT_FOUND;
  } else {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring quoted = L"\"";
    quoted += exe;
    quoted += L"\"";
    ok = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(quoted.c_str()),
                        static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t))) ==
         ERROR_SUCCESS;
  }
  RegCloseKey(key);
  return ok;
}
