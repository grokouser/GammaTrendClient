# GammaTrendClient — Qt6 + QCustomPlot telemetry dashboard (VS2022 / vcpkg / WSL)

Demo GUI for API telemetry: 2 years of 1-minute data (1,051,200 points),
dual-Y-axis time plot, range navigator, daily rollup.

## Layout
- Source lives in WSL home: `~/GammaTrendClient` (git repo).
- Build dir MUST be on NTFS: `C:/Users/thurston/Dev/GammaTrendClient_build`
  (Windows toolchain cannot write into `\\wsl.localhost\...` trees).

## Configure (from WSL)
```bash
CMAKE="/mnt/c/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
SRCW=$(wslpath -w ~/gt_api_test)
"$CMAKE" -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:/Users/thurston/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -S "$SRCW" -B C:/Users/thurston/Dev/GammaTrendClient_build
cmake --build C:/Users/thurston/Dev/GammaTrendClient_build --config Release --parallel 8
```
vcpkg installs qtbase (trimmed: gui/widgets/network/pcre2/harfbuzz/freetype...) + qcustomplot
into the build tree automatically on first configure (~45 min cold, cached after).

## Run
```bash
/mnt/c/Users/thurston/Dev/GammaTrendClient_build/Release/gt_api_test.exe            # interactive
gt_api_test.exe --render out.png --selftest                                    # headless snapshot
```

## Features
- Main plot: X = date/time, left Y = counts (requests, errors), right Y = °C (GPU, CPU)
- Interval-bin combo (1 min raw → 1 day), smoothing slider (0–240 min moving avg)
- Navigator bar: drag/resize selection rectangle over full 2-year range
- Daily rollup plot: bars (avg counts/day) + temp line (secondary axis)
- Export PNG button; checkboxes toggle each series
- Adaptive sampling + prefix-sum smoothing keep 1M-point pans interactive

## Pitfalls (already solved here)
- qcustomplot port has no CMake config: imported manually in CMakeLists;
  consumers MUST define `QCUSTOMPLOT_USE_LIBRARY` (dllimport) or linking fails.
- windeployqt skips vcpkg Qt plugins; CMake copies platforms/styles/imageformats itself.
- vcpkg port `cpp-httplib` config name is `httplib` (if you re-add it).
