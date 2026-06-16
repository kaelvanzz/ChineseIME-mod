#!/bin/bash
set -e

echo "Building ChineseIME Native for Linux..."
echo

if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found. Please install cmake first."
    echo "  Ubuntu/Debian: sudo apt install cmake"
    echo "  Fedora/RHEL:   sudo dnf install cmake"
    echo "  Arch:          sudo pacman -S cmake"
    exit 1
fi

if ! command -v pkg-config &> /dev/null; then
    echo "ERROR: pkg-config not found. Please install it."
    exit 1
fi

echo "Checking dependencies..."
MISSING_DEPS=0

if ! pkg-config --exists dbus-1 2>/dev/null; then
    echo "  - dbus-1 not found (libdbus-dev)"
    MISSING_DEPS=1
fi

if ! pkg-config --exists gtk+-3.0 2>/dev/null; then
    echo "  - gtk+-3.0 not found (libgtk-3-dev)"
    MISSING_DEPS=1
fi

if [ $MISSING_DEPS -eq 1 ]; then
    echo ""
    echo "Please install missing dependencies:"
    echo "  Ubuntu/Debian: sudo apt install libdbus-dev libgtk-3-dev"
    echo "  Fedora/RHEL:   sudo dnf install dbus-devel gtk3-devel"
    echo "  Arch:          sudo pacman -S dbus gtk3"
    exit 1
fi

echo "All dependencies found."
echo

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Creating build directory..."
rm -rf build_linux
mkdir build_linux

echo "Backing up Windows CMakeLists and copying Linux CMakeLists..."
cp CMakeLists.txt CMakeLists.txt.win
cp CMakeLists_linux.txt CMakeLists.txt

echo "Running CMake..."
cd build_linux
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..

echo
echo "Building Release..."
cmake --build . --config Release

cd "$SCRIPT_DIR"

echo "Restoring Windows CMakeLists..."
mv CMakeLists.txt.win CMakeLists.txt

echo
echo "Build complete!"
echo "SO location: ../natives/Linux/chineseime_native_linux.so"
echo

if [ -f "../natives/Linux/chineseime_native_linux.so" ]; then
    echo "SUCCESS: chineseime_native_linux.so created successfully."
else
    echo "WARNING: SO not found in expected location."
fi