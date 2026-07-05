# liteDS-v2 — IN-RACE gameplay benchmark (host + A55), menu vs race

Date: 2026-07-04
Companion to `results-rgds-2026-07-04.md`, which measured the **boot/intro/menu**
sequence (600f from direct boot, no input). That workload is *not* gameplay —
this doc re-measures the architecture stack on an **actual Quick Race** (live 3D
rasterization + GXFIFO + DMA every frame).

## Completeness / status

- **Host in-race matrix: COMPLETE** — 5 configs × frameskip{0,9}, median of 3,
  quiet-host guarded. This is the robust dataset and carries the analysis.
- **Device (RG DS / A55) in-race matrix: PARTIAL (stopped at user request).**
  Only **rep 1** landed, and only for baseline/dispatch/link. `es`, `full`, and
  reps 2–3 were not collected. Device numbers below are **single runs, not
  medians** — directional only. What remains to finish the device deliverable is
  listed in §6.

Everything upstream of the matrices is complete and committed: the race input
script, the golden trace (double-record byte-identical, verifies bit-exactly on
baseline/dispatch/link), the savestate workflow, and both determinism gates.

## 1. The race workload (PPM-verified)

`tools/headless/baselines/shrek-race.script` drives Shrek Smash n' Crash Racing
from direct boot through a **button-only** menu path into a live Quick Race, then
holds A (accelerate). No touchscreen input is used or required — every menu is
button-navigable. Milestones read off PPM framebuffer dumps:

| frame | screen (PPM-verified) |
|------:|------------------------|
| ~900  | main menu, "Single Player" highlighted |
| ~960  | SELECT GAME TYPE (Tournament) → DOWN,DOWN → Quick Race |
| ~1080 | QUICK RACE track select ("Swamp" default) |
| ~1150 | SELECT CHARACTER ("Shrek" default) |
| ~1230 | SELECT PROFILE ("DEFAULT") |
| ~1320 | "Loading…" |
| **~1560** | **RACE LIVE** — HUD "1st" / "Lap 1/3", P1–P4 list, minimap, live 3D |
| 1600  | savestate dump frame **F** (kart on the start line) |
| 1600+ | A held: kart accelerates; race timer runs; AI karts race (minimap) |

Example in-race frames (hashes from the exact-timing `link` build, software
renderer, FreeBIOS direct boot, fixed RTC; PPMs were viewed, not committed):

- frame 1600 (top/bottom): `7cbb…` HUD "1st", kart at the start line, live 3D track.
- frame 2601: top=`e273d069c34c921e` bottom=`a49af41ad9db9363` — mid-race,
  timer running, kart driving down-track.
- frame 3399: "4th", timer "00:27.76", AI karts spread around the minimap.

A-only drives the kart forward; it parks against a wall by ~frame 2400 while the
live 3D race (AI karts, timer, full per-frame 3D redraw) continues. The measured
window (see §3) is the **active-driving** portion, before the park.

### Gates

- **Script determinism:** two script-replay runs → identical frame hashes at
  F+600 (frame 2200: top=`7c2d6ad0e16fcab9` bottom=`97a8740177abdc3d`). PASS.
- **Golden trace:** `shrek-race-3400.trace` (exact-timing config, 3400f)
  double-recorded byte-identical; reproduces bit-exactly (3400/3400 frames) on
  baseline, dispatch, and link via `--verify-trace`. PASS.
- **Savestate-load determinism:** two savestate-loaded runs (host and device) →
  identical frame hashes. PASS on both.
- **Savestate ≡ script (no distortion):** savestate-loaded frame N reproduces
  script frame 1601+N **to the frame-hash** (loaded f1000 == script f2601,
  both `e273d069c34c921e`/`a49af41ad9db9363`); and savestate-loaded window FPS
  (413.75) ≈ script-replay same-window FPS (408.10), ~1.4%. The
  script-once/savestate-many shortcut is exact, not merely close.

## 2. Benchmark workflow: script-once / savestate-many

Replaying ~1540 frames of boot+menus every run is wasteful. Instead: bake a
race-start savestate once (`--dump-savestate 1600:…`), then load it and measure a
steady in-race window many times (`--savestate … --bench-window 60:960`), holding
A so the kart drives during the measured frames. The savestate embeds
copyrighted RAM contents so it is **gitignored** (`savestates/`, `*.mln`) and
regenerated locally from the committed script — see
`tools/headless/baselines/README-shrek-race.md` for the one-command recipe.

New harness knobs (isolated in `tools/headless/main.cpp`):
`--bench-window <s>:<e>` (avg FPS over frames [s,e] only, still runs all frames;
reported in summary + JSON) and `--dump-savestate <f>:<path>`.

**Window = frames 60:960** (900 frames, ~15 s emulated). This is the
active-driving window. Note the later window 60:1860 measured ~6 % *higher* FPS
(424 vs 399 on host full) because the kart is parked there and the scene goes
near-static — the 60:960 window is the more representative gameplay load. Host
and device use the identical window.

## 3. HOST in-race results (Apple Silicon, arm64) — COMPLETE, median of 3

Window FPS, `--bench-window 60:960`, savestate-loaded, quiet-host guarded.
(Fastmem is inert on macOS — `IsFastMemSupported` is false under `__APPLE__`, and
the harness confirms `fastmem_active=no` even when requested — so fm-on ≡ fm-off
on host; not re-run.)

| config   | fs0 (median) | fs9 (median) |
|----------|-------------:|-------------:|
| baseline | 356.13 | 678.84 |
| dispatch | 358.88 | 698.85 |
| link     | 357.32 | 683.26 |
| es       | 375.51 | 771.26 |
| full     | 398.47 | 847.39 |

Stage-over-stage (each vs previous stage):

| stage added   | in-race fs0 | in-race fs9 |
|---------------|------------:|------------:|
| dispatch      | +0.8 %  | +2.9 %  |
| linking       | −0.4 %  | −2.2 %  (noise) |
| event slices  | **+5.1 %** | **+12.9 %** |
| mem Tier A    | **+6.1 %** | +9.9 %  |
| **full vs baseline** | **+11.9 %** | **+24.8 %** |

### Host: menu vs race, full vs baseline

| workload | fs0 baseline→full | fs9 baseline→full |
|----------|------------------:|------------------:|
| menu (old, single runs, fs0 only) | 1046.9 → 1254.0 = **+19.8 %** | — |
| race (this doc, median of 3)       | 356.1 → 398.5 = **+11.9 %** | 678.8 → 847.4 = **+24.8 %** |

Absolute FPS collapses ~3× menu→race (baseline 1047→356) — the software 3D
renderer is now the dominant per-frame cost.

## 4. DEVICE in-race results (RG DS / Cortex-A55) — PARTIAL, rep 1 only, single runs

Window FPS, same window, `--mode jit`. **Not medians** — one run per cell,
stopped mid rep-1. Clock stayed pinned at **1.992 GHz** for every run (no
throttle); SoC temp ranged 53–70 °C (hotter than the menu workload's 45–48 °C —
in-race is a heavier sustained load). The link fs9 cell ran at a 66.9 °C start /
70 °C end and reads low (44.9) — treat as a hot outlier.

| config   | fastmem | fs0 | fs9 |
|----------|---------|----:|----:|
| baseline | on  | 28.82 | 58.52 |
| baseline | off | 27.41 | 54.79 |
| dispatch | off | 25.81 | 56.87 |
| link     | off | 25.94 | 44.90 * (hot outlier) |
| es       | off | —    | —    (not collected) |
| full     | on/off | — | —  (not collected) |

### Device: menu vs race (menu numbers from `results-rgds-2026-07-04.md`, medians)

| workload | baseline fs0 | baseline fs9 |
|----------|-------------:|-------------:|
| menu (fm off, median of 3) | 76.79 | 122.21 |
| race (fm off, rep 1 single) | 27.41 | 54.79 |

Absolute A55 in-race baseline is ~27 FPS at fs0 (below 60) and ~55 at fs9 — i.e.
the software renderer makes Shrek **sub-full-speed in-race at fs0 on a single
A55 core**, unlike the menu workload (77 FPS). This alone re-frames the earlier
"comfortable full-speed headroom" conclusion: that headroom was a menu artifact.

## 5. Analysis — where does the stack pay in real gameplay?

**Verdict (host, robust): the CPU stack pays LESS in-race at fs0, MORE at fs9.**

1. **At fs0 the renderer dilutes the CPU-stack win.** full-vs-baseline drops from
   +19.8 % (menu) to **+11.9 %** (race). In-race, live 3D rasterization is a large
   fixed per-frame cost that none of the JIT/scheduler/mem work touches, so the
   same CPU savings are a smaller fraction of a bigger frame. The ~3× absolute
   FPS collapse (1047→356) is the renderer share growing, exactly as predicted.
2. **At fs9 the stack pays MORE (+24.8 %).** Rendering 1 frame in 10 amortizes
   the renderer away and the run goes CPU-bound again — where the stack's gains
   concentrate. This mirrors the menu-era "gains grow when CPU-bound" finding and
   confirms frameskip is the lever that re-exposes the architecture win.
3. **Event slices stay the dominant single stage** (+5.1 % fs0, +12.9 % fs9),
   consistent with menu. Dispatch/link remain flat-to-noise as enablers for ES.
4. **Mem Tier A pays MORE in-race than in menu** (+6.1 % fs0 vs +4.4 % menu).
   This is the one stage whose share *grows* in gameplay — consistent with the
   heavier in-race DTCM block-transfer / DMA traffic (3D matrix + vertex data)
   that Tier A's inline block path targets. A real, if modest, gameplay-specific
   signal.

### Fastmem verdict (corrected workload) — PRELIMINARY but flagged

The menu-workload "fastmem is a wash on A55" conclusion is **suspect** and the
in-race rep-1 data **contradicts it**:

| baseline | fs0 | fs9 |
|----------|----:|----:|
| fastmem **on**  | 28.82 | 58.52 |
| fastmem **off** | 27.41 | 54.79 |
| on − off        | **+5.1 %** | **+6.8 %** |

In-race, fastmem-on beat fastmem-off at both frameskips on the one baseline rep
we have. This is directionally what the corrected reasoning predicted: gameplay
has more MainRAM **STORE** traffic than menus, and stores are exactly what M3
Tier A does **not** inline (no raw-store path) while fastmem *does* cover them.

**If this holds up under medians and on the `full` config, it changes the app
recommendation.** The app currently ships **fastmem-off for stability**; a
consistent ~5–7 % in-race win would justify prioritizing the signal-handler fix
and the fastmem-on path there. **This must be confirmed** — it rests on a single
rep of one config, and the decision-relevant cell (`full` ± fastmem, where Tier A
already covers the loads/blocks) was not measured. Do not act on it yet.

## 6. To finish the device deliverable (if resumed)

Re-run `tools/android-bench/run-race.sh` (committed): the scoped fastmem
matrix — {baseline, full} × fastmem{on,off} × fs{0,9} plus dispatch/link/es
fm-off × fs{0,9}, **medians of 3**, 30 s thermal gaps, temp/clock sampled, and a
co-tenant-app R-state guard. ~40 min of device time. The two cells that most need
data: **full ± fastmem** (the app-default decision) and **es** (to confirm the
dominant ES stage on A55 in-race). Watch the ~70 °C cells for throttling if the
device runs hotter unattended.
