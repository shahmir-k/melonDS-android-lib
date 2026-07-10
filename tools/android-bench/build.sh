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
      # LITEV_LDMSTM (default ON): DraStic-style ldp/stp-paired inline LDM/STM
      # block transfers + MainRAM inline block-LOAD tier. A/B: LITEV_LDMSTM=OFF ./build.sh full
      # LITEV_INSTANT_DIVSQRT (validated +5% cooled emu-core): compute the ARM9
      # hardware DIV/SQRT result at register-write time and skip the scheduled
      # completion event (~1016 Event_Div + ~79 Event_Sqrt scheduler slices/frame
      # removed). DraStic's approach; ARM9-only so MP-safe. Correct at frameskip 0.
      # LITEV_SPU_BATCH / LITEV_COARSE_RTC (DraStic event-flood coarsening, default ON):
      # removes ~1026 scheduler events/frame (SPU 547->68 via 8-sample batches;
      # RTC 548->~0.5 by skipping inert 32768Hz ticks). Cooled interleaved A/B on
      # `full` @1416MHz fs0: 9.73 -> 10.09 fps median (+3.7%, both flags). Render
      # recognizable, audio still plays (coarser). A/B off: LITEV_SPU_BATCH=OFF
      # LITEV_COARSE_RTC=OFF ./build.sh full. (fs>0 harness hang is PRE-EXISTING:
      # reproduces on baseline with flags OFF; real gameplay/app runs fs0.)
      # LITEV_DMA_GXFIFO_FAST (bit-exact DMA dispatch elision, default ON): direct
      # MainRAM->WriteToGXFIFO fast path for the geometry-DMA stream. DMA::Run9 is
      # ~10.5% of the emu thread on the Shrek race; ~60% of that is the GXFIFO
      # dispatch chain this elides. A/B off: LITEV_DMA_GXFIFO_FAST=OFF ./build.sh full
      # LITEV_IDLE_AGGRESSIVE (DraStic register-recurrent idle recognition, default
      # OFF): accepts poll loops with a dead spin/timeout counter as idle so the
      # CPU fast-forwards to the next event. A/B on: LITEV_IDLE_AGGRESSIVE=ON ./build.sh full
      # LITEV_HLE_BIOS_SWI (DraStic HLE BIOS, default OFF): intercept Div/CpuSet/
      # CpuFastSet/Sqrt SWIs natively (bit-exact vs FreeBIOS) instead of running the
      # LLE BIOS handler through the JIT; attacks the arm9_exec bucket. A/B on:
      # LITEV_HLE_BIOS_SWI=ON ./build.sh full
      # LITEV_GEOM_CLIP_FAST (DraStic trivial-accept fast clip, default OFF): skip
      # Sutherland-Hodgman for fully-inside polygons (bit-exact early-out). A/B on:
      # LITEV_GEOM_CLIP_FAST=ON ./build.sh full
      # LITEV_SCHED_FAST (DraStic leaner scheduler, default OFF, bit-exact): cached
      # next-event deadline + RunSystem early-out. run_system's ~2058 RunSystem
      # calls/frame fire an event only ~596 times; the early-out skips the mask
      # scan+dispatch for the other ~1461. Same events/order => ON==OFF byte-identical
      # (final_top/final_bot/audio). NOTE: run_system on `full` is ~92% inline 2D/3D
      # raster (lcd_draw), so SCHED_FAST attacks only the ~1ms scheduler-machinery
      # slice, not the raster. A/B on: LITEV_SCHED_FAST=ON ./build.sh full
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_JIT_LDMSTM=${LITEV_LDMSTM:-ON} -DLITEV_INSTANT_DIVSQRT=ON -DLITEV_SPU_BATCH=${LITEV_SPU_BATCH:-ON} -DLITEV_COARSE_RTC=${LITEV_COARSE_RTC:-ON} -DLITEV_DMA_GXFIFO_FAST=${LITEV_DMA_GXFIFO_FAST:-ON} -DLITEV_IDLE_AGGRESSIVE=${LITEV_IDLE_AGGRESSIVE:-OFF} -DLITEV_TIMER_FAST=${LITEV_TIMER_FAST:-OFF} -DLITEV_SCHED_FAST=${LITEV_SCHED_FAST:-OFF} -DLITEV_HLE_BIOS_SWI=${LITEV_HLE_BIOS_SWI:-OFF} -DLITEV_ARM7_IDLE=${LITEV_ARM7_IDLE:-OFF} -DLITEV_JIT_FLAGMERGE=${LITEV_FLAGMERGE:-ON} -DLITEV_GEOM_CLIP_FAST=${LITEV_GEOM_CLIP_FAST:-OFF}" ;;
    soft2d)
      # Banded deferred software-2D raster (LITEV_SOFT2D_THREADED) + banded
      # multi-core software-3D raster (LITEV_SOFT3D_BANDED) on top of the `full`
      # stack. A/B against `full` isolates the software-raster-off-critical-path
      # delta on the A55. MUST be bit-exact vs `full` (final_top/final_bot/audio
      # identical).
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_SOFT2D_THREADED=ON -DLITEV_SOFT3D_BANDED=ON" ;;
    soft3dfast)
      # soft2d stack (2D threading + banded 3D) PLUS subaffine (approximate)
      # software-3D span interpolation (LITEV_SOFT3D_FAST). FPS-first: kills the
      # per-pixel perspective-correct divide in RenderPolygonScanline. Deliberately
      # NOT bit-exact vs soft2d/full — A/B against `full` for the fps delta and
      # eyeball the framebuffer for recognizability.
      # LITEV_SPU_BATCH / LITEV_COARSE_RTC (DraStic event-flood coarsening, default ON):
      # removes ~1026 scheduler events/frame (see `full`). On this render-bound
      # config the emu-core saving is in the fps noise (neutral) but the floods are
      # gone (SPU 547->68, RTC 548->~0.5, verified) + spu_mix -20%; render OK, audio
      # OK. A/B off: LITEV_SPU_BATCH=OFF LITEV_COARSE_RTC=OFF ./build.sh soft3dfast.
      # LITEV_DMA_GXFIFO_FAST (bit-exact DMA dispatch elision, default ON): see `full`.
      # A/B off: LITEV_DMA_GXFIFO_FAST=OFF ./build.sh soft3dfast
      # LITEV_IDLE_AGGRESSIVE / LITEV_TIMER_FAST (DraStic emu-core levers, default
      # OFF): A/B on: LITEV_IDLE_AGGRESSIVE=ON ./build.sh soft3dfast
      # LITEV_HLE_BIOS_SWI (DraStic HLE BIOS, default OFF): see `full`. Attacks the
      # arm9_exec bucket by intercepting Div/CpuSet/CpuFastSet/Sqrt natively (bit-exact
      # vs FreeBIOS). A/B on: LITEV_HLE_BIOS_SWI=ON ./build.sh soft3dfast
      # LITEV_GEOM_CLIP_FAST (DraStic trivial-accept fast clip, default OFF): skip
      # Sutherland-Hodgman for fully-inside polygons (bit-exact early-out vs the full
      # clip). A/B on: LITEV_GEOM_CLIP_FAST=ON ./build.sh soft3dfast
      # LITEV_SCHED_FAST (DraStic leaner scheduler, default OFF, bit-exact vs this
      # config): cached next-event deadline + RunSystem early-out. NOTE: run_system
      # on soft3dfast is ~78% the emu thread BLOCKING to join the async render
      # thread at VBlank (lcd_vblank_wait), NOT scheduler dispatch -- SCHED_FAST
      # attacks only the ~1ms genuine scheduler-machinery slice. A/B on:
      # LITEV_SCHED_FAST=ON ./build.sh soft3dfast
      # LITEV_SOFT3D_HANDNEON (DraStic-disassembled inlined per-pixel depth test,
      # default OFF => byte-identical to plain soft3dfast): replaces the per-pixel
      # indirect fnDepthTest function-pointer call in the raster with an inlined
      # branchless compare (ported from FUN_0015fb8c / the 0x8dxxx plot kernels).
      # A/B on: LITEV_HANDNEON=ON ./build.sh soft3dfast
      # LITEV_SOFT3D_INTERPNEON (4-wide NEON subaffine interpolation ramp, default
      # OFF => byte-identical to plain soft3dfast): vectorizes the per-pixel s64
      # accumulator ramp (z + r/g/b/s/t linear step between perspective anchors) in
      # the interior batched fast path — z via int64x2, rgb/st via int32x4. Attacks
      # the diffuse ramp ALU a prior NOSASTEP test flagged as a ~+11% wall-time
      # ceiling (helps energy/sustained-fps under the RG DS throttle). Approximate.
      # A/B on: LITEV_INTERPNEON=ON ./build.sh soft3dfast
      # LITEV_SOFT3D_ASM (hand-written AArch64 assembly transcription of DraStic's
      # span-fill inner texture-modulate loop, default OFF => byte-identical to plain
      # soft3dfast): replaces shade4's compiler-intrinsic modulate with
      # src/GPU3D_Soft_asm.S, transcribed instruction-for-instruction from
      # libdrastic_arm64.so's 0x5ff60-0x5ffbc 4px/iter body (exact register roles +
      # NEON scheduling; only the vertex factors are loaded per-pixel instead of dup'd
      # since melonDS is Gouraud). Fewer host instructions/pixel-batch => lower
      # dynamic-instruction energy/frame => higher SUSTAINED fps under the RG DS
      # thermal throttle. Verified bit-exact vs the intrinsic on arm64 hardware
      # (400k px). A/B on: LITEV_ASM=ON ./build.sh soft3dfast
      # LITEV_SOFT2D_NEON (4-wide NEON port of the per-pixel 2D BG/OBJ colour-effect
      # compositor SoftRenderer2D::ColorComposite — DraStic-style; teardown
      # FUN_001494a4, 927 NEON ops — replacing a ~133-instr out-of-line CALL PER
      # PIXEL with a branchless 4px/iter NEON kernel, ~3x fewer instrs/px). In the
      # threaded 2D config the 2D worker is frame-limiting, so this unblocks it:
      # +59% steady-state fps on M3 (native, single-thread shows noise; A55 differs
      # in magnitude). Verified bit-exact (top/bottom/audio hashes ON==OFF, 2000
      # frames). Default OFF => byte-identical. A/B on: LITEV_SOFT2D_NEON=ON ./build.sh soft3dfast
      # LITEV_SOFT2D_OBJNEON (NEON branchless reject-scan for the 2D sprite
      # interleaver SoftRenderer2D::InterleaveSprites, default OFF => byte-identical
      # to plain soft3dfast): InterleaveSprites runs once per BG priority level (up
      # to 4×/scanline); at a given priority most of the 256 pixels carry no
      # matching opaque sprite, so the scalar loop is dominated by the
      # (OBJLine[i]&OpaPrioMask)!=attrmask reject path. This lever tests the prio
      # match 16 lanes at a time (uint32x4 vceq + reduce) and skips whole 16-pixel
      # chunks with zero matches; matching chunks fall through to the identical
      # scalar body (palette gather has no AArch64 NEON equivalent). ~3.5× fewer
      # instrs on the dominant reject path => lower dynamic-instruction energy/frame
      # => higher SUSTAINED fps under the RG DS thermal throttle. Bit-exact
      # (final_top/bot/audio hashes ON==OFF, 2000 frames). On M3: +3.8% single-thread
      # (where the interleaver is on the critical path); neutral in the threaded
      # config (the 2D worker has slack at ~595fps native — it waits on the emu
      # thread). A/B on: LITEV_SOFT2D_OBJNEON=ON ./build.sh soft3dfast
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_SOFT2D_THREADED=ON -DLITEV_SOFT3D_BANDED=ON -DLITEV_SOFT3D_FAST=ON -DLITEV_SOFT3D_HANDNEON=${LITEV_HANDNEON:-OFF} -DLITEV_SOFT3D_INTERPNEON=${LITEV_INTERPNEON:-OFF} -DLITEV_SOFT3D_ASM=${LITEV_ASM:-OFF} -DLITEV_SOFT2D_NEON=${LITEV_SOFT2D_NEON:-OFF} -DLITEV_SOFT2D_OBJNEON=${LITEV_SOFT2D_OBJNEON:-OFF} -DLITEV_INSTANT_DIVSQRT=ON -DLITEV_SPU_BATCH=${LITEV_SPU_BATCH:-ON} -DLITEV_COARSE_RTC=${LITEV_COARSE_RTC:-ON} -DLITEV_DMA_GXFIFO_FAST=${LITEV_DMA_GXFIFO_FAST:-ON} -DLITEV_IDLE_AGGRESSIVE=${LITEV_IDLE_AGGRESSIVE:-OFF} -DLITEV_TIMER_FAST=${LITEV_TIMER_FAST:-OFF} -DLITEV_SCHED_FAST=${LITEV_SCHED_FAST:-OFF} -DLITEV_HLE_BIOS_SWI=${LITEV_HLE_BIOS_SWI:-OFF} -DLITEV_ARM7_IDLE=${LITEV_ARM7_IDLE:-OFF} -DLITEV_JIT_FLAGMERGE=${LITEV_FLAGMERGE:-ON} -DLITEV_GEOM_CLIP_FAST=${LITEV_GEOM_CLIP_FAST:-OFF}" ;;
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
    full-instantdiv)
      # Instant ARM9 divider/sqrt on top of `full`. A/B against `full` isolates
      # the LITEV_INSTANT_DIVSQRT delta — removing ~1233 Event_Div + ~80 Event_Sqrt
      # dispatches/frame (the single largest in-race scheduler event source).
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_INSTANT_DIVSQRT=ON" ;;
    full-relaxed9)
      # M6.12 / plan §8: the app-matching `full-neon` stack + DraStic-style flat
      # ARM9 timing. A/B against `full-neon` isolates the LITEV_RELAXED_ARM9_TIMING
      # delta on the A55 (targets the 7.55ms ARM9 bucket incl. GXFIFO write timing).
      # ARM7 timing stays exact (WiFi invariant). Deliberate semantic timing change.
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_NEON_GEOMETRY=ON -DLITEV_RELAXED_ARM9_TIMING=ON" ;;
    full-max)
      # Max emu-core stack: full + geometry-NEON + relaxed-ARM9 + instant-div/sqrt.
      # The cumulative ceiling for the emu-core grind (2026-07-07); A/B each delta
      # against full to attribute the gains.
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_NEON_GEOMETRY=ON -DLITEV_RELAXED_ARM9_TIMING=ON -DLITEV_INSTANT_DIVSQRT=ON" ;;
    full-gxthreaded)
      # DraStic backlog #3: batched GXFIFO threaded-code interpreter. Exactly the
      # `swtable-pin` stack + LITEV_GXFIFO_THREADED. A/B against `swtable-pin`
      # isolates the geometry-dispatch delta (removes the per-command bl/ret +
      # prologue/epilogue by hoisting Run()'s drain loop into ExecuteCommand() as
      # threaded code, plus the computed-goto jump table). Geometry output +
      # audio are bit-exact vs swtable-pin.
      echo "-DLITEV_JIT_DISPATCH=ON -DLITEV_LINK_UNCOND=ON -DLITEV_LINK_COND=ON -DLITEV_LINK_FALLTHROUGH=ON -DLITEV_EVENT_SLICES=ON -DLITEV_MEM_DTCM_BLOCK=ON -DLITEV_MEM_MAINRAM_LOAD=ON -DLITEV_MEM_SWTABLE=ON -DLITEV_JIT_FIXEDREG=ON -DLITEV_JIT_GLOBALREG=ON -DLITEV_GXFIFO_THREADED=ON" ;;
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
