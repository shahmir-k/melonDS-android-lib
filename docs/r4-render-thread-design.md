# R4 — Render-Thread Offload: Design Document

Status: proposed (design only; no code)
Date: 2026-07-05
Milestone: M6 / R4 (liteDS-v2 60 fps campaign — Appendix D.7 of `liteDS-v2-plan.md`)
Flag: `LITEV_RENDER_THREAD` (CMake, default OFF)
Predecessor evidence: D.7 reanalysis; `m6.11-runframe-decomposition-device.md`; app-thread
simpleperf (Mali GL userspace driver ~7 ms self + GL renderer C++ ~1.7 ms, all inside
`NDS::RunFrame`; app blit = 2.1 ms GPU-completion stall; core emulation ~13.5 ms).

---

## 1. Thesis and the number we are chasing

The app's in-race `RunFrame` ≈ 22 ms decomposes as **core emulation ~13.5 ms + ~8.5 ms of
GL render submission** that melonDS's GL renderer issues *synchronously, on the emulation
thread, inside `NDS::RunFrame`*. Three of four A55 cores sit idle. The core-emulation
compute (13.5 ms) already fits the 16.6 ms/frame 60 fps budget; the render submission is
what pushes wall time over. R4 moves that ~8.5 ms off the emulation thread so:

```
wall/frame  ->  max(core_emulation ≈ 13.5 ms,  render_submission ≈ 8.5 ms − R2/R3 savings)
            ≈   13.5 ms   =>   ~60–70 fps at 1×, ~55–58 at 3× after R2/R3
```

This is the decisive lever: every other M6 item shaves milliseconds off one side of a
serial sum; R4 changes the sum into a `max()`. It is also the largest single change, so
it ships last and behind a default-OFF flag, coexisting with the synchronous path forever.

R4 does **not** try to make the core faster or the renderer faster — it overlaps them.
R2 (deferred blit) and R3 (GL draw/state diet) shrink the render side; R4 hides whatever
remains behind the core. They compose.

---

## 2. The load-bearing discovery: melonDS already double-buffers 3D geometry

The single most important fact for this design, found by reading the code rather than the
plan: **the DS geometry pipeline and the 3D renderer are already decoupled by a
double-buffered bank swap.** This is what makes R4 tractable instead of a rewrite.

`GPU3D` holds two banks each of vertex and polygon storage
(`src/GPU3D.h:305–315`):

```cpp
Vertex  VertexRAM[6144 * 2];      // two banks of 6144 vertices
Polygon PolygonRAM[2048 * 2];     // two banks of 2048 polygons
Vertex*  CurVertexRAM;            // geometry engine writes here (the "build" bank)
Polygon* CurPolygonRAM;
std::array<Polygon*,2048> RenderPolygonRAM;   // the RENDER input: pointers into a bank
u32 RenderNumPolygons;
```

At VBlank with a pending swap (`GPU3D::VBlank`, `src/GPU3D.cpp:2542–2611`), the just-built
bank's polygons are separated (opaque/translucent), Y-sorted into `RenderPolygonRAM`,
`RenderNumPolygons` is latched, the render-time registers are latched
(`RenderDispCnt`, `RenderToonTable`, `RenderEdgeTable`, `RenderFogDensityTable`,
`RenderClearAttr1/2`, `RenderAlphaRef`, `RenderFogColor/Offset/Shift`, `RenderXPos`), and
then **the bank toggles** (`CurRAMBank ^= 1`): the *next* frame's geometry builds into the
*other* bank. `Renderer3D::RenderFrame` (`GPU3D_OpenGL.cpp:1274`) reads *only*
`RenderPolygonRAM`, `RenderNumPolygons`, the `Render*` latched registers, and VRAM via the
texture cache — never the live build bank.

**Consequence:** after any given VBlank, the 3D render input is a complete, self-contained
snapshot sitting in a bank the geometry engine will not touch again until the *next* buffer
swap. If we forbid the emulation thread from starting a *second* swap before the render
thread has consumed the first — which is exactly depth-1 queue back-pressure — the 3D
geometry needs **zero deep copy**. We pass the bank identity, not the bytes.

The 2D compositor is the harder half (it consumes per-scanline register state captured
*during* the frame, plus dirty VRAM/palette/OAM), and is where the real packet-copy cost
lives. See §3 and §4.

---

## 3. Frame packet contents

A "frame packet" is everything the two GL renderers (`GLRenderer` 2D compositor +
`GLRenderer3D`) consume to produce one frame, captured on the emulation thread so the
render thread can replay GL submission without touching live emulation state.

Three dispositions:
- **Immutable-after-VBlank (no copy):** the render input is already in a bank/latch the
  emulation thread will not disturb before the depth-1 gate releases it. Pass by reference.
- **Double-buffered (write into the shadow the render thread is not reading):** ping-pong
  the CPU-side config structs the renderer already fills; the emulation thread writes bank
  `1−r`, the render thread reads bank `r`.
- **Deep-copied (memcpy into the packet):** small, mutable-mid-next-frame state with no
  existing double buffer.

### 3.1 Packet contents, sizes, disposition

Struct sizes derived from definitions in `src/GPU3D.h`, `src/GPU2D_OpenGL.h`,
`src/GPU_OpenGL.h`, `src/GPU.h` (LP64, natural alignment).

| # | Content | Source | Size | Disposition |
|---|---|---|---:|---|
| 1 | 3D polygon list | `RenderPolygonRAM` (`std::array<Polygon*,2048>`) | **16 KB** (pointers) | Immutable-after-VBlank (bank-gated) |
| 2 | 3D polygon storage (live bank) | `PolygonRAM` bank, ≤2048 × `sizeof(Polygon)=224 B` | ≤ **448 KB** | Immutable-after-VBlank (bank-gated, **not copied**) |
| 3 | 3D vertex storage (live bank) | `VertexRAM` bank, ≤6144 × `sizeof(Vertex)=64 B` | ≤ **384 KB** | Immutable-after-VBlank (bank-gated, **not copied**) |
| 4 | 3D render registers | `RenderDispCnt`, `RenderToonTable[32]` (64 B), `RenderEdgeTable` (16 B), `RenderFogDensityTable[34]`, `RenderClearAttr1/2`, `RenderAlphaRef`, `RenderFog*`, `RenderXPos`, `RenderNumPolygons` | **< 1 KB** | Deep-copy (tiny; latched at VBlank but cheap to snapshot for safety) |
| 5 | 2D per-scanline config (×2 engines) | `sScanlineConfig.uScanline[192]` (160 B/line = 30 KB) + `sSpriteScanlineConfig` (768 B) | **~62 KB** | Double-buffer |
| 6 | 2D layer/sprite config (×2 engines) | `sLayerConfig` (~144 B) + `sSpriteConfig` (uRotscale 512 B + uOAM[128] ~8 KB) + `CompositorConfig` | **~18 KB** | Double-buffer |
| 7 | OAM shadow (×2 engines) | `GPU2D_OpenGL.OAM[512]` u16 | **2 KB** | Double-buffer |
| 8 | Final-pass / capture config | `FinalPassConfig.uScreenSwap[192]` (768 B) + `CaptureConfig.uSrcAOffset[192]` (768 B) | **~1.5 KB** | Double-buffer |
| 9 | Aux input (VRAM/FIFO display only) | `AuxInputBuffer[2]` = 256×192 u16 ×2 | **192 KB** (only when display mode 2/3 or capture-B active; else 0) | Deep-copy (mode-gated) |
| 10 | Palette | `GPU.Palette` | **2 KB** | Deep-copy dirty range (`PaletteDirty` mask) |
| 11 | VRAM flat mirrors | `VRAMFlat_ABG/BBG/AOBJ/BOBJ` (1024 KB) + extpals (80 KB) + `VRAMFlat_Texture/TexPal` (640 KB) | worst **~1.75 MB**; typical dirty **< 256 KB** | Double-buffer, dirty-range fill |
| 12 | Render-span list | list of `(LastLine, line)` partial-composite spans + capture spans | < 1 KB | Deep-copy |
| 13 | Control | frameskip flag, capture-active flag, `ScreensEnabled`, `AbortFrame`, bank id `r` | < 64 B | Deep-copy |

**Worst-case copy cost on the A55.** The only bytes actually `memcpy`'d each frame are the
double-buffered config structs (items 5–8: ~84 KB), palette/OAM dirty (items 7/10: ~4 KB),
the aux buffer when active (item 9: 192 KB), and the *dirty* VRAM ranges into the shadow
flat mirror (item 11). Items 1–3 (the 832 KB of geometry, the expensive part) are **not
copied** — bank-gated. A55 single-core `memcpy` bandwidth measures ~5–8 GB/s
(`__memcpy_aarch64_simd`).

- **Typical in-race frame** (no VRAM-display, modest VRAM churn): ~84 KB configs + ~4 KB
  pal/OAM + < 256 KB VRAM dirty ≈ **~0.1–0.3 ms**.
- **Worst case** (VRAM-display active + full VRAM rewrite): 84 KB + 192 KB aux + 1.75 MB
  VRAM ≈ 2.0 MB ≈ **~0.3–0.4 ms** at 5 GB/s.

Both are **well under the 1 ms budget**, because the geometry banks — the mass — are never
copied. This is the payoff of §2. The kill criterion (§9) is >2.5 ms, ~7× over worst case.

Note on item 11: `MakeVRAMFlat_*Coherent` (the dirty-driven flatten, already run by the GL
renderer today, part of the current ~1.2 ms compositor cost) *is* the shadow-fill step. It
already walks only dirty granules (`VRAMDirtyGranularity == 512`, `NonStupidBitField`
dirty maps). R4 redirects its destination to the render-thread-idle shadow copy; it does
not add a second pass. Two sets of flat mirrors cost ~3.5 MB of steady RAM — acceptable.

### 3.2 Why the 2D path needs a capture/submit split

`GLRenderer::DrawScanline` (`GPU_OpenGL.cpp:399`) and `GPU2D_OpenGL::DrawScanline` are
called *per visible scanline during `RunFrame`* (`GPU.cpp:1181–1185`). Each call does two
separable things:
1. **Capture** — latch per-scanline 2D register state into the CPU-side config structs
   (items 5–8), and, for VRAM/FIFO display modes, copy VRAM/DispFIFO into `AuxInputBuffer`
   (item 9). This is cheap CPU work reading emulation state.
2. **Submit** — when register state changes (`need_render`), issue a partial GL composite
   `RenderScreen(LastLine, line)` (`GPU_OpenGL.cpp:426–430`) and, on capture, `DoCapture`.

R4 splits these: **capture stays on the emulation thread** (it must, it reads live
emulation state per scanline); **submit moves to the render thread**, replaying the
recorded render-span list (item 12) against the double-buffered config structs. The config
structs the renderer already fills are, in effect, 90% of the packet — R4 makes them
double-buffered and defers the `glBufferSubData`/draw calls.

---

## 4. Queue + threading model

### 4.1 Topology

The app *already* runs three roles across two GL contexts (verified in the app glue):
- **Emulation-loop thread** — owns a shared GL context (`OpenGLContext` /
  `NativeGlContext`, `eglCreateContext` with a shared context), calls `RunFrame` and, today,
  the inline GL renderer submission + `blitAcceleratedFrame`, then pushes a `Frame` to
  `FrameQueue` (`MelonInstance::runFrame`).
- **Presentation thread** — consumes `FrameQueue::getPresentFrame`, blits to the window
  surface, synchronized by EGL fences (`renderFence`/`presentFence`,
  `eglCreateSyncKHR`/`eglWaitSyncKHR`).
- Shared GL objects + EGL fence handshake + `FrameQueue` (depth 9) already exist and are
  proven in production.

R4 **splits the emulation-loop thread into two**:
- **Emu thread (new):** pure `NDS::RunFrame` + per-scanline capture into the packet. Never
  issues a GL call. Owns no GL context.
- **Render thread (new):** inherits the GL context the emu-loop thread uses today. Consumes
  packets, issues all GL renderer submission (2D composite replay + `GLRenderer3D::RenderFrame`
  + capture) + `blitAcceleratedFrame`, creates `renderFence`, pushes to `FrameQueue`.
- The presentation thread is unchanged.

Because the render thread simply takes over the exact GL work and context the emu-loop
thread does today, the EGL infrastructure, shared-object lifetimes, and fence handshake are
reused verbatim. This is the primary risk reducer.

### 4.2 Packet queue: depth-1 double buffer

```
struct RenderPacket { /* §3 items, two preallocated instances A/B */ };
RenderPacket packets[2];
// SPSC handoff, one slot:
std::mutex          m;
std::condition_variable cv;
RenderPacket*       ready   = nullptr;   // filled, awaiting render
bool                inFlight = false;    // render thread busy on the other packet
int                 buildBank = 0;       // which packet + which GL config-shadow emu fills
```

- **Emu thread, end of frame N:** finish `RunFrame` (geometry swapped into
  `RenderPolygonRAM`, per-scanline configs filled into shadow `buildBank`). Publish packet:
  `ready = &packets[buildBank]`, `notify`. **Then, before starting frame N+1, block until
  the render thread has *released the geometry bank*** (not necessarily finished
  presenting — just past the point where it has finished reading `RenderPolygonRAM` and the
  live geometry bank). Flip `buildBank ^= 1`.
- **Render thread:** wait for `ready`; take it; `inFlight=true`; do GL submission; when the
  geometry read is complete, signal `bankReleased` (lets emu proceed) *early*; finish blit +
  fence + `FrameQueue` push; clear `inFlight`.

**Depth-1 rationale (not deeper):** the 3D geometry double buffer has exactly two banks
(§2). At depth 1, at most one un-consumed buffer-swap is outstanding, so the emulation
thread can build bank `1−r` while the renderer reads bank `r`; a *third* frame would toggle
back to bank `r` and corrupt the in-flight render. Depth-1 back-pressure is therefore not a
tuning choice — it is dictated by melonDS's two-bank geometry storage. (Deeper queues would
require deep-copying geometry, blowing the §3 copy budget; rejected.)

**Blocking semantics:** the emu thread blocks only when the render thread is still holding
the previous packet's geometry bank — i.e. only when **render is slower than emu for that
frame**. When core (13.5 ms) > render (8.5 ms), the emu thread never blocks and wall time =
core. When a dense-geometry frame makes render > core, the emu thread blocks the difference
and wall = render. Either way wall = `max(core, render)`, the goal.

**Early bank release** is the key optimization: the render thread reads geometry
(`BuildPolygons`, the vertex-RAM walk) *first*, then does the rest of GL submission +
blit + present. It signals `bankReleased` right after the geometry read, so the emu thread
resumes as early as possible and overlaps the bulk of GL submission. Getting this ordering
right is worth several ms; getting it wrong (releasing after present) serializes and eats
the win — see Kill Criteria.

### 4.3 Fences

Two fence classes, both already present in the app:
- **Present fence** (`presentFence`) — the presentation thread signals when the previous
  use of a `Frame` texture is done; the render thread waits before reusing it. Unchanged.
- **Render fence** (`renderFence`) — the render thread signals GL-submission completion; the
  presentation thread waits before blitting to the window. Unchanged.
- **Capture readback fence (new)** — see §5.1. When a frame uses display capture, the render
  thread's capture readback (`glReadPixels` into a shared CPU buffer) is fenced; the emu
  thread waits on it only if/when it reads captured VRAM.

---

## 5. The hard cases (each with a decided answer)

### 5.1 Display capture — the one true feedback edge (decided: synchronous fallback first, capture-stall later)

**The problem.** Display capture is the *only* path where rendered output flows *back into
emulation state*, breaking the otherwise strictly-downstream pipeline. Mechanism
(`GPU.cpp:1589–1697`, `GPU_OpenGL.cpp` `DoCapture`/`SyncVRAMCapture`):
- `CheckCaptureStart` marks destination LCDC VRAM blocks `CBFlag_IsCapture` and calls
  `Rend->AllocCapture`. During the frame the 2D path renders the composited output into a GL
  capture texture (`DoCapture`). `CheckCaptureEnd` marks `CBFlag_Complete`.
- The captured pixels live **only in a GL texture**, not in emulation VRAM, until the
  emulator *touches* a captured VRAM block. On CPU read/write of such a block,
  `SyncVRAMCaptureBlock` → `Rend->SyncVRAMCapture` does a `glReadPixels` back into VRAM so
  emulation sees the captured image. `SyncAllVRAMCaptures` forces this at savestate/reset/
  renderer-swap.

With a render thread, `SyncVRAMCapture` becomes an **emu→render synchronous dependency in
the wrong direction**: the emu thread, mid-frame N+1, may read a VRAM block whose contents
depend on the render thread finishing frame N's capture *and* reading it back. This is
exactly the pipeline hazard.

**Decided answer, in two tiers (fallback ships first, per the "correctness never
regresses" directive):**

- **Tier 1 — capture-active synchronous fallback (implemented FIRST, PR step 1).** Detect
  capture use per frame: `GPU.CaptureEnable` set during the frame, or any
  `VRAMCaptureBlockFlags` entry with `CBFlag_IsCapture`. For any such frame, **do not
  offload**: run that frame's GL submission synchronously on the emulation thread (the
  present path), exactly as flag-OFF. The queue drains to depth 0 around it. Cost: capture
  frames pay the old serial price, but they are *rare* — the in-race Shrek workload never
  captures (D.3/M6.8), and most games use capture only in specific effects/transitions.
  This makes R4 *correct by construction* for every game from day one; the async win simply
  does not apply to capture frames.

- **Tier 2 — capture-triggered stall (later, evidence-gated).** For games where capture is
  hot enough that Tier 1 costs real FPS, keep rendering async but: (a) the render thread,
  on completing a capturing frame, does the `glReadPixels` into a *shared CPU capture
  buffer* and signals the capture fence; (b) `SyncVRAMCaptureBlock` on the emu thread, when
  it fires, waits on that fence and copies from the shared buffer instead of calling the
  renderer directly. This preserves the lazy semantics (only stalls if the CPU actually
  reads captured VRAM) while keeping the readback off the emu thread's GL context. Only
  built if a capture-heavy title shows Tier 1 losing FPS.

This is the point named in §6 where "render is downstream" could break — and the reason the
fallback is unconditional and first.

### 5.2 Savestate / pause / reset — drain protocol (decided)

Any operation that reads or mutates emulation *or* renderer state coherently must
**drain the queue to depth 0** first:

1. Emu thread signals `drainRequested`.
2. Emu thread blocks until `inFlight == false && ready == nullptr` (render thread idle,
   no packet pending).
3. Perform the operation:
   - **Savestate save/load:** `NDS::DoSavestate`. Renderer state is transient (textures
     re-derived from VRAM on next render); `Renderer::PreSavestate/PostSavestate` hooks
     exist. On *load*, force `SyncAllVRAMCaptures` (drained, so this runs on whichever
     thread owns GL — do it before draining GL, or route through the render thread while
     idle). Savestate serializes `RenderPolygonRAM`/`CurRAMBank` already
     (`GPU3D.cpp:558–579`), so bank identity is restored consistently.
   - **Reset:** `nds->Reset()` + renderer `Reset()`; clears both banks; queue already
     drained.
   - **Pause:** simply stop feeding packets; render thread idles on the condvar. Resume =
     resume feeding. No teardown.
4. Resume normal feeding.

Rule: **the emu thread never mutates state the render thread might read without a completed
drain.** The RCHV-wrapped app savestates (D.3) are unaffected — R4 is below that layer.

### 5.3 Android pause/resume — EGL surface lifecycle (decided)

The window *surface* can be destroyed on Android `onPause`/`surfaceDestroyed`; the *context*
and shared GL objects persist. Ownership split:
- The **render thread owns the offscreen render context** (the one that produces
  `frameTexture` in the shared object space). It renders to FBOs/textures, never directly to
  the window surface, so surface loss does not stall it — it keeps producing frames into
  `FrameQueue`.
- The **presentation thread owns the window surface** (`eglCreateWindowSurface` /
  `eglMakeCurrent` on the window). On `surfaceDestroyed` it releases the surface
  (`eglMakeCurrent(..., EGL_NO_SURFACE, ...)`, `OpenGLContext.cpp:212/220`); on
  `surfaceCreated` it recreates it. This is exactly today's presentation-thread behavior.
- On resume, the render thread's context/objects are intact; no packet is lost. On pause,
  §5.2 pause (stop feeding) applies; the render thread parks.

Context creation: the render thread creates its context at emu start (or inherits the one
the emu-loop thread creates today), shared with the presentation context so `frameTexture`
handles cross threads. Surface lifecycle stays entirely on the presentation thread — R4 does
not touch it.

### 5.4 Screenshots / rewind — which frame they read (decided)

`screenshotRenderer->renderScreenshot(&nds->GPU, currentRenderer, renderFrame)` and rewind
capture run at the end of `runFrame` today, reading the just-rendered `renderFrame`. Under
R4 the "just-rendered" frame is the one the render thread *last completed*, which is one
frame behind the emu thread. Decision:
- Screenshots/rewind read **the last completed rendered frame** (the render thread's most
  recent `FrameQueue` output). This is a 1-frame display latency, identical to R2's deferred
  blit, and acceptable (D.7 accepts one frame of latency on this device).
- Rewind savestate capture (`saveState`) reads *emulation* state, which is current on the
  emu thread — it uses §5.2 drain semantics so the savestate and its screenshot are
  coherent (the screenshot is the last rendered frame, the state is current; the ≤1-frame
  skew is already inherent to rewind and matches flag-OFF within one frame).

### 5.5 Frameskip interaction (decided)

`LITEV_AGGRESSIVE_SKIP` sets `SkipThisFrame` in `GPU::StartFrame`, gating the renderer draw
calls (`GPU.cpp:1177–1202`) while CPU/DMA/timers run full-speed. Under R4:
- A skipped frame produces **no packet** (nothing to render). The emu thread runs `RunFrame`
  with rendering gated off and does not publish; the render thread simply gets fewer packets.
- Frameskip and render-thread offload are **orthogonal and composable**: frameskip reduces
  how *often* the render side runs; R4 overlaps whatever renders. Both flags may be ON.
- Care: the per-scanline capture (§3.2) must also be gated by `SkipThisFrame` so a skipped
  frame does not waste packet-fill CPU. This mirrors the existing draw-call gating.

### 5.6 Deferred blit (R2) composition (decided)

R2 keeps N-buffered frame textures and blits the *previous* (guaranteed-complete) frame,
fence-asserted. R4 subsumes and generalizes this: the render thread already produces into
`FrameQueue` a frame behind the emu thread, and the presentation thread blits a completed
texture. With R4:
- R2's "blit previous frame" is the natural steady state — the render thread's output *is*
  one frame behind, and the presentation thread never blits an incomplete texture (the
  `renderFence` guarantees completeness).
- The 2.1 ms blit stall disappears because blit runs on the render/presentation side, off the
  emu thread's critical path, against a fenced-complete texture.
- If R2 ships first (it should, per D.7 sequencing), R4 inherits its N-buffering and fence
  discipline unchanged. `LITEV_RENDER_THREAD` and R2's `debug.litev.deferblit` are
  compatible; with R4 ON, R2's manual defer becomes redundant but harmless.

### 5.7 GL error / context-loss recovery (decided)

- **GL errors during submission** are confined to the render thread. On a detected error
  (glGetError in debug, or a failed shader compile / FBO-incomplete), the render thread logs,
  drops the current packet (present the previous frame again), and continues. Emulation is
  never affected — it is upstream.
- **EGL context loss** (`EGL_CONTEXT_LOST`, GPU reset): the render thread tears down and
  recreates its context + all GL objects (`Renderer::Stop` then re-`Init`), re-derives
  textures from VRAM on the next packet (all render state is a pure function of the packet +
  VRAM). No emulation state is lost. The queue drains during recreation (§5.2 semantics).
- **Shader compile** already happens lazily on the render side
  (`NeedsShaderCompile`/`ShaderCompileStep`, `MelonInstance.cpp:471–481`); under R4 it runs
  on the render thread before consuming the first packet. Unchanged logic, new thread.

---

## 6. Flag + rollout

- **`LITEV_RENDER_THREAD`** — CMake option, **default OFF**. Flag-OFF compiles and runs the
  exact current synchronous path (renderer submission inline in `RunFrame`), byte-for-byte.
  Upstream-identical with no flag, per liteDS-v2 flag discipline.
- **Runtime toggle** — `debug.litev.renderthread` (Android property) and an app setting,
  read at emu start (thread topology is chosen at start, not mid-run; toggling requires an
  emu restart / renderer re-init, which the drain protocol §5.2 already supports).
- **Both paths coexist long-term.** The synchronous path is the correctness oracle and the
  capture-frame fallback (§5.1) *is* the synchronous path invoked per-frame. There is no plan
  to delete it — capture frames use it forever, and it is the golden-parity reference.

**Why the golden traces stay valid (the precise argument).** The liteDS golden gates compare
*emulation* state (RAM/register/timestamp/frame-hash traces from the headless
`--verify-trace`). R4 changes **only when and on which thread GL submission happens** — it
moves work that reads emulation state downstream of the traced state, and issues no writes
back into emulation state, **with exactly one exception: display capture (§5.1)**. Formally:

> For every non-capture frame, the set of emulation-state writes performed by `RunFrame` is
> identical flag-ON vs flag-OFF, because the render thread only *reads* the packet and VRAM
> and writes *GL objects*, which are not emulation state and not traced. Therefore the golden
> traces are bit-identical.

The one place this argument breaks is capture feedback (`SyncVRAMCapture` writes rendered
pixels into VRAM, which *is* emulation state and *is* traced). R4 defends this by making
capture frames run the synchronous path (§5.1 Tier 1) — so even on capture frames the
emulation-state writes are byte-identical to flag-OFF, and the golden traces remain valid
**unconditionally**. This is why Tier 1 is not optional and ships first.

Additional: the headless harness (`LITEV_HEADLESS`) uses the software renderer, so
`LITEV_RENDER_THREAD` has **no effect on the golden traces at all** — they are generated
software-side. The flag only alters the app's GL path. The traces gate the core; screenshot
parity (§7) gates the render path.

---

## 7. Correctness gates + bench plan

**Correctness gates (all must pass before default-ON consideration):**
1. **Golden traces untouched.** `--verify-trace shrek-600.trace` and
   `shrek-race-3400.trace` (and the event-slices/full-config goldens) pass bit-exact with
   `LITEV_RENDER_THREAD` compiled in (they exercise the software path, so this proves the
   core is unperturbed by the flag's presence). Argument in §6.
2. **Screenshot parity flag-ON vs flag-OFF.** On-device, in-race, capture top+bottom
   screenshots at fixed frames (e.g. 60/300/600/900 of the race savestate) with the flag ON
   and OFF; compare pixel-exact **allowing a ≤1-frame temporal shift** (async output is one
   frame behind). Any content difference beyond the shift fails. Run a capture-using title
   (once a suite ROM is available) to validate §5.1 Tier 1 parity explicitly.
3. **3-lap stability.** Three full laps of the Shrek race, flag-ON, no crash, no flicker, no
   texture corruption, no queue leak (packet slots return to idle; no fence handle leak).
4. **Latency check.** Measure input→display latency flag-ON vs flag-OFF; expect +1 frame
   (~16.6 ms), matching R2. Must not exceed +1 frame (a stalled/decoupled queue would show
   more).
5. **Pause/resume/savestate/reset** cycled 20× under load with no deadlock (drain protocol
   §5.2) and no surface-loss crash on Android backgrounding (§5.3).

**Bench plan:**
- Workload: in-race Shrek savestate (`shrek-race.mln`), hold-A, on the Anbernic RG DS
  (4×A55 @ 1.992 GHz), same discipline as `m6.11-runframe-decomposition-device.md` (quiet
  device, thermals logged, medians of 3).
- Instrument via the existing `debug.litev.prof` `LITEV_PROF` line
  (`MelonInstance.cpp:552–583`), extended to report the emu-thread `runFrame` and the
  render-thread submission bucket separately.
- **Expected `LITEV_PROF` shape after R4** (flag-ON, in-race):
  - Emu-thread `runFrame` (core only, no GL) ≈ **13.5 ms** (down from ~22 ms).
  - New **render-thread bucket** ≈ **8.5 ms − R2/R3 savings** (2D composite + 3D submit +
    blit), running *concurrently*.
  - **wall/frame ≈ max(13.5, render) ≈ 13.5 ms** when core-bound → ~60+ fps at 1×,
    ~55–58 at 3× after R2/R3.
  - `fenceWait` on the emu thread ≈ 0 when core-bound (emu never blocks); rises only if a
    dense-geometry frame makes render > core.
  - `blit` bucket on the emu thread → **0** (moved to render thread).
- A/B: flag-OFF baseline (~30 ms wall, 31–33 fps) vs flag-ON, same scene, same thermals.

---

## 8. Implementation plan (PR-sized, each independently gated)

Ordered so **correctness never regresses** — the capture fallback and the flag scaffolding
land before any thread is introduced, and each step is behind `LITEV_RENDER_THREAD` and
independently screenshot-gated.

| Step | Scope | Risk | Effort |
|---|---|---|---|
| **0. Flag + packet scaffolding** | `LITEV_RENDER_THREAD` CMake option (default OFF); `RenderPacket` struct + double-buffered config-struct storage in the GL renderers; **no thread yet** — flag-ON still runs synchronously but through the packet (fill packet, then immediately consume on the same thread). Proves the packet captures everything with zero behavior change. | Low | ~2–3 d |
| **1. Capture detection + synchronous fallback** | Per-frame capture-active detection (§5.1 Tier 1); wire the "capture frame → synchronous, no offload" branch *before* the thread exists (in step-0 it is a no-op; it becomes load-bearing in step 3). Screenshot-parity on a capture title. | Low–Med | ~2 d |
| **2. 2D capture/submit split** | Refactor `GLRenderer::DrawScanline` / `GPU2D_OpenGL::DrawScanline` into capture (emu-side, fills packet) + submit (replays render-span list). Still single-threaded. Screenshot-exact vs OFF. This is the fiddly core change. | **Med–High** | ~4–6 d |
| **3. Render thread + depth-1 queue** | Introduce the render thread; move GL submission + blit + `FrameQueue` push onto it; SPSC depth-1 queue with early bank release (§4.2); reuse existing EGL contexts/fences. Capture frames take the step-1 synchronous branch. Full correctness gates §7. | **High** | ~5–7 d |
| **4. Drain protocol + lifecycle** | Savestate/pause/reset drain (§5.2); Android surface pause/resume (§5.3); screenshot/rewind frame selection (§5.4); GL/context-loss recovery (§5.7). Cycle tests. | Med | ~3–4 d |
| **5. Bench + tune early-release ordering** | On-device A/B; tune the bank-release point (§4.2) to maximize overlap; confirm wall = max(core, render); confirm packet copy < 1 ms via a temporary `LITEV_PROFILE` counter. | Med | ~2–3 d |
| **6. (evidence-gated) Capture-stall Tier 2** | Only if a capture-heavy title shows Tier 1 losing FPS (§5.1 Tier 2). | Med | ~3 d |

**Total core effort:** ~18–25 engineer-days for steps 0–5; step 6 conditional.

Each step 0–4 ships behind the flag with flag-OFF byte-identical and its own screenshot
gate; step 2 is the highest-risk correctness change (the 2D split) and step 3 the highest-
risk concurrency change (thread + queue). Sequence R2 and R3 before step 5 so the render
bucket is already smaller when overlap is measured (D.7 sequencing).

---

## 9. Kill criteria (what would prove the design wrong early)

Measure these as early as the relevant step allows; any one firing means stop and rethink:

1. **Packet copy > 2.5 ms/frame** (measured, step 5, temporary `LITEV_PROFILE` counter). The
   §3 budget is ~0.3–0.4 ms worst case *because geometry is bank-gated, not copied*. If the
   measured copy exceeds 2.5 ms, the bank-gating assumption (§2) has failed — e.g. a game
   flushes geometry more than once per displayed frame, forcing geometry deep-copies. Kills
   the "no-copy geometry" premise; would require a different (deeper-copy or per-region)
   strategy or abandonment.
2. **Sync overhead eats the win** (step 5): if flag-ON wall/frame is not measurably below
   flag-OFF (< ~2 ms improvement) on the core-bound in-race scene, the overlap is not
   materializing — most likely the bank is released too late (after present, §4.2), so emu
   and render serialize. If correct early-release ordering still shows no win, the render
   side is not actually concurrent with the core (contention on the shared GL driver, memory
   bandwidth saturation on the A55) → R4 does not pay and is shelved.
3. **`fenceWait`/emu-block time ≈ render time every frame** (step 5): means the queue is
   effectively serializing (render always slower than core, or depth-1 too shallow for this
   workload). Depth-1 is dictated by geometry banks (§4.2), so if this fires, the render side
   must first be shrunk by R2/R3 until render < core; if it cannot be, wall stays at render
   time and R4's ceiling is the render time, not the core floor.
4. **Screenshot parity fails beyond the 1-frame shift** (step 2 or 3): a real correctness
   bug in the capture/submit split or the double-buffering. Blocks default-ON until fixed;
   does not kill the design but gates it.
5. **Capture Tier 1 costs > ~10% FPS on a mainstream title** (step 3, when a capture ROM is
   available): the synchronous-fallback assumption ("capture is rare") is wrong for that
   title, forcing Tier 2 (step 6) earlier than planned. Not a design kill, a scope expansion
   signal.

The decisive early signal is (1)+(2): if the geometry bank-gating holds (copy < 1 ms) *and*
early bank release produces real overlap, R4 delivers wall = max(core, render) and the 60
fps campaign's decisive lever lands. If (1) fails, the whole "cheap packet" premise is
wrong; if (2) fails after correct ordering, the A55 cannot actually run core and GL
submission concurrently and the campaign must look elsewhere (reserve tier: ARM9 idle-skip,
Tier-B memory stubs, hot-region recompilation).

---

## 10. Summary of decisions

- **3D geometry is already double-buffered** (two banks, swapped at VBlank) — pass by bank
  reference, never copy the 832 KB. This is what keeps the packet copy < 1 ms and makes R4
  feasible.
- **Depth-1 queue** is mandated by the two-bank geometry storage, not chosen; emu blocks only
  when render > core, giving wall = max(core, render).
- **2D compositor needs a capture/submit split**: per-scanline register capture stays on the
  emu thread (fills double-buffered config structs ≈ 84 KB), GL submission moves to the
  render thread.
- **Display capture is the only feedback edge** and is handled by an unconditional
  synchronous fallback (ships first), preserving golden-trace validity by construction.
- **The render thread inherits the app's existing GL context + fence + FrameQueue
  infrastructure**, which already runs a producer/consumer GL split — the biggest risk
  reducer.
- **`LITEV_RENDER_THREAD` default OFF, both paths permanent**; golden traces provably
  untouched (render is downstream; capture forced synchronous).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
