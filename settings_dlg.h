#pragma once

#include "theme.h"

#include <windows.h>

void SettingsDialogShow(HINSTANCE instance, HWND owner, AppSettings& settings,
                        bool* dirtyOut);
bool SettingsDialogIsOpen();
HWND SettingsDialogHwnd();
void SettingsDialogFocus();
void CenterDialogOnDesktop(HWND hwnd);
