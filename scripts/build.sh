#!/usr/bin/env bash
set -euo pipefail

# ============================================
# GammaTrendClient - Build from scratch (Linux)
# ============================================
# A new user clones this repo and runs this script.
# It installs Qt6 development packages, configures CMake,
# and builds the Release executable.
# ============================================

# ------------------------------------------------------------------
# Guard: refuse to run under WSL. Use the Windows .bat script instead.
# ------------------------------------------------------------------
if {
    grep -qiE "microsoft|wsl" /proc/version 2>/dev/null ||
    grep -qiE "microsoft|wsl" /proc/sys/kernel/osrelease 2>/dev/null
}; then
    echo
    echo "============================================================"
    echo "  You appear to be running inside WSL (Windows Subsystem"
    echo "  for Linux). This build.sh script is intended for native"
    echo "  Linux only."
    echo ""
    echo "  Please use the Windows build script instead:"
    echo ""
    echo "    scripts\\build.bat"
    echo ""
    echo "  Example (from Windows CMD):"
    echo "    C:\\Users\\thurston\\Dev\\GammaTrendClient\\scripts\\build.bat"
    echo "============================================================"
    echo
    exit 1
fi

# Resolve the project root (parent of scripts/).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT}/build"

echo "============================================"
echo " GammaTrendClient Build Script (Linux)"
echo "============================================"
echo " Project root: ${ROOT}"
echo " Build dir:    ${BUILD_DIR}"
echo

# Allow git to work from this path.
git config --global --add safe.directory "${ROOT}" 2>/dev/null || true

# Refuse to build with uncommitted changes (protects the source tree).
if [[ -n "$(git -C "${ROOT}" status --porcelain)" ]]; then
    echo "ERROR: Project has uncommitted changes."
    echo "Commit or stash all source files before building."
    exit 1
fi
echo "[OK] Working tree is clean."

# ------------------------------------------------------------------
# Step 1: Install Qt6 development packages (if missing)
# ------------------------------------------------------------------
echo "[1/4] Ensuring Qt6 development packages are installed ..."

# Detect package manager.
if command -v apt-get >/dev/null 2>&1; then
    PM="apt-get"
    SUDO="sudo"
    PKG_INSTALL="${SUDO} apt-get install -y"
elif command -v dnf >/dev/null 2>&1; then
    PM="dnf"
    SUDO="sudo"
    PKG_INSTALL="${SUDO} dnf install -y"
elif command -v pacman >/dev/null 2>&1; then
    PM="pacman"
    SUDO="sudo"
    PKG_INSTALL="${SUDO} pacman -S --needed --noconfirm"
else
    echo "ERROR: No supported package manager (apt-get/dnf/pacman) found."
    echo "Install Qt6 development packages manually, then re-run this script."
    exit 1
fi

# Base Qt6 packages needed by CMakeLists.txt (Qt6::Widgets, Qt6::PrintSupport, Qt6::Network).
QT_PKGS=(
    qtbase6-dev
    qttools6-dev-tools
    libqt6widgets6
    libqt6printsupport6
    libqt6network6
)

# Check which are missing (dpkg-query for apt-based systems).
MISSING=()
if [[ "${PM}" == "apt-get" ]]; then
    for pkg in "${QT_PKGS[@]}"; do
        if ! dpkg -s "${pkg}" >/dev/null 2>&1; then
            MISSING+=("${pkg}")
        fi
    done
fi

if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo "Installing missing Qt6 packages: ${MISSING[*]}"
    ${PKG_INSTALL} "${MISSING[@]}"
else
    echo "[OK] Qt6 development packages already present."
fi

# ------------------------------------------------------------------
# Step 2: Remove any stale CMake cache (prevents "source dir mismatch")
# ------------------------------------------------------------------
echo "[2/4] Cleaning stale CMake cache ..."
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    rm -f "${BUILD_DIR}/CMakeCache.txt"
fi
if [[ -d "${BUILD_DIR}/CMakeFiles" ]]; then
    rm -rf "${BUILD_DIR}/CMakeFiles"
fi
echo "[OK] Build dir is clean."

# ------------------------------------------------------------------
# Step 3: Configure CMake
# ------------------------------------------------------------------
echo "[3/4] Configuring CMake (Unix Makefiles, Release) ..."
cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="" \
    || {
        echo "ERROR: CMake configure failed."
        exit 1
    }
echo "[OK] CMake configured."

# ------------------------------------------------------------------
# Step 4: Build Release
# ------------------------------------------------------------------
echo "[4/4] Building Release configuration ..."
cmake --build "${BUILD_DIR}" --config Release --parallel "$(nproc)" \
    || {
        echo "ERROR: Build failed."
        exit 1
    }

echo
echo "============================================"
echo " BUILD COMPLETE"
echo "============================================"
echo " Executable: ${BUILD_DIR}/gt_api_test"
echo "============================================"
echo
