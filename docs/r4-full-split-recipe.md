# R4 — Full Per-Scanline Capture/Submit Split: Implementation Recipe

Status: implementation handoff (grounded in code at HEAD 7e54405d)
Date: 2026-07-05
Milestone: M6 / R4 (Appendix D.7). Companion to `docs/r4-render-thread-design.md`.
Flag: `LITEV_RENDER_THREAD` (default OFF).

This document turns the design's "step 2 (2D capture/submit split, med–high risk,
4–6 d)" into a concrete, call-site-level recipe, grounded in the exact GL renderer
functions as they stand at 7e54405d. It records a load-bearing structural finding
about why the split is **monolithic** (no smaller bit-exact increment exists beyond
what the current seam already landed), and specifies the byte-exact command-log
approach, the exact call sites to convert, and the verification protocol.

Read this together with the design doc. The design says WHAT and WHY; this says
WHERE (exact functions/lines) and HOW (the command-log, the snapshot obligations).

---

## 0. The load-bearing finding: the split is monolithic

The current seam (7e54405d) already extracts the **one and only** chunk of GL that
can be deferred byte-exactly by a state *snapshot* rather than a full command log:
the final 2D VBlank composite, made safe by snapshotting the 3D color output
(`OutputTex3D → SubmitShadow3DTex`) at the VBlank point. That works because the 2D
composite's single cross-boundary input is that one texture.

Everything else that `NDS::RunFrame` issues to GL is **interleaved with live
emulation-state mutation within the same frame** and therefore cannot be deferred
without copying, per deferred operation, the exact bytes it read:

- **Per-scanline VRAM uploads** (`GLRenderer2D::UpdateAndRender`, the
  `glTexSubImage2D(VRAMTex_BG …)` at GPU2D_OpenGL.cpp:719 and `PalTex_BG` at :740):
  these upload the *current* dirty VRAM/palette. The emulator keeps writing VRAM on
  later scanlines of the same frame (that is the entire reason raster effects need
  per-scanline uploads). A composite issued at line 50 must see VRAM-as-of-line-50;
  a later upload at line 120 overwrites the same `VRAMTex_BG`. Deferring the line-50
  composite to submit while re-using the (now line-120) texture is **not** bit-exact.
- **Prerender draws** (`PrerenderLayer` :1599, `PrerenderSprites` :1556) render into
  `BGLayerTex[]`/`SpriteTex` from `LayerConfigUBO`/`SpriteConfigUBO`, which
  `UpdateLayerConfig`/`UpdateOAM` overwrite in place as the frame advances.
- **Mid-frame composites** (`GLRenderer2D::RenderScreen` :1760 and the final-pass
  `GLRenderer::RenderScreen` at GPU_OpenGL.cpp:626) upload `ScanlineConfigUBO` for a
  span and `CompositorConfigUBO` (via `UpdateCompositorConfig`) at draw time. The
  compositor config is a single struct mutated per composite.
- **3D raster** (`Start3DRendering` → `GLRenderer3D::RenderFrame`, GPU.cpp:1221 at
  VCount 215): geometry is bank-gated and safe (design §2), **but** `RenderFrame`
  reads texture VRAM via the texcache, and texture VRAM can be written by the CPU/DMA
  between VCount 215 and end-of-frame (VCount 262). Deferring `RenderFrame` past that
  window would use post-write textures → not bit-exact by construction. (The current
  seam correctly sidesteps this by snapshotting the *output*, not deferring the
  render.)

**Consequence:** there is no clean "defer one more thing" step. To make RunFrame
issue ~zero GL you must convert *all* of the above call sites at once into a recorded
command log with per-op byte copies, then replay the log. That is one atomic change
(the design's step 2), and it is the honest reason this cannot be salami-sliced into
smaller independently-bit-exact commits.

**Critical verification constraint:** the headless golden harness uses the *software*
renderer and issues no GL (design §6). So `--verify-trace` proves only that the flag
does not perturb the **core** — it can NOT validate any of this GL split. GL
capture/submit correctness is **exclusively device-verifiable** (on-device screenshot
parity, design §7 gate 2). Budget the implementation loop accordingly: every change
needs an APK build + device screenshot-diff to know if it is bit-exact.

---

## 1. The command-log approach (byte-exact by construction)

Do NOT try to defer by re-reading live state at submit. Instead, during RunFrame,
**intercept every GL-issuing op in the renderer and append a record to a per-frame
command log**, deep-copying the exact bytes/config the op would have used *at that
moment*. At submit, replay the log in order. Because the replayed GL command stream
is byte-identical to the inline stream (same ops, same bytes, same order), the output
is bit-exact — this is the same guarantee the current 3D-output snapshot relies on,
generalized to the whole frame.

This is exactly the design's "frame packet" (§3) + "render-span list" (§3.1 item 12),
realized as an ordered opcode log rather than a set of parallel structs, which makes
the interleaving/ordering correct for free.

### 1.1 Log record types (minimum set)

```cpp
enum class GLOp : u8 {
    UploadBGVRAM,      // glTexSubImage2D(VRAMTex_BG, engine): {engine, yStart, rows, bytes*}
    UploadOBJVRAM,     // glTexSubImage2D(VRAMTex_OBJ, engine): {engine, bytes*}
    UploadPalBG,       // {engine, TempPalBuffer copy (256*(1+64) u16)}
    UploadPalOBJ,      // {engine, TempPalBuffer copy (256*(1+16) u16)}
    PrerenderLayer,    // {engine, layer, LayerConfig snapshot}
    PrerenderSprites,  // {engine, SpriteConfig snapshot + SpritePreVtxData copy + NumSprites}
    RenderSpritesSpan, // {engine, ystart, yend, SpriteScanlineConfig span, SpriteConfig snap}
    Composite2D,       // {engine, ystart, yend, ScanlineConfig span, CompositorConfig snap,
                       //  LayerConfig snap, ForcedBlank/UnitEnabled}
    Render3D,          // {} — bank-gated; replay reads RenderPolygonRAM (safe under depth-1)
                       //      BUT texture VRAM must come from the shadow (Stage B); see §2
    FinalPassSpan,     // {ystart, yend, FinalPassConfig snap, AuxUsageMask, aux buffer copies,
                       //  DispCntA/B, MasterBrightness, vramcap}
    Capture,           // {ystart, yend, capture config} — Tier-1 forces synchronous, so a
                       //  captured frame takes NO log; see §3
};
```

Storage: two preallocated logs (double-buffered, A/B) per the depth-1 queue. Size the
byte arena to worst case (§3 of design: ~84 KB configs + palettes; VRAM handled by the
Stage-B shadow, not copied per-op — see §2). Reset the log at StartFrame.

### 1.2 Exact call sites to convert (all in the OpenGL renderer)

| Inline site (file:line @ 7e54405d) | Currently issues | Convert to |
|---|---|---|
| `GLRenderer2D::UpdateAndRender` GPU2D_OpenGL.cpp:699–724 | BG VRAM `glTexSubImage2D` | `UploadBGVRAM` record (or Stage-B shadow ref) |
| …:739–741 | BG pal upload | `UploadPalBG` (copy `TempPalBuffer`) |
| …:750–772 | `PrerenderLayer` loop | `PrerenderLayer` records |
| …:784–800 | OBJ VRAM/pal + `PrerenderSprites` | `UploadOBJVRAM`+`UploadPalOBJ`+`PrerenderSprites` |
| …:657 `DoRenderSprites(line)` | sprite span raster | `RenderSpritesSpan` record |
| …:664 `RenderScreen(LastLine,line)` | per-engine composite | `Composite2D` record |
| `GLRenderer::DrawScanline` GPU_OpenGL.cpp:531 `RenderScreen` | final-pass composite | `FinalPassSpan` record |
| …:537 `DoCapture` | capture | (capture frames are synchronous, Tier 1) |
| `GLRenderer2D::VBlank` :812–819 | VBlank sprite+composite | `RenderSpritesSpan`+`Composite2D` |
| `GLRenderer::VBlankSubmit` GPU_OpenGL.cpp:746 | final VBlank composite | `FinalPassSpan` (already deferred) |
| `Start3DRendering` GPU.cpp:1221 → `RenderFrame` | 3D raster | `Render3D` record (needs Stage B) |

The capture/replay boundary is the same `SubmitFrame()` entry that exists today; it
grows from "replay the one deferred composite" into "drain the whole log."

### 1.3 Replay

`SubmitFrame()`: for each record in order, re-issue the GL with the snapshotted bytes.
The per-op config snapshots are uploaded to the SAME UBOs before each draw (the UBO is
a scratch register at replay time; ordering makes single-UBO reuse correct). VRAM
texture uploads replay from the Stage-B shadow (see §2). Keep the existing
`SubmitReplaying`/`SubmitShadow3DTex` mechanism OR delete it once `Render3D` is in the
log (rendering 3D fresh at submit from the bank + shadow textures removes the need for
the output snapshot — but only after Stage B provides shadow texture VRAM).

---

## 2. Stage B — VRAM/palette shadow is REQUIRED, not optional

`UploadBGVRAM`/`Render3D` read `VRAMFlat_*` mirrors (`MakeVRAMFlat_*Coherent`,
GPU.cpp). Deep-copying the dirty VRAM range into each `UploadBGVRAM` record is one
option, but the flat mirrors are large (design item 11: up to ~1.75 MB) and re-used.
The design's answer (§3.1 item 11) is to **redirect `MakeVRAMFlat_*Coherent`'s
destination to a render-thread-owned shadow**, double-buffered / dirty-range-filled,
so the log's uploads and `Render3D`'s texcache read a stable snapshot. This is Stage B
and it is a prerequisite for `UploadBGVRAM`/`Render3D` to be bit-exact under the log —
even single-threaded, because VRAM mutates within the frame between the capture point
and submit.

Measure the shadow-fill copy cost with a temporary counter (design kill-criterion #1:
>2.5 ms A55 kills the premise; budget <1 ms). The fill is the *same* dirty-granule walk
that already runs today; only its destination changes, so the added cost is the extra
memcpy of dirty granules into the shadow (typ. <256 KB → ~0.05 ms at 5 GB/s).

---

## 3. Capture frames (Tier 1) — unchanged, keep synchronous

`GPU.CaptureActiveThisFrame` (already computed, GPU.cpp `StartFrame`/`StartScanline`)
gates this. A capture-active frame writes NO log and takes the fully synchronous path
(exactly flag-OFF), because `SyncVRAMCapture` writes rendered pixels back into traced
VRAM (the one feedback edge). This is already wired; the log path simply must check the
flag and no-op the capture. Under the render thread (Stage C) a capture frame drains
the queue to depth 0 and runs synchronously (design §5.1 Tier 1).

---

## 4. Stage C — thread + depth-1 queue

Only after Stages A+B are device-bit-exact single-threaded. Then:
- Emu thread: after RunFrame(N) publish log A/B, immediately RunFrame(N+1); block only
  when the render thread still holds the geometry bank (depth-1 back-pressure, design §4.2).
- Render thread: owns the GL context (inherit the app's existing emu-loop GL context;
  study `MelonInstance::runFrame` + `OpenGLContext`/`FrameQueue`/EGL fences — the app
  already runs a producer/consumer GL split, the biggest risk reducer, design §4.1).
  Replay the log, blit, fence, push to `FrameQueue`, signal bank release early
  (design §4.2 early-release is worth several ms; releasing after present serializes).
- Hard cases: savestate/pause/reset → drain to depth 0 first (§5.2); Android surface
  loss → presentation thread owns the window surface, render thread owns offscreen
  context, rebuild on loss (§5.3); screenshots/rewind → last completed frame (§5.4);
  frameskip → gate the log fill by `SkipThisFrame` too (§5.5).
- Sync: DraStic-proven minimal model — one mutex + one condvar for the flip
  (docs/drastic-teardown/07-threading-scheduler.md).

---

## 5. Verification protocol (per stage)

1. Host goldens both flags bit-exact (`--verify-trace shrek-600.trace`,
   `shrek-race-3400.trace`) — proves the flag doesn't perturb the core. Necessary,
   NOT sufficient for GL correctness.
2. **Device screenshot parity** flag-ON vs flag-OFF, same savestate, static content
   pixel-identical (only animated timer/minimap differ ≤1–2 frame latency). This is the
   ONLY test that validates the GL split. Do it after EVERY stage (A single-thread, B
   single-thread, C threaded). View every screenshot.
3. Stage C only: ≥5 min in-race threaded, 0 crashes/tearing/deadlock; pause/resume,
   savestate load, rapid input stress. Any intermittent corruption = a real data race.
4. Stage C perf gate: LITEV_PROF in-race, medians, ≥35 lines each flag. Report FPS delta.
   Target wall → max(core, render). Watch kill-criteria (design §9): packet copy >2.5 ms,
   no measurable win after correct early-release, or `fenceWait` ≈ render every frame.

---

## 6. Prerequisite decision (design addendum 10) — do NOT skip

Before investing the 4–6 d in Stage A, measure the **app** RunFrame core floor on
device (renderer disabled, or a frameskip sweep). If core ≈13.5 ms → full split reaches
wall≈max(13.5,~18)≈18 ms ⇒ ~55 fps, worth it. If core ≈20 ms+ → even a perfect split
caps <60 fps ⇒ pivot to the M6.6 hybrid (DraStic software-NEON render across all 4
cores, addendum 9). This measurement gates the whole investment.

---

## 7. Session boundary (this handoff)

At 7e54405d the baseline is re-certified clean: host goldens bit-exact both flags
(gates 1–2), app builds both flags exit 0 (gate 3). No forward code landed this session
because (a) the split is monolithic (§0) — there is no smaller independently-bit-exact
commit than the whole log, and (b) GL correctness is device-only-verifiable, so a
partial log would be an unverified/broken flag-ON path, which the directive forbids.
The next agent implements §1+§2 as one atomic change behind `LITEV_RENDER_THREAD`,
device-screenshot-gated, then §4. This recipe + the design doc are the full spec.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>

---

## 8. STRATEGIC UNLOCK — Record-and-Immediately-Replay (RIR) breaks the monolith

Two implementation sessions stalled on §0's "monolithic / no incrementally-bit-exact
sub-piece" finding. The finding is true for the DEFERRED log, but there is an
intermediate mode that IS incrementally verifiable and de-risks ~80% of the surface:

**RIR mode** (a bring-up sub-mode of LITEV_RENDER_THREAD): each converted call site
records its op to the log AND immediately replays it from the recorded snapshot at the
SAME frame-point — instead of deferring to SubmitFrame. Properties:
- Bit-exact PER OP by construction: the op runs at the same moment with the same live
  state, merely routed through record→replay-from-snapshot. No deferral, no shadow yet
  (VRAM is read live at the same instant).
- Therefore you convert ONE call site at a time and device-screenshot-verify each: if
  flag-ON+RIR still renders identical, that op's record/replay/payload-copy plumbing is
  proven. This validates the entire error-prone mechanical surface incrementally.

**Phased plan:**
- PHASE 1 (RIR bring-up): convert every §1.2 call site to record→immediate-replay,
  device-verifying per site / small batch. Done when flag-ON+RIR is pixel-identical and
  ALL GL flows through the log. Most of the work, fully verifiable, no shadow.
- PHASE 2 (deferral flip + shadow): flip replay to SubmitFrame-time; add the Stage-B
  VRAM/palette shadow (§2). This isolates the ONE remaining risk (within-frame state
  mutation) onto an already-proven log. After this, RunFrame emits ~zero GL → the
  flag-ON RunFrame reading is the authoritative app core floor. Still single-thread.
- PHASE 3 (thread): §4 render thread + depth-1 queue. Next session.

RIR is a scaffold (record+immediate-replay costs a hair more than inline but is
correctness-equivalent); it can be a runtime sub-prop used only during bring-up, or
removed once Phase 2 lands. This is the method the next implementer uses.
