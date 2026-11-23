# GammaTrendClient — Qt6 + QCustomPlot telemetry dashboard

Demo GUI for API telemetry: 2 years of 1-minute data (1,051,200 points),
dual-Y-axis time plot, range navigator, daily rollup.

## Layout

- Source: `GammaTrendClient/` (this repo)
- Scripts: `scripts/` (build.bat for Windows, build.sh for Linux, plus helpers)
- Build output: `build/` (created by the build scripts)
- vcpkg (Windows only): `vcpkg/` (created by vcpkg_clean.bat, isolated to this project)

## Scripts (scripts/)

All build and run procedures are entry-pointed through `scripts/`. Do not run cmake or vcpkg by hand — use these scripts.

| Script | Platform | Purpose |
|--------|----------|---------|
| `scripts\build.bat` | Windows | Build Release from scratch. Requires `vcpkg/` to be installed first (run `vcpkg_clean.bat` if missing). Output: `build\Release\gt_api_test.exe` |
| `scripts\build.sh` | Linux (native) | Build Release from scratch. Installs Qt6 dev packages via apt, configures CMake, builds. **Refuses to run under WSL** — use `build.bat` from Windows instead. |
| `scripts\vcpkg_clean.bat` | Windows | Remove and reinstall vcpkg from scratch. Run this once before `build.bat` if `vcpkg/` is missing or corrupt. |
| `scripts\run_bat.sh` | Linux / WSL | Run a Windows `.bat` script through `cmd.exe` (e.g. invoke `build.bat` from WSL). Usage: `run_bat.sh /path/to/script.bat [args...]` |

### How to use the scripts

**Windows (recommended):**

```cmd
cd C:\Users\thurston\Dev\GammaTrendClient
REM First time only — install vcpkg:
scripts\vcpkg_clean.bat
REM Then build (and re-use on subsequent runs):
scripts\build.bat
REM Run the executable:
build\Release\gt_api_test.exe
gt_api_test.exe --render out.png --selftest   # headless snapshot
```

**Linux (native, not WSL):**

```bash
cd ~/GammaTrendClient
chmod +x scripts/build.sh
./scripts/build.sh
./build/gt_api_test
./build/gt_api_test --render out.png --selftest   # headless snapshot
```

**WSL (Windows Subsystem for Linux):**

Do not run `build.sh` inside WSL — it will exit with an error. Use the Windows script instead:

```bash
# From WSL, invoke the Windows build script via cmd.exe:
./scripts/run_bat.sh /mnt/c/Users/thurston/Dev/GammaTrendClient/scripts/build.bat
```

Or run `build.bat` directly from a Windows Developer Command Prompt.

## Prerequisites

### Windows
- Visual Studio 2022 (Community/Professional/Enterprise) with **Desktop development with C++** workload
- Git for Windows
- `scripts\vcpkg_clean.bat` downloads and bootstraps an isolated vcpkg into `GammaTrendClient/vcpkg/` automatically (no pre-installed vcpkg needed)

### Linux (native)
- CMake 3.24+
- GCC or Clang (C++17)
- Qt6 development packages: `qtbase6-dev`, `qttools6-dev-tools`, `libqt6widgets6`, etc. (installed automatically by `build.sh` via apt)
- Git
- Build essentials: `build-essential`, `ninja-build` (optional)

## What the scripts do

### build.bat (Windows)
1. Verifies the working tree is clean
2. Requires `vcpkg/` to exist (run `vcpkg_clean.bat` first if not)
3. Locates `cmake.exe` from Visual Studio 2022
4. Removes any stale CMake cache
5. Configures CMake (Visual Studio 17 2022, x64) with the vcpkg toolchain
6. Builds Release → `build\Release\gt_api_test.exe`

Subsequent runs reuse the installed vcpkg packages and are much faster.

### build.sh (Linux native)
1. Refuses to run under WSL
2. Verifies the working tree is clean
3. Installs Qt6 development packages via apt/dnf/pacman (if missing)
4. Removes any stale CMake cache
5. Configures CMake (Unix Makefiles, Release)
6. Builds Release → `build/gt_api_test`

### vcpkg_clean.bat (Windows)
1. Removes the existing `vcpkg/` directory
2. Clones vcpkg from GitHub
3. Bootstraps vcpkg (`bootstrap-vcpkg.bat`)
4. Installs dependencies from `vcpkg.json` (`vcpkg install --triplet x64-windows`)

### run_bat.sh (Linux / WSL)
- Converts line endings and streams output from a Windows `.bat` file run via `cmd.exe`
- Usage: `run_bat.sh <bat_path> [args...]`

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
- `build.sh` will not run under WSL. Use `build.bat` from Windows CMD, or
  `run_bat.sh` from WSL to invoke it.
