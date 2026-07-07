# liteDS-v2 — GPU geometry offload + render-pipeline parallelization scope (2026-07-07)

Analysis-only (no code, no tests). Two asks: (A) scope moving the 3D geometry transform off
the CPU; (B) find render-pipeline work that can go to the 3 idle A55 cores.

## The load-bearing discovery: the geometry engine is TWO separable things

`GPU3D.cpp` (renderer-agnostic, runs on the emu thread as ARM9 feeds GXFIFO) does:
1. **A cycle-timing model** — `AddCycles`, `VertexPipeline`/`PolygonPipeline`/`VertexSlotsFree`,
   `StallPolygonPipeline`. This drives `GXSTAT`/GXFIFO level, which **ARM9 reads** and which gates
   the GXFIFO IRQ/DMA. It is cheap (counter math) but **MP-adjacent** (it shapes ARM9 timing) →
   MUST stay on the CPU, serial.
2. **The actual transform + clip + setup** — `SubmitVertex` (fixed-point `MatrixMult4x4` against
   ClipMatrix, per vertex), `ClipPolygon`/`ClipSegment` (fixed-point frustum clip, variable output),
   `SubmitPolygon` (attributes, Y-sort). This is the **~2.3ms** and is **embarrassingly parallel /
   output-timing-independent** — the timing was already accounted in (1).

**This separation is the whole opportunity.** The expensive part (2) has no data dependency on
the timing model (1); (1) just needs to run to keep GXSTAT correct. So (2) can move to the GPU or
to helper cores while (1) stays serial on the emu thread.

## Part A — GPU-side vertex transform (the recommended lever)

**Shader-portable (move to GPU):** per-vertex position transform (ClipMatrix mult), texcoord
transform, per-vertex color/normal lighting math. All independent per vertex → a vertex shader.
**Frustum clip** (`ClipSegment`) can move to the GPU's fixed-function clip (near-plane) or a
geometry/compute pass; the DS's exact fixed-point clip won't match bit-for-bit but **FPS-first
makes visually-close acceptable** — this is precisely what changed the calculus.

**Must stay CPU:** the cycle-timing model (1) [GXSTAT/MP], and the polygon **Y-sort + attribute
setup** if we want DS-exact ordering (sortable on GPU but fiddly; can stay CPU cheaply).

**Mechanism:** instead of `SubmitVertex` writing *transformed* clip-space verts into VertexRAM,
write *raw* model-space verts + the current matrices into a per-polygon buffer; upload that +
matrices; the vertex shader transforms. `GPU3D_OpenGL::BuildPolygons` (which today marshals
pre-transformed verts) becomes a thinner raw-vertex upload.

**Expected CPU cut:** ~2.3ms (the geometry transform bucket) offloaded to the **idle GPU**.
**MP risk: NONE** — the transform math is not MP-coupled; the timing model stays CPU/serial.
**Effort:** large (rewrite the `GPU3D`→`GPU3D_OpenGL` vertex path + shaders; handle lighting,
texcoord modes, w-buffer depth, polygon attributes). **Accuracy:** fixed-point→float + GPU clip
= visual differences, gated on "boots + playable + looks right" not golden traces.
**This is the cleanest, MP-safest, biggest single render-pipeline lever.**

## Part B — Parallelizing render onto the idle cores (honest ceiling first)

Two hard constraints bound this, and they must be stated plainly:

**Constraint 1 — the GL context is single-threaded.** Only one thread may issue GL calls. So the
GL *draws/uploads* (RenderScreen, BuildPolygons upload) cannot be split across cores — R4 already
put them on ONE render thread (core #2). Cores #3/#4 can only take **CPU-side render PREP**, then
hand results to the single GL thread.

**Constraint 2 — the dominant cost is SERIAL emulation, which no core count can split.** Of the
~24ms heavy frame, ~13–16ms is ARM9 (~7ms) + ARM7 (~1.9) + DMA (~1.7) + per-scanline event
dispatch — a single cycle-ordered timeline, MP-coupled (ARM7=WiFi, IPC ordering). DraStic runs
this serially too. **This is the floor; the 3 idle cores cannot go below it.**

**What CPU render PREP is actually parallelizable onto cores #3/#4:**
| Work | Size (heavy scene) | Parallel? | Notes |
|---|---|---|---|
| 3D geometry transform | ~2.3ms | YES (per-vertex) | Same bucket as Part A — do it on GPU OR banded across cores, not both |
| BuildPolygons marshal | small | YES (per-polygon) | Low payoff |
| 2D per-scanline capture | light on 3D scene | partial | GL-single-thread limits it; DraStic-T3 model |
| GL draws/upload | — | NO | single GL context; already on R4's core #2 |

**So the realistic idle-core win is the geometry transform (~2.3ms) and little else** — and moving
it to the GPU (Part A) is cleaner than banding it across cores (banding needs GXFIFO decoupling =
timing/MP risk; the GPU route has none). The other render prep is small or GL-serialized.

**The blunt truth you asked me not to sugar-coat:** the 3 idle cores are *not* ~10ms of parked
performance. They're idle because (a) rendering moved to the GPU and (b) the remaining bottleneck
is serial emulation. The honest recoverable render-CPU via parallelism/GPU-offload is **~2–3ms**
(24ms → ~21–22ms), which helps thermals and stacks with the store/loads ARM9 wins, but is **not**
a path to 60 by itself. The path to 60 would additionally need to cut the serial emulation floor
(ARM9 already chipped; the ~8ms event-dispatch/scheduler bucket is the next serial target to
analyze), which is a separate front.

## Part C — Can the cycle-timing model itself be parallelized? (analyzed, not assumed)

Analyzing every angle rather than asserting "serial":

1. **Parallelize just the counters?** No value — the timing model (`AddCycles` + pipeline
   counters) is a handful of integer ops, not a cost bucket. Splitting it saves ~0 while adding
   sync overhead > the work itself. Amdahl says don't.
2. **Data-dependency parallelism?** Each command's pipeline state (`VertexPipeline`,
   `VertexSlotsFree`) is derived from the previous command's state — a strict recurrence. There is
   no independent work to overlap *within* the timing model. Serial by construction.
3. **Run the WHOLE geometry engine (timing + transform) on a helper core, parallel to ARM9?**
   This is the real question behind the ask. The barrier is `GXSTAT`: ARM9 **reads** the geometry
   busy-flag / FIFO level to pace its GXFIFO writes. For a helper core to run geometry in parallel,
   ARM9's `GXSTAT` reads must see cycle-accurate geometry state → the two cores must lock-step at
   every `GXSTAT` read → that serializes them again (they'd spend more time synchronizing than
   computing). The ONLY way to actually run them in parallel is to **let ARM9 see approximate/stale
   `GXSTAT`** (relaxed geometry timing). Consequences:
   - Under FPS-first, stale GXSTAT is visually acceptable (expendable).
   - BUT it shifts ARM9's timeline (it paces differently) → shifts IPC → **potential MP desync**.
     This is exactly the class of change the MP test harness exists to gate.
   - The WIN is the same **~2.3ms** geometry-transform overlap as Part A — but via a CPU helper
     core WITH the GXSTAT/MP risk, versus Part A's GPU route which has **none** (GPU offload doesn't
     touch ARM9 pacing at all; the timing model stays exactly where it is, serial and cheap).
   ⇒ Parallelizing the timing model is not a NEW or bigger win — it's a **riskier path to the same
   2.3ms** the GPU route gets cleanly. The GPU route dominates.

**And the broader floor, since the ask implies it:** the same lock-step-or-relax dilemma applies to
every serial bucket. ARM9's instruction stream is one CPU (unsplittable short of speculative
rollback — no DS emulator does this; SMC + memory ordering make it unsafe). ARM9↔ARM7 on two cores
needs lock-step at every IPC/shared-mem touch (frequent) and ARM7 is the WiFi/MP core. DMA and event
ordering are cycle-metered. **Every one of these can only be "parallelized" by relaxing a timing
coupling that feeds ARM9 — which risks MP and buys the overlap only where there's real work (there
isn't much outside geometry).** The honest floor holds: geometry (~2.3ms) is the one parallelizable
compute bucket; the rest is serial not by laziness but by data/timing dependency that DraStic shares.

## Recommendation (ranked, MP-safe)
1. **GPU-side geometry transform (Part A)** — ~2.3ms to the idle GPU, zero MP risk, FPS-first
   permits it. The one render lever that matches the "this isn't optimal" instinct. Big but bounded.
2. Do NOT band geometry across CPU cores instead — same bucket, worse risk (GXFIFO/MP), and it
   competes with (1). Pick the GPU.
3. Accept that idle-core parallelism alone tops out ~2–3ms; the bigger remaining lever is the
   serial ~8ms event-dispatch/scheduler bucket (next analysis), not more render threads.
