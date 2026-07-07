# liteDS-v2 — Render-prep off-core offload: implementation scope (2026-07-07)

The performance gap on the heavy 8-kart scene is **render-prep on the emu-thread critical path**,
not the ARM9 core (see docs/liteDS-v2-geometry-parallel-scope.md + the R4 finding: `SubmitVertex`
geometry transform runs on the emu thread in `ExecuteCommand`, and per-scanline 2D capture "stays
on the emulation thread" — R4 only threads the raster/blit). DraStic runs its whole software
render, geometry included, on 3 helper cores → its critical path is just emulation (~13ms → 60fps).
This scopes moving our render-prep off the emu thread onto the idle cores, DraStic-style.

Two sub-projects, independent value, do the geometry one first (dominant on dense scenes):
**G) geometry-transform offload** (primary — scales with polygon count = the 8-kart cost), and
**T) 2D-capture offload** (secondary — light on 3D-heavy scenes).

## The MP-safety key (why this is safe where GXFIFO-drain wasn't)

`ExecuteCommand` does two things per GX command: (1) the **cycle/pipeline model**
(`VertexPipeline`, `PolygonPipeline`, `GXStat`) which **ARM9 reads back** (GXSTAT poll) and which
gates the GXFIFO IRQ/DMA → **timing, MP-adjacent**; and (2) the **geometry math** (`SubmitVertex`
transform, `ClipPolygon`, polygon assembly) which only the **renderer** consumes.
**The offload keeps (1) exactly where it is — on the emu thread, cycle-identical — and moves only
(2) to a helper core.** So ARM9's emulated timeline, GXSTAT, and IPC are byte-for-byte unchanged →
**MP is unaffected by construction.** This is the crucial difference from GXFIFO accumulate-drain
(which relaxed the timing model itself). Only the *wall-clock* moment the geometry math runs
changes; the emulated result is identical (same fixed-point code, just on another core).

## G — Geometry-transform offload

### What runs where
- **Emu thread (critical path):** read `CmdFIFO`; run the cycle/pipeline model + `GXStat` exactly
  as today; **execute the matrix-stack ops** (push/pop/load/mult — cheap) so it always holds the
  live matrix state; **execute test commands synchronously** (`POS_TEST`/`VEC_TEST`/`BOX_TEST`,
  0x50/0x60/0x70 — ARM9 reads their results, so they cannot defer); **record** each geometry-
  producing command (VERTEX 0x23-0x2A, BEGIN/END 0x40/0x41, and the matrix-state deltas) into a
  double-buffered **GX command log**.
- **Helper core (extend the R4 render thread):** replay the command log → maintain its own matrix
  state by replaying the matrix ops → run the expensive `SubmitVertex` transform + `ClipPolygon` +
  `SubmitPolygon` → fill the geometry bank → then the existing `BuildPolygons` + raster + blit.

### Key design decisions
- **Matrix state on both threads:** matrix ops are cheap and both sides need the live matrix (emu
  for tests, helper for transform), so both replay them. Only the per-vertex math (the ~2.3ms) is
  helper-exclusive. Record matrix ops inline in the command log in stream order.
- **Reuse melonDS's existing geometry double-buffer** (VertexRAM/PolygonRAM bank toggle + R4's
  depth-1 SPSC handoff + early `bankReleased`): the command log is a *third* double-buffered arena;
  the helper consumes log[frame] while the emu records log[frame+1]. Emu blocks only if the helper
  is behind (dense frame) — the exact behavior R4 already implements for the geometry bank.
- **Test commands stay synchronous:** they're rare and cheap; ARM9 correctness requires it. They
  read the emu-thread matrix state (which is why the emu thread keeps executing matrix ops).

### Phases
- **G0 (analysis, done):** confirmed transform is on the emu thread, timing is separable, tests
  must stay sync.
- **G1:** refactor `ExecuteCommand` so the per-command handler cleanly separates cycle-accounting
  (keep on emu) from geometry math (make callable from a replay pass). The `LITEV_GXFIFO_BATCH`
  jump-table handlers already isolate per-command bodies — split each body's "timing" vs "geometry"
  statements. Default-OFF flag `LITEV_GEOM_OFFLOAD`.
- **G2:** command-log recording on the emu thread (double-buffered), matrix-op + vertex/poly
  recording; test commands synchronous.
- **G3:** replay + transform on the render thread; write the geometry bank there instead of the
  emu thread.
- **G4:** wire into R4's bank handoff (emu blocks depth-1 on dense frames); flag ON in the app.

### Expected win / risk
- Win: the geometry transform (~2.3ms on the 8-kart scene, and it *scales with polygon count* — the
  reason the heavy scene is heavy) leaves the emu critical path onto an idle core. Combined with
  R4's raster offload, the emu critical path approaches emulation-only (~13ms) — the DraStic profile.
- Risk: correctness of the timing/geometry split (the helper's geometry output must equal today's
  inline output — verifiable by the FBHASH gate: threaded geometry == serial); the record/replay
  overhead on the emu thread (must be << the transform it removes — it is: recording is a memcpy of
  params, transform is matrix math + clip). **MP: safe by construction (timing model untouched).**
- Gate (FPS-first): boots + playable (user) + FBHASH threaded==serial + faster cooled on device.

## T — 2D per-scanline capture offload (secondary)

R4 design line 527 already flags this: "2D compositor needs a capture/submit split." Today the
per-scanline register/palette/OAM/VRAM capture (`UpdateAndRender`) stays on the emu thread.
- **Emu thread:** snapshot each scanline's 2D state (DISPCNT/BGCNT/scroll/palette/OAM/VRAM-dirty)
  into a per-scanline ring — cheap, and it already computes most of this for the dirty check.
- **Helper core:** the DraStic-T3 model — composite the 192 scanlines from the snapshots
  (`RenderScreen` bands) off the emu thread.
- Lower priority: light on the 3D-heavy 8-kart scene; do it after G proves the pattern + win.

## Bottom line
G is the lever that matches DraStic's actual architecture and the idle-core instinct: move the
scene-scaling geometry transform off the emu critical path onto a helper core, with the MP-critical
timing model left exactly in place. It's a real project (record/replay split of the geometry
engine), but it's the honest fix for the 2× dense-scene gap, and it's MP-safe by construction so it
doesn't even need the MP harness to ship (though the harness remains the backstop for any future
timing-touching lever).
