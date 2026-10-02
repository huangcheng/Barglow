<p align="center">
  <img src="assets/logo/barglow-256.png" alt="Barglow" width="128" height="128">
</p>

<h1 align="center">Barglow</h1>

<p align="center">
  Taskbar audio visualizer for Windows 11<br>
  <a href="README.zh-CN.md">简体中文</a>
  ·
  <a href="https://github.com/huangcheng/Barglow/releases">Releases</a>
  ·
  <a href="LICENSE">MIT License</a>
</p>

---

Barglow draws a translucent RGB spectrum on the Windows 11 taskbar from system audio (WASAPI loopback). It runs from the tray, stays out of the way, and hides itself for fullscreen apps.

## Features

- Real-time spectrum bars with peak caps (WASAPI loopback + FFT)
- Saturated LED-style RGB sweep along the taskbar
- Overlay host that follows primary taskbar geometry / DPI / autohide
- Quiet / Present / Loud strength, light / dark / follow-Windows theme
- English + Simplified Chinese UI
- Autostart with Windows
- Hides when a fullscreen app covers the taskbar

## Requirements

- Windows 11 (x64)
- Visual Studio 2022+ with Desktop development with C++ (to build from source)

## Install

1. Download the latest `Barglow.exe` from [Releases](https://github.com/huangcheng/Barglow/releases).
2. Run it. A tray icon appears.
3. Right-click the tray icon → **Settings** / **About** / **Exit**.

No installer. Settings are stored in `%AppData%\Barglow\settings.ini`.

## Build

```bat
msbuild Barglow.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output: `x64\Release\Barglow.exe`

## Usage

| Action | Result |
| --- | --- |
| Double-click tray icon | Open Settings |
| Right-click tray | Settings / About / Exit |
| Settings → Strength | Quiet / Present / Loud |
| Settings → Theme | Follow Windows / Force dark / Force light |
| Settings → Language | Follow system / English / Simplified Chinese |
| Enable visualizer | Toggle bars on/off |
| Start with Windows | HKCU Run autostart |

## Screenshots

<p align="center">
  <img src="assets/logo/barglow-logo.png" alt="Barglow logo" width="240">
</p>

## License

[MIT](LICENSE) © HUANG Cheng

Repository: https://github.com/huangcheng/Barglow
