#!/usr/bin/env bash
set -euo pipefail

# --- locate project root & emsdk ----------------------------------------
ROOT="$(cd "$(dirname "$0")" && pwd)"
EMSDK_PATH="$ROOT/../../../SDKS/emsdk"

# --- load Emscripten ----------------------------------------------------
source "$EMSDK_PATH/emsdk_env.sh"

# --- prepare output dir ------------------------------------------------
mkdir -p "$ROOT/web_build"

# --- gather all source files -------------------------------------------
# Finds .cpp and .c under src/ and builds a space-separated list
SRC_FILES=$(find "$ROOT/src" -type f \( -name '*.cpp' -o -name '*.c' \))

# --- compile & link with em++ ------------------------------------------
em++ $SRC_FILES \
    -std=c++17 \
    -Os -Wall \
    -I"$ROOT" \
    -I"$ROOT/lib/raylib/src" \
    -I"$ROOT/lib/box2d/include" \
    -L"$ROOT/lib/box2d/build/src" -lbox2d \
    -L"$ROOT/lib/raylib/src" -lraylib.web \
    -s USE_GLFW=3 \
    -s ASYNCIFY \
    --shell-file "$ROOT/shell.html" \
    -s TOTAL_STACK=64MB \
    -s INITIAL_MEMORY=256MB \
    -s MAXIMUM_MEMORY=512MB \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ASSERTIONS=2 \
    -s SAFE_HEAP=1 \
    -DPLATFORM_WEB \
    -o "$ROOT/web_build/index.html"

# --- serve ----------------------------------------------------------------
emrun --no_browser --port 8080 "$ROOT/web_build/index.html"

# --embed-file "$ROOT/assets@/assets" \

