@echo on
setlocal enabledelayedexpansion

REM ============================================
REM GammaTrendClient - Build from scratch
REM ============================================
REM A new user pulls this repo from GitHub and runs this script.
REM It creates an isolated vcpkg under the project folder,
REM installs Qt6 + QCustomPlot, configures CMake VS2022 x64,
REM and builds the Release executable.
REM ============================================

REM Push to the project root (parent of scripts).
pushd %~dp0..
if errorlevel 1 (
    echo ERROR: Could not change to project root.
    exit /b 1
)
set ROOT=%CD%
set VCPKG_ROOT=%ROOT%\vcpkg
set BUILD_DIR=%ROOT%\build

echo.
echo ============================================
echo  GammaTrendClient Build Script
echo ============================================
echo  Project root: %ROOT%
echo  Vcpkg root:   %VCPKG_ROOT%
echo  Build dir:    %BUILD_DIR%
echo ============================================
echo.

REM Allow git to work from this path.
git config --global --add safe.directory "%ROOT%" 2>nul

REM Refuse to build with uncommitted changes.
set "GIT_DIRTY="
for /f "delims=" %%a in ('git status --porcelain 2^>nul') do set "GIT_DIRTY=1"

if defined GIT_DIRTY (
    echo ERROR: Project has uncommitted changes.
    echo Commit or stash all source files before building.
    popd
    exit /b 1
)

echo [OK] Working tree is clean

REM ------------------------------------------------------------------
REM Step 1: Clone vcpkg locally isolated from other installs
REM ------------------------------------------------------------------
if not exist "%VCPKG_ROOT%\bootstrap-vcpkg.bat" (
    echo [1/5] Cloning vcpkg into %VCPKG_ROOT% ...
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
    if errorlevel 1 (
        echo ERROR: Failed to clone vcpkg.
        popd
        exit /b 1
    )
) else (
    echo [1/5] vcpkg already present at %VCPKG_ROOT% skipping clone.
)

REM ------------------------------------------------------------------
REM Step 2: Bootstrap vcpkg
REM ------------------------------------------------------------------
echo [2/5] Bootstrapping vcpkg ...
call "%VCPKG_ROOT%\bootstrap-vcpkg.bat"
if errorlevel 1 (
    echo ERROR: vcpkg bootstrap failed.
    popd
    exit /b 1
)
echo [OK] vcpkg bootstrapped.

REM ------------------------------------------------------------------
REM Step 3: Install dependencies Qt6 + QCustomPlot
REM ------------------------------------------------------------------
echo [3/5] Installing vcpkg dependencies first run takes ~45 min cached after
call "%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows ^
    qtbase[gui,widgets,network,concurrent,thread,async-io,doubleconversion,freetype,harfbuzz,pcre2,png] ^
    qcustomplot
if errorlevel 1 (
    echo ERROR: vcpkg install failed.
    popd
    exit /b 1
)
echo [OK] Dependencies installed.

REM ------------------------------------------------------------------
REM Step 3: Install dependencies via manifest mode pinned Qt 6.8.2 LTS
REM ------------------------------------------------------------------
echo [3/5] Installing vcpkg dependencies manifest mode
call "%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows
if errorlevel 1 (
    echo ERROR: vcpkg install failed.
    popd
    exit /b 1
)
echo [OK] Dependencies installed.

REM ------------------------------------------------------------------
REM Step 4: Locate cmake.exe in Visual Studio 2022
REM ------------------------------------------------------------------
set CMAKE=
for %%V in (Professional Community Enterprise BuildTools) do (
    if not defined CMAKE if exist "C:\Program Files\Microsoft Visual Studio\2022\%%V\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\%%V\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
)
REM Also try cmake on PATH
if not defined CMAKE where cmake.exe >nul 2>&1 set "CMAKE=cmake.exe"

if not defined CMAKE (
    echo ERROR: Could not find cmake.exe. Install Visual Studio 2022 with C++ workload or add cmake to PATH.
    popd
    exit /b 1
)
echo [OK] Using cmake: %CMAKE%

REM ------------------------------------------------------------------
REM Step 5: Remove any stale CMake cache prevents source dir mismatch
REM ------------------------------------------------------------------
if exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [4/5] Removing stale CMake cache ...
    del /q /f "%BUILD_DIR%\CMakeCache.txt" 2>nul
    rmdir /s /q "%BUILD_DIR%\CMakeFiles" 2>nul
)
echo [OK] Build dir is clean.

REM ------------------------------------------------------------------
REM Step 6: Configure CMake
REM ------------------------------------------------------------------
echo [5/5] Configuring CMake Visual Studio 17 2022, x64
"%CMAKE%" -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -S "%ROOT%" -B "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    popd
    exit /b 1
)
echo [OK] CMake configured.

REM ------------------------------------------------------------------
REM Step 7: Build Release
REM ------------------------------------------------------------------
echo Building Release configuration ...
"%CMAKE%" --build "%BUILD_DIR%" --config Release --parallel 8
if errorlevel 1 (
    echo ERROR: Build failed.
    popd
    exit /b 1
)

echo.
echo ============================================
echo  BUILD COMPLETE
echo ============================================
echo  Executable: "%BUILD_DIR%\Release\gt_api_test.exe"
echo ============================================
echo.

popd
