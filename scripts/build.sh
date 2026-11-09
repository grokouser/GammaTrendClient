@echo off
setlocal enabledelayedexpansion

REM Push to the project root (parent of scripts/).
REM pushd supports UNC paths (e.g. \\wsl.localhost\...) which cd /d does not.
pushd "%~dp0.."

REM After pushd, %CD% is an absolute Windows path (e.g. Y:\...).
REM Add it as a git safe.directory so git works from the WSL UNC path.
git config --global --add safe.directory "%CD%" 2>nul

REM Check for uncommitted changes
git status --porcelain | findstr . >nul 2>&1
if errorlevel 0 (
    echo ERROR: Project has uncommitted changes. Commit all source files before building.
    popd
    exit /b 1
)

echo Project is clean. Proceeding with build...

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set SRC=%~dp0..\
set BUILD=C:\Users\thurston\Dev\GammaTrendClient_build

echo Configuring CMake...
%CMAKE% -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/Users/thurston/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake -S "%SRC%" -B "%BUILD%"
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    popd
    exit /b 1
)

echo Building...
%CMAKE% --build "%BUILD%" --config Release --parallel 8
if errorlevel 1 (
    echo ERROR: Build failed.
    popd
    exit /b 1
)

echo Build complete.
popd
