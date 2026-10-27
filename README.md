# Monitor Control

A small Windows tray app that sets the brightness and contrast of external monitors over DDC/CI.
Plain C++20 and the Win32 API: no frameworks, no runtime to install.

- Left-click the tray icon: a flyout with a brightness slider (and a contrast slider, if the monitor
  supports it) for each monitor, plus an "All monitors" slider when there is more than one.
- Right-click: *Refresh monitors*, *Start with Windows*, *Open log*, *Exit*.
- Hover the icon to see the current brightness of each monitor.

Monitors are found again automatically when displays are plugged in, unplugged or rearranged, and
after the PC resumes from sleep.

## Command line

```
MonitorControl.exe --brightness 40                                every monitor
MonitorControl.exe --monitor 2 --brightness 70 --contrast 50      monitor 2 (counted from the left)
MonitorControl.exe --monitor "DELL U2414H" --brightness +10       every monitor with that name
MonitorControl.exe -b -10                                         step down
MonitorControl.exe --list                                         numbers, names and current values
MonitorControl.exe --help
```

If Monitor Control is running in the tray, the command is handed to it: it makes the change and its
sliders follow. Otherwise the command makes the change itself and exits. Exit codes: 0 = ok,
1 = failed (no matching monitor, DDC/CI error), 2 = invalid arguments.

The exe is a console program that asks Windows (11 24H2 and later) not to give it a console window of
its own (`consoleAllocationPolicy` in `res\app.manifest`). Typed in cmd or PowerShell it behaves like
any command-line tool: output in the terminal (errors on stderr), the shell waits, `%ERRORLEVEL%` /
`$LASTEXITCODE` are set. Started from Explorer, a shortcut or autostart there is no console window.
Started without arguments from a terminal, it launches the tray app detached and returns the prompt.
On older Windows a console window flashes briefly when the tray app starts.

A successful change prints nothing when there is no console (a shortcut or hotkey), so it can be bound
to keys; errors and `--list` show a message box there.

## Requirements

- Windows 11 24H2 (build 26100) or later, x64. Windows 10 and older Windows 11 releases also work,
  but a console window flashes briefly when the tray app starts (see [Command line](#command-line)).
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
