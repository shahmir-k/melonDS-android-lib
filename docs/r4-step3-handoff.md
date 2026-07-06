# R4 Render-Thread — STEP 3 handoff

STEP 1 + STEP 2 are DONE, device-verified, committed on `liteDS-v2-android` (NOT pushed):
- `dea7387b` r4: STEP 1 — zero-GL RunFrame (remove VBlank 3D-output snapshot)
- `e3074445` r4: STEP 2 — double-buffer the replay-read state (concurrency-safe, single-thread)

These are the concurrency-**safety foundation**: RunFrame issues NO GL, and every
piece of state the deferred raster reads back at SubmitFrame is now either A/B
double-buffered (texture-VRAM shadow + `Render*` registers, keyed to `LogBuildBank`)
or depth-1 bank-gated (geometry). Single-thread it is bit-exact + device-clean; the
banks are ready for a concurrent reader. STEP 3 is the app-glue thread itself.

## What STEP 3 must do (all in the APP glue, `app/src/main/cpp/MelonInstance.cpp`)

The emu loop is **Java-driven**: Java calls `MelonInstance::runFrame()` per frame via
JNI (`MelonDS.cpp:196`) on the thread that owns the offscreen GL context. A separate
**presentation thread** already consumes `FrameQueue::getPresentFrame` and blits to the
window. STEP 3 inserts a third role: a native **render thread**.

### Context (no makeCurrent handoff needed — use a shared context)
`OpenGLContext::InitContext(long sharedGlContext)` already does
`eglCreateContext(display, config, sharedContext, ...)`. Create the render thread's
own context in the **same share group** as the emu thread's context (pass the emu
`EGLContext` as `sharedGlContext`). All melonDS GL objects (OutputTex3D, FBOs, shaders,
`FrameQueue` frame textures) are then visible on both. The render thread needs its own
pbuffer surface (copy the `eglCreatePbufferSurface` path, `OpenGLContext.cpp:166`) to
`eglMakeCurrent` against. The emu thread keeps ITS context for the leftover GL below.

### The split of today's `runFrame()` (MelonInstance.cpp:373)
Stays on **emu thread**: `isRenderConfigurationDirty`/`updateRenderer`, frameskip prop,
`nds->RunFrame()` (now GL-free), `retroAchievementsManager->FrameUpdate()`,
`ndsSave/gbaSave/firmwareSave->CheckFlush()`, rewind/screenshot capture (reads emu state).

Moves to **render thread** (these are the GL touchpoints — grep them in `runFrame`):
- `frameQueue.getRenderFrame()` + `presentFence` wait + `validateRenderFrame` (450-449)
- the GPU-timer queries `litevGpu*` / `litevBeginQuery`/`litevEndQuery` (they measure
  the submit+blit GPU time — must run where the GL runs)
- `NeedsShaderCompile`/`ShaderCompileStep` (471-481) — design §5.7: compile on render thread
- `nds->GPU.SubmitFrame()` (493)  ← replays the log bank + rasters 3D reading the STEP-2 banks
- `blitAcceleratedFrame` (518)
- `renderFence = eglCreateSyncKHR` + `glFlush` + `frameQueue.pushRenderedFrame` (531-533)

### Depth-1 handshake (one mutex + one condvar, DraStic model — docs/drastic-teardown/07)
The state the render thread reads for frame N is bank `r = LogReplayBank`; emu records
frame N+1 into bank `1-r`. Two geometry banks ⇒ depth 1.

- Publish (emu, after RunFrame N): under the lock, wait until the previous packet is
  consumed, then set `packetBank = LogBuildBank` (the bank just built), `hasPacket=true`,
  notify. `MelonInstance` must call `nds->GPU.SetSubmitReplayBank(packetBank)` — **add
  this tiny core setter** to feed `GLRenderer::LogReplayBank` from the packet (today
  SubmitFrame sets `LogReplayBank = LogBuildBank` itself at GPU_OpenGL.cpp:1380-ish; the
  threaded path must instead take it from the packet).
- Consume (render): wait `hasPacket`; take bank; `hasPacket=false`; notify emu; do the
  GL block above; then **release the bank** (see early release).
- **Early bank release (design §4.2, worth several ms):** the geometry (RenderPolygonRAM
  + the vertex-RAM walk) is consumed inside `GLRenderer3D::RenderFrameBody` at
  `BuildPolygons` (`GPU3D_OpenGL.cpp:1514-1516`), which is the LAST log record. Everything
  RenderFrameBody reads AFTER that (RenderSceneChunk's `RR.Render*` registers + the
  texture shadow) is STEP-2 banked, so the emu thread may resume right after BuildPolygons.
  Add a release hook: a `std::function` (or a core callback) invoked from RenderFrameBody
  immediately after the vertex upload at 1516, which signals the emu thread's condvar.
  Releasing only after the full SubmitFrame instead SERIALIZES emu↔submit (no win) — this
  ordering is the whole payoff. Because render (~8 ms) < emu core (~13 ms cool), the emu
  thread should then never block; wall = emu core.

### Hard cases — the drain protocol (design §5, docs §5.2)
- **Capture frames** (`GPU.CaptureActiveThisFrame`): already forces `DeferReplay=false`
  ⇒ SubmitFrame runs the synchronous `VBlankSubmit`. Under the thread this frame must
  DRAIN the queue to 0 and run its GL on the render thread synchronously (emu blocks until
  the render thread finishes that frame). Shrek-race never captures, so this can be a
  correctness stub first, but it must not corrupt.
- **Pause / savestate / reset**: before the emu thread calls `DoSavestate`/`Reset`/pause,
  DRAIN (block until `hasPacket==false && renderIdle`). These are stress-tested in gate 4.
- **Android surface loss / pause (§5.3)**: presentation thread owns the window surface
  (already true); render thread owns only offscreen FBOs, so surface loss doesn't stall it.
  On teardown, join/park the render thread; on resume, restart it.

### Also carried by the packet / handled
- Frameskip (§5.5): a skipped frame produces no packet (RunFrame gated, no record).
  Already: skipped frames don't set SubmitPending, so just don't publish.
- The 2D config replay stages through the LIVE `GLRenderer2D::LayerConfig/ScanlineConfig/
  SpriteConfig` members (RIRReplay memcpy's arena→live member→UBO, then RenderScreen reads
  the live member). Under the thread the emu's frame-N+1 DrawScanline writes those SAME
  live members. **This is a real second race the STEP-2 scope did not cover.** For the
  Shrek workload render(8ms)<emu(13ms) with a ~5ms margin so it is unlikely to manifest,
  but per gate 4 if you see 2D corruption, make RIRReplay stage into render-thread-private
  config buffers (or have RenderScreen read from the arena payload directly instead of the
  live member — see GPU2D_OpenGL.cpp RIRReplay Composite2D case ~2295 and RenderScreen's
  `LayerConfig`/`OutputTex3D` reads ~1986). This is the one known STEP-3 sync gap.

## FPS expectation (measured, thermally-aware)
Deferred single-thread on-device (LITEV_PROF, in-race, flag-ON renderthread=1):
`runFrame≈23ms submit≈7ms blit≈1.3ms wall≈32ms ≈31fps`. **BUT the device was thermally
throttled (79-80C, min-freq pinned 1992000 but SoC thermally capped).** PREP breakdown:
`full2D=1.6ms flatten=0.34ms cfg=0.19ms` — so only ~2ms of the 23ms runFrame is render
prep; the rest is core emulation, thermally inflated from the ~13ms cool floor.
Threaded wall = max(emu-core+prep, submit+blit). Cool: max(~15, ~8) ≈ 15ms ⇒ ~60fps.
Throttled: max(~23, ~8) ≈ 23ms ⇒ ~43fps.
**The FPS gate MUST run on a COOLED device** (force-stop between runs, wait temp<60C;
the game self-heats to 80C within ~1 min, so cooldown must be with the app stopped).

## Gates status
1. Host golden shrek-600.trace bit-exact flag OFF+ON — PASS (both steps).
2. App builds flag OFF + ON exit 0 — PASS.
3. Device screenshot parity per step — PASS (STEP1 defer/sync; STEP2 defer/sync; clean, no
   corruption; only the expected <=1-frame animation shift).
4/5. STEP 3 stability + FPS — NOT DONE (thread not built).
6. Device restored to apk-yesterday-e3badc8.apk, props reset — DONE.

## Deliverables in scratchpad
- `apk-r4-step2-dblbuf.apk` — flag-ON, threading-ready single-thread APK (STEP 1+2).
- `apk-r4-step1-zerogl.apk` — STEP 1 APK.
- `r4-core-step1-step2.patch` — the two core commits.
- `r4-app-glue-step2.diff` — current app glue (unchanged by STEP 1/2; SubmitFrame call at
  MelonInstance.cpp:492-493 is the seam the thread replaces).
