#pragma once

#include <windows.h>
#include <cstdint>

enum class StrengthId : int {
  Quiet = 0,
  Present = 1,
  Loud = 2,
};

enum class ThemeMode : int {
  FollowWindows = 0,
  ForceDark = 1,
  ForceLight = 2,
};

enum class TaskbarEdge : int {
  Bottom = 0,
  Top = 1,
  Left = 2,
  Right = 3,
};

struct Rgb {
  float r = 1.f;
  float g = 1.f;
  float b = 1.f;
};

struct Palette {
  float opacityCap = 0.40f;
  float brightness = 1.f;
};

struct AppSettings {
  StrengthId strength = StrengthId::Present;
  ThemeMode themeMode = ThemeMode::FollowWindows;
  bool enabled = true;
  bool startWithWindows = false;
};

float StrengthMultiplier(StrengthId id);
bool SystemTaskbarIsLight();
bool ResolveUseLightPalette(ThemeMode mode);
Palette ResolvePalette(const AppSettings& settings);

// LED-lamp RGB strip: t 0..1 = cyan -> green -> yellow -> orange -> red -> magenta.
Rgb LedStripRgb(float t, float brightness = 1.f);
