#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

IMGUI_VERSION="v1.91.8"
BUILD_TYPE="${1:-Release}"

echo ""
echo "===================================================="
echo "       STL Viewer & Exporter - Linux/macOS Setup     "
echo "===================================================="
echo ""

# ── Check system dependencies ────────────────────────────────────────────────

check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found."
        return 1
    fi
}

echo "Checking dependencies..."
check_cmd cmake
check_cmd g++ || check_cmd clang++
check_cmd git

MISSING=""
if ! pkg-config --exists glfw3 2>/dev/null; then MISSING+=" libglfw3-dev"; fi
if ! pkg-config --exists glew 2>/dev/null;  then MISSING+=" libglew-dev"; fi

if [ -n "$MISSING" ]; then
    echo ""
    echo "Missing packages:$MISSING"
    if command -v apt-get &>/dev/null; then
        echo "Install with:  sudo apt-get install -y$MISSING libgl1-mesa-dev"
    elif command -v dnf &>/dev/null; then
        echo "Install with:  sudo dnf install -y glfw-devel glew-devel mesa-libGL-devel"
    elif command -v brew &>/dev/null; then
        echo "Install with:  brew install glfw glew"
    fi
    read -p "Continue anyway? [y/N] " -n 1 -r
    echo
    [[ $REPLY =~ ^[Yy]$ ]] || exit 1
fi
echo "OK"
echo ""

# ── Download Dear ImGui ──────────────────────────────────────────────────────

if [ ! -f "imgui/imgui.cpp" ]; then
    echo "Downloading Dear ImGui ${IMGUI_VERSION}..."
    mkdir -p imgui
    BASE="https://raw.githubusercontent.com/ocornut/imgui/${IMGUI_VERSION}"

    for f in imgui.cpp imgui.h imgui_internal.h imgui_demo.cpp imgui_draw.cpp \
             imgui_tables.cpp imgui_widgets.cpp imconfig.h imstb_rectpack.h \
             imstb_textedit.h imstb_truetype.h; do
        echo "  imgui/$f"
        curl -sL "${BASE}/${f}" -o "imgui/${f}"
    done
    for f in imgui_impl_glfw.cpp imgui_impl_glfw.h imgui_impl_opengl3.cpp \
             imgui_impl_opengl3.h imgui_impl_opengl3_loader.h; do
        echo "  imgui/$f (backend)"
        curl -sL "${BASE}/backends/${f}" -o "imgui/${f}"
    done
    echo "Done."
else
    echo "Dear ImGui already present."
fi
echo ""

# ── Download stb_image_write ─────────────────────────────────────────────────

if grep -q "PLACEHOLDER" stb/stb_image_write.h 2>/dev/null; then
    echo "Downloading stb_image_write.h..."
    curl -sL "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" \
         -o "stb/stb_image_write.h"
    echo "Done."
else
    echo "stb_image_write.h already present."
fi
echo ""

# ── Build ────────────────────────────────────────────────────────────────────

echo "Building (${BUILD_TYPE})..."
cmake --preset "$(echo $BUILD_TYPE | tr '[:upper:]' '[:lower:]')" 2>/dev/null || \
    cmake -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "===================================================="
echo "                  Build complete!                     "
echo "===================================================="
echo ""
echo "Run:  ./build/stl_viewer"
echo "      ./build/stl_viewer model.stl"
echo "      ./build/stl_viewer ./stl_folder/"
