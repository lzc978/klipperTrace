# KlipperTrace (C++/ImGui)

[English](README.md) | [简体中文](README.zh-CN.md)

A lightweight, cross-platform Klipper log analysis and visualization tool (GNU GCC toolchain).

## Screenshots

### English UI

![KlipperTrace English UI](assets/screenshot1.jpg)

### Chinese UI

![KlipperTrace Chinese UI](assets/screenshot2.jpg)

## Features

- Adaptive parsing for `Stats ...` lines without fixed schemas.
- Auto grouping by `group:` blocks (e.g. `mcu:`, `nozzle_mcu:`).
- Interactive series visibility/filter controls.
- Click any chart timeline position to jump to nearby raw (unscreened) log context.
- Automatic `shutdown` root-cause extraction with dump snippets and quick analysis.
- Automatic timing/jitter issue detection (e.g. `Timer too close`) with nearby stats hints.

## Build Dependencies

- GNU Make
- g++
- git (for third-party sources)
- Python3 (optional, only used in some `pkg-config` fallback scenarios)
- OpenGL development libraries
- GLFW3 development libraries
- `pkg-config`

Ubuntu/WSL example:

```bash
sudo apt update
sudo apt install -y build-essential git pkg-config libgl1-mesa-dev libglfw3-dev
```

## Build and Run

```bash
make
./bin/klipper_trace /path/to/klipper.log
```

You can also launch without arguments and input the log path in the UI.

## Cross-Compile Windows EXE (on Linux/WSL)

```bash
sudo apt install -y g++-mingw-w64-x86-64-posix
make TARGET=windows
```

Output:

- `bin/klipper_trace.exe`

## Parsing Rules

- Parse only lines containing `Stats <time>:`.
- Detect `group:` switches (e.g. `mcu:`, `extruder:`).
- Parse all numeric `key=value` fields.
- New fields are auto-added to their groups without code changes.
