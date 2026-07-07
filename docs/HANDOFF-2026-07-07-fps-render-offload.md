# liteDS-v2 Handoff — 2026-07-07 — FPS-FIRST / FULL RENDER-PREP OFFLOAD

Paste this whole file as the opening prompt of a fresh session. It supersedes the MP-preserving
framing of HANDOFF-2026-07-06.md and HANDOFF-2026-07-06-fps-first.md. Read those + plan D.7 for
history, but THIS directive governs.

---

## 0. THE DIRECTIVE (from the user, verbatim intent — do not soften)
**FPS is the ONLY priority right now. We are WILLING TO BREAK MULTIPLAYER to get it.** Multiplayer
sync will be **re-implemented later as a separate layer** — a route back must exist, but do NOT
preserve MP timing now. Break bit-exactness, GXSTAT timing, ARM7/WiFi timing, whatever — all fair
game if it buys FPS. The pass/fail gate is: **(1) game boots, reaches the heavy race, is visually
PLAYABLE (user checks on device), and (2) measured FPS improves, cooled, on the heavy 8-kart scene.**
The only hard floor: it must still RUN (no crash/hang/softlock).

**Work autonomously. Do not stop to ask permission or re-litigate settled decisions. Measure on the
device — do NOT guess magnitudes.** (The prior session burned the user's patience by reasoning from
stale numbers instead of measuring, and by re-deriving facts the handoffs already documented.)

---

## 1. THE TARGET — offload the ENTIRE render prep onto the idle CPU cores
The heavy 8-kart in-race scene is ~24ms/~31fps on the RG DS (4×A55), **one core pinned, three idle.**
DraStic hits 60 by spreading work across all 4 cores; we pin emulation on one. The lever the user
has repeatedly named:

> **Move the ENTIRE render prep off the emulation thread onto helper cores, so the emu thread does
> ONLY CPU emulation (ARM9 / ARM7 / DMA / timers / SPU).**

"Render prep" = everything the emu thread currently does to prepare a frame that is NOT core CPU
emulation:
- **GPU3D geometry** — `SubmitVertex` (transform) + `SubmitPolygon` (clip/cull/setup) + the
  `GPU3D::VBlank` polygon sort. (~16% of the app frame — see §2 sizing note.)
- **2D per-scanline capture** — `GPU2D::DrawScanline` / the compositor capture half (R4 design
  §3.2 explicitly leaves this on the emu thread).
- **VRAM flatten** — `MakeVRAMFlat_*` (dirty-granule flatten the renderer consumes).
- **3D render-register snapshots**, capture config, etc.

Together these are the **~8-9ms of render prep** the user keeps pointing at. R4 (the landed render
thread) offloads only the **GL submission** (`ReplayLog`, `GLOp::Render3D`/`FinalPassSpan`), NOT the
prep above. **Offloading all the prep → emu thread ~14-15ms → ~40-45fps.** That is THE lever.

**Architecture to build:** extend R4's record/replay. The emu thread RECORDS the raw inputs each
prep stage reads (this session already proved geometry is fully reconstructable from a resolved-
input log — see §3); a helper/render thread REPLAYS geometry-transform + 2D-capture + VRAM-flatten,
then does the existing GL submission. The emu thread keeps only an APPROXIMATE cycle model for GXSTAT
(exact one needs the transform result; approx is fine now — MP is expendable). This is the M6.6
"DraStic-style full render offload" hybrid, now unblocked by the break-MP waiver.

---

## 2. GROUNDING FACTS the prior session got wrong — do NOT repeat these
- **DO NOT quote the headless "GPU3D 5%" for the app.** In HEADLESS the software rasterizer eats
  ~63% (`RunSystemNs`) and masks everything; the app uses **GL**, so that 63% vanishes and the
  render prep reweights up: **GPU3D geometry ≈16% of the ~24ms app frame + 2D capture + VRAM flatten
  ≈ the 8-9ms the user cites.** The app is render-prep-heavy on the emu thread; the handoff's "5%"
  was a headless artifact.
- **Heavy scene is ARM9-bound (~55%) AND render-prep-heavy (~35%).** ARM9 is a single JIT (can't
  parallelize its instruction stream) → the offloadable win is the render prep + the small parallel
  emulation bits (ARM7, DMA), not ARM9 itself.
- **The app repo IS local** (prior session wrongly claimed it wasn't, wasting hours). See §4.
- **"MP-safe by construction" is FALSE** for the render-prep offload — the geometry timing (GXSTAT)
  depends on the cull/clip result, so offloading the transform forces an approximate timing model
  that shifts ARM9 pacing → breaks MP. That is now ACCEPTED. Do not chase an exact-timing offload.

---

## 3. WHAT'S ALREADY DONE THIS SESSION (foundation — build on it, don't redo)
On core branch `liteDS-v2` (`melonDS-profiler/quickmelonDS`), all default-OFF, pushed:
- **Store-side sw-table RETRY WON**: −3-4% ARM9, device-measured cooled, bit-exact
  (armwrestler/rockwrestler SMC + shrek goldens). `LITEV_MEM_SWTABLE_STORE`. (The prior "closed-
  negative" was an impl bug; DraStic-faithful single-table + SMC-folded-delta + reg-resident base
  won.)
- **Geometry offload G1b (PROVEN)** `LITEV_GEOM_OFFLOAD`: a resolved-input event log
  (BEGIN{polygonMode} + VERTEX{CurVertex, ClipMatrix, TexMatrix, VertexColor, TexCoords,
  RawTexCoords, TexParam, CurPolygonAttr, Viewport}) recorded in `SubmitVertex`, replays through the
  EXACT `SubmitVertex`/`SubmitPolygon` and reproduces geometry **0 mismatches / 635 frames**. This
  PROVES the whole geometry stage is reconstructable from a captured log — the template for offloading
  every other prep stage the same way.
- **Geometry offload G2** (commit a88db0e8): emu-inline records + skips the transform (approx
  `SubmitPolygonTiming`), `ReplayGeometry` fills the REAL bank at VBlank before the sort. Host-verified:
  bottom screen bit-identical; shrek-600/armwrestler/rockwrestler goldens pass; race stays exact to
  frame 865 then drifts (approx-timing accumulation — accepted). **Same-thread (no win yet)** — it's
  the correctness step; the win comes from putting the replay on a helper core (G3), which needs the
  app/R4 (host headless renders synchronously so it can't show the overlap).

**Next step for geometry:** don't just thread geometry alone (~5% headless / ~16% app) — fold it
into the FULL render-prep offload (§1). Same record/replay pattern, extended to 2D capture + flatten.

---

## 4. REPOS / DEVICE / BUILD (exact, verified this session)
| What | Path | Branch | Notes |
|---|---|---|---|
| **Core dev (edit here)** | `~/Documents/GitHub/melonDS-profiler/quickmelonDS` | `liteDS-v2` | store-side + geometry offload live here; headless `build.sh` device path |
| **R4 render-thread core** | same repo | `liteDS-v2-android` | R4 render thread (`ReplayLog`/`LogBuild` GL-submission offload) |
| **APP (GL, R4) — IS LOCAL** | `~/Documents/GitHub/melonDS-android-app` | `liteDS-v2-app-r4` | cloned this session; gradle app; submodule `melonDS-android-lib` @ `4492c0e1` = the R4 core |
| **v1 READ-ONLY reference** | `~/Documents/GitHub/quickmelonDS` | v1 | DO NOT modify, DO NOT confuse with the app (it has app/ + a v1 lib) |

- **Device test = headless (core/ARM9 levers):** `tools/android-bench/build.sh <config>` cross-
  compiles the headless binary to arm64/A55; `tools/android-bench/run-race.sh` runs it over adb vs
  the **`savestates/shrek-race.mln`** heavy scene, cooled, `window_fps`. Software renderer — good for
  ARM9/DMA/GXFIFO, NOT for the render-prep-offload win (headless render is synchronous).
- **Device test = app (the render-prep/FPS win):** build the gradle app in `melonDS-android-app`.
  **MANDATORY (AGP caches stale .so):** `rm -rf app/.cxx app/build/intermediates/cxx && ./gradlew
  clean :app:assembleGitHubProdDebug`; verify packaged .so mtime > your edit. To land a core change
  in the app: port/merge `liteDS-v2` core → the app's `melonDS-android-lib` submodule (mostly
  disjoint files; conflicts only in shared headers, take both), then rebuild.
- **Device is measured COOLED** (force-stop, wait `thermal_zone0` < 52000, take first ~30
  `LITEV_PROF` lines). `adb`-injected game input is DEAD on this device — correctness is the FBHASH
  gate on a deterministic savestate; the USER is the final playability check.
- **zsh gotcha:** unquoted `$FLAGS` does NOT word-split → cmake silently drops flags → vacuous
  "verified" builds. Use `${=FLAGS}` and `grep FLAG:BOOL build/CMakeCache.txt` to confirm.
- Host goldens (Apple Silicon arm64 = the shipped A64 backend) gate CORRECTNESS; PERF is device-only.

---

## 5. THE BACKLOG — everything now unblocked by "break MP for FPS", ranked
1. **FULL RENDER-PREP OFFLOAD (§1) — THE lever.** Geometry + 2D per-scanline capture + VRAM flatten
   → render thread, via extended R4 record/replay. Emu thread = CPU only. Target ~40-45fps. Big
   effort; this is the priority. G1b/G2 (§3) are the geometry third, already built.
2. **Emulation frameskip** — skip frames, set frameskip to 1
3. **ARM7 on its own core** — run ARM7 parallel to ARM9 (breaks WiFi timing → breaks MP → now OK).
   ~4% + removes a serialization point. Core-side.
4. **GXFIFO accumulate-then-drain** — drain the whole GX command list in one branchless loop, kill
   the ~271ns/cmd per-command dispatch + de-interleave cmd/param streams for D-cache. `src/GPU3D.cpp`.
   Was closed only for FIFO-timing-exactness. Medium.
5. **ARM9 DMA bulk block-movers** — bulk-memcpy RAM→RAM DMA instead of per-unit cycle-metered.
   `src/DMA.cpp`. Low-med (DMA ~4%).
6. **3D geometry LOD / lower sub-pixel precision / aggressive cull** — cut GPU3D + ARM9 matrix work
   on the 8-kart scene. Med, visible quality loss (now acceptable).
7. **Lower internal 3D resolution** — biggest RENDER lever but the user previously rejected ("horrible
   UX"). Re-confirm with the user before doing; may be acceptable under FPS-first.

**DO NOT re-chase (measured losses, not MP-blocked):** RELAXED_ARM9 timing (−5%, melonDS bakes
timing at compile-time so a runtime relaxed model ADDS work); NEON_GEOMETRY (+1%, dispatch-bound);
R2 deferred-blit / R3 GL-diet (0-regressive). Palette/OAM COW = headless-only, moot for the GL app.

---

## 6. MULTIPLAYER — deferred, re-added later (do not preserve now)
The whole render-prep offload + approx GXSTAT timing + ARM7-on-own-core will DESYNC multiplayer.
That is accepted. A two-instance headless LocalMP harness already exists (Phases 0-2: two NDS in one
process share one LocalMP, Shrek associates, 130 packets) at `tools/headless/` — it's the scaffold to
LATER re-implement/verify MP sync as a separate layer once FPS is won. Do not spend effort keeping MP
alive during the FPS work; just keep the code paths present so re-sync is possible.

---

## 7. FIRST MOVES for the fresh session
1. Confirm the device is plugged in (`adb devices`). If not, tell the user — nothing is measurable
   without it, and guessing is what went wrong before.
2. In `melonDS-android-app/melonDS-android-lib`, read `src/GPU_OpenGL.cpp` `ReplayLog()`/`LogBuild`
   + `RenderThread.cpp` to learn the record/replay + depth-1 handoff you'll extend.
3. Scope the FULL render-prep offload (§1): what the emu thread records per stage (geometry log
   already designed in §3; add 2D-capture inputs + VRAM dirty-flatten), what the render thread
   replays. Design the packet + the approx GXSTAT model. Then build it behind a flag, default OFF.
4. Port the store-side + geometry offload from `liteDS-v2` into the app core; gradle-build (§4 clean
   step); measure cooled heavy-scene FPS vs the current app baseline. Establish the real number, then
   land the full offload and re-measure.
5. Stack the core-side levers (§5.2-5.5) in `liteDS-v2`, each default-OFF, measured via `build.sh`.

Everything default-OFF; measure cooled on device; the user is the playability gate.
