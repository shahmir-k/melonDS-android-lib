#!/usr/bin/env bash
#
# Cross-compile the liteDS-headless CLI for Android arm64-v8a (Cortex-A55)
# using the Android NDK's CMake toolchain. Produces one static-libc++ binary
# per liteDS perf config, so the A55 benchmark matrix (Unit 7) is runnable on
# a device over adb WITHOUT porting the Android app.
#
# Usage:
#   tools/android-bench/build.sh            # build all configs
#   tools/android-bench/build.sh baseline   # build one config
#
# Output: build-android/<config>/liteDS-headless  (stripped)
#
# Configs (cumulative liteDS stack, matching docs/liteDS-v2-plan.md Appendix C):
#   baseline  - no LITEV perf flags (upstream-equivalent core)
#   dispatch  - + LITEV_JIT_DISPATCH, links OFF
#   link      - + all LITEV_LINK_* (dispatcher + block chaining)
#   es        - + LITEV_EVENT_SLICES (event-true scheduler slices)
#   full      - + LITEV_MEM_DTCM_BLOCK + LITEV_MEM_MAINRAM_LOAD (full stack)
#
# LITEV_AGGRESSIVE_SKIP is compiled into every config so the runtime --frameskip
# knob is exercisable across the whole matrix; it is inert at frameskip 0, so the
# frameskip-0 numbers remain representative of each config.
set -euo pipefail

# ---- toolchain / device target ---------------------------------------------
NDK="${ANDROID_NDK:-/Users/shahmir/android-sdk/ndk/27.0.12077973}"
CMAKE_BIN="${CMAKE_BIN:-/Users/shahmir/android-sdk/cmake/3.22.1/bin/cmake}"
ABI="arm64-v8a"
API="${ANDROID_API:-34}"          # RG DS is Android 14 (API 34)
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"

[ -f "$TOOLCHAIN" ] || { echo "error: NDK toolchain not found at $TOOLCHAIN"; exit 1; }
[ -x "$CMAKE_BIN" ] || { echo "error: cmake not found at $CMAKE_BIN"; exit 1; }

# NDK r27's llvm-strip.
STRIP="$(echo "$NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip)"

# Common flags: no Qt/SDL, no GL, no GDB; headless harness on; static libc++ so
# the binary runs standalone in /data/local/tmp; LTO off for fast, reproducible
# cross builds.
COMMON=(
  -G "Unix Makefiles"
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
  -DANDROID_ABI="$ABI"
  -DANDROID_PLATFORM="android-$API"
  -DANDROID_STL=c++_static
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_QT_SDL=OFF
  -DENABLE_OGLRENDERER=OFF
  -DENABLE_GDBSTUB=OFF
  -DENABLE_JIT=ON
  -DENABLE_LTO=OFF
  -DENABLE_LTO_RELEASE=OFF
  -DLITEV_HEADLESS=ON
  -DLITEV_PROFILE=OFF
  -DLITEV_AGGRESSIVE_SKIP=ON
)

# Per-config LITEV flag deltas.
config_flags() {
  case "$1" in
    baseline)
      echo "-DLITEV_JIT_DISPATCH=OFF" ;;
    dispatch)
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=OFF -DLITEV_LINK_COND=OFF -DLITEV_LINK_FALLTHROUGH=OFF" ;;
    link)
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON" ;;
    es)
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON" ;;
    full)
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON" ;;
    *)
      echo "error: unknown config '$1'" >&2; return 1 ;;
  esac
}

build_one() {
  local cfg="$1"
  local bdir="$REPO/build-android/$cfg"
  local flags; flags="$(config_flags "$cfg")" || exit 1
  echo "=============================================================="
  echo ">>> configuring config=$cfg  (ABI=$ABI API=$API)"
  echo "    flags: $flags"
  echo "=============================================================="
  # shellcheck disable=SC2086
  "$CMAKE_BIN" -S "$REPO" -B "$bdir" "${COMMON[@]}" $flags
  "$CMAKE_BIN" --build "$bdir" --target liteDS-headless -j "$JOBS"

  local bin="$bdir/liteDS-headless"
  [ -f "$bin" ] || { echo "error: build produced no binary for $cfg"; exit 1; }
  local before; before=$(stat -f%z "$bin")
  "$STRIP" --strip-all "$bin"
  local after; after=$(stat -f%z "$bin")
  echo ">>> $cfg: $bin  ($before -> $after bytes stripped)"
}

CONFIGS=(baseline dispatch link es full)
if [ $# -ge 1 ]; then CONFIGS=("$@"); fi

for c in "${CONFIGS[@]}"; do
  build_one "$c"
done

echo
echo "=== binary sizes (stripped) ==="
for c in "${CONFIGS[@]}"; do
  b="$REPO/build-android/$c/liteDS-headless"
  [ -f "$b" ] && printf "  %-10s %8d bytes  (%s)\n" "$c" "$(stat -f%z "$b")" "$(file -b "$b" | cut -c1-40)"
done
