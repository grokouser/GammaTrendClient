@echo off
setlocal enabledelayedexpansion

REM ============================================
REM GammaTrendClient - Build (Windows)
REM ============================================

REM Push to project root (parent of scripts).
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

REM Check vcpkg is installed
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo ERROR: vcpkg not found at %VCPKG_ROOT%
    echo Run "scripts\vcpkg_clean.bat" first.
    popd
    exit /b 1
)

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
echo [OK] Working tree is clean.

REM ------------------------------------------------------------------
REM Locate cmake.exe in Visual Studio 2022
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
REM Remove any stale CMake cache
REM ------------------------------------------------------------------
if exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Removing stale CMake cache...
    del /q /f "%BUILD_DIR%\CMakeCache.txt" 2>nul
    rmdir /s /q "%BUILD_DIR%\CMakeFiles" 2>nul
)
echo [OK] Build dir is clean.

REM ------------------------------------------------------------------
REM Configure CMake with vcpkg toolchain
REM ------------------------------------------------------------------
echo Configuring CMake Visual Studio 17 2022, x64
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
REM Build Release
REM ------------------------------------------------------------------
echo Building Release configuration...
"%CMAKE%" --build "%BUILD_DIR%" --config Release --parallel 8
if errorlevel 1 (
    echo ERROR: Build failed.
    popd
    exit /b 1
)

echo.
echo =========================================================================================
echo  BUILD COMPLETE
echo =========================================================================================
echo  Executable: "%BUILD_DIR%\Release\gt_api_test.exe"
echo =========================================================================================
echo.

popd
