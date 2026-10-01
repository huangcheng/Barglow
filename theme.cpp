#include "theme.h"

#include <algorithm>
#include <cmath>

namespace {

bool ReadLightThemeValue(const wchar_t* valueName, bool defaultValue) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_READ, &key) != ERROR_SUCCESS) {
    return defaultValue;
  }
  DWORD data = defaultValue ? 1u : 0u;
  DWORD size = sizeof(data);
  DWORD type = 0;
  const LONG rc = RegQueryValueExW(key, valueName, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(&data), &size);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS || type != REG_DWORD) {
    return defaultValue;
  }
  return data != 0;
}

Rgb LerpRgb(const Rgb& a, const Rgb& b, float t) {
  t = (std::max)(0.f, (std::min)(1.f, t));
  return {
      a.r + (b.r - a.r) * t,
      a.g + (b.g - a.g) * t,
      a.b + (b.b - a.b) * t,
  };
}

Rgb FromBytes(BYTE r, BYTE g, BYTE b) {
  return { r / 255.f, g / 255.f, b / 255.f };
}

}  // namespace

float StrengthMultiplier(StrengthId id) {
  switch (id) {
    case StrengthId::Quiet: return 0.62f;
    case StrengthId::Loud: return 1.45f;
    case StrengthId::Present:
    default: return 1.0f;
  }
}

bool SystemTaskbarIsLight() {
  if (ReadLightThemeValue(L"SystemUsesLightTheme", false)) {
    return true;
  }
  return ReadLightThemeValue(L"AppsUseLightTheme", false);
}

bool ResolveUseLightPalette(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::ForceDark: return false;
    case ThemeMode::ForceLight: return true;
    case ThemeMode::FollowWindows:
    default: return SystemTaskbarIsLight();
  }
}

Rgb LedStripRgb(float t, float brightness) {
  t = (std::max)(0.f, (std::min)(1.f, t));
  brightness = (std::max)(0.2f, (std::min)(1.f, brightness));

  // Saturated EQ spectrum from the reference: red, orange, yellow, green, cyan, blue, violet.
  static const Rgb kStops[] = {
      FromBytes(255, 24, 32),
      FromBytes(255, 110, 0),
      FromBytes(255, 230, 0),
      FromBytes(70, 230, 40),
      FromBytes(0, 220, 190),
      FromBytes(30, 70, 255),
      FromBytes(190, 0, 255),
  };
  static const float kPos[] = { 0.f, 0.14f, 0.30f, 0.48f, 0.64f, 0.80f, 1.f };
  constexpr int n = 7;

  Rgb c = kStops[n - 1];
  for (int i = 0; i < n - 1; ++i) {
    if (t <= kPos[i + 1]) {
      const float u = (t - kPos[i]) / (kPos[i + 1] - kPos[i]);
      const float s = u * u * (3.f - 2.f * u);
      c = LerpRgb(kStops[i], kStops[i + 1], s);
      break;
    }
  }
  return { c.r * brightness, c.g * brightness, c.b * brightness };
}

Palette ResolvePalette(const AppSettings& settings) {
  const bool light = ResolveUseLightPalette(settings.themeMode);
  Palette p;
  p.opacityCap = (light ? 0.22f : 0.36f) * StrengthMultiplier(settings.strength);
  p.brightness = 1.f;
  return p;
}
