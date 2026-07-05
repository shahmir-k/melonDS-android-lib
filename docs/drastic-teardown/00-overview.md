# DraStic Internal Architecture Teardown — Overview

Reverse-engineered from the **legally-owned** DraStic binaries (the user owns the app).
Target: `reference/universal/lib/arm64-v8a/libdrastic_arm64.so`, **version `r2.6.0.4a`**.
This is reference material to inform the melonDS performance fork's 60fps campaign. All
docs describe algorithms/structures in prose + pseudocode; **no DraStic source is
reproduced verbatim** — it is stripped machine code, reconstructed via decompilation.

Confidence labels used throughout: **[proven-from-binary]** (readable in the decompiled/
disassembled image), **[inferred]** (strong deduction; exact bytes runtime-generated or
not in image), **[unknown-too-stripped]** (not conclusively recoverable).

---

## 1. Binary facts

| Fact | Value | Evidence |
|------|-------|----------|
| Primary binary | `libdrastic_arm64.so`, 1.356 MB, ARM aarch64, ELF, **stripped** | `file`, `nm -D` |
| Version | `r2.6.0.4a` (string `Version: %s build %d` / `r2.6.0.4a`) | strings [proven] |
| Functions recovered | **3285** functions decompiled (3265 fully, 20 partial) | Ghidra count [proven] |
| Exported symbols | 341 dynamic; only **72 JNI** exports keep names, rest stripped | `nm -D` [proven] |
| Companion `libdrastic_cpu.so` | 10 KB — **NOT the JIT**; a CPU-model detector (reads `/proc/cpuinfo`, `getauxval`), exports only `getCpuType` | doc 01 [proven] |
| Graphics | **GLES2 only, no EGL, no glDrawElements/VBO** → GL only blits+post-processes a software framebuffer | imports [proven] |
| Audio | **OpenSL ES** buffer-queue (`slCreateEngine`, `SL_IID_*`) | imports [proven] |
| Scripting | **Lua 5.3.0** embedded (controller/overlay overrides) | strings [proven] |
| Compression | zlib (savestates) | imports [proven] |
| Memory backing | file-backed mmap (`drastic_mapped_memory.dat`, `..._vram.dat`) + ashmem | strings, doc 02 [proven] |
| 32-bit build | `armeabi-v7a/libdrastic.so` — equally stripped, no extra symbols | `nm -D` [proven] |

### Tooling used
- **Ghidra 11.3.1** (official release zip; openjdk@21) headless `analyzeHeadless` +
  a Jython postScript (`decomp_export.py`) exporting every function's decompiled C to
  `scratchpad/decomp/all_decomp.c` (250k lines) + `index.txt` (addr, name, size, callers).
- **radare2 6.1.8**, plus binutils `objdump`/`nm`/`otool`/`strings` for raw NEON disasm
  where the decompiler produced intrinsics rather than opcodes.
- **capstone** (ARM mode) for the ARM7/ARM9 BIOS blobs.
- Raw corpus + helper (`getfn.sh`) live in the scratchpad; paths are recorded in each doc.

---

## 2. Subsystem map (JNI boundary → native subsystems)

The Java↔native contract is class `com.dsemu.drastic.DraSticJNI` (72 native methods).
Entry points and where they land:

```
Java (DraSticEmuActivity / DraSticGlView GLSurfaceView renderer thread)
  │
  ├─ onInit ──────────► FUN_0011b41c (state alloc) + FUN_0011cbec (double-buf FB alloc)
  ├─ startGame ───────► FUN_0011be00: mprotect RWX 59MB, setjmp, → recompiler_entry (BLOCKS:
  │                      the emu runs on the calling Java thread — no spawned "emu thread")
  ├─ updateFrame ─────► packs input regs, returns frame-status word (paused/progress/settle)
  ├─ updateInput ─────► DS buttons + touch bitfield
  ├─ applyConfig(J) ──► a packed 64-bit config BITFIELD (not a struct ptr) — doc 11
  ├─ signalScreen ────► emu→GL condvar: a frame is ready
  ├─ waitScreen ──────► GL thread blocks until a frame is ready
  ├─ renderFrame* ────► FUN_0011ceac: glTexSubImage2D + glDrawArrays + .dfx post-chain
  ├─ saveState/loadState ─► .dss (zlib), async writer thread — doc 10
  ├─ *Cheat* ─────────► usrcheat.dat AR interpreter — doc 10
  └─ getPerformanceCounters / getFrameInfo ─► profiling hooks — doc 11
```

| Doc | Subsystem | One-line verdict |
|-----|-----------|------------------|
| [01](01-cpu-recompiler.md) | **CPU recompiler** (crown jewel) | ARM9+ARM7 JIT; **cycle cost BAKED at compile time**; **fixed-static register allocation**; liveness + condition-folding; **compile-time idle-loop detection** + HALT/WFI skip; return-to-dispatcher |
| [02](02-memory.md) | **Memory** | **Branchless 2 KB-granular software pointer-table fastmem** + mmap-aliased RAM mirrors; per-region MMIO fn-ptr table |
| [03](03-gpu2d.md) | **GPU 2D** | Software scanline compositor, **NEON** blend kernels; two engines split across 2 threads |
| [04](04-gpu3d-geometry.md) | **GPU 3D geometry** | **Deferred/batched** GXFIFO; branchless threaded-code interpreter; **NEON 4×4 matmul** |
| [05](05-gpu3d-raster-gl.md) | **GPU 3D raster + GL** | Software rasterizer on **4 dedicated band-threads**; decode-once texture cache; double-buffered FB + deferred dirty upload |
| [06](06-spu-audio.md) | **SPU audio** | 16-ch mixer on emu thread; **drop-on-full**, NOT audio-slaved; 44.1 kHz nearest-neighbour resample |
| [07](07-threading-scheduler.md) | **Threading + scheduler** | Emu on caller thread; 2D/3D helper threads; render fully off critical path; event-delta scheduler |
| [08](08-hle-bios.md) | **HLE/BIOS** | **LLE execution of a clean-room custom BIOS**; firmware synthesized; direct-boot |
| [09](09-dma-timers-io.md) | **DMA/timers/RTC/IO** | IO = switch + shadow array; volatile regs computed on-demand; DMA fast-path block copies |
| [10](10-save-cheat-input.md) | **Save/cheat/input** | .dss v15 zlib savestate; R4 usrcheat AR interpreter; packed input bitfields |
| [11](11-config-frameskip.md) | **Config/frameskip** | 64-bit config bitfield; audio-buffer-driven throttle; fixed frameskip omits GPU compose |

---

## 3. Threading + timing architecture

### Thread topology [proven — doc 07]
```
                 ┌───────────────────────────────────────────────┐
   Java UI ──────┤ EMU THREAD  (the thread that called startGame) │
   thread        │  loop: recompiler_entry                         │
                 │   ├─ run ARM9 block(s) to next event boundary   │
                 │   ├─ arm9_to_arm7  → run ARM7 to same timestamp  │  ARM9:ARM7 = 66:33 MHz (2:1)
                 │   ├─ arm7_to_event_update → scheduler:           │
                 │   │      GPU scanline/HBlank/VBlank, SPU mix,     │
                 │   │      DMA, timers; recompute next event       │
                 │   ├─ 2D engine A (top screen) rendered here      │
                 │   ├─ 3D geometry (GXFIFO drain) here             │
                 │   └─ at VBlank: fill FB half, flip index, signal │
                 └───────┬──────────────┬──────────────┬───────────┘
                         │ condvar      │ condvar       │ condvar flip (1 mutex)
          ┌──────────────▼───┐  ┌───────▼────────┐  ┌──▼──────────────────────┐
          │ 2D ENGINE-B thr  │  │ 3D RASTER x4    │  │ GL THREAD (GLSurfaceView)│
          │ FUN_0013cca0     │  │ band-threads    │  │ waitScreen → texSubImage │
          │ bottom screen    │  │ FUN_0015f53c    │  │  + drawArrays + .dfx FX  │
          └──────────────────┘  │ 12×16-line RR   │  │  vsync swap (2nd 60Hz cap)│
                                └─────────────────┘  └──────────────────────────┘
      OpenSL ES callback thread (owned by Android): drains audio ring, feeds silence on underrun.
      Generic ≤32-thread fork/join pool (FUN_001c196c): util only (Huffman/hash) — NOT the hot path.
```

### Timing model [proven — docs 01, 07]
- **Event-delta scheduler** (`FUN_0012d6f8`): sorted delta-list of `{countdown, callback}`;
  time-slice length = cycles until the next event. Not a fixed slice.
- **Cycle cost is BAKED at compile time** into each block: per-instruction base cost + LDM/STM
  popcount table + memory-region waitstate tables, summed at translate time and flushed as a
  single subtract against the block's cycle down-counter (`cpu+0x2290`, signed, runs toward 0).
  **This confirms the melonDS fork's finding** — relaxing per-instruction DS timing would
  regress DraStic the same way. Do not move DS timing to runtime.
- **Frame pacing = audio back-pressure + GLSurfaceView vsync.** No sleep-to-60 loop drives
  time; the emu is gated by (a) the audio ring high-water mark and (b) the vsync'd GL swap.
  Audio is a **lossy sink** (drop frame on full, silence on underrun), never blocking the emu.

---

## 4. Top techniques — "why DraStic is fast on weak ARM" (ranked, with evidence)

1. **Per-block liveness analysis → dead-flag/dead-register elimination.** Backward pass
   computes live-regs + live-flags bitmasks per block; the emitter skips NZCV updates and
   register write-backs that are provably dead. Highest-ROI idea to port. [proven — doc 01 §6]
2. **Compile-time baked cycle cost** (popcount + waitstate tables), one subtract per block —
   near-zero runtime timing overhead. [proven inputs — doc 01 §5]
3. **Branchless 2 KB software pointer-table fastmem** with mmap-aliased RAM mirrors: a JIT load
   is `index = addr>>11; test sign bit; host = addr + (entry<<2); ldr` — no SIGSEGV handler,
   no per-access branch beyond one sign test. [proven — doc 02]
4. **Render fully off the emu critical path**: double-buffered software framebuffer, emu's only
   present cost is one buffer-index flip + condvar signal; GL thread does upload + post-FX.
   [proven — doc 07, 05]
5. **Multithreaded software rendering** with *dedicated* helper threads (2D engine-B on its own
   thread; 3D rasterizer split into 12×16-line bands across 4 threads) — not a generic pool.
   [proven — doc 03, 05, 07]
6. **NEON everywhere it pays**: 4×4 geometry matmul (`smull/smlal2 + shrn #12`), 2D
   composite/blend (`ld4` + format convert 4 px/iter), raster shade/fog/AA kernels — while
   leaving scalar the code that doesn't vectorize (edge-walk, OAM iterate). [proven — docs 03/04/05]
7. **Deferred, batched GXFIFO** processed by a branchless threaded-code jump-table interpreter
   over de-interleaved command/param streams — no per-command call/ret. [proven — doc 04]
8. **Two-tier idle skip**: (a) **compile-time idle-loop detection** — the compiler recognises a
   block that branches back to its own start while only recirculating its own state (a software
   poll) and bakes `counter = -1` so it yields to the scheduler on entry instead of spinning;
   (b) runtime **HALT/WFI** deactivates the CPU from the scheduler until an IRQ. Guest idle ≈
   zero host work. **melonDS lacks (a) — high-value port.** [proven — doc 01 §9]
9. **Software SMC bitmap** (2-byte granular code-presence map) instead of mprotect faults on
   write-heavy guest RAM; fine-grained per-page invalidation, not full flush. [proven — doc 01 §8]
10. **Decode-once texture cache** (hash-chained, texture decoded to linear RGBA on miss so the
    per-texel sampler is format-agnostic) + **specialised LDM/STM stubs** (`arm64_(load|store)_
    block1..16`, one per register count, no emitted loops). [proven — docs 05, 01 §7]
11. **Fixed-static register allocation**: guest r0–r14 permanently pinned to host x13–x27,
    x28 = context, w12 = cycle counter, and guest CPSR flags mapped 1:1 onto host NZCV — so
    most guest ALU ops translate ~one-to-one with no emulated flag word and no per-block reg
    load/spill. Dispatch is **return-to-dispatcher** (only intra-block branches are patched;
    block→block goes through a generated 2 KB-page table + C resolver). [proven — doc 01 §6]

---

## 5. Most relevant to the melonDS 60fps campaign

- **CPU timing (doc 01):** DraStic bakes timing at compile time — *this validates staying the
  course*; relaxing per-instruction timing is the wrong lever. The right levers are liveness-
  driven dead-code elimination and condition folding.
- **Idle-skip (doc 01):** two tiers — **compile-time software idle-loop detection** (bakes an
  immediate scheduler-yield into recognised poll loops) *plus* runtime HALT/WFI scheduler
  deactivation. melonDS has the HALT tier but **lacks the compile-time detector — the single
  biggest CPU-side gap and a high-value port.**
- **Render offload (docs 07, 05):** the single biggest architectural win — decouple the GL
  upload/present onto its own thread behind a 2-buffer framebuffer with one condvar flip, and
  run the two 2D engines / 3D bands on helper threads with a start/done condvar handshake.
- **Geometry (doc 04):** drop-in the NEON column-major 4×4 matmul and the batched GXFIFO drain.
- **Pacing (docs 06, 07):** pace off the audio ring + vsync, never block the emu in the audio
  callback — removes lag-spike stalls.
- **Fastmem (doc 02):** the branchless 2 KB pointer table + mmap aliasing is a robust alternative
  to melonDS's fragile fault-based Android fastmem.
- **Profiler feature ideas (docs 01 §4.3, 11):** DraStic ships a per-block self-profiler (exec
  counts, translated size, live masks) and exposes `getPerformanceCounters` (per-frame ARM9 vs
  ARM7 busy time as Q12.4 ms). Good reference targets for the melonDS-profiler project.

---

## 6. Honest gaps (per-subsystem, not invented)

- **CPU:** the fixed guest→host register mapping (r0–r14 → x13–x27, x28=ctx, w12=cycles) and
  the batched `SUB w12,#imm` cost flush are proven at the opcode level; what remains inferred is
  only whether every block emits exactly one such subtract vs one per conditional segment, and
  the interior of the generated `recompiler_cpu_next_action_*` state machine. [doc 01 §12]
- **Memory:** interiors of individual I/O register decoders beyond CP15/VRAM/WRAM were not all
  traced (the dispatch *table* is proven); GBA-slot mapping in DS mode assumed open-bus. [doc 02]
- **Raster:** raw SIMD opcodes for some shade kernels sit outside the objdump window (NEON
  proven via Ghidra intrinsics); exact depth-compare direction / blend formula inferred. [doc 05]
- **Cheat:** the AR opcode-handler bodies were not located in the corpus (dispatch is proven,
  opcode semantics are standard-AR inferred). [doc 10]
- **Scheduler:** the `recompiler_cpu_next_action_*` generated state-machine body is named by
  strings but not decompilable beyond the ARM9→ARM7→events order. [doc 01 §12]

---

## 7. Raw artifacts (for a deeper follow-up pass)

All under the session scratchpad
`/private/tmp/claude-501/-Users-shahmir-Documents-GitHub-melonDS-profiler/07cb9938-789e-44a0-b38b-08b31ed7003b/scratchpad/`:
`decomp/all_decomp.c` (full decompilation), `decomp/index.txt` (function index),
`strings_arm64.txt`, `api_xref.txt`, `sym_imports.txt`, `sym_defined.txt`, `ANCHORS.md`
(shared anchor map), `getfn.sh` (function extractor), and the Ghidra project under
`ghidra_proj/`. Note: the scratchpad is session-scoped and may be cleaned up; re-running the
Ghidra headless step reproduces the corpus.
