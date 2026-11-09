@echo off
setlocal enabledelayedexpansion

REM Change to the project root (parent of scripts/)
cd /d "%~dp0.."

REM Check for uncommitted changes
git status --porcelain | findstr . >nul 2>&1
if errorlevel 0 (
    echo ERROR: Project has uncommitted changes. Commit all source files before building.
    exit /b 1
)

echo Project is clean. Proceeding with build...

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set SRC="C:\Users\thurston\Dev\GammaTrendClient"
set BUILD="C:\Users\thurston\Dev\GammaTrendClient_build"

echo Configuring CMake...
%CMAKE% -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/Users/thurston/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake -S %SRC% -B %BUILD%
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

echo Building...
%CMAKE% --build %BUILD% --config Release --parallel 8
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo Build complete.
