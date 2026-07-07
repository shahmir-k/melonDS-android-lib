# liteDS-v2 — Multi-core split: MP-safe parallelization scope (2026-07-06)

Deep-scope requested before writing any code. Conclusion up front, evidence below.

## TL;DR (revised after the 13-vs-24ms correction)

Two facts are solid; one thing is genuinely unresolved.

SOLID:
1. **DraStic's multi-core usage is RENDERING (2D compositor + 4-way 3D software raster), NOT
   emulation.** Its emulation is one serial thread (teardown 07 §0/§1/§6). So "parallelize
   emulation" is the wrong framing — you can't split one CPU's instruction stream, and
   ARM7/GXFIFO/IPC timing is MP-untouchable.
2. **The right target for a helper thread is RENDER-PREP CPU** (the DraStic-T3 model), not
   emulation. Our GL renderer composites on the GPU, but there is still per-scanline 2D CAPTURE +
   GPU3D GEOMETRY submission running on the emu thread that feeds it.

UNRESOLVED (and I previously got this wrong): **how much render-prep CPU is on the emu thread for
the HEAVY scene.** Addendum 15 measured ~8ms (→ multi-core worth ~6-8ms); the handoff says R4
gives ~0 on the heavy scene (→ render-prep is small there). The docs contradict; headless can't
settle it. The whole go/no-go on multi-core hinges on this one on-device app measurement.

Do NOT trust the earlier draft of this doc that said "13ms emulation + GPU-bound, multi-core dead"
— that double-counted a retired sub-bucket figure. The frame is ~24ms and CPU-bound.

## Evidence

### DraStic's thread topology (teardown 07 §0, §1, §6)
- T2 EMU CORE (one thread): `ARM9 slice → ARM7 slice → event_update → GPU/SPU/DMA`. Serial.
- T1 GL: blit only.
- **T3: 2D-engine software scanline renderer** (192 lines of one 2D engine).
- **T4/T5/T6 + T7 coordinator: 4-way 3D SOFTWARE rasterizer** (scanline bands).
- Generic pool (T8+): background/util only, explicitly "NOT the per-frame emu critical path."
- Teardown's own ranked "most actionable": items 1–4 are ALL render offload. Emulation split
  is not on the list — DraStic doesn't do it.

### Our app renders on the GPU (GPU_OpenGL.cpp)
- `Rend2D_A/B = GLRenderer2D(...)` — 2D compositing on GPU.
- `Rend3D = GLRenderer3D / ComputeRenderer3D` — 3D raster on GPU.
- So DraStic's T3–T7 work (2D + 3D software raster) has no CPU analogue on our app.

### CORRECTION — the heavy-scene frame is ~24ms EMULATION, not 13ms (do not repeat this error)
The established number for the heavy 8-kart scene is **~24ms runFrame, emulation-bound** (HANDOFF
2026-07-06 §1/§3; R4 gives ~0 there). Plan addendum 15 ALREADY corrected the exact miscount an
earlier pass (and I) made: summing only the arm9/gpu3d/arm7/dma sub-buckets (arm9 6.9 + gpu3d 2.3
+ arm7 1.9 + dma 1.7 ≈ 13ms) EXCLUDES the ~8-9ms of **per-scanline event dispatch + 2D-render-prep
CPU** that also runs serially on the emu thread. The full serial emulation floor on the heavy
scene is ~22-24ms, NOT 13ms. There is no "missing 19ms" and the scene is NOT GPU-bound — that was
a fabricated mystery from double-counting the already-corrected 13.5ms figure.
Buckets (heavy scene, serial on the emu thread):
- arm9_exec       ~7ms    ← dominant; store/loads sw-table already chip this
- per-scanline event dispatch + 2D-render-prep CPU  ~8-9ms  ← LARGE; see open question below
- gpu3d geometry  ~2.3ms  ← GXFIFO-timing-coupled
- arm7 + dma      ~3.6ms  ← MP-critical / cycle-metered, must not move
- SPU + residual  remainder

## What can move off-core MP-safely — the honest candidate list

| Candidate | Size | Parallelizable? | MP-safe? | Verdict |
|---|---|---|---|---|
| ARM9 emulation | ~6.9ms | NO — one instruction stream | — | Impossible |
| ARM7 emulation | ~1.9ms | Split from ARM9? | NO — IPC/timer/WiFi sync | Off-limits (MP) |
| DMA | ~1.7ms | NO — cycle-metered, interleaves CPU | NO | Off-limits (timing) |
| GPU3D **geometry** | ~2.3ms | Maybe (producer/consumer on GXFIFO) | RISKY — GXFIFO level gates IRQ+DMA→ARM9 timing→IPC | Only real candidate; marginal + risky |
| 2D compositor | 0 on app | already on GPU | — | N/A (app) |
| 3D raster | 0 on app | already on GPU | — | N/A (app) |

**The ONLY off-core emulation candidate is GPU3D geometry (~2.3ms), and it is both small and
timing-fragile** (the same GXFIFO cycle-metering that closed the GXFIFO drain in addendum 27:
`CmdFIFO.Level()` gates the GX IRQ and DMA). Best plausible case if it fully overlapped ARM9:
~2ms/frame — and it would need a snapshot/handshake like R4, plus a proof it doesn't shift
IPC timing. High effort, high MP-risk, ≤2ms upside. Not worth starting blind.

## The REAL open question: how much of the 24ms is parallelizable render-prep vs serial emulation?

The heavy-scene frame is ~24ms and CPU-bound (established; NOT GPU-bound). The unresolved
question is the SPLIT of that 24ms — and here the docs CONTRADICT each other:
- **Addendum 15/16 (lighter/deferred scene):** render CPU on the emu thread ≈ **8ms** (2D
  per-scanline 4.7 + 3D raster 3.7 + flatten 0.4 + cfg 0.2). Deferring it drops the core floor to
  ~13.2ms → R4 threading → ~60fps@3x. i.e. ~8ms IS parallelizable render-prep (DraStic-T3-style).
- **HANDOFF-2026-07-06 (heavy 8-kart scene):** R4 gives **~0** — "render is a real fraction only on
  LIGHTER scenes (~14ms runFrame); the heavy scene is the opposite," ~24ms is mostly emulation.
These can't both describe the heavy scene. Either (a) the heavy scene's render-prep is small (so
multi-core buys little — my original conclusion), or (b) it's ~8ms (so a DraStic-T3-style 2D/geom
worker thread IS worth ~6-8ms — multi-core viable). **This is not settled in the docs and cannot
be settled from headless.** The decisive measurement is an on-device APP per-frame partition of
the heavy scene: serial-emulation (ARM9/ARM7/DMA/event-dispatch, unmovable) vs render-prep CPU
(2D per-scanline + geometry submission, movable to a helper core).

NOTE: this supersedes an earlier draft of this doc that claimed the frame was "13ms emulation +
19ms mystery / possibly GPU-bound" — that was a double-count of the already-retired 13.5ms
sub-bucket figure (addendum 15). The frame is ~24ms and CPU-bound; the only open part is the
movable/unmovable split above.

## If it IS CPU/ARM9-bound (case C): the real DraStic gap

DraStic's serial emulation is faster than ours because of JIT quality we have NOT fully ported —
all MP-safe (they don't change emulated timing), all cheaper than a multi-core rewrite:
- **Full per-block liveness** (explicit live-regs + live-flags masks → dead-flag/dead-reg
  elimination). Teardown 01 §11 ranks this DraStic's **#1 highest-ROI** trick. We have partial
  elision (CONDFOLD/FIXEDREG) but NOT the full liveness pass. Directly cuts arm9_exec.
- **Full r0–r14 register pin** (we stop at r0–r7). More guest ALU ops map 1:1, fewer spills.
- Specialised LDM/STM per-register-count stubs.

These attack the ~6.9ms ARM9 bucket that dominates the app CPU path — the same bucket the
store-side win (−3–4%) just chipped. They are the honest continuation, NOT multi-core.

## Corrected recommendation

The frame is ~24ms CPU-bound. Whether multi-core helps depends ENTIRELY on the movable/unmovable
split of that 24ms, which the docs report inconsistently (addendum 15's ~8ms render-prep vs the
handoff's R4-gives-0 on the heavy scene). So:

1. **Settle the split on the heavy scene** — on-device APP partition: serial emulation
   (ARM9/ARM7/DMA/per-scanline event dispatch — unmovable, MP-coupled) vs render-prep CPU (2D
   per-scanline capture + GPU3D geometry submission — potentially movable to a helper core, the
   DraStic-T3 model). Needs the melonDS-android app repo (not local). This is the decisive number.
2. If render-prep on the heavy scene is ~6-8ms → **multi-core (2D/geom worker thread) IS worth
   building** (DraStic's actual T3/T4-7 model applied to our render-prep, not to emulation).
3. If it's small (R4-gives-0 story) → multi-core buys little; the lever is the serial emulation
   buckets: ARM9 (chipped by store/loads), the ~8-9ms per-scanline event-dispatch bucket
   (investigate — is it reducible beyond EVENT_SLICES?), and JIT quality.

Correction vs the earlier draft: I previously concluded "multi-core is dead, ≤2ms upside" from
the wrong premise that the app renders everything on the GPU with only 13ms of serial emulation.
The 2D-render-PREP (per-scanline capture feeding the GL compositor) is CPU work on the emu thread
and may be several ms — so multi-core is NOT clearly dead. It is UNRESOLVED pending the split.
