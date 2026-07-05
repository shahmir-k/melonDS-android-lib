# DraStic Teardown 07 — Threading Model & Event Scheduler

Reverse-engineered from `libdrastic_arm64.so` (arm64, stripped except JNI exports).
Evidence tags: **[proven]** = read directly from decompiled binary; **[inferred]** =
deduced from structure/naming/DS hardware knowledge; **[unknown]** = too stripped to
resolve. Addresses are file/vaddr offsets into the `.so`.

This is arguably the most important doc in the set: DraStic hits full-speed DS on weak
ARM SoCs primarily because it **(a) drives the whole emulator from one blocking recompiler
loop with a setjmp/longjmp fast-exit, (b) offloads ALL GL work to a separate thread behind
a double-buffered framebuffer + condvar handshake, (c) parallelizes the 2D and 3D software
renderers across dedicated helper threads, and (d) paces frames off the audio ring, not a
sleep timer.** melonDS can copy each of these independently.

---

## 0. TL;DR thread topology

```
                        ANDROID PROCESS (DraStic)
┌───────────────────────────────────────────────────────────────────────────┐
│                                                                             │
│  T0  Java UI thread ──JNI──> onInit / startGame / applyConfig / updateFrame │
│        (latches input into global regs 0x0024c48c.. ; never runs emu)       │
│                                                                             │
│  T1  Java GL thread (GLSurfaceView "GLThread")                              │
│        loop: waitScreen()  ── blocks on cond DAT_0402dbac ──┐               │
│              renderFrame() -> FUN_0011ceac:                 │               │
│                 glBindTexture / glTexSubImage2D / glDrawArrays(6 verts)     │
│        Pure blit + .dfx post-process. NO emulation here.    │               │
│                                                             │ signalScreen  │
│  T2  EMU CORE thread  (whoever called startGame JNI)        │ (from T2)     │
│        FUN_0011be00 boot -> setjmp(&DAT_03d7c800)           │               │
│        -> recompiler dispatch FUN_001327bc ("recompiler_entry")             │
│        per-frame: ARM9 slice -> ARM7 slice -> event_update -> GPU/SPU/DMA   │
│        frame end: FUN_0011cb14() flips fb buffer + cond_signal ────────────┘│
│        input latch  FUN_00116e74 ; frame events FUN_00180834                │
│        PACING: FUN_001807dc spins usleep(10) while audio ring >3/4 full     │
│           │            │                    │                               │
│           │ cond       │ cond               │ cond                          │
│           ▼            ▼                    ▼                               │
│  T3  2D-engine helper   T7 3D coordinator   (generic pool, lazy)            │
│      FUN_0013cca0        FUN_0015f2c8        T8..T39 fork-join pool          │
│      renders 192 lines   dispatches bands    FUN_001c1b14 (<=32 threads)    │
│      of one 2D engine    to T4/T5/T6         util/Lua/bg jobs, NOT hot path │
│      (FUN_0015004c)         │                                               │
│                            ├─> T4 FUN_0015f53c  raster band  (FUN_001596a4) │
│                            ├─> T5 FUN_0015f53c  raster band                 │
│                            └─> T6 FUN_0015f53c  raster band                 │
│                                                                             │
│  Ttmp  async savestate writer  FUN_0017a1bc  (spawned per Save, transient)  │
│                                                                             │
│  Taudio  OpenSL ES buffer-queue callback thread (owned by OpenSL, no        │
│          pthread_create in our code) — see doc 06 (audio agent)             │
└───────────────────────────────────────────────────────────────────────────┘
```

**No thread is named** — there is no `prctl(PR_SET_NAME)` or `pthread_setname_np` anywhere
in the binary **[proven — grep found zero hits]**. Topology below is reconstructed from
entry functions and the state they touch.

---

## 1. Every `pthread_create` site (exhaustive)

Five distinct spawn sites. **Note the ANCHORS.md guess that `FUN_0017a888` is an
audio/render thread is WRONG — it is the async savestate writer.**

| # | Spawn site | Entry fn | Count | Role | Evidence |
|---|-----------|----------|-------|------|----------|
| A | `FUN_0013d3c4` (core init) | `FUN_0013cca0` | 1 | 2D-engine parallel scanline renderer | [proven] |
| B | `FUN_0015f614` (core init, ×3 unrolled) | `FUN_0015f53c` | 3 | 3D software rasterizer band workers | [proven] |
| C | `FUN_0015f614` (tail) | `FUN_0015f2c8` | 1 | 3D geometry/raster **coordinator** | [proven] |
| D | `FUN_0017a888` (saveState) | `FUN_0017a1bc` | 1 (transient) | Async `.dss` savestate compress+write | [proven] |
| E | `FUN_001c196c` (lazy singleton via `FUN_001c1854`) | `FUN_001c1b14` | ≤32 | Generic fork-join worker pool | [proven] |

The **EMU CORE thread itself is NOT spawned here.** `startGame` (`FUN_0011be00`) installs a
`setjmp(&DAT_03d7c800)` and then *enters the recompiler dispatch and blocks*, so the
emulator runs on whatever Java thread invoked the `startGame` JNI (the app's dedicated
emulation thread). It only returns on quit. **[proven — setjmp + dispatch call at tail of
`FUN_0011be00`; no run/step JNI exists in the export table]**

The **audio thread** is created inside OpenSL ES (`slCreateEngine`, doc 06), not by DraStic
code — hence it is absent from the pthread_create list. **[inferred]**

### 1.A  T3 — 2D-engine helper (`FUN_0013cca0`)

Classic single-consumer condvar worker. Created by the core-object constructor
`FUN_0013d3c4`, which also builds two 2D render contexts via `FUN_0015016c(...,0,...)` and
`FUN_0015016c(...,1,...)` (engine A "main" and engine B "sub").

```
loop:
    lock(mtxA); while(!start_flag) cond_wait(startA, mtxA); start_flag=0; unlock(mtxA)
    FUN_0015004c(base+0x84298, 0, 0xbf, 0)   // render scanlines 0..191 of one 2D engine
    lock(mtxB); done_flag=1; cond_signal(doneB); unlock(mtxB)
```

`FUN_0015004c` walks scanlines `param2..param3` (`0..0xbf` = 0..191, a full DS screen) and
per line calls `FUN_0014c610` (BG/OBJ compositor). **So one 2D engine renders on T3 in
parallel while the emu thread produces the other.** **[proven]** This is a cheap 2× win on
the pure-2D games that dominate the DS library.

### 1.B / 1.C  T4–T7 — 3D software rasterizer pool

`FUN_0015f614` unrolls **three** identical worker setups (indices 1,2,3 stamped at
`+0x…12`), each running `FUN_0015f53c`, then spawns one coordinator `FUN_0015f2c8`. Each
worker owns its own mutex/mutex/cond/cond quad (start + done handshake), stride `0x24100`.

Worker body (`FUN_0015f53c`):
```
loop:
    wait start_flag (cond +0x24070)
    if core.renderer_mode(+0x4a0)==0:  FUN_001596a4(self)   // SOFTWARE raster (1.3 KB)
    else:                              FUN_0015e648(self)   // alt path (2.1 KB, hi-res?)
    set done_flag(+0x240d1); signal done (cond +0x240a0)
```

Coordinator body (`FUN_0015f2c8`): gated by a "3D active" flag `core+0x468`; when active it
loops calling `FUN_00159bb4` (960 B geometry/scanline dispatch) which farms scanline bands
out to the three workers by setting their start flags and broadcasting. The
`geometry_log_*.bin` / "Video Geometry" strings confirm this is the **3D geometry+raster
engine**. **[proven for structure; band-partition math is [inferred] — the workers are
launched with per-thread indices 1/2/3 and share a common line buffer at `core+0x356cb0`].**

**Speed trick:** DS 3D is rasterized in software (no GLES draw of DS geometry — confirmed in
ANCHORS: no `glDrawElements`/VBOs). DraStic splits the 192-line frame across 3 worker
threads + the coordinator, i.e. up to **4-way data-parallel scanline rasterization**, then
uploads the finished RGBA buffer as one texture. **[inferred from thread count + software-GL
fact]**

### 1.D  Ttmp — async savestate (`FUN_0017a888` → `FUN_0017a1bc`)

`saveState` snapshots emulator RAM into a freshly `malloc(0x680000)` buffer *synchronously*
(memcpy of the two 0x18000 VRAM banks + core state, header `"DraStic-SaveState-----…"`),
then hands the buffer to a spawned thread `FUN_0017a1bc` which zlib-compresses and writes
`_savestate_temp.dss`. A `DAT_0401e09c` spin-flag (`while(busy) usleep(1)`) serializes
concurrent saves. **Savestates never block the emu loop for I/O.** **[proven]**

### 1.E  T8+ — Generic fork-join worker pool (the "32-thread pool")

Created lazily on first use by `FUN_001c1854` (refcount `DAT_0402cd38`, allocates a 0x400-byte
control block then `FUN_001c196c(block, 0x20)`). `FUN_001c196c` clamps the requested count to
`[1,32]`, initializes **5 sync primitives** and spawns N **joinable** workers:

| offset | primitive | purpose |
|--------|-----------|---------|
| `+0xf5` | mutex | protects the job ring / semaphore |
| `+0xdf` | cond | "job available" — workers sleep here |
| `+0xeb` | mutex (`+0x3ac`) | protects the completion flag |
| `+0xc8` | cond (`+0x37c`) | "all jobs done" — submitter sleeps here |
| `+0xd4` | mutex (`+0x3d4`) | protects the outstanding-job counter `+0x108` |

Failure prints `"Thread pool initialization failed."` **[proven]**

**Data structure = 32-slot ring of `{fn ptr, arg}` (0x10 bytes each) at `+0x110`:**
- head `+0x310`, tail `+0x314` (masked `& 0x1f`), counting-semaphore `+0x31c`.
- `FUN_001c1e6c(pool, fn, arg)` = **submit**: write slot at head, advance head; if the ring
  is full it first flushes via the barrier below.
- `FUN_001c1bec(pool)` = **run-and-join barrier**: `pending = (head-tail)&0x1f`; store it as
  outstanding-count `+0x108`; set busy `+0x378=1`; add `pending` to semaphore and
  `cond_broadcast(+0x320)` to wake workers; then `cond_wait(+0x37c)` until busy clears.
- `FUN_001c1d80` = **worker dequeue**: `cond_wait` on the semaphore, pop tail slot.
- `FUN_001c1ce0` = **worker loop**: dequeue → `(*fn)(arg)` → atomically decrement `+0x108`;
  when it reaches 0, clear busy and `cond_signal(+0x37c)` to release the submitter.

This is a textbook **fork-join task pool**: enqueue K jobs, call the barrier, block until all
K finish. Its actual callers (`FUN_001b6aec`, `FUN_001c860c`) live in the `0x1b_000–0x1c_000`
C++/STL/Lua/util range, so this pool is used for **background/util work (Lua, asset/shader
loading, parallel file ops), NOT the per-frame emu critical path** — the hot 2D/3D
rendering uses the dedicated condvar threads in 1.A–1.C instead. **[inferred from caller
addresses + the fact it is a lazy singleton, not created at core boot]**

> **Actionable for melonDS:** this generic pool is a clean, copy-able primitive but is *not*
> where DraStic's 60fps comes from. The per-frame parallelism (2D helper + 3-way raster)
> is. Prioritize those.

---

## 2. Emu ↔ Render handshake (the render offload)

### Framebuffer: double-buffered, emu never touches GL

`FUN_0011cbec` (called from `onInit`) allocates one **3 MB** aligned region
(`posix_memalign(&DAT_0402db48, 0x10, 0x300000)`) and splits it into **two 1.5 MB halves**:
- `DAT_0402d1f8` = base (buffer 0), `DAT_0402d200` = base+0x180000 (buffer 1).
- Each half stores **two DS screens** at stride `0xc0000` (selected by `screen & 1`).
- `DAT_0402db50` = current **write/front index**; the render side always reads the *other*
  half `~DAT_0402db50 & 1`. **[proven]**

All of this is guarded by **one mutex `DAT_0402db84`** + **one cond `DAT_0402dbac`** — the
`signalScreen`/`waitScreen` pair.

### The handshake

```
EMU CORE thread (T2)                         GL thread (T1)
────────────────────                         ──────────────
... software-render frame into               waitScreen():
    buffer[DAT_0402db50] ...                   lock(db84)
frame complete:                                cond_wait(dbac, db84)   <── blocks
  FUN_0011cb14():                              unlock(db84)
    lock(db84)                                 ↓ (woken)
    DAT_0402db50 ^= 1     (buffer flip)        renderFrame() = FUN_0011ceac:
    cond_signal(dbac)  ───────────────────────►  lock(db84); read ~db50 half; unlock
    unlock(db84)                                  glBindTexture(2D, tex)
                                                  glTexSubImage2D(... front half ...)
(also FUN_0011cb5c sets per-screen dirty          glDrawArrays(GL_TRIANGLES, 0, 6)   // 2 tris
 mask DAT_0402db80 + signal)                      // + second screen at offset 6
                                                  // + .dfx post-process passes
```

`FUN_0011ceac` uploads only via `glTexSubImage2D` from the *already-finished* CPU buffer and
draws a 6-vertex quad per screen (client-array verts, no VBO). **Rendering is therefore
100% off the emu thread's critical path** — the emu thread's only GL-related cost is the
mutex-protected buffer-index flip in `FUN_0011cb14` (a few instructions). **[proven]**

`waitScreen` uses a bare `cond_wait` with **no predicate loop** → technically vulnerable to
spurious wakeups, but harmless here because a spurious early `renderFrame` just re-blits the
last good buffer. When the emulator is **paused**, the input-latch loop `FUN_00116e74` calls
`FUN_0011ce78` (an internal signalScreen) every `usleep(50000)` so the GL thread keeps
redrawing and never deadlocks. **[proven]**

**Why this matters:** the DS produces two 256×192 screens; DraStic's emu thread writes plain
RGBA into RAM and flips an index. The expensive `glTexSubImage2D` upload + xBR/CRT/FXAA
`.dfx` shader chain all run on the GL thread, overlapped with the *next* frame's emulation.
Triple-buffering is not used — a single mutex + 2 buffers + vsync'd GLSurfaceView is enough.

---

## 3. Event scheduler & CPU interleave

### Run model

`startGame` → `setjmp(&DAT_03d7c800)` → `FUN_001327bc` (the **`recompiler_entry`**). From
there the ARM9 recompiler executes translated blocks until it hits a **next-action** boundary,
at which point it transfers per the two string-named transitions:

```
recompiler_entry
   │
   ├─ run ARM9 recompiled blocks up to next scheduled event timestamp
   │        │  string: recompiler_cpu_next_action_arm9_to_arm7
   │        ▼
   ├─ run ARM7 recompiled blocks to catch up to the same timestamp
   │        │  string: recompiler_cpu_next_action_arm7_to_event_update
   │        ▼
   └─ event_update: advance GPU scanline (HBlank/VBlank), SPU, DMA, timers,
      recompute "cycles until next event", then loop back to ARM9.
```

**[proven that these three phases exist and run in this order — from the three string labels
`recompiler_entry`, `…arm9_to_arm7`, `…arm7_to_event_update`, which name the transition
trampolines in the recompiler.]** The transition code itself is inside JIT-emitted / hand-
asm regions and does not decompile to C, so the exact machinery is **[unknown-too-stripped]**.

### Time-slice size — event-driven, not fixed

The slice is **not a constant cycle count**; it is "run until the next queued hardware
event." ARM9 runs ahead, ARM7 catches up to ARM9's timestamp (the DS clocks are 66 MHz ARM9
: 33 MHz ARM7 = 2:1), then the event pass fires everything due at that timestamp. The
frame-boundary handler `FUN_00180834` runs once per frame off the event pass (it calls the
input latch `FUN_00116e74` and processes pending-action bit flags in `core+0x80010`:
reset / load-state / save-state / screenshot / screen-swap, etc.). VBlank is one such
scheduled event that (a) triggers the frame-end FPS/flip path and (b) longjmps back to the
recompiler re-entry. **[proven for the frame-boundary handler and the setjmp/longjmp exit;
the exact per-event cycle budget is [inferred] as standard DS event-queue scheduling.]**

### Fast exit: setjmp/longjmp

Both the boot re-entry (`&DAT_03d7c800`) and the per-core exit (`core+0x3b2f800`, used by
`FUN_00116e74`'s tail `longjmp`) use setjmp/longjmp so the recompiler can bail out of deep
translated code to the scheduler in O(1) without unwinding — the standard "fast CPU exit"
trick. **[proven]**

---

## 4. Frame limiter / pacer — audio-driven

There is **no sleep-to-60Hz timer in the emu path.** The only per-frame timing code found is:

1. **FPS meter (measurement only):** the frame-end handler (just before `FUN_0013d318`)
   samples `gettimeofday` (`FUN_0011b26c`) into a **20-entry ring** at `0x3d9b138` and
   computes `31666666.667 / Δµs` and `Δ/16666.667` (16666.667 µs = 1e6/60) to derive the
   displayed FPS / speed %. This only *reports* speed; it does not gate. **[proven]**
   `getPerformanceCounters` returns two ×16 fixed-point floats from `core+0x3b2f70c/0x3b2f710`
   = ARM9 and ARM7 load — the profiler surface. **[proven]**

2. **The actual pacer = audio back-pressure `FUN_001807dc`:**
   ```
   while ( (audio_period * 3) >> 2  <=  (write_head - read_head) & 0xffff )
       usleep(10);
   ```
   The emu thread spins in `usleep(10)` while the audio ring (`core+0x40000`, head
   `+0x4000c`, tail `+0x40008`, threshold `+0x40018`) holds **more than ¾** of a period of
   queued samples. Because the OpenSL ES buffer-queue callback (doc 06) drains the ring at
   the fixed DS sample rate, this **paces emulation to real time via audio consumption**,
   not a frame clock. **[proven for the code; coupling to OpenSL rate is [inferred] and
   should be confirmed against the audio agent's finding.]**

3. **Vsync ceiling:** the GL thread is an Android `GLSurfaceView` `GLThread`; its
   `eglSwapBuffers` (done Java-side — no EGL in native, per ANCHORS) is vblank-locked to the
   panel's 60 Hz, providing a second, independent cap on presented frames. **[inferred]**

So: **pacing is decoupled from rendering and driven by audio.** If audio is disabled or the
device runs slow, the emu is limited by wall-clock only through the audio ring; the render
thread never throttles the emu (it just misses/repeats blits). This matches the audio
agent's expected OpenSL back-pressure model — **coordinate to confirm the `+0x40018`
threshold is the OpenSL enqueue high-water mark.**

---

## 5. The config "7" and a note on counts

`onInit` sets `ram0x0024c010 = 0x700000007` (two packed 32-bit `7`s). This value is copied
into the DS core config at `core+0x8a68c` at each boot (`FUN_00116e74`). It is a **config
pair, not a thread count** — the 2D helper is hard-coded to 1 thread and the 3D raster pool
to 3 workers + 1 coordinator (all unrolled constants in `FUN_0015f614`). The precise meaning
of the `(7,7)` pair is **[unknown-too-stripped]** (candidates: JIT block/idle-loop tuning or
a frameskip/latency parameter); it is **not** the render/worker thread count. **[proven that
it is not the thread count; its semantics unresolved.]**

---

## 6. DraStic-specific speed tricks (ranked for a melonDS 60fps campaign)

1. **Render fully off the emu thread** behind a 2-buffer + 1-condvar handshake
   (`signalScreen`/`waitScreen`, `FUN_0011cb14` flip, `FUN_0011ceac` blit). The emu thread's
   GL cost per frame is one index flip. **Highest-value, cleanest to port.**
2. **Software-render then upload one texture** — no per-primitive GL, no VBOs, so GL-thread
   cost is a single `glTexSubImage2D` + 6-vert quad + `.dfx` post chain, overlapped with the
   next frame.
3. **Parallel 2D engine** — a dedicated helper thread (`FUN_0013cca0`) renders one 2D engine's
   192 scanlines while the emu thread does the other. Nearly free 2× on 2D-only titles.
4. **4-way parallel 3D software raster** — coordinator + 3 band workers over 192 lines.
5. **Event-driven scheduler with setjmp/longjmp fast exit** — ARM9→ARM7→event_update, run
   until next event (no fixed slice), O(1) bail-out of translated code.
6. **Audio-driven pacing** — emu blocks on the audio ring high-water mark, decoupling
   frame pacing from both render and any sleep timer; render just drops/repeats.
7. **Async savestates** — snapshot to RAM synchronously, zlib+write on a throwaway thread.

**Most actionable for melonDS right now:** items **1–3**. melonDS already software-renders
2D/3D, so the structural wins are (a) moving the GL blit + any post-processing onto a
separate thread with a double-buffered framebuffer and a single condvar flip, and
(b) running the two 2D engines (and 3D scanline bands) on helper threads with a
start/done condvar handshake identical to `FUN_0013cca0`/`FUN_0015f53c`. Pair that with
audio-ring pacing (item 6) to keep timing off a sleep loop.

---

## 7. Key functions index

| Addr | Inferred name | Role |
|------|---------------|------|
| `FUN_0011be00` | `startGame_boot` | setjmp re-entry + enters recompiler; emu runs on caller thread |
| `FUN_001327bc` | `recompiler_run` | ARM9/ARM7/event dispatch entry (`recompiler_entry`) |
| `FUN_00116e74` | `latch_input_and_events` | per-frame input latch; pause loop; longjmp to CPU re-entry |
| `FUN_00180834` | `frame_boundary_handler` | pending-action flags (reset/save/load/screenshot/swap) |
| `FUN_001807dc` | `audio_pacer_wait` | spins usleep(10) while audio ring > ¾ full |
| `FUN_0011cbec` | `fb_alloc_init` | 3 MB posix_memalign, 2×1.5 MB halves, screen cond/mutex init |
| `FUN_0011cb14` | `fb_flip_and_signal` | flip `DAT_0402db50`, cond_signal(dbac) |
| `FUN_0011ceac` | `gl_blit_frame` | GL thread: bind + glTexSubImage2D + glDrawArrays |
| `signalScreen`/`waitScreen` | (JNI) | emu↔GL condvar (`DAT_0402db84`/`DAT_0402dbac`) |
| `FUN_0013d3c4` | `core_ctor` | builds core object; spawns 2D helper; calls `FUN_0015f614` |
| `FUN_0013cca0` | `2d_engine_worker` | renders 192 lines of one 2D engine (`FUN_0015004c`) |
| `FUN_0015f614` | `raster_pool_init` | spawns 3× raster workers + 1 coordinator + sync quads |
| `FUN_0015f53c` | `raster_band_worker` | software 3D raster band (`FUN_001596a4`/`FUN_0015e648`) |
| `FUN_0015f2c8` | `raster_coordinator` | dispatches scanline bands (`FUN_00159bb4`) |
| `FUN_0017a888` | `savestate_save` | snapshot RAM, spawn async writer |
| `FUN_0017a1bc` | `savestate_writer` | zlib compress + write `.dss` |
| `FUN_001c196c` | `pool_init` | generic fork-join pool, ≤32 joinable workers |
| `FUN_001c1e6c` | `pool_submit(fn,arg)` | enqueue job into 32-slot ring |
| `FUN_001c1bec` | `pool_run_and_join` | broadcast + block until all jobs done |
| `FUN_001c1d80` | `pool_dequeue` | worker pops job off ring |
| `FUN_001c1ce0` | `pool_worker_loop` | run job, decrement counter, signal done |

*Do not git-commit (per task).*
