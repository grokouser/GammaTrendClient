@echo off
setlocal

REM Go to project root parent of scripts
pushd "%~dp0.."
if errorlevel 1 (
    echo ERROR: Could not change to project root.
    exit /b 1
)

REM Clear stale VCPKG_ROOT to avoid mismatched root warnings
set "VCPKG_ROOT="

echo Removing existing vcpkg...
rmdir /s /q vcpkg
if errorlevel 1 (
    echo ERROR: Failed to remove vcpkg directory.
    popd
    exit /b 1
)

echo Cloning vcpkg...
git clone https://github.com/microsoft/vcpkg.git vcpkg
if errorlevel 1 (
    echo ERROR: Failed to clone vcpkg.
    popd
    exit /b 1
)

echo Bootstrapping vcpkg...
call vcpkg\bootstrap-vcpkg.bat
if errorlevel 1 (
    echo ERROR: vcpkg bootstrap failed.
    popd
    exit /b 1
)

echo Installing dependencies manifest mode...
call vcpkg\vcpkg.exe install --triplet x64-windows
if errorlevel 1 (
    echo ERROR: vcpkg install failed.
    popd
    exit /b 1
)

echo DONE.
popd
endlocal
