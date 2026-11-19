# GammaTrendClient — Qt6 + QCustomPlot telemetry dashboard

Demo GUI for API telemetry: 2 years of 1-minute data (1,051,200 points),
dual-Y-axis time plot, range navigator, daily rollup.

## Layout

- Source: `GammaTrendClient/` (this repo)
- Scripts: `scripts/` (build.bat for Windows, build.sh for Linux)
- Build output: `build/` (created by the build scripts)
- vcpkg (Windows only): `vcpkg/` (created by build.bat, isolated to this project)

## Prerequisites

### Windows
- Visual Studio 2022 (Community/Professional/Enterprise) with **Desktop development with C++** workload
- Git for Windows
- The build script downloads and bootstraps vcpkg automatically (no pre-installed vcpkg needed)

### Linux
- CMake 3.24+
- GCC or Clang (C++17)
- Qt6 development packages: `qtbase6-dev`, `qttools6-dev-tools`, `libqt6widgets6`, etc.
- Git
- Build essentials: `build-essential`, `ninja-build` (optional)

## Build — Windows (recommended)

From a Developer Command Prompt for VS2022 (or any cmd/PowerShell):

```cmd
cd C:\Users\thurston\Dev\GammaTrendClient
scripts\build.bat
```

What `build.bat` does (run once, from scratch):

1. Verifies the working tree is clean
2. Clones an isolated vcpkg into `GammaTrendClient/vcpkg/` (does **not** touch any other vcpkg on the machine)
3. Bootstraps vcpkg and installs Qt6 + QCustomPlot (~45 min first run, cached after)
4. Locates cmake.exe from Visual Studio 2022
5. Configures CMake (Visual Studio 17 2022, x64) with the vcpkg toolchain
6. Builds Release → `build/Release/gt_api_test.exe`

Subsequent runs reuse the installed vcpkg packages and are much faster.

Run the executable:

```cmd
build\Release\gt_api_test.exe
gt_api_test.exe --render out.png --selftest   # headless snapshot
```

## Build — Linux

```bash
cd ~/GammaTrendClient
chmod +x scripts/build.sh
./scripts/build.sh
```

What `build.sh` does:

1. Verifies the working tree is clean
2. Installs Qt6 development packages via apt (if missing)
3. Configures CMake (Unix Makefiles / Ninja, Release)
4. Builds → `build/gt_api_test`

Run the executable:

```bash
./build/gt_api_test
./build/gt_api_test --render out.png --selftest   # headless snapshot
```

## Features

- Main plot: X = date/time, left Y = counts (requests, errors), right Y = °C (GPU, CPU)
- Interval-bin combo (1 min raw → 1 day), smoothing slider (0–240 min moving avg)
- Navigator bar: drag/resize selection rectangle over full 2-year range
- Daily rollup plot: bars (avg counts/day) + temp line (secondary axis)
- Export PNG button; checkboxes toggle each series
- Adaptive sampling + prefix-sum smoothing keep 1M-point pans interactive

## Pitfalls (already solved here)

- qcustomplot port has no CMake config: imported manually in CMakeLists.txt;
  consumers MUST define `QCUSTOMPLOT_USE_LIBRARY` (dllimport) or linking fails.
- windeployqt skips vcpkg Qt plugins; CMake copies platforms/styles/imageformats itself (Windows).
- Build directory must be on NTFS when using the Windows MSVC toolchain —
  do not build inside a `\\wsl.localhost\...` tree from Windows.
- Never delete `vcpkg_installed/` or `CMakeCache.txt` mid-build; use the
  `scripts/build.bat` / `scripts/build.sh` entry points instead of running
  cmake by hand.
