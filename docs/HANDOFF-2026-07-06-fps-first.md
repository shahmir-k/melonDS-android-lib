# liteDS-v2 Session Handoff — 2026-07-06 (FPS-FIRST pivot)

Supersedes the exactness-constrained framing of HANDOFF-2026-07-06.md. Read that one +
plan `docs/liteDS-v2-plan.md` Appendix D.7 addenda 1–27 for the full history. THIS doc is
the pivot: the directive changed, so the backlog is re-scored.

## 0. THE PIVOT (read this first)
**User directive (2026-07-06, verbatim intent): "I DON'T CARE ABOUT BREAKING EXACTNESS —
ALL I CARE ABOUT IS FPS."**

This overturns the campaign's #1 non-negotiable. The entire prior campaign gated every core
change on bit-exact golden traces and REFUSED any lever that changed emulated timing. That
constraint is now LIFTED. Consequences:
- **The pass/fail gate changes.** No longer "bit-exact vs golden trace." The new gate is:
  (1) the game still BOOTS, reaches the race, and is visually PLAYABLE (user visual check +
  no crash/hang over a few-minute in-race session), and (2) MEASURED FPS improves, cooled,
  on the heavy 8-kart in-race scene. Golden traces become a smoke test (did it still run?),
  not a correctness bar. Screenshot/visual parity is now "good enough", not "insufficient".
- **The practical floor** (not exactness, but reality): approximations that CRASH or HANG the
  game, or desync it into a softlock, are still failures. "Don't care about exactness" ≠
  "don't care if it runs." Aggressive CPU-cycle skipping is the highest-risk lever for this.
- **MULTIPLAYER: CONFIRMED STILL REQUIRED (user, 2026-07-06).** MP was the entire project
  thesis and stays in. Therefore FPS levers that break ARM7/WiFi timing are OFF THE TABLE
  (they'd desync MP). The permitted set is the **MP-SAFE subset**: ARM9-side, render,
  GPU3D geometry, and multi-core parallelism — none of which touch ARM7/WiFi. Concretely this
  RULES OUT: coarse/relaxed scheduling, ARM7 frame-dropping, any WiFi/IPC timing change. It
  KEEPS IN: everything in §3a/§3c that lives on the ARM9/GPU/render side. "Exactness
  expendable" now means *visual/GPU/ARM9-timing* exactness — NOT ARM7/WiFi timing.

## 1. Where FPS actually goes on the HEAVY scene (the wall)
The target is the heavy 8-kart in-race scene (slot-2 savestate), 3x GL, on the Anbernic
RG DS (4×A55, throttles 1.99→1.6GHz @83°C within ~20s — passive cooling IS the sustained
constraint). Measured reality (handoff §3, orchestrator-verified):
- **This scene is EMULATION-bound: ~24ms runFrame, ~31fps, with OR without the render thread.**
  The R4 render thread (landed, correct) gives ~0 here because there's little render to
  overlap — the CPU emulation IS the frame. (R4 helps only LIGHTER scenes, ~14ms runFrame,
  where render is a real fraction — addendum 9's "emulation already fits 60, render is the
  gap" was that lighter regime; the heavy scene is the opposite.)
- Of app-relevant emu-compute, **ARM9 dominates (~55%)**; GPU3D ~5%, ARM7 ~4%, DMA ~4%.
- **The DraStic gap (addendum 9): we waste 3 of 4 cores.** DraStic spreads emulation + a
  software-NEON renderer across all 4 A55s; liteDS runs emulation on ~1 core + render on a
  2nd (R4). On the heavy scene, one core is pinned at ~24ms while three sit idle.
- Thermal compounding: less work per frame = less heat = higher sustained clock = more FPS.
  Every ms cut pays twice.

**Bottom line: to move the heavy scene you must cut EMULATION time or parallelize emulation
across the idle cores. Everything below is scored against that.**

## 2. Current shippable state (unchanged this session)
- Branch `liteDS-v2` @ **b52d2459**. Shippable core = loads-only sw-table + widened register
  pin = **−9.67% ARM9** vs fault-based fastmem (measured, in-race, cooled). This is the whole
  landed core win; it already sits at ~31fps on the heavy scene.
- Landed+ON in the ship config (`swtable-pin`): JIT_DISPATCH+LINK*, EVENT_SLICES,
  MEM_DTCM_BLOCK, MEM_MAINRAM_LOAD, MEM_SWTABLE(loads), FIXEDREG, GLOBALREG, plus
  NEON_RENDERER, AGGRESSIVE_SKIP, R4 RENDER_THREAD (on liteDS-v2-android), GL_STATE_CACHE.
- All flags default OFF; the app turns a subset ON. See §6.

## 3. Backlog RE-SCORED under FPS-only

### 3a. RE-OPENED — were closed ONLY to preserve timing-exactness (now fair game)
- **GXFIFO twin-stream accumulate-then-drain** (plan addendum 27, doc04). Closed because
  draining the whole display list at once changes the cycle-metered FIFO level → IRQ/DMA
  timing. Under FPS-only you CAN drain the geometry command list in one branchless loop,
  killing the per-command dispatch (271ns/cmd) + de-interleaving cmd/param streams for
  D-cache locality. Files: src/GPU3D.cpp. Potential: MEDIUM (geometry is only ~5% of the
  heavy frame, but the 8-kart scene is geometry-heavy; and NEON-geometry math was already
  landed-but-small because *dispatch* dominated — this attacks dispatch directly). Risk:
  visual glitches on GX-timing-sensitive effects; gate = looks right + faster.
- **DMA bulk block movers** (doc09). Closed because DMA.cpp Run9()/Run7() is per-unit
  cycle-metered (interleaves with the CPU, breaks at ARM9Target). Under FPS-only, bulk-memcpy
  the whole RAM→RAM transfer and batch the cycles. Potential: LOW-MED (DMA ~3.7% of frame).
  Files: src/DMA.cpp.

### 3b. STAYS CLOSED — closed for reasons OTHER than exactness (do NOT re-chase)
- **Store-side sw-table** (LITEV_MEM_SWTABLE_STORE): bit-exact BUT measured a net ARM9
  REGRESSION on the A55 (+~3–6%; inline per-store table-chase+SMC check > predicted
  SlowWrite on the in-order core). Exactness was never the reason. Flag exists, default OFF.
- **RELAXED_ARM9 timing** (LITEV_RELAXED_ARM9_TIMING): the obvious "make emulation less
  accurate to go faster" lever — but MEASURED −4.8% FPS (SLOWER), because melonDS bakes cycle
  timing at compile-time so a runtime relaxed model ADDS work. Counter-intuitive; do not
  expect this to be a win. (It's the cautionary tale: "break exactness" only helps when it
  REMOVES work, and melonDS's exact path is often already the cheap one.)
- **Palette/OAM COW shadow**: attacks the 2D software-rasterizer bucket, which is HEADLESS-
  ONLY. The app uses the GL renderer, so this bucket is ~0 on device regardless of exactness.

### 3c. NEW FPS frontier — approximation levers the old constraint forbade entirely
These were never in the DraStic backlog because they're not "correct emulation" — they're
approximations. Now permitted. Ranked by expected heavy-scene FPS impact:
1. **Multi-core emulation / bigger off-core split (the M6.6 hybrid + beyond)** — HIGHEST.
   The heavy scene is single-core emulation-bound with 3 idle cores. DraStic's actual speed
   comes from using all 4. Move more off the emu thread: R4 already offloads GL submission;
   next is the 2D compositor + parts of GPU3D geometry to helper threads (DraStic's soft-NEON
   banded-raster model), and ultimately splitting ARM9/ARM7/GPU3D across cores. This is mostly
   PARALLELISM, not approximation — it doesn't even need the exactness waiver (it was in
   "reserve", not "closed") — and it's the single biggest lever. Big effort; start here.
2. **Emulation frameskip (skip whole emulated frames, not just rasterization)** — HIGH but
   RISKY. AGGRESSIVE_SKIP currently skips only 2D/3D *rasterization* (--frameskip N). True
   frame-dropping of CPU work (advance game logic at reduced rate / interpolate) can ~double
   throughput but risks input lag, physics desync, softlocks. Gate hard on "still playable".
3. **3D geometry LOD / aggressive culling / lower sub-pixel precision** — MED. Cut GPU3D +
   ARM9 matrix work on the 8-kart scene (fewer polys processed). Visible quality loss.
4. **Aggressive idle/poll-loop skip** — LOW for Shrek (<0.15ms busy-wait) but general.
5. **Lower internal 3D resolution** — the single biggest RENDER lever, BUT the user previously
   rejected it ("horrible UX"). Re-confirm before doing — under "all I care about is FPS" it
   may now be acceptable, but it's a UX call only the user makes.

## 4. Recommended next steps (FPS-first, MP-SAFE order — MP confirmed required)
1. **Ground the current number end-to-end**: build the app with the shippable core (loads-only
   +pin) merged into liteDS-v2-android (R4), measure cooled heavy-scene FPS. Establishes the
   real baseline the new levers move. (Needs the melonDS-android app repo — NOT cloned locally;
   see §5.)
2. **Attack the emulation wall via cores (lever 3c.1)** — the honest biggest win AND fully
   MP-safe (parallelism, no ARM7/WiFi change). Scope the next off-core split (2D compositor /
   GPU3D geometry to helper threads, DraStic's soft-NEON banded-raster model). Start here.
3. **Cheap MP-safe experiments in parallel**: GXFIFO accumulate-drain (3a) + a RENDER-side /
   GPU3D-side frameskip or 3D-LOD prototype (3c.2/3c.3) — all ARM9/GPU-side only. Each a
   default-OFF flag, measured cooled on device, gate = boots + playable (user check) + faster.
4. Re-confirm internal-res (3c.5) with the user if 2–3 don't reach the target.
NOTE: emulation frameskip (3c.2) must skip only RENDER/ARM9-side work — NOT ARM7 or the WiFi
scheduler — to stay MP-safe. ARM7/timing-breaking variants are excluded by the MP decision.

## 5. Device / build gotchas (STILL APPLY — the physical constraints didn't change)
- **Device**: Anbernic RG DS, adb serial e0ca3841840f2ab9. adb-injected GAME INPUT IS DEAD
  (only Android UI taps work) → drive gameplay via deterministic savestates + input-scripts,
  NOT live adb input. USER is the final visual/playability check.
- **Measure FPS COOLED**: force-stop the co-tenant emu (me.magnum.melonds.dev), wait
  thermal_zone0 < 52000 (52°C), take the first stable window. Interleave A/B reps + re-cool
  between them (thermal drift dwarfs the effect otherwise). Host is Apple-Silicon arm64 = the
  real ARMJIT_A64 codegen, but host is OUT-OF-ORDER and HIDES in-order-A55 wins → PERF IS
  DEVICE-ONLY. (This is why store-side "looked flat" on host but regressed on device.)
- **Android headless build**: tools/android-bench/build.sh (NDK r27 at
  /Users/shahmir/android-sdk/ndk/27.0.12077973, API 34, arm64-v8a, LITEV_PROFILE=ON for
  arm9_exec_ns). Device bench dir /data/local/tmp/liteds (rom shrek.nds, savestate
  shrek-race.mln, hdata firmware dir already provisioned). Harness: tools/android-bench/
  run-race.sh (in-race window 60:960). Profile keys: `window_fps:`, `arm9_exec_ns_per_frame:`.
- **APP build (for felt FPS)**: the melonDS-android app repo (github.com/shahmir-k/
  melonDS-android, branch liteDS-v2-app-r4) is NOT cloned locally. Full path = clone it, merge
  liteDS-v2 core into liteDS-v2-android (clean FF), point the app submodule at it, then
  MANDATORY clean build: `rm -rf app/.cxx app/build/intermediates/cxx && ./gradlew clean
  :app:assembleGitHubProdDebug` (AGP ships a STALE .so otherwise — burned multiple agents),
  verify packaged .so mtime > edit. R4 render props: debug.litev.renderthread / earlyrelease
  / rtserial / fbhash.
- **Verify YOURSELF; don't trust agent claims** (they've given false passes, false regression
  panics, stale-.so builds, an over-reported 55fps). Run each golden/measurement as its own
  command (shell loops caused false failures). For the NEW gate, "yourself" = build it, run
  it on device cooled, read the raw FPS, and have the USER eyeball playability.
- Reinstall a KNOWN-GOOD build on the device after measuring; don't leave a broken/baseline
  build that confuses the user.

## 6. Repos / branches / flags
| What | Branch @ head |
|---|---|
| Core mainline (this work) | `liteDS-v2` @ **b52d2459** (3 unpushed local commits: 8ef81c3d store impl, 5cd17c5e store split-off, b52d2459 docs) |
| Core + R4 render thread | `liteDS-v2-android` @ 4492c0e1 (pre this session's core; re-merge mainline = clean FF) |
| App (R4, full) | `liteDS-v2-app-r4` @ 1d7746a8 (repo not local) |
| v1 reference (NEVER modify) | ~/Documents/GitHub/quickmelonDS (standalone, separate) |

Flags (all default OFF): the exactness-preserving stack is landed (see §2). New FPS-first
flags should follow the same convention (default OFF, own name, A/B-able), but gate on
playable+faster, not bit-exact. `LITEV_MEM_SWTABLE_STORE` and `LITEV_RELAXED_ARM9_TIMING`
exist but are measured-negative — leave OFF.

## 7. One-paragraph resume prompt for next session
Continue liteDS-v2 under the NEW directive: **FPS is the only goal; visual/GPU/ARM9 exactness
is expendable, but MULTIPLAYER STAYS IN (user-confirmed) so ARM7/WiFi timing is UNTOUCHABLE.**
The heavy 8-kart in-race scene is emulation-bound (~24ms, ~31fps) + thermal-throttled, with 3
of 4 cores idle — that's the wall. Biggest lever is MP-safe: MULTI-CORE emulation/render
(DraStic's model, pure parallelism) — scope the next off-core split (2D compositor / GPU3D
geometry to helper threads). In parallel, cheap MP-safe approximation experiments now permitted:
GXFIFO accumulate-drain, RENDER/GPU-side frameskip, 3D LOD — each a default-OFF flag, gated on
"boots + playable (user visual check) + measurably faster COOLED on device", NOT on golden
traces. Do NOT: touch ARM7/WiFi timing (kills MP); re-chase store-side sw-table or RELAXED_ARM9
(both measured SLOWER, not exactness) or palette-COW (GL-moot). Verify every FPS number yourself
on device, cooled; host hides in-order-A55 effects. Ground the baseline with an end-to-end app
build first (app repo needs cloning, see §5). Full history: plan D.7 addenda 1–27 +
HANDOFF-2026-07-06.md; this pivot: HANDOFF-2026-07-06-fps-first.md.
