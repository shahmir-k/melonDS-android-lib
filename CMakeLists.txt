cmake_minimum_required(VERSION 3.16)

cmake_policy(VERSION 3.15)
if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
endif()

set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DefaultBuildFlags.cmake")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

option(USE_VCPKG "Use vcpkg for dependency packages" OFF)
if (USE_VCPKG)
	include(ConfigureVcpkg)
endif()

project(melonDS
    VERSION 1.1
    DESCRIPTION "DS emulator, sorta"
    HOMEPAGE_URL "https://melonds.kuribo64.net"
    LANGUAGES C CXX)

include(CheckSymbolExists)
include(CheckLibraryExists)
include(CMakeDependentOption)
include(CheckIPOSupported)

include(SetupCCache)
include(Sanitizers)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

detect_architecture("__x86_64__" x86_64)
detect_architecture("__i386__" x86)
detect_architecture("__arm__" ARM)
detect_architecture("__aarch64__" ARM64)

cmake_dependent_option(ENABLE_JIT "Enable JIT recompiler" ON
    "ARCHITECTURE STREQUAL x86_64 OR ARCHITECTURE STREQUAL ARM64;NOT ARCHITECTURE STREQUAL x86_64 OR NOT APPLE" OFF)
cmake_dependent_option(ENABLE_JIT_PROFILING "Enable JIT profiling with VTune" OFF "ENABLE_JIT" OFF)
option(ENABLE_OGLRENDERER "Enable OpenGL renderer" ON)

check_ipo_supported(RESULT IPO_SUPPORTED)
cmake_dependent_option(ENABLE_LTO_RELEASE "Enable link-time optimizations for release builds" ON "IPO_SUPPORTED" OFF)
cmake_dependent_option(ENABLE_LTO "Enable link-time optimizations" OFF "IPO_SUPPORTED" OFF)

if (ENABLE_LTO_RELEASE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if (ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
endif()

if (WIN32)
    option(BUILD_STATIC "Statically link dependencies" OFF)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    if (NOT MINGW)
        add_definitions(-D__WIN32__ -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES)
        if (BUILD_STATIC)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    elseif (BUILD_STATIC AND MINGW)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_GDBSTUB "Enable GDB stub" ON)
if (ENABLE_GDBSTUB)
    add_definitions(-DGDBSTUB_ENABLED)
endif()

option(BUILD_QT_SDL "Build Qt/SDL frontend" ON)

# liteDS-v2 additions (all default OFF -> upstream-identical build with no flags).
option(LITEV_HEADLESS "Build the liteDS-headless benchmark harness" OFF)
option(LITEV_PROFILE "Enable LiteProfile frame-breakdown counters" OFF)
if (LITEV_PROFILE)
    # Reach the core too (not just the headless harness) so in-core call sites
    # (e.g. the Unit 3 dispatcher re-entry counters in ARM.cpp) are compiled in.
    add_compile_definitions(LITEV_PROFILE=1)
endif()
# liteV soft-2D threading: banded deferred software-2D raster. Snapshots the
# per-scanline 2D register state on the emu thread, then rasterizes all 192
# scanlines off the LCD critical path at VBlank, split into N contiguous bands
# across a small helper-thread pool (mirrors GPU3D_Soft's threading). Default OFF
# => flag-OFF build is byte-identical; flag-ON must be bit-exact (top/bottom/audio
# framebuffer hashes identical to flag-OFF).
option(LITEV_SOFT2D_THREADED "liteV: banded deferred software 2D raster off the LCD critical path" OFF)
if (LITEV_SOFT2D_THREADED)
    add_compile_definitions(LITEV_SOFT2D_THREADED=1)
endif()
# Banded software 3D rasterizer: split GPU3D_Soft's single render-thread scanline
# raster into N contiguous scanline bands across cores. Each band walks all
# scanlines (to keep the incremental edge state exact) but only writes its own
# rows; disjoint scanline writes => no shared-write race. Flag-OFF is byte-identical
# (all changes behind #ifdef LITEV_SOFT3D_BANDED); flag-ON must be bit-exact.
option(LITEV_SOFT3D_BANDED "liteV: banded (multi-core) software 3D rasterizer" OFF)
if (LITEV_SOFT3D_BANDED)
    add_compile_definitions(LITEV_SOFT3D_BANDED=1)
endif()
# Software-render phase profiler: names every render thread and logs a per-60-frame
# decomposition (emu barrier / snapshot, 3D clear/raster/final + per-band, 2D wall +
# per-band GetLine block, whole render critical path). Diagnostic only; default OFF.
option(LITEV_SOFTPROF "liteV: software-render phase profiler (thread names + phase ms)" OFF)
if (LITEV_SOFTPROF)
    add_compile_definitions(LITEV_SOFTPROF=1)
endif()
# Async software 3D: let the banded raster run PAST VBlank into the next frame.
# Stock melonDS starts the 3D raster at VCount 215 and hard-barriers the emu thread on
# it at the next VBlank (GPU::VBlank -> Finish3DRendering), so the raster gets only
# ~1/2 a frame of wall. MEASURED on the Shrek race (A55, software renderer): the raster
# needs ~26 ms but is given ~15 ms, so the emu stalls ~9.4 ms EVERY frame -- that stall
# IS the 40fps wall. The game flushes 3D geometry only every OTHER frame, so the raster
# actually has two frames of headroom. This flag barriers the emu ONLY when VBlank is
# about to mutate state the raster still reads (geometry re-sort / Render* regs), and
# only flattens texture VRAM behind that same barrier. Output is unchanged (the 2D
# compositor already paces against the raster per-scanline via Sema_ScanlineCount);
# FBHASH-gated. Default OFF.
option(LITEV_RENDER_4CORE "liteV: let the software raster use ALL 4 cores (emu core 3 idles ~43% once SOFT3D_ASYNC removes its barrier stall; emu nice -10 still preempts)" OFF)
if (LITEV_RENDER_4CORE)
    add_compile_definitions(LITEV_RENDER_4CORE=1)
endif()

option(LITEV_SOFT3D_STREAM "liteV: stream 3D scanlines to the 2D compositor as bands finish them (kills the 8.7ms 2D block behind the raster barrier)" OFF)
if (LITEV_SOFT3D_STREAM)
    add_compile_definitions(LITEV_SOFT3D_STREAM=1)
endif()

# LITEV_SOFT3D_OVERLAP: same goal as STREAM (overlap the ~9.3ms 2D composite with the
# ~14.7ms 3D raster instead of serializing it behind the raster barrier), but drives the
# overlap from the CONSUMER (the 2D async thread's GetLine) instead of a producer-side
# releaser. STREAM regressed because its releaser ran on the 3D coordinator thread and
# BUSY-WAITED, stealing a core from the 3 band workers. Here the final pass + scanline
# release happen inside GetLine on the s2d-async thread -- which was going to BLOCK for
# the raster anyway -- pinned to the emu's idle core 3 (bands stay {0,1,2}). Per-row
# readiness is a double-buffered flag array (RowRastered[parity][row]) so the two frames
# in flight under SOFT3D_ASYNC (consumer of frame N overlaps the raster kick of N+1 over
# a single-buffered ColorBuffer) never collide. FBHASH-gated. Default OFF.
option(LITEV_SOFT3D_OVERLAP "liteV: overlap the 2D composite with the 3D raster, consumer-driven (final pass in GetLine), no extra threads" OFF)
if (LITEV_SOFT3D_OVERLAP)
    add_compile_definitions(LITEV_SOFT3D_OVERLAP=1)
endif()

option(LITEV_SOFT3D_ASYNC "liteV: soft 3D raster may span into the next frame (no VBlank barrier unless state is mutated)" OFF)
if (LITEV_SOFT3D_ASYNC)
    add_compile_definitions(LITEV_SOFT3D_ASYNC=1)
endif()
# Subaffine 3D span interpolation: replaces the per-pixel perspective-correct
# divide (Interpolator::SetX) in RenderPolygonScanline's span loops with a
# perspective-correct anchor every 8 pixels + linear stepping in between.
# FPS-first, APPROXIMATE (deliberately NOT bit-exact — some texture warp on
# large near polys is expected). All changes behind #ifdef LITEV_SOFT3D_FAST;
# flag-OFF is byte-identical.
option(LITEV_SOFT3D_FAST "liteV: subaffine (approximate) software 3D span interpolation" OFF)
if (LITEV_SOFT3D_FAST)
    add_compile_definitions(LITEV_SOFT3D_FAST=1)
endif()
# Hand-NEON raster energy lever (builds on LITEV_SOFT3D_FAST): ports DraStic's
# INLINED branchless per-pixel depth compare (disassembled from FUN_0015fb8c and
# the 0x8dxxx plot kernels) into RenderPolygonScanline / RenderShadowMaskScanline,
# replacing melonDS's per-pixel indirect FUNCTION-POINTER depth test. Removes one
# indirect call (+ its register save/restore) per rasterized pixel — a pure
# dynamic-instruction/ENERGY reduction (the lever for the RG DS thermal throttle),
# bit-identical to the four DepthTest_* functions. All changes behind
# #ifdef LITEV_SOFT3D_HANDNEON; flag-OFF is byte-identical.
option(LITEV_SOFT3D_HANDNEON "liteV: DraStic-disassembled inlined per-pixel depth test" OFF)
if (LITEV_SOFT3D_HANDNEON)
    add_compile_definitions(LITEV_SOFT3D_HANDNEON=1)
endif()
# 4-wide NEON of the per-pixel SUBAFFINE INTERPOLATION RAMP (builds on
# LITEV_SOFT3D_FAST): in the interior batched fast path, the diffuse per-pixel
# s64 accumulator ramp (z + r/g/b/s/t linear step between perspective anchors,
# the SA_STEP_Z / SA_LOAD_RGBST macros) is computed 4 pixels at a time with
# NEON — z via int64x2 pairs (vaddq_s64), r/g/b/s/t via int32x4 (the linear
# ramp values fit 32-bit between anchors). The perspective-correct anchor divide
# every SA_SUB px stays scalar. A prior NOSASTEP measurement showed the ramp ALU
# is a real ~+11% wall-time ceiling (not memory-hidden). Approximate (soft3dfast
# already is); all changes behind #ifdef LITEV_SOFT3D_INTERPNEON; flag-OFF is
# byte-identical.
option(LITEV_SOFT3D_INTERPNEON "liteV: 4-wide NEON subaffine span interpolation ramp" OFF)
if (LITEV_SOFT3D_INTERPNEON)
    add_compile_definitions(LITEV_SOFT3D_INTERPNEON=1)
endif()
# Per-polygon edge-gradient raster (builds on LITEV_SOFT3D_FAST). A/B alternative to
# EDGENEON (mutually exclusive at build time). The ~46% per-scanline edge-interp cost is
# the perspective ARITHMETIC (Interpolate mul/shift, dependency-stalled on the in-order
# A55). This variant replaces the per-scanline edge Interpolate for W + colour/texcoord
# (index 0..5) with an incremental sub-affine DDA along Y: anchor the true perspective
# value every SA_SUB_Y rows (default 8, tunable) via the edge interpolator, then pure ADD
# per row between anchors (mirrors the shipped SA_STEP_Z span code). APPROXIMATE (FPS-first,
# no strict gate) -- small edge/texture warp is expected on tall near polys. Z is NOT DDA'd:
# it stays on the exact InterpolateZ path (it drives the depth test). Reuses the same
# per-edge endpoint snapshot as EDGENEON. All changes behind #ifdef LITEV_SOFT3D_GRADIENT
# (reads under #if FAST && (EDGENEON||GRADIENT)); flag-OFF is byte-identical.
option(LITEV_SOFT3D_GRADIENT "liteV: per-polygon edge-gradient raster, stage 2 = sub-affine DDA (approx)" OFF)
if (LITEV_SOFT3D_GRADIENT)
    add_compile_definitions(LITEV_SOFT3D_GRADIENT=1)
endif()
# EDGENEON (builds on LITEV_SOFT3D_FAST; A/B alternative to GRADIENT, mutually exclusive at
# build time). Stage-1 measured that the ~46% per-scanline edge-interp cost is the
# perspective ARITHMETIC (Interpolate mul/shift, dependency-stalled on the in-order A55),
# NOT the endpoint memory. This variant NEON-vectorizes the EXACT per-scanline EDGE (Y)
# interpolation that INTERPNEON left scalar: the 6 independent attrs per edge (W + colour/
# texcoord) share the edge yfactor/shift, so they batch 4 lanes/iter with a select for the
# y0<y1 vs y0>=y1 branch. BYTE-IDENTICAL to the scalar path (same integer ops, SIMD-packed;
# Z stays on the exact scalar InterpolateZ). Reuses the same per-edge endpoint snapshot as
# GRADIENT. All changes behind #ifdef LITEV_SOFT3D_EDGENEON (reads under #if FAST &&
# (EDGENEON||GRADIENT)); flag-OFF is byte-identical.
option(LITEV_SOFT3D_EDGENEON "liteV: NEON-vectorize the EXACT per-scanline edge interpolation" OFF)
if (LITEV_SOFT3D_EDGENEON)
    add_compile_definitions(LITEV_SOFT3D_EDGENEON=1)
endif()
# Branchless per-pixel depth test (builds on LITEV_SOFT3D_HANDNEON + the NEON batch).
# The #1 branch-miss in RenderPolygonScanline is the data-dependent per-pixel depth test
# `if(!DTEST(...)) continue;` (~10.6% of RPS branch-miss), which mispredicts on the in-order
# A55. In the NEON batch (shade deferred to shade4), replace it with survivor COMPACTION:
# compute a branchless pass mask (DepthTestMask, bit-identical to DepthTestInline), buffer
# the slot unconditionally, then `nb += pass` -- a failed pixel's slot is overwritten by the
# next pixel and never shaded, so there is NO extra shade. The exact 2-level under-retry
# stays behind the edge-only (predictable) `dstattr & 0xF` test. EXACT (same survivor set /
# same shade4/plot2 writes / same depth-write semantics -- just no data-dependent branch).
# All changes behind #ifdef LITEV_SOFT3D_DTEST_BRANCHLESS (batch reads under #if HANDNEON &&
# DTEST_BRANCHLESS); flag-OFF is byte-identical.
option(LITEV_SOFT3D_DTEST_BRANCHLESS "liteV: branchless NEON-batch per-pixel depth test (survivor compaction)" OFF)
if (LITEV_SOFT3D_DTEST_BRANCHLESS)
    add_compile_definitions(LITEV_SOFT3D_DTEST_BRANCHLESS=1)
endif()
# COMPACTVTX (builds on LITEV_SOFT3D_FAST/BANDED): DraStic-style compact, contiguous,
# direct-indexed vertex layout for the software raster. Our `Vertex` is a full 64B cache
# line and `Polygon.Vertices[10]` is a POINTER array, so the raster's per-scanline
# `polygon->Vertices[i]->FinalPosition/FinalColor/TexCoords` is a double-indirection into
# scattered fat structs -- a non-prefetchable dependent pointer-chase that stalls the
# in-order A55 (measured: ~45% of band-thread L1D-load-miss). This lever builds a raster-
# private compact vertex buffer ONCE per frame (in RenderPolygons, when it is off the emu
# critical path), holding only the raster-needed fields {X,Y,W,Z,R,G,B,S,T} packed to 32B
# (2/line), and retargets every raster vertex read to it. Bands then read a contiguous,
# hardware-prefetchable, L2-warm buffer instead of chasing pointers -- and the scattered
# gather is paid 1x (build) instead of Nx (per band). Distinct from the null "endpoint-
# hoist", which kept the scattered layout and only re-cached L1-resident values. Output
# byte-identical (same values, compact layout); all changes behind #ifdef
# LITEV_SOFT3D_COMPACTVTX; flag-OFF is byte-identical. Assumes the scalar exact edge path
# (shipping config: EDGENEON/GRADIENT OFF), though the SnapshotEdge path is retargeted too.
option(LITEV_SOFT3D_COMPACTVTX "liteV: DraStic-style compact contiguous direct-indexed raster vertex buffer" ON)
if (LITEV_SOFT3D_COMPACTVTX)
    add_compile_definitions(LITEV_SOFT3D_COMPACTVTX=1)
endif()
# EDGEHOIST (builds on LITEV_SOFT3D_COMPACTVTX): hoist the per-EDGE endpoint attributes
# into the HOT per-poly RendererPolygon state. Post-compact, RenderPolygonScanline's #1
# remaining L1D stall is the per-scanline re-read of the edge endpoints (crossing-check Y,
# filledge X, W/Z, colour/texcoord) from the compact arena -- a SEPARATE region reached by
# a cold `rp->CompactV` deref, touched once per scanline (LRU-cold, evicted by the span
# loop). The endpoints only change at a vertex CROSSING (rare), so snapshot them ONCE at
# edge-setup into rp->EHL/EHR and read from there every scanline. Unlike the pre-compact
# EDGENEON EdgeEndpoints (which was byte-exact but NULL because it LEFT the scattered
# `Vertices[i]->FinalPosition` crossing/filledge chase in place), this hoists POSITION too,
# so the per-scanline hot path touches ONLY the contiguous rp (Slopes + Edge snapshot,
# spatially prefetched by the mandatory Slope fetch) and makes ZERO compact-arena derefs.
# Byte-identical (verbatim value copies at setup); all changes behind #ifdef
# LITEV_SOFT3D_EDGEHOIST; flag-OFF is byte-identical. Targets the scalar shipping edge
# path (EDGENEON/GRADIENT OFF, which have their own endpoint cache).
option(LITEV_SOFT3D_EDGEHOIST "liteV: hoist per-edge endpoints into hot RendererPolygon state (on top of compact)" OFF)
if (LITEV_SOFT3D_EDGEHOIST)
    add_compile_definitions(LITEV_SOFT3D_EDGEHOIST=1)
endif()
# AAFOLD (builds on LITEV_SOFT3D_FAST/NEON): fold the AA-edge (part 1 / part 3) span pixels
# into the same batched shade path the interior (part 2) already uses. Post-compact the
# raster L1D is CAPACITY-bound: only working-set SHRINK converts, never relocation. The AA-
# edge pixels currently take a SEPARATE, duplicated (part1==part3), branchy, scalar per-pixel
# shadeFast inline (measured ~7.9% of band L1I, ~10% of band branch-miss -- the largest
# stall axis). This lever SHRINKS two axes relocation can't touch: for the dominant pure-
# modulate case, the wide-edge pixels buffer their depth survivors and go through the SHARED
# branchless NEON shade4 (already resident from the interior) + a SHARED plotEdge, instead of
# the edge's own scalar shadeFast + duplicated plot. BYTE-IDENTICAL: shade4==shadeFast for
# this case, and the exact per-pixel xcov coverage / push-down / translucent logic is
# replayed in x-order (columns are framebuffer-independent within a scanline, so deferring
# the plot changes nothing). Thin edges (<4 px) keep the unchanged scalar path (no
# regression). All changes behind #ifdef LITEV_SOFT3D_AAFOLD; flag-OFF is byte-identical.
option(LITEV_SOFT3D_AAFOLD "liteV: fold the AA-edge span pixels into the batched shade4 fast path" OFF)
if (LITEV_SOFT3D_AAFOLD)
    add_compile_definitions(LITEV_SOFT3D_AAFOLD=1)
endif()
# TEX1B (builds on LITEV_SOFT3D_FAST): 1-byte palette-texel decode-once cache (DraStic's
# trade). The decode-once texture arena stores a 2-byte RGBA5551 per texel today; on a
# 100%-palette scene (Shrek = 100% fmt3 16-color) that arena is ~403KB/band and = 79% of
# the 512KB L2 the 3 band threads SHARE (aggregate overflows L2) -- the dominant L1/L2
# capacity-buster, bigger than the vertex data compact shrank. This lever stores a 1-BYTE
# value per texel for PALETTE formats (fmt 1/2/3/4/6) -- HALVING the arena working set --
# plus a small per-texture DECODED palette (u16 RGBA5551) in the arena. Per pixel:
# packed = palette[arena1b[texAddr]], reproducing the CURRENT 2B value BIT-FOR-BIT (the
# palette stores exactly what DecodeTexel+pack produces, incl. the current 0/31 alpha
# rounding for A3I5/A5I3 -- NOT an accuracy change). Direct (fmt7) + 4x4 (fmt5) stay 2B
# exactly as now. BYTE-IDENTICAL, FBHASH-gateable; attacks L2 not just L1. All changes
# behind #ifdef LITEV_SOFT3D_TEX1B; flag-OFF is byte-identical. Independent of AAFOLD.
option(LITEV_SOFT3D_TEX1B "liteV: 1-byte palette-index decode-once texture cache (halves the arena working set)" OFF)
if (LITEV_SOFT3D_TEX1B)
    add_compile_definitions(LITEV_SOFT3D_TEX1B=1)
endif()
# UNDERCOLO (builds on LITEV_SOFT3D_FAST): co-locate the AA/translucent under-slot next to
# its top-slot to kill the +BufferSize (+195KB) scatter jump. Measured: Shrek has AA ON and
# ~29% of interior pixels touch the under-slot -- a data-dependent +195KB write per touch,
# the load-bearing framebuffer scatter (stubbing the final pass moved L2-refill only 6%, so
# the under-slot IS the target). This is a PURE ADDRESS REMAP (layout-only, bit-exact): the
# planes switch to a per-ROW interleave -- each buffer row occupies a pow2 RowStride (1024)
# words with the top sub-row at [0,258) and the under sub-row at [512,770), so a pixel's
# under-slot is 512 words (2KB, SAME 4KB page) away instead of +195KB. Top-slot stays
# stride-1 within a row (coherence preserved). The pow2 layout makes the top/under test a
# cheap `& 512` (no modulo). NO stored value changes -- FBHASH-gateable. All changes behind
# #ifdef LITEV_SOFT3D_UNDERCOLO; flag-OFF is byte-identical. (Assumes BANDTILE/PIPELINE2 OFF,
# the shipping config.) Independent of the other soft3d flags.
option(LITEV_SOFT3D_UNDERCOLO "liteV: co-locate the AA under-slot next to its top-slot (kill the +195KB scatter)" OFF)
if (LITEV_SOFT3D_UNDERCOLO)
    add_compile_definitions(LITEV_SOFT3D_UNDERCOLO=1)
endif()
# DRASTIC: alternate DraStic-style TILE-BASED software 3D renderer (ground-up rewrite), dropped
# in behind a flag so the current renderer is untouched and we A/B at every step. Goal: cut the
# render's CPU-CORE cost so it stops starving the emulator on the 4-core A55 (the 48-vs-60 root
# cause). P1 is the integration SKELETON: a TileRenderer3D : Renderer3D that plugs into the same
# interface, runs SYNCHRONOUS, and fills a distinctive test pattern into its own framebuffer to
# prove the seam (entry points, GetLine format/timing, 2D compositing) with no hang/crash --
# rasterization lands in later phases. Selected at GPU_Soft.cpp construction; the new files
# compile to nothing when OFF, so the shipping build is byte-identical. Build with OVERLAP/
# PIPELINE2 OFF (the composite's async cross-class casts assume the reference renderer). All
# changes behind #ifdef LITEV_SOFT3D_DRASTIC; flag-OFF is byte-identical.
option(LITEV_SOFT3D_DRASTIC "liteV: alternate DraStic-style tile-based software 3D renderer (WIP; P1 skeleton)" OFF)
if (LITEV_SOFT3D_DRASTIC)
    add_compile_definitions(LITEV_SOFT3D_DRASTIC=1)
endif()
# BANDTILE (builds on LITEV_SOFT3D_BANDED): DraStic's cache-resident raster tiles. We
# rasterize into full-frame Color/Depth/Attr buffers (~1.2MB incl. the x2 AA under-slot) ->
# every per-pixel depth read-modify-write + attr access misses the 512KB shared L2 to DRAM.
# Each band instead rasters into a small CACHE-RESIDENT sub-tile (BtChunkRows=16 lines, both
# AA slots ~= 95KB, L2/L1-resident, matching DraStic's ctx+0x20000 16-line tiles), then
# copies the tile out to the real framebuffer. ScanlineFinalPass (Phase 2) is UNCHANGED and
# runs on the full framebuffer AFTER copy-out, so its y-1/y+1 neighbour reads never cross a
# tile edge. Retarget is by SHADOWING ColorBuffer/DepthBuffer/AttrBuffer/BufferSize/
# FirstPixelOffset in the raster fns (no per-site edits). OUTPUT byte-identical (same raster
# math + writes, staged through a tile + copied out); gate via strict fbhash. All changes
# behind #ifdef LITEV_SOFT3D_BANDTILE; flag-OFF is byte-identical.
option(LITEV_SOFT3D_BANDTILE "liteV: DraStic cache-resident per-band raster tiles" OFF)
if (LITEV_SOFT3D_BANDTILE)
    add_compile_definitions(LITEV_SOFT3D_BANDTILE=1)
endif()
# PIPELINE2 (steps 1+2 here; step 3 = the depth-2 flip, added later): deepen the async
# render pipeline from depth-1 to depth-2 to remove the ~4.5ms/frame emu barrier bubble at
# VBlank -> FlushAsyncRender -> Semaphore_Wait(AsyncDone). STEP 1 double-buffers the 3D
# planes (ColorBuffer/DepthBuffer/AttrBuffer) by frame parity: raster writes bank[P2RenderBank],
# GetLine/ScanlineFinalPass read bank[P2ConsumeBank], via a shadow-pointer base offset keyed
# on a parity toggled at the 3D kick and latched at the render-wake / VBlank. STEP 2 double-
# buffers the 2D emu->render snapshots by frame parity. Both are still DEPTH-1 and therefore
# OUTPUT byte-identical (the barrier still sequences 2D-N before 3D-N+1, so only one bank/slot
# is ever live -- the parity just alternates); they add the banks step 3 will exploit.
# fbhash-gateable. NOTE: mutually exclusive with LITEV_SOFT3D_BANDTILE (both shadow the raster
# framebuffer base). All behind #ifdef LITEV_SOFT3D_PIPELINE2; flag-OFF is byte-identical.
option(LITEV_SOFT3D_PIPELINE2 "liteV: depth-2 async pipeline scaffolding (steps 1+2: parity-banked 3D planes + 2D snapshots)" OFF)
if (LITEV_SOFT3D_PIPELINE2)
    add_compile_definitions(LITEV_SOFT3D_PIPELINE2=1)
endif()
# Hand-written AArch64 assembly transcription of DraStic's span-fill inner
# texture-modulate loop (builds on LITEV_SOFT3D_NEON): replaces the compiler's
# intrinsic modulate in shade4 with src/GPU3D_Soft_asm.S, transcribed
# instruction-for-instruction from libdrastic_arm64.so's 0x5ff60-0x5ffbc 4px/iter
# body (exact register roles + NEON scheduling; only the vertex factors are loaded
# per-pixel instead of dup'd, since melonDS is Gouraud). Fewer host instructions
# per pixel-batch => less dynamic-instruction energy/frame => higher SUSTAINED fps
# under the RG DS thermal throttle. Bit-identical to the intrinsic modulate; all
# changes behind #ifdef LITEV_SOFT3D_ASM; flag-OFF is byte-identical. AArch64 only.
option(LITEV_SOFT3D_ASM "liteV: hand-written AArch64 asm DraStic span-fill modulate" OFF)
if (LITEV_SOFT3D_ASM)
    add_compile_definitions(LITEV_SOFT3D_ASM=1)
endif()
# 4-wide NEON port of the per-pixel 2D BG/OBJ colour-special-effects compositor
# (SoftRenderer2D::ColorComposite, called 256×/scanline from DrawScanline_BGOBJ).
# The scalar version is a ~134-instruction out-of-line CALL PER PIXEL (branchy
# blend/window/priority logic that clang does NOT auto-vectorize). This lever
# replaces it with a branchless NEON kernel (arm masks/selects) doing 4 px/iter,
# mirroring DraStic's NEON-vectorized 2D composite (teardown FUN_001494a4, 927
# NEON ops). Bit-exact with the scalar path (top/bottom/audio hashes identical);
# all changes behind #ifdef LITEV_SOFT2D_NEON; flag-OFF is byte-identical.
# AArch64 only (scalar fallback elsewhere).
option(LITEV_SOFT2D_NEON "liteV: 4-wide NEON 2D BG/OBJ colour-effect compositor" OFF)
if (LITEV_SOFT2D_NEON)
    add_compile_definitions(LITEV_SOFT2D_NEON=1)
endif()
# NEON branchless reject-scan for SoftRenderer2D::InterleaveSprites (the sprite
# priority resolver, called once per BG priority level = up to 4×/scanline). At a
# given priority most of the 256 pixels carry no matching opaque sprite, so the
# scalar loop is dominated by the (OBJLine[i]&OpaPrioMask)!=attrmask reject path.
# This lever tests the prio match 16 lanes at a time and skips whole 16-pixel
# chunks with zero matches; matching chunks fall through to the identical scalar
# body (palette gather has no AArch64 NEON equivalent). Bit-exact: every drawn
# pixel + DrawPixel RMW is byte-identical (final_top/bot hashes ON==OFF).
# AArch64 only; flag-OFF is byte-identical. Default OFF.
option(LITEV_SOFT2D_OBJNEON "liteV: NEON reject-scan for the 2D sprite interleaver" OFF)
if (LITEV_SOFT2D_OBJNEON)
    add_compile_definitions(LITEV_SOFT2D_OBJNEON=1)
endif()
# 4-wide NEON port of the per-pixel 3D-layer 2D compositor (SoftRenderer2D::
# DrawBG_3D, inlined into DrawScanlineBGMode<0> and called once per scanline in a
# 3D game where BG0 is the 3D layer). The scalar loop is 1 px/iter with two
# per-pixel branches (opaque-alpha test + window test) and a shift-down RMW into
# BGOBJLine. This lever replaces it with a branchless NEON mask/select kernel
# doing 4 px/iter. Profiled as the top scalar non-vectorized 2D loop once
# LITEV_SOFT2D_NEON (ColorComposite) + LITEV_SOFT2D_OBJNEON (sprite reject-scan)
# are ON. Bit-exact (final_top/bot/audio hashes ON==OFF); all changes behind
# #ifdef LITEV_SOFT2D_BG3DNEON; flag-OFF is byte-identical. AArch64 only.
option(LITEV_SOFT2D_BG3DNEON "liteV: 4-wide NEON 2D 3D-layer compositor (DrawBG_3D)" OFF)
if (LITEV_SOFT2D_BG3DNEON)
    add_compile_definitions(LITEV_SOFT2D_BG3DNEON=1)
endif()
# Unit 2: shadow-mode equivalence assert for the CyclesBudget slot. Debug-only gate;
# implies nothing else. Aborts if the budget and timestamp/target mechanisms disagree.
option(LITEV_SHADOW_ASSERT "Assert CyclesBudget == timestamp/target equivalence" OFF)
if (LITEV_SHADOW_ASSERT)
    add_compile_definitions(LITEV_SHADOW_ASSERT=1)
endif()

# Unit 3: AArch64-only emitted JIT dispatcher. When ON, JIT block exits jump to a
# generated dispatcher stub that does the fast block lookup inline and branches
# straight into the next block, staying in JIT context for the whole slice; it
# only returns to C++ (ARM_Ret) on budget-expiry, StopExecution, region miss, or
# lookup miss. Default OFF -> the build is bit-identical to the shadow-budget
# tree. The x64 JIT ALWAYS keeps the unmodified upstream C++ dispatch loop; this
# option therefore requires an AArch64 target and errors at configure time on x86.
option(LITEV_JIT_DISPATCH "AArch64 emitted JIT block dispatcher (chained block exits)" OFF)
if (LITEV_JIT_DISPATCH)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_DISPATCH=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_DISPATCH requires an AArch64 target "
                            "(the x64 JIT keeps the upstream C++ dispatch loop).")
    endif()
endif()

# Stage 2 (liteDS-v2 core-speed / DraStic parity): native-NZCV condition guard.
# EXACTNESS-PRESERVING. For guest ARM instructions with a COMPOUND condition
# (cond >= 8: HI/LS/GE/LT/GT/LE), the upstream CheckCondition emits a 5-instruction
# flag-table lookup out of RCPSR. Since this backend keeps guest CPSR in a GP reg
# (not host NZCV), we can instead transiently `MSR NZCV, X<cpsr>` and use a native
# `B.<inverted-cond>` skip (2 instructions). Same architectural branch decision,
# fewer host ops / smaller blocks. Single-flag conditions (cond < 8, already a
# 1-instruction TBZ/TBNZ) are left untouched. No register-cache / cycle-model
# interaction, so ON must stay bit-exact on the exact-timing goldens. AArch64 only.
option(LITEV_JIT_CONDFOLD "Stage 2: native-NZCV guard for compound conditions (exactness-preserving, AArch64)" OFF)
if (LITEV_JIT_CONDFOLD)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_CONDFOLD=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_CONDFOLD requires an AArch64 target.")
    endif()
endif()

# Stage 1 of the fixed-register / CPSR-in-NZCV JIT rearchitecture (DraStic teardown
# 01 §6.1). An UNCONDITIONAL flag-setting ADD/SUB/RSB/CMP/CMN leaves its guest NZCV
# resident in the host PSTATE instead of extracting it into the emulated RCPSR flag
# word; the word is reconstructed lazily (bit-exact CSET/BFI) only at consumers /
# host-NZCV clobbers / block boundaries, and CheckCondition then evaluates the guest
# condition natively on the resident host flags. Pure codegen change: cycle counts,
# timing and the register cache are untouched. Default OFF; flag-off is byte-identical.
option(LITEV_JIT_FIXEDREG "Stage 1: keep guest CPSR flags in host NZCV on the ALU hot path (AArch64)" OFF)
if (LITEV_JIT_FIXEDREG)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_FIXEDREG=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_FIXEDREG requires an AArch64 target.")
    endif()
endif()

# GLOBALREG (DraStic teardown 01 §6.1 — global cross-block fixed register
# allocation). Builds ON TOP of LITEV_JIT_FIXEDREG + LITEV_JIT_DISPATCH. Pins a
# SUBSET of the never-banked guest GP registers (r0..r7) to fixed callee-saved
# host registers (W19..) GLOBALLY: they are loaded ONCE at ARM_Dispatch (JIT
# slice entry), spilled ONCE at ARM_Ret (slice exit), and kept LIVE across block
# boundaries / the emitted dispatcher / linked chains (callee-saved => preserved
# for free across the dispatcher and any C helper). The per-block RegisterCache
# no longer reload-on-first-use or spill-at-block-end the pinned regs, removing
# the block-boundary register traffic DraStic eliminates. Pinning only r0..r7
# (never mode-banked) keeps mode/exception switches transparent; the sole
# file-coherence point is the interpreter fallback, which is bracketed with an
# explicit spill-before / reload-after. Codegen-only: cycles/timing/register-file
# semantics unchanged, so flag-ON must stay bit-exact on the exact-timing +
# torture goldens. Default OFF -> emission-identical to the FIXEDREG tree.
# AArch64 only (the x64 JIT keeps the upstream dynamic per-block register cache).
option(LITEV_JIT_GLOBALREG "GLOBALREG: globally pin never-banked guest regs to fixed host regs across block boundaries (AArch64)" OFF)
if (LITEV_JIT_GLOBALREG)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_GLOBALREG=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_GLOBALREG requires an AArch64 target.")
    endif()
endif()

# Unit 4: direct block linking (patch, don't dispatch). Builds ON TOP of the
# Unit 3 dispatcher: an eligible block exit whose next PC is a compile-time
# constant emits a per-hop-commit guard followed by a patchable `B` (initially to
# the dispatcher). When the target block exists the site is patched to branch
# straight into it, bypassing the dispatcher's inline lookup. Three independent
# kill switches gate the three eligible exit CLASSES (unconditional static
# branch / conditional-branch edge / block-end fall-through) so a regression can
# be bisected to a class. All default ON, but each only takes effect when
# LITEV_JIT_DISPATCH is ON (linking has no meaning without the dispatcher). With
# all three OFF the build is bit-identical to the pure Unit-3 dispatcher.
option(LITEV_LINK_UNCOND "Unit 4: link unconditional static branch exits" ON)
option(LITEV_LINK_COND "Unit 4: link conditional-branch edge exits" ON)
option(LITEV_LINK_FALLTHROUGH "Unit 4: link block-end fall-through exits" ON)
if (LITEV_JIT_DISPATCH AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(_litev_any_link OFF)
    if (LITEV_LINK_UNCOND)
        add_compile_definitions(LITEV_LINK_UNCOND=1)
        set(_litev_any_link ON)
    endif()
    if (LITEV_LINK_COND)
        add_compile_definitions(LITEV_LINK_COND=1)
        set(_litev_any_link ON)
    endif()
    if (LITEV_LINK_FALLTHROUGH)
        add_compile_definitions(LITEV_LINK_FALLTHROUGH=1)
        set(_litev_any_link ON)
    endif()
    # Umbrella: the link registry / JitBlock link fields / patch machinery are
    # compiled in whenever at least one exit class links. All three OFF -> the
    # bookkeeping vanishes and the tree is the pure Unit-3 dispatcher.
    if (_litev_any_link)
        add_compile_definitions(LITEV_JIT_LINK=1)
    endif()
endif()

# liteDS-v2 commit-stub: hoist the per-hop commit (StopExecution / Timestamp+=Cycles
# / budget / CPSR store) that EmitLinkExit inlined at EVERY linkable exit into ONE
# shared per-CPU stub each link exit BLs into. Shrinks the block-tail L1I footprint
# (attacks the measured fetch stall). Only meaningful with the emitted dispatcher +
# at least one link class (it accelerates linkable exits); AArch64-only. BIT-EXACT:
# identical ops/order, and the unlinked patch-B re-enters the dispatcher whose
# re-commit is a no-op (the stub already set RCycles=0). Default OFF -> flag-off is
# byte-identical (all code behind #ifdef LITEV_JIT_COMMIT_STUB).
option(LITEV_JIT_COMMIT_STUB "Commit-stub: shared per-CPU per-hop commit for linkable exits" OFF)
if (LITEV_JIT_COMMIT_STUB)
    if (LITEV_JIT_DISPATCH
        AND (LITEV_LINK_UNCOND OR LITEV_LINK_COND OR LITEV_LINK_FALLTHROUGH)
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_COMMIT_STUB=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_COMMIT_STUB requires LITEV_JIT_DISPATCH, at "
                            "least one LITEV_LINK_* class, and an AArch64 target.")
    endif()
endif()

# liteDS-v2 cycle down-counter (DraStic register-budget model). Defers the per-hop
# `NDS.ARMxTimestamp += Cycles` (a MOVP2R over a multi-MB NDS address + a 64-bit
# cache-cold RMW on EVERY block boundary -- the dominant IPC-0.63 stall) into a
# callee-saved slice accumulator (Ran = the freed W25; guest r6's GLOBALREG pin is
# dropped to make room), reconciling `Timestamp += Ran` ONCE per slice in C++. The
# cheap 32-bit budget RMW and the StopExecution residual split are UNCHANGED, so this
# is BIT-EXACT vs the per-hop model (identical event-firing schedule -> MP-safe).
# AArch64-only; needs the emitted dispatcher. Default OFF -> flag-off is byte-identical
# (GLOBALREG table, Linkage.S, reg defs all restored to the shipped 7-pin allocation).
option(LITEV_JIT_CYCLE_DOWNCOUNTER "Cycle down-counter: defer per-hop Timestamp commit into a slice register (drops the r6 pin)" OFF)
if (LITEV_JIT_CYCLE_DOWNCOUNTER)
    if (LITEV_JIT_DISPATCH AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_CYCLE_DOWNCOUNTER=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_CYCLE_DOWNCOUNTER requires LITEV_JIT_DISPATCH and an AArch64 target.")
    endif()
endif()

# Return-target cache: a per-CPU single-entry predictor in the emitted dispatcher for repeated
# indirect-branch targets (guest returns/BX/computed jumps -- the branch class direct-linking
# cannot cover). On a match it skips the region + tag lookup and branches straight to the cached
# block. Pure predictor (cleared on any invalidation) -> BIT-EXACT timing (MP-safe). Default OFF ->
# flag-off is byte-identical (all code behind #ifdef LITEV_JIT_RETCACHE).
option(LITEV_JIT_RETCACHE "Dispatcher return-target cache: skip the block lookup for repeated indirect-branch targets" OFF)
if (LITEV_JIT_RETCACHE)
    if (LITEV_JIT_DISPATCH AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_RETCACHE=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_RETCACHE requires LITEV_JIT_DISPATCH and an AArch64 target.")
    endif()
endif()

# Unit 5 (M2.1): event-true scheduler slices. When ON, each scheduler iteration
# runs the CPUs to the TRUE next deadline (next scheduled event / frame end /
# next timer overflow) instead of the fixed kMaxIterationCycles cap, so the CPUs
# stay in generated code for whole event intervals. Independent of, but designed
# to combine with, LITEV_JIT_DISPATCH. Default OFF -> NextTarget() keeps the fixed
# cap and the build is bit-identical to the pre-Unit-5 tree.
#
# SEMANTIC NOTE: longer slices coarsen the ARM9/ARM7 interleave, an OBSERVABLE
# timing change (cross-CPU shared-memory/IPC visibility shifts). The flag-ON build
# is therefore NOT bit-exact vs the flag-OFF golden by design; it carries its own
# recorded baseline (tools/headless/baselines/shrek-600-eventslices.trace).
option(LITEV_EVENT_SLICES "Unit 5: event-true scheduler slices (remove fixed iteration cap)" OFF)
if (LITEV_EVENT_SLICES)
    add_compile_definitions(LITEV_EVENT_SLICES=1)
endif()

# Unit 6 (M2.3): ARM7 IPC/SPI idle detection. Counts consecutive empty polls on
# the IPC receive-FIFO-empty (0x04000184) and SPI-idle (0x040001C0) ARM7 read
# paths; past a small threshold sets ARM7.IdleLoop so the existing branch-to-self
# fast-forward advances ARM7Timestamp to the next scheduled event. Lives ONLY on
# the IPC/SPI read paths — no W_* / WiFi register involvement. Default OFF.
#
# SEMANTIC NOTE: flag-ON skips idle intervals, an OBSERVABLE timing change, so it
# is NOT bit-exact vs any flag-OFF golden by design. The flag-OFF build compiles
# the detector to nothing and stays bit-identical to the pre-Unit-6 tree.
option(LITEV_ARM7_IDLE "Unit 6: ARM7 IPC/SPI idle detection (WiFi paths untouched)" OFF)
if (LITEV_ARM7_IDLE)
    add_compile_definitions(LITEV_ARM7_IDLE=1)
endif()

# Milestone 3 (M3): tiered memory fast paths. AArch64-only (the A64 JIT emits the
# guarded raw accesses); the x64 JIT keeps the unmodified upstream helper paths.
# These claim ZERO semantic change: every fast path adds EXACTLY the cycles the
# slow helper would (cycles are accounted at compile time by Comp_AddCycles_*,
# independent of the runtime memory path), reads/writes the SAME bytes, and falls
# back to the existing helper on any runtime-guard miss. Default OFF -> the build
# is emission-identical to the pre-M3 tree (every fast path is #if'd out).
#
#   LITEV_MEM_FAST         umbrella; ON enables the two sub-flags below by default.
#   LITEV_MEM_DTCM_BLOCK   Tier A: DTCM-hit inline path for LDM/STM block xfers
#                          (plan order 1; the single biggest memory-helper cost).
#   LITEV_MEM_MAINRAM_LOAD Tier A: MainRAM-hit inline path for single u32 loads
#                          (plan order 4; SlowRead9<u32>'s dominant case). Stores
#                          stay on the helper (no MainRAM raw-store thunk: v1
#                          rejection, preserves JIT invalidation semantics).
# Each sub-flag is independently toggleable for per-shape bisection.
option(LITEV_MEM_FAST "M3: tiered memory fast paths (DTCM block + MainRAM load)" OFF)
option(LITEV_MEM_DTCM_BLOCK "M3 Tier A: DTCM inline path for LDM/STM block transfers" ${LITEV_MEM_FAST})
option(LITEV_MEM_MAINRAM_LOAD "M3 Tier A: MainRAM inline path for single u32 loads" ${LITEV_MEM_FAST})
if (LITEV_MEM_DTCM_BLOCK OR LITEV_MEM_MAINRAM_LOAD)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        if (LITEV_MEM_DTCM_BLOCK)
            add_compile_definitions(LITEV_MEM_DTCM_BLOCK=1)
        endif()
        if (LITEV_MEM_MAINRAM_LOAD)
            add_compile_definitions(LITEV_MEM_MAINRAM_LOAD=1)
        endif()
    else()
        message(FATAL_ERROR "LITEV_MEM_FAST requires an AArch64 target "
                            "(the x64 JIT keeps the upstream helper paths).")
    endif()
endif()

# M6.14 Tier A (plan Appendix D.7): MainRAM-hit inline path for LDM/STM BLOCK
# LOADS. In-race the ARM9 issues ~8.2k SlowBlockTransfer9 (LDM) calls/frame that
# re-enter SlowRead9 once per word -> ~23k MainRAM word-reads/frame (86% of all
# slow reads). This composes the two landed Tier A wins: a block (LDM) whose
# whole address range lies inside MainRAM takes a guarded direct-pointer copy
# instead of per-word SlowRead9, exactly as MEM_DTCM_BLOCK does for DTCM. LOADS
# ONLY (stores stay on the helper -> JIT invalidation untouched). ARM9 only
# (SlowBlockTransfer9; ARM7/WiFi untouched). Same exactness class as MEM_DTCM_BLOCK:
# cycles are baked at compile time by Comp_AddCycles_CDI independent of the memory
# path, the copy touches the SAME bytes SlowBlockTransfer9 would, and any runtime
# guard miss (DTCM overlay, region exit, MainRAM mirror wrap) falls back to the
# exact helper. Default OFF -> emission-identical to the pre-M6.14 tree.
option(LITEV_MEM_MAINRAM_BLOCK "M6.14 Tier A: MainRAM inline path for LDM/STM block LOADS (ARM9)" OFF)
if (LITEV_MEM_MAINRAM_BLOCK)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_MEM_MAINRAM_BLOCK=1)
    else()
        message(FATAL_ERROR "LITEV_MEM_MAINRAM_BLOCK requires an AArch64 target "
                            "(the x64 JIT keeps the upstream helper paths).")
    endif()
endif()

# DraStic LDM/STM block-transfer match (docs/drastic-teardown/01-cpu-recompiler.md
# §7.1): DraStic translates every LDM/STM to a single BL into a hand-written
# straight-line stub, one per register count (arm64_load_block1..16 /
# arm64_store_block1..16), that moves the guest words with ldp/stp PAIRS (two
# words per instruction) instead of an emitted per-word loop. melonDS on this
# build already pairs the stack marshalling, but (a) its inline DTCM/MainRAM
# block tiers copy the contiguous memory side with SCALAR ldr/str per word, and
# (b) the MainRAM-hit inline block LOAD tier (the dominant ~86%-of-slow-reads
# case) is left disabled, so those LDMs fall through to the per-word
# SlowBlockTransfer9 helper. This flag ports the DraStic win: it enables the
# MainRAM inline block-LOAD tier AND pairs the contiguous memory side of the
# DTCM+MainRAM inline copies with ldp/stp. Same exactness class as
# LITEV_MEM_MAINRAM_BLOCK/DTCM_BLOCK: cycles baked at compile time by
# Comp_AddCycles_*, the paired copy moves the SAME bytes in the same order, and
# any runtime guard miss falls back to the exact helper. Default OFF ->
# emission-identical to the pre-LDMSTM tree.
option(LITEV_JIT_LDMSTM "DraStic-style ldp/stp-paired inline LDM/STM block transfers + MainRAM block-load tier (AArch64)" OFF)
if (LITEV_JIT_LDMSTM)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_LDMSTM=1)
    else()
        message(FATAL_ERROR "LITEV_JIT_LDMSTM requires an AArch64 target "
                            "(the x64 JIT keeps the upstream helper paths).")
    endif()
endif()

# DraStic JIT emit-quality (teardown 01, FUN_001912f0): merge the host NZCV flags into
# guest CPSR with one MRS + masked AND/ORR (3 host instrs) instead of a per-flag CSET+BFI
# pair (8 instrs) for full-flag arithmetic S-ops (CMP/SUBS/ADDS...). Guest CPSR N/Z/C/V
# == host NZCV MRS layout -> bit-exact. AArch64-only; the x64 path keeps CSET/BFI.
option(LITEV_JIT_FLAGMERGE "DraStic: one-shot MRS-NZCV flag merge vs per-flag CSET+BFI (AArch64)" OFF)
if (LITEV_JIT_FLAGMERGE)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_JIT_FLAGMERGE=1)
    endif()
endif()

# DraStic memory model (docs/drastic-teardown/02-memory.md §2): a BRANCHLESS
# software page table replacing melonDS's fault-based (SIGSEGV) fastmem on the
# single-access load hot path. Each JIT guest load indexes a flat per-CPU table
# of packed host-pointer deltas by (addr >> 11) (2 KB pages, 2M entries, 16 MB),
# tests the entry, and on a non-zero delta reads the backing store directly with
# one predicated branch; a zero entry diverts to a resolver that lazily installs
# the delta (for fastmem-compatible flat RAM: MainRAM/DTCM/SharedWRAM/ARM7WRAM)
# and returns the exact SlowRead value. LOADS-only in this first tranche (stores
# keep the exact SlowWrite helper, so JIT/SMC invalidation is completely
# untouched). The table pointer lives in the ARM context (loaded per-access from
# RCPU), NOT in a permanently-reserved host reg, so it does not consume the
# MemBase reg that fault-based fastmem pins -- STEP 2 (LITEV_JIT_GLOBALREG) frees
# x26 and widens the global pin. Default OFF -> emission-identical to upstream.
option(LITEV_MEM_SWTABLE "DraStic-style branchless software page-table fastmem (load hot path, AArch64)" OFF)
if (LITEV_MEM_SWTABLE)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        add_compile_definitions(LITEV_MEM_SWTABLE=1)
    else()
        message(FATAL_ERROR "LITEV_MEM_SWTABLE requires an AArch64 target "
                            "(the software page table is emitted by ARMJIT_A64).")
    endif()
endif()

# STORE-side branchless sw-table (extends the load table to JIT stores + inline SMC
# check). Built + bit-exact + torture-gated, but MEASURED a NET ARM9 REGRESSION on the
# in-order A55 (+5.92% marginal; gives back ~60% of the loads-only win) -- the inline
# per-store table-pointer chase + SMC bitmap check costs more than melonDS's predicted
# SlowWrite call, and stores never had the heavy slow path that made LOADS win. So this
# is CLOSED-NEGATIVE and split behind its OWN flag, default OFF: LITEV_MEM_SWTABLE alone
# = loads-only (the -9.67% win), stores keep the exact SlowWrite. Kept (not deleted) for
# the record / future re-aim. Requires LITEV_MEM_SWTABLE. See plan D.7 addendum 27.
option(LITEV_MEM_SWTABLE_STORE "STORE-side branchless sw-table (closed-negative on A55; default OFF)" OFF)
if (LITEV_MEM_SWTABLE_STORE)
    if (NOT LITEV_MEM_SWTABLE)
        message(FATAL_ERROR "LITEV_MEM_SWTABLE_STORE requires LITEV_MEM_SWTABLE.")
    endif()
    add_compile_definitions(LITEV_MEM_SWTABLE_STORE=1)
endif()

# DraStic-recompiler density match: emit the sw-table load probe as a SHARED BL stub
# (one per size/sign) instead of inlining the ~7-instruction probe at every guest load
# site. Smaller JIT blocks -> less i-cache pressure on the in-order Cortex-A55. Stores
# route through the existing shared PatchedStoreFuncs thunk. Bit-exact vs the inline
# probe (same probe body + same SlowRead*SW / SlowWrite resolvers). Requires
# LITEV_MEM_SWTABLE. Default OFF. See plan D.7 (final density divergence).
option(LITEV_SWTABLE_STUB "Shared BL load-probe stub instead of inline sw-table probe (AArch64)" OFF)
if (LITEV_SWTABLE_STUB)
    if (NOT LITEV_MEM_SWTABLE)
        message(FATAL_ERROR "LITEV_SWTABLE_STUB requires LITEV_MEM_SWTABLE.")
    endif()
    add_compile_definitions(LITEV_SWTABLE_STUB=1)
endif()

# Milestone 4 (renderer track, software-renderer scope). All default OFF.
option(LITEV_NEON_RENDERER "Enable NEON SIMD 2D output paths (AArch64 only)" OFF)
option(LITEV_SPU_FAST_INTERP "Replace SPU channel interpolation with cheap linear interp" OFF)
option(LITEV_AGGRESSIVE_SKIP "Enable per-frame 2D/3D rasterization skip gate" OFF)

# M6.11 step 3: flag-gated integer-NEON of the GPU3D geometry engine's hot
# per-vertex / per-command fixed-point math (SubmitVertex position + texcoord
# transforms, MatrixMult4x4). Default OFF -> byte-identical scalar behaviour.
# The option is host-agnostic: the source guards every NEON block with
# `#if defined(__ARM_NEON)` and falls back to the scalar reference elsewhere, so
# all configs build on both aarch64 (NEON active) and x86_64 (scalar). Set via
# add_compile_definitions so the LITEV_* re-export loop below carries it PUBLIC.
option(LITEV_NEON_GEOMETRY "M6.11: integer-NEON GPU3D geometry math (AArch64 active, scalar fallback elsewhere)" OFF)
if (LITEV_NEON_GEOMETRY)
    add_compile_definitions(LITEV_NEON_GEOMETRY=1)
endif()

# M6.12 (plan §8, "Milestone 5 ARM9 relaxed timing"): DraStic-style flat
# cycles-per-instruction for ARM9 ONLY. Replaces the per-access MemTimings
# region-table walk (code-fetch wait states, data-access sequential/non-seq
# distinctions, incl. every GXFIFO MMIO write's accounting) with a fixed flat
# cost. The flat cost source (ARMv5::CodeRead32 / DataRead*/DataWrite* in CP15.cpp)
# propagates to BOTH the interpreter runtime AND the JIT's compile-time-baked cycle
# constants (the decode loop reads cpu->CodeCycles/DataCycles), so JIT-emitted
# blocks skip the timing math and more cycle contributions fold into ConstantCycles.
# ARM7 timing stays EXACT permanently (WiFi/MP invariant) -- every relaxed path is
# gated on Num==0. Default OFF -> byte-identical to the exact-timing tree. This is a
# DELIBERATE semantic timing change: exact-timing goldens will NOT verify under it;
# it carries its own golden (shrek-600-relaxed9.trace). Host-agnostic (pure C++;
# no arch guard) so all configs build on aarch64 and x86_64.
option(LITEV_RELAXED_ARM9_TIMING "M6.12/plan §8: flat cycles-per-instruction for ARM9 (ARM7 stays exact)" OFF)
if (LITEV_RELAXED_ARM9_TIMING)
    add_compile_definitions(LITEV_RELAXED_ARM9_TIMING=1)
endif()

# Instant ARM9 hardware divider/sqrt: compute the DIV/SQRT result at register-
# write time and skip the scheduled completion event. In-race profiling showed
# Event_Div is the single largest scheduler load (~1233 dispatches/frame, 42% of
# all events) plus Sqrt (~80/frame); eliminating them cuts scheduler/event-
# dispatch overhead and coarsens slicing. The busy bit is never observed set
# (result ready same-cycle) — a deliberate ARM9 timing relaxation (FPS-first).
option(LITEV_INSTANT_DIVSQRT "Instant ARM9 divider/sqrt: compute on write, skip Event_Div/Event_Sqrt" OFF)
if (LITEV_INSTANT_DIVSQRT)
    add_compile_definitions(LITEV_INSTANT_DIVSQRT=1)
endif()

# DraStic batched audio ring (teardown doc 11): SPU::Mix generates N samples per
# scheduled Event_SPU instead of one, cutting the SPU scheduler-event flood
# (547/frame) by ~N x. Sample generation is bit-identical per iteration; only
# scheduler granularity + SPU-IRQ/capture latency coarsen (FPS-first). N is the
# batch factor (default 8 -> 547/frame -> ~68/frame). Default OFF.
option(LITEV_SPU_BATCH "DraStic #11: batch N audio samples per Event_SPU (coarsen SPU event flood)" OFF)
set(LITEV_SPU_BATCH_N "8" CACHE STRING "SPU batch factor (samples generated per scheduled Event_SPU)")
if (LITEV_SPU_BATCH)
    add_compile_definitions(LITEV_SPU_BATCH=1 LITEV_SPU_BATCH_N=${LITEV_SPU_BATCH_N})
endif()

# DraStic-style inert-tick RTC coarsening: the 32768Hz RTC oscillator schedules
# ~548 events/frame just to bump a counter. When no periodic INT1 frequency /
# INT2 alarm is armed (the common gameplay case), the periodic ProcessIRQ(1) is a
# no-op, so ClockTimer hops straight to the next 1024-tick boundary in one event
# instead of ~1024 per-tick events -- removing essentially the whole RTC flood.
# Reverts to the exact per-tick path the moment a game arms an RTC IRQ. FPS-first,
# default OFF. (Known: HANGS at --frameskip 99; real gameplay runs frameskip 0.)
option(LITEV_COARSE_RTC "DraStic: skip inert 32768Hz RTC ticks when no periodic RTC IRQ is armed" OFF)
if (LITEV_COARSE_RTC)
    add_compile_definitions(LITEV_COARSE_RTC=1)
endif()

# DraStic-style DMA streamlining for the GXFIFO geometry-DMA path (the dominant
# in-race DMA: MainRAM display-list -> 0x04000400, fixed dst, incrementing src).
# The stock loop routes every word through NDS::ARM9Read32 (region decode) and
# NDS::ARM9Write32 -> ARM9IOWrite32 -> GPU3D::Write32 (I/O address dispatch) before
# reaching GPU3D::WriteToGXFIFO. Since IsGXFIFODMA already proves src==MainRAM /
# dst==0x04000400 / DstAddrInc==0, this fast path reads the source word directly
# from MainRAM (exactly what ARM9Read32's 0x02 case does) and calls WriteToGXFIFO
# directly (exactly what the 0x400 dispatch resolves to, incl. the GeometryEnabled
# guard). Per-word DMA timing (UnitTimings9_32) and stall handling are unchanged,
# so this is BIT-EXACT dispatch elision, not an accuracy tradeoff. Profiled on the
# Shrek race: DMA::Run9 is ~10.5% of the emu thread and ~60% of that is this
# GXFIFO chain. Default OFF.
option(LITEV_DMA_GXFIFO_FAST "DraStic: direct MainRAM->WriteToGXFIFO fast path for geometry DMA (dispatch elision)" OFF)
if (LITEV_DMA_GXFIFO_FAST)
    add_compile_definitions(LITEV_DMA_GXFIFO_FAST=1)
endif()

# DraStic thread-affinity (teardown 07): pin the software render threads (async 2D, 3D
# render, band workers) to cores {1,2}, off the emu's core (3, pinned by the app glue)
# and the UI/Mali core (0). Without this the lib-created render threads land on core 3
# and preempt the critical emu thread (~13ms/frame descheduling: app runFrame 33.5ms vs
# 20.4ms headless for identical code). Android-only (Renderer thread cores).
option(LITEV_PIN_RENDER "Pin software render threads to cores {1,2}, off the emu core" OFF)
if (LITEV_PIN_RENDER)
    add_compile_definitions(LITEV_PIN_RENDER=1)
endif()

# DraStic-style aggressive idle-loop recognition (teardown doc 01 item 6). Stock
# melonDS's IsIdleLoop (ARMJIT.cpp) rejects any loop with a cross-iteration
# register recurrence (a register read before it is rewritten in the iteration --
# e.g. a spin/timeout counter or a poll that also bumps a tick). DraStic
# fast-forwards such loops anyway: the recurrent register is a dead
# timeout/accumulator and the observable wait outcome is driven by the memory/IO
# the loop polls, not by that register's exact value. With this flag IsIdleLoop
# accepts the loop as long as it does NO store and NO coprocessor access (both
# still hard-rejected), so it only reads mem/IO, computes in regs, and branches
# back. Fast-forwards the CPU to the next scheduled event on each such taken
# back-branch (same mechanism as stock idle loops). Accuracy tradeoff (FPS-first)
# -- a genuine register-recurrent compute loop that also polls would advance one
# iteration per event; verify no desync. Default OFF.
option(LITEV_IDLE_AGGRESSIVE "DraStic: recognize register-recurrent poll loops as idle (fast-forward)" OFF)
if (LITEV_IDLE_AGGRESSIVE)
    add_compile_definitions(LITEV_IDLE_AGGRESSIVE=1)
endif()

# DraStic-style "baked" timer deadline (attacks the run_system bucket). Under
# LITEV_EVENT_SLICES, NextTarget() recomputes NextTimerDeadline() on every one of
# the ~1957 scheduler iterations/frame -- an 8-timer scan whose result is provably
# INVARIANT between timer overflows/reloads and control writes (RunTimer advances
# Counter by cycles<<shift while TimerTimestamp advances by cycles, leaving the
# absolute soonest-overflow SysTimestamp unchanged). This caches the deadline and
# recomputes only when a timer actually overflows/reloads (HandleTimerOverflow) or
# its control/prescaler/start state is written (TimerStart); savestate-load and
# reset invalidate it. EXACT (no accuracy tradeoff) -- pure recompute elision.
# Default OFF.
option(LITEV_TIMER_FAST "DraStic: cache NextTimerDeadline, recompute only on timer overflow/control write" OFF)
if (LITEV_TIMER_FAST)
    add_compile_definitions(LITEV_TIMER_FAST=1)
endif()

# DraStic-style leaner scheduler dispatch (bit-exact). Caches the soonest armed
# event deadline so NextTarget skips the per-iteration SchedListMask min-scan and
# RunSystem early-outs (skips scan+dispatch) when nothing is due -- ~1461 of the
# ~2058 RunSystem calls/frame on the Shrek race fire no event. Same events, same
# order, cheaper dispatch => flag ON must be byte-identical to OFF.
option(LITEV_SCHED_FAST "DraStic: cached next-event deadline + RunSystem early-out (bit-exact)" OFF)
if (LITEV_SCHED_FAST)
    add_compile_definitions(LITEV_SCHED_FAST=1)
endif()

# R4 (Appendix D.7; docs/r4-render-thread-design.md): render-thread offload seam.
# Compiles in the capture/submit phase split so the Android app glue can later
# move GL renderer submission off the emulation thread (RunFrame produces a
# frame packet on the emu thread; a separable submit phase replays the GL). This
# flag ONLY affects the accelerated (OpenGL) renderer path. Default OFF ->
# byte-identical to the synchronous path (renderer submission inline in
# RunFrame). The headless harness uses the software renderer and issues no GL, so
# the flag is INERT there and the golden traces are unaffected whether it is
# compiled in or not (verified: shrek-600 bit-exact with -DLITEV_RENDER_THREAD=ON).
# Set via add_compile_definitions so the LITEV_* re-export loop below carries it
# PUBLIC: the seam appends fields/virtuals to renderer types that the out-of-tree
# Android frontend constructs, so ALL consumers must agree on one ABI (see the
# re-export rationale below).
option(LITEV_RENDER_THREAD "R4: render-thread offload capture/submit seam (GL submission separable from RunFrame; OpenGL path only)" OFF)
if (LITEV_RENDER_THREAD)
    add_compile_definitions(LITEV_RENDER_THREAD=1)
endif()

# G) Geometry-transform off-core offload (docs/liteDS-v2-renderprep-offload-scope.md).
# Moves the GPU3D vertex transform (SubmitVertex) off the emu-thread critical path: the emu
# thread keeps the exact cycle/timing model + GXSTAT + test commands (MP-safe by construction)
# and RECORDS the GX command stream; a helper (render) thread replays it and does the transform.
# STEP G1 (current): flag + passive command-stream recording only, no behaviour change.
# Default OFF; flag-OFF is byte-identical (all changes behind #ifdef LITEV_GEOM_OFFLOAD).
option(LITEV_GEOM_OFFLOAD "G: offload GPU3D geometry transform off the emu thread (record/replay; WIP)" OFF)
if (LITEV_GEOM_OFFLOAD)
    add_compile_definitions(LITEV_GEOM_OFFLOAD=1)
endif()

# LITEV_GEOM_CLIP_FAST: DraStic-style trivial-accept fast clip in GPU3D::ClipPolygon.
# Profiling shows most on-screen polygons are fully inside all 6 frustum planes, yet
# melonDS runs the full Sutherland-Hodgman clip (3 planes x 2 passes, temp copies,
# per-plane attribute interpolation) on every one. This flag adds a cheap min/max
# bounding test on the clip-space coords: if every vertex is inside all 6 planes the
# clip is a pure copy, so we skip Sutherland-Hodgman entirely and just apply the
# idempotent vertex-colour rounding the full clip would have left behind. BIT-EXACT
# for accepted polys (pure early-out); partially/fully-outside polys still run the
# exact full clip. IN-PLACE (no threading/offload). Applied to `core` in
# src/CMakeLists.txt. Default OFF; flag-OFF is byte-identical.
option(LITEV_GEOM_CLIP_FAST "DraStic trivial-accept fast clip: skip Sutherland-Hodgman for fully-inside polygons (bit-exact early-out)" OFF)

# --- DraStic backlog #3: batched GXFIFO threaded-code interpreter ------------------------
# Hoists GPU3D::Run()'s per-command drain loop INTO GPU3D::ExecuteCommand() as a threaded
# internal loop: each command re-dispatches through the computed-goto jump table (shared with
# LITEV_GXFIFO_BATCH) and falls through to the next command with NO bl/ret + NO function
# prologue/epilogue per command (DraStic docs/drastic-teardown/04-gpu3d-geometry.md §3).
# CmdFIFORead() (and its CheckFIFODMA/CheckFIFOIRQ side effects) still runs once per command
# in the identical order, so geometry OUTPUT and audio are byte-identical to flag-OFF.
# Applied on `core` in src/CMakeLists.txt (mirrors LITEV_GXFIFO_BATCH). Default OFF.
option(LITEV_GXFIFO_THREADED "DraStic #3: batched GXFIFO threaded-code interpreter (no call/ret per GX command)" OFF)

# LITEV_GEOM_RECIP — DraStic-style float reciprocal for the per-vertex geometry divides in
# SubmitPolygon (viewport X/Y transform + Z depth). Replaces the integer sdiv (non-pipelined
# on Cortex-A55) with one frecpe + Newton-Raphson reciprocal reused across the shared-denominator
# X/Y divides. APPROXIMATE (~16 mantissa bits), FPS-first: the quotient feeds screen coords / depth
# where that precision is visually fine. GPU3D.cpp only. Default OFF.
option(LITEV_GEOM_RECIP "liteV: DraStic-style float reciprocal for SubmitPolygon per-vertex divides (approximate)" OFF)
if (LITEV_GEOM_RECIP)
    add_compile_definitions(LITEV_GEOM_RECIP=1)
endif()

# DraStic-style HLE BIOS SWIs (attacks the arm9_exec bucket). melonDS runs the LLE
# BIOS, so every guest SWI dispatches into real BIOS code through the JIT via the SVC
# exception vector. This flag intercepts the hottest, side-effect-free SWIs at the
# A_SVC/T_SVC dispatch site and executes them natively in C++ (compute result, set
# r0..r3, NOP-return so the pipeline/dispatcher resumes at the instruction after the
# SWI) instead of running BIOS code: Div (0x09), CpuSet (0x0B), CpuFastSet (0x0C),
# Sqrt (0x0D). Results are bit-exact vs FreeBIOS (verified on host over millions of
# inputs; the abs-remainder Div quirk and no-round CpuFastSet are matched). Everything
# else (SoftReset/IntrWait/VBlankIntrWait/Halt/decompress/IPC/...) falls through to the
# real BIOS. FPS-first (fewer cycles than the BIOS handler); ARM9+ARM7. Default OFF.
option(LITEV_HLE_BIOS_SWI "DraStic: HLE the hottest side-effect-free BIOS SWIs natively (Div/Sqrt/CpuSet/CpuFastSet)" OFF)
if (LITEV_HLE_BIOS_SWI)
    add_compile_definitions(LITEV_HLE_BIOS_SWI=1)
endif()

add_subdirectory(src)

# --- liteDS-v2 core-fix: export LITEV_* configuration as a PUBLIC usage requirement ---
# The LITEV_* feature macros above are set with add_compile_definitions(), which is
# DIRECTORY-scoped: it configures core's own sources (and core's in-tree subdirectory
# consumers: the Qt/SDL frontend and the headless harness) but is NOT part of `core`'s
# exported usage requirements. Several of these macros change the LAYOUT of core public
# types that downstream code allocates and constructs:
#   * LITEV_JIT_DISPATCH -> Compiler::DispatcherEntry[]  (Compiler is embedded in ARMJIT)
#   * LITEV_JIT_LINK     -> ARMJIT::PendingLinks9/7 + Compiler link fields
#   * LITEV_ARM7_IDLE    -> NDS::ARM7Idle
# and `melonDS::ARMJIT JIT` is the FIRST member of NDS, so any of these shifts every
# later NDS member and grows sizeof(NDS).
#
# An OUT-OF-TREE dependent that links `core` and includes its public headers (e.g. the
# Android JNI frontend target `melonDS-android-frontend`, whose MelonInstance.cpp does
# `new NDS(...)`) lives in a DIFFERENT directory scope and therefore does NOT inherit
# these directory definitions. It would then size `operator new(sizeof(NDS))` for a
# SMALLER layout than NDS::NDS (compiled in core with the flags) constructs, so a late
# member write (a FIFO past the JIT member) runs off the end of the undersized heap
# block -> SIGSEGV/SEGV_ACCERR into the allocator guard page during construction.
#
# Re-export every LITEV_* directory definition as a PUBLIC compile definition of `core`
# so ALL dependents (in-tree and out-of-tree) compile against one identical ABI. This
# adds nothing new to core's own sources (they already have the macros via the
# directory scope, so the in-tree/headless build stays byte-identical); it only makes
# the same macros flow across the `target_link_libraries(... core ...)` boundary.
get_directory_property(_litev_dir_defs COMPILE_DEFINITIONS)
foreach(_litev_def IN LISTS _litev_dir_defs)
    if(_litev_def MATCHES "^LITEV_")
        target_compile_definitions(core PUBLIC ${_litev_def})
    endif()
endforeach()

if (BUILD_QT_SDL)
    add_subdirectory(src/frontend/qt_sdl)
endif()

if (LITEV_HEADLESS)
    add_subdirectory(tools/headless)
endif()
