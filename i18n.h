#pragma once

#include "theme.h"

#include <windows.h>

enum class UiLang {
  English = 0,
  SimplifiedChinese = 1,
};

enum class StrId {
  AppName,
  TrayTip,
  MenuSettings,
  MenuAbout,
  MenuExit,
  SettingsTitle,
  Strength,
  Quiet,
  Present,
  Loud,
  Theme,
  FollowWindows,
  ForceDark,
  ForceLight,
  Language,
  LangFollowSystem,
  LangEnglish,
  LangZhHans,
  EnableVisualizer,
  StartWithWindows,
  Close,
  AboutTitle,
  AboutDesc,
  AboutVersion,
  AboutAuthor,
  AboutRepoLabel,
  Ok,
  Count,
};

UiLang DetectSystemUiLang();
void ApplyLanguagePreference(LanguagePreference pref);
UiLang CurrentUiLang();
const wchar_t* Tr(StrId id);
void ApplyDialogIcon(HWND hwnd, HINSTANCE instance);
void LocalizeMenu(HMENU menu);
