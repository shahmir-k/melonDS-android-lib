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
