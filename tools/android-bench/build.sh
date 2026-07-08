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
  -DLITEV_PROFILE="${LITEV_PROFILE:-OFF}"
  -DLITEV_AGGRESSIVE_SKIP=ON
)

# Optional separate output tree so a profiled build (LITEV_PROFILE=ON) does not
# clobber the default non-profiled binaries. Defaults empty => unchanged paths.
BUILD_TAG="${BUILD_TAG:-}"

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
    soft2d)
      # Banded deferred software-2D raster (LITEV_SOFT2D_THREADED) on top of the
      # `full` stack. A/B against `full` (inline per-scanline 2D) isolates the
      # 2D-off-critical-path delta on the A55. MUST be bit-exact vs `full`
      # (final_top/final_bot/audio_hash identical).
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_SOFT2D_THREADED=ON" ;;
    swtable)
      # DraStic branchless software page-table fastmem on the load hot path
      # (LITEV_MEM_SWTABLE), on top of the `full` stack. A/B against `full`
      # (fault-based fastmem, --fastmem on) isolates the sw-table load-path delta.
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_MEM_SWTABLE=ON" ;;
    swtable-pin)
      # STEP 2: sw-table + widened global register pin (frees the MemBase reg).
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_MEM_SWTABLE=ON -DLITEV_JIT_FIXEDREG=ON -DLITEV_JIT_GLOBALREG=ON" ;;
    swtable-pin-store)
      # STORE-side sw-table RETRY (plan D.7 addendum 28): swtable-pin + the DraStic-faithful
      # store fast path (LITEV_MEM_SWTABLE_STORE). Single store table, SMC folded into the
      # entry (delta zeroed on code pages). A/B against `swtable-pin` (loads-only) isolates
      # the store-path delta on the A55.
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_MEM_SWTABLE=ON -DLITEV_JIT_FIXEDREG=ON -DLITEV_JIT_GLOBALREG=ON -DLITEV_MEM_SWTABLE_STORE=ON" ;;
    full-neon)
      # M6.11 step 3: the app-matching `full` stack + integer-NEON GPU3D geometry.
      # A/B against `full` isolates the LITEV_NEON_GEOMETRY delta on the A55.
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_NEON_GEOMETRY=ON" ;;
    full-relaxed9)
      # M6.12 / plan §8: the app-matching `full-neon` stack + DraStic-style flat
      # ARM9 timing. A/B against `full-neon` isolates the LITEV_RELAXED_ARM9_TIMING
      # delta on the A55 (targets the 7.55ms ARM9 bucket incl. GXFIFO write timing).
      # ARM7 timing stays exact (WiFi invariant). Deliberate semantic timing change.
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_NEON_GEOMETRY=ON -DLITEV_RELAXED_ARM9_TIMING=ON" ;;
    *)
      echo "error: unknown config '$1'" >&2; return 1 ;;
  esac
}

build_one() {
  local cfg="$1"
  local bdir="$REPO/build-android${BUILD_TAG}/$cfg"
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
  b="$REPO/build-android${BUILD_TAG}/$c/liteDS-headless"
  [ -f "$b" ] && printf "  %-10s %8d bytes  (%s)\n" "$c" "$(stat -f%z "$b")" "$(file -b "$b" | cut -c1-40)"
done
