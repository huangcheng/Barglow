#pragma once

#include "theme.h"

#include <string>

bool SettingsLoad(AppSettings& out);
bool SettingsSave(const AppSettings& settings);

bool AutostartIsEnabled();
bool AutostartSet(bool enabled);

std::wstring SettingsDir();
