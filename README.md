# Monitor Control

A small Windows tray app that sets the brightness and contrast of external monitors over DDC/CI.
Plain C++20 and the Win32 API: no frameworks, no runtime to install.

- Left-click the tray icon: a flyout with a brightness slider (and a contrast slider, if the monitor
  supports it) for each monitor, plus an "All monitors" slider when there is more than one.
- Right-click: *Refresh monitors*, *Start with Windows*, *Open log*, *Exit*.
- Hover the icon to see the current brightness of each monitor.

Monitors are found again automatically when displays are plugged in, unplugged or rearranged, and
after the PC resumes from sleep.

## Requirements

- Windows 10 or 11, x64.
- DDC/CI turned on in the monitor's own on-screen menu (it usually is by default).
- Built-in laptop panels don't speak DDC/CI and are not listed.

## Building

Visual Studio 2026 (toolset v145) with the C++ desktop workload, and the WiX v7 SDK for the installer
(restored from NuGet automatically).

```
msbuild MonitorControl.slnx -restore -p:RestorePackagesConfig=true -p:Configuration=Release -p:Platform=x64 -m
bin\x64\Release\Tests.exe
```

Output in `bin\x64\Release`: `MonitorControl.exe` (portable) and `MonitorControl.msi` (per-user
install, no admin rights).

The version comes from [Nerdbank.GitVersioning](https://github.com/dotnet/Nerdbank.GitVersioning)
(`version.json` + git height). `res\make-icon.ps1` regenerates `res\app.ico`.

The log is at `%LOCALAPPDATA%\MonitorControl\log.txt`.
