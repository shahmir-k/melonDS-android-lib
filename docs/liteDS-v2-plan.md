# liteDS v2 Plan — Dispatch-Core Rebuild on a Fresh melonDS Branch

Status: proposed
Date: 2026-07-04
Predecessor: `liteDS-opt-plan.md` (v1), `OPTIMIZATION_HISTORY.md`, `drastic-vs-melonds-analysis.md`

---

## 1. Guiding Thesis

v1 proved, incrementally, that every reduction in JIT↔C++ boundary crossings wins:

- bounded chaining: +6–10% per step
- wider scheduler slices: +7–8% per step
- trace superblocks: +9–21% per step
- compiling trace groundwork out of production: +79% on the non-profiled harness

But each step was built *around* the existing dispatch architecture, adding machinery
(chain helper, trace recipes, promoted traces) to approximate what a linking JIT gets
structurally. The remaining wall is not JIT codegen quality — it is **how often
generated code stops and re-enters the C++ world, and what each re-entry costs**.

Two structural taxes, measured against the current tree:

**Tax 1 — per-block-transition cost.** Every block→block transition, even a chained
one, calls `ARM9_ContinueBlock` (`ARMJIT.cpp:71`). In a production build that helper
performs: three branch checks, a runtime HLE probe (`ARM9LibHLE.TryHandle` — on every
transition, and again in the dispatch loop at `ARM.cpp:633`), a region check,
`LookUpBlock` (hash walk), then `MaybePromoteTrace` — two more hash lookups plus a
counter (`ARMJIT.cpp:1785–1798`). When chaining misses (128-cycle budget, target
reached, etc.) the exit falls back to `ARM_Ret`, which spills/reloads 96 bytes of
callee-saved registers (`ARMJIT_A64/ARMJIT_Linkage.S`), then re-enters the full C++
dispatch loop at `ARM.cpp:622`. At 1–2M block transitions per emulated second and
~100ns per transition, dispatch overhead alone consumes 10–20% of a core before any
emulation happens.

**Tax 2 — per-slice scheduler cost.** With `kMaxIterationCycles = 512` (`NDS.cpp:54`),
a frame is ~1,094 scheduler iterations. Each does `NextTarget()`, a GPU3D poll,
`RunSystem`, and a condvar round-trip to the ARM7 thread **with zero actual overlap**:
`NDS.cpp:1293–1310` runs ARM9 to target, then notifies the ARM7 thread, then
immediately blocks waiting for it. ARM9 and ARM7 never execute simultaneously.
That is ~2,200 futex crossings per frame (1–5µs each when the fast path misses) paid
as bus fare for sequential execution.

DraStic's binary confirms the alternative architecture works on DS-class hardware:
the strings `recompiler_cpu_next_action_arm9_to_arm7` and
`recompiler_cpu_next_action_arm7_to_event_update` show the recompiled-code world
owning the scheduler hot path, with C++ handling only cold events. Dolphin does the
same for a single-CPU console (patched block links, emitted-asm dispatcher, downcount
checked in generated code).

**v2 inverts v1's approach: build the dispatch architecture first, then port the
validated orthogonal wins on top.** Most v1 JIT-side machinery becomes unnecessary
rather than ported.

---

## 2. Base Branch and Ground Rules

- **Branch from the clean upstream `melonDS-android-lib` sync point** — the state
  `LITEV_UPSTREAM_DELTA.md` measures against — not from the current fork. Rebasing
  onto upstream releases stays viable, and upstream WiFi/timing accuracy fixes keep
  flowing in (existing sync policy already declares these priority).
- Keep the v1 discipline that worked:
  - `LITEV_*` CMake flags, all defaulting OFF; build is upstream-identical with no flags.
  - The divergence-log format in `LITEV_UPSTREAM_DELTA.md`.
  - Benchmark gate per change; production-build (`LITEV_PROFILE=off`) numbers recorded
    alongside profiled numbers (v1 Phase 10 showed they can diverge by 80%).
  - The rejected-experiments table in `OPTIMIZATION_HISTORY.md` carries over as
    standing constraints.
- **Two validation gates for every milestone:**
  1. **Perf gate** — the Shrek harness, profiled and production builds.
  2. **WiFi/MP gate** — a scripted two-instance local-multiplayer regression test
     (lobby handshake + N minutes of synced gameplay; Pictochat plus at least one
     real MP game) that must pass before anything ARM7- or scheduler-adjacent merges.
     v1 never automated this; with ARM7 in scope it is non-negotiable.

### Critical requirement

**WiFi and local multiplayer functionality must be upheld.** Concretely:

- ARM7 instruction/cycle timing stays exact (no relaxed-timing mode for ARM7, ever).
- WiFi MMIO paths stay exact slow paths; no fastmem over the WiFi register range.
- `Event_Wifi` scheduling cadence (`Wifi.cpp:328`, 1MHz `USCounter`) is untouched.
- Any change to ARM9/ARM7 interleaving must preserve event ordering at sync
  boundaries and pass the MP gate bit-exact.

---

## 3. Milestone 0 — Harness First

Port `LiteProfile` (`LiteProfile.h` + `LITE_PROFILE_*` call sites) and the benchmark
harness onto the fresh branch before any optimization. This is also the substrate the
melonDS-profiler project consumes (frame-by-frame section breakdown, lag-spike
attribution).

New counters v2 needs from day one:

- block-transition count split by kind: linked-direct / dispatcher-hit / dispatcher-miss
  / full C++ re-entry
- mean cost per transition kind
- scheduler iterations vs. actual events fired per frame
- time-in-JIT vs. time-in-C++ ratio per frame
- `ARM7WaitNs` equivalent for whatever ARM7 execution model is active

Exit criterion: harness runs on the clean branch, baseline numbers recorded for the
benchmark suite.

---

## 4. Milestone 1 — Dispatch Core Rewrite

Flag: `LITEV_JIT_LINKING` (1.2–1.4 land together behind it; 1.1 is independent).

### 1.1 Compile-time HLE hooks (first: small, independent, immediate)

Today `ARM9LibHLE.TryHandle()` runs on every block dispatch (`ARM.cpp:633`) and every
chain attempt (`ARMJIT.cpp:113`). Hook addresses are known constants after the
boot-time scan.

Exact change:

- In `CompileBlock`, query the hook table for the block's start address; for hooked
  addresses, emit the HLE handling inline in the block prologue.
- Flush the JIT block cache once after `ScanAndRegister()` (hooks register at boot,
  blocks compile afterward — ordering is already safe).
- Delete both runtime probes.

### 1.2 Emitted-assembly dispatcher

Today every block exit returns through `ARM_Ret` (96-byte callee-saved spill/reload)
into the C++ loop at `ARM.cpp:622`, which redoes the region check, last-block cache,
`LookUpBlock`, and `MaybePromoteTrace` per block.

Exact change:

- Add `ARM_Dispatcher` (and `ARM7_Dispatcher`) to `ARMJIT_A64/ARMJIT_Linkage.S`.
  It performs `LookUpBlock`'s job inline in asm — the fast lookup is a tag-compare on
  a flat `u64` array (`ARMJIT.cpp:2930–2944`), ~6 instructions: load
  `FastBlockLookupStart/Size` and the region base from fixed `ARM`-struct offsets
  (extend `ARMJIT_x64/ARMJIT_Offsets.h`), bounds-check, tag-compare, `br` into the
  block on hit.
- On miss only, `ARM_Ret` to C++ for `SetupExecutableRegion` / compile-on-miss.
- Block exits jump to the dispatcher instead of returning. The callee-saved frame is
  entered once per timeslice, not once per block.
- Delete the `LastJitBlockAddr/Entry` cache — the dispatcher is the fast case.

**Downcount model:** replace the per-dispatch `ARM9Timestamp < ARM9Target` C++ check
with a `CyclesRemaining` slot in the `ARM` struct:

- decremented in-register per block (cycles already pinned in `w28` / `RCycles`)
- reloaded and sign-tested by the dispatcher and by link-site guards:
  one `ldr` + `tbnz` per transition
- anything that must force an exit (event reschedule, IRQ raise, GXFIFO stall,
  DMA start) writes 0 to the slot; all of those happen in C++ code, so there is no
  cross-thread subtlety for ARM9.

### 1.3 Direct block linking (patch, don't call)

Replaces `ARM9_ContinueBlock`, the 128-cycle chain budget, trace recipes, and
promoted traces entirely.

Exact changes:

- `JitBlock` (`JitBlock.h`) gains:
  - up to 2 outgoing link records `{patchSiteOffset, targetAddr, targetMode}`
    (a direct exit has at most taken + fall-through)
  - a `TinyVector<LinkSite>` of incoming links
- In the A64 compiler, exits with a statically known same-mode target (the cases v1
  chaining already classifies: `ExitBranchARMImm`, `ExitBranchThumbImm`, conditional
  taken/fall-through) emit:

  ```
  ldr  w_tmp, [RCPU, #CyclesRemaining]
  tbnz w_tmp, #31, ->dispatcher_exit
  b    <patchable>          ; initially -> dispatcher
  ```

- **Link lazily:** when the C++ miss path compiles or finds the target block, patch
  the pending site to `b target_entry` (+ icache maintenance). A64 `b` reaches
  ±128MB — the whole code cache.
- **Unlink** in `InvalidateByAddr` (`ARMJIT.cpp:2792`) and `RetireJitBlock`: walk the
  dying block's incoming links, rewrite each patch site back to `b dispatcher`.
  `ResetBlockCache` unlinks for free (all code memory dropped). The invalidation path
  already calls `ClearJitCache()` on both CPUs; the incoming-link walk is not the
  expensive part.
- **IRQ delivery:** `TriggerIRQ`/`SetIRQ` run in C++ (events, MMIO writes) — they zero
  `CyclesRemaining`; the next link-site guard exits. Latency is unchanged from today
  because IRQs already only fire at C++ boundaries.

Deleted, not ported: `ARM9_ContinueBlock`, `MaybePromoteTrace` / promoted traces /
`BuildTraceRecipe`, linear-trace machinery. Keep the `Trace*` metadata fields under
`LITEV_PROFILE` only — they are good profiler taxonomy.

### 1.4 Indirect exits

`bx lr`, `pop {pc}`, and other dynamic targets jump to the dispatcher — with 1.2 that
is ~10 instructions and no C++. v1's stack-PC exit classification and BX-LR helper
chaining are superseded, not ported.

### M1 follow-up (conditional) — hot-region recompilation

Direct linking replaces trace superblocks' dispatch benefit, but not their codegen
benefit: inside a recompiled trace, the register cache kept guest registers in host
registers across former block boundaries and batched cycle accounting. With linking,
each block still flushes register state at its exit and reloads at the next entry.

If post-M1 profiles (M0 counters) show hot loops bottlenecked on boundary register
flush/reload, recompile hot regions as single large units *on top of* linking —
Dolphin's large-block approach. `MaxBlockSize=512` already covers straight-line code;
this targets loops spanning multiple blocks. The v1 trace-recipe plumbing
(`BuildTraceRecipe` feeding concatenated instrs into `CompileBlock`) is a working
prototype of exactly this and is the reference for the implementation, even though
the v1 runtime machinery itself is not ported.

Evidence-gated: do not build until the counters demand it.

### Milestone 1 exit criteria

- Per-transition cost measured at ≤ ~5ns linked / ≤ ~15ns dispatcher
  (vs. ~100ns+ today), via the M0 counters.
- Production-build FPS gate on the benchmark suite.
- **Interpreter-diff validation:** run JIT and interpreter modes over the benchmark
  and diff RAM/register checksums per frame (melonDS's interpreter mode provides
  this for free). Zero divergence required.
- MP gate pass (dispatch changes touch ARM7 blocks too).

---

## 5. Milestone 2 — Scheduler and Timeslice Restructure

### 2.1 Event-true slices (remove the fixed cap)

`kMaxIterationCycles = 512` forces ~1,094 iterations/frame regardless of event
density, yet `NextTarget()` already computes the true next event.

Exact change:

- Run ARM9 to the actual next target; delete the fixed cap.
- Mid-slice actions that create earlier deadlines call a new `ForceExecutionExit()`
  (zeroes the downcount slots). Exact call sites: `ScheduleEvent` when the new event
  is earlier than the current target; `CPUStop` writes (DMA start, GXFIFO stall);
  IRQ raises.
- Slice length becomes event-density-driven — often thousands of cycles fully inside
  generated code.

v1's "scheduler window slice" and 96/128-cycle slice experiments were fighting this
cap; they do not port.

### 2.2 ARM7 back inline

`NDS.cpp:1293–1310` has zero overlap and ~2,200 condvar crossings per frame of pure
overhead. Fold ARM7 execution back onto the main thread and give it the same
dispatcher/linking treatment as ARM9 (shared A64 compiler; own dispatcher label and
link bookkeeping, `num=1`).

WiFi safety: this **restores upstream's execution ordering exactly.** ARM7 cycle
counting, WiFi MMIO slow paths, and `Event_Wifi` cadence are untouched.

### 2.3 ARM7 idle detection (port from v1 — the WiFi-safe ARM7 win)

Port the v1 IPC-poll idle detection (`0x04000184` empty-FIFO counting) and SPI-poll
detection. Validated, documented in `LITEV_UPSTREAM_DELTA.md`, and safe by
construction: idle only advances the clock to the next scheduled event; interrupts
wake naturally. Combined with upstream's branch-to-self `IdleLoop` detection this
covers most real ARM7 time in single-player.

**Measure ARM7's share of frame time after this step before considering 2.4.**

### 2.4 ARM7 concurrency — explicitly last, evidence-gated

Trigger condition: post-2.3 profiles show ARM7 ≥ ~15% of frame time. Otherwise skip.

Design sketch if triggered:

- Run ARM7's slice on a worker concurrently with ARM9's *same* slice; join before
  `RunSystem`. Sync-boundary granularity is unchanged.
- Serialize (lockstep fallback for the slice) whenever:
  - the slice's ARM7 code touches shared WRAM, IPC registers
    (`0x04000180–0x0400018C`), or IF/IE — detectable at block-compile time via the
    same region classification fastmem uses (`TraceMemRegionMask` already exists), or
  - any WiFi event lies inside the slice.
- Honest framing: sequential-at-512-cycles is already not hardware-exact (ARM9's
  whole slice of writes becomes visible to ARM7 atomically); concurrency is
  differently-approximate, plus guards. The MP gate decides.
- **If the MP gate ever fails, this milestone dies and stays dead.**

---

## 6. Milestone 3 — Memory Fast Paths (port v1's biggest wins, reshaped)

v1 Phase 8 (DTCM/MainRAM inline patch thunks, tiny-slowblock fast exits: +30.7%,
+21.3%, +18.4%) is the most valuable v1 JIT-side content and is conceptually
independent of dispatch. **Port the concepts, re-implement the emission** on the new
exit structure — many v1 patches were shaped around the old helper boundaries, and
cherry-picking would fight the new codegen.

### Three-tier memory path structure

v2 makes the tiering explicit. Every emitted memory access is assigned one of three
tiers at compile time, by the same region classification fastmem already does
(`ClassifyAddress9/7`, `TraceMemRegionMask`):

- **Tier A — inline raw access.** Statically-known-safe cases (DTCM, stable MainRAM
  loads): emitted guarded loads/stores, as in v1 Phase 8. Budgeted — inlining pays
  only when guard cost < helper cost × hit rate (the v1 A55 lesson).
- **Tier B — custom-ABI helper stubs (new in v2).** Hand-written A64 stubs with a
  JIT-private calling convention: they preserve the JIT's live register state
  (`RCPU`, `RCPSR`, `RCycles`, register-cache contents), so a call costs a `bl` and
  nothing else — no C-ABI caller-saved spill/reload, no register-cache flush. One
  stub per shape (`store_block_3words_dtcm`, `load16_mainram`, ...), selected at
  compile time. This is the tier DraStic's binary shows heavily
  (`arm64_store_block1`–`16`, `arm64_load_memory8_unsigned`, per
  `drastic-vs-melonds-analysis.md`). It is the correct home for cases v1 found too
  branchy to inline but too hot for the C++ helper — the rejected "branchy
  read/write subsets" experiment failed precisely because its only two options were
  inline-everything or full C-ABI call.
- **Tier C — C++ helpers.** Genuinely cold or exactness-critical cases: MMIO,
  palette/OAM/VRAM, and all WiFi paths. Unchanged, exact, forever.

Current slow paths all go through `QuickCallFunction` (C ABI) — i.e., today there is
no Tier B; building it is the main new engineering in this milestone beyond the
Phase 8 re-port.

Order (the v1 gradient, then the new tier):

1. DTCM block-transfer fast path (Tier A)
2. Tiny/small DTCM slowblock inlining (Tier A)
3. Runtime DTCM hit detection in guarded raw transfers (Tier A)
4. MainRAM load hits in patch thunks (Tier A; stores keep invalidation-preserving
   fallback)
5. Tier B stub set for the hottest remaining helper-bound shapes, chosen from
   profiler slowmem-miss data

Standing constraints from v1 rejections: no MainRAM raw-store patch thunk (Tier A);
no branchy read/write inline subsets (route to Tier B instead); palette/OAM/VRAM/MMIO
stay Tier C exact.

---

## 7. Milestone 4 — Renderer Track (parallel, orthogonal)

Runs alongside M1–M3; touches no core-CPU code.

Port as-is (all v1-validated):

- NEON 2D paths (`GPU2D_NEON`)
- OpenGL identical-frame skip (`RenderFrameIdentical`)
- Partial 3D GL dirty-page uploads
- Compositor upload skip
- PBO async texture upload (`SoftwareRenderUploader`)
- SPU fast interpolation flag
- Aggressive-frameskip flag

New structural work:

- **Frame-stable 2D state model** (from `drastic-vs-melonds-analysis.md`): snapshot
  BG/OBJ/extpal flat state at frame start; stamp/dirty-range tracking on mid-frame
  writes and remaps; reuse stable state across scanlines; exact resync fallback for
  dirtied spans. Prioritize the top-screen 3D/2D interaction path. Do not revisit
  bottom-screen aux-plane cleanup or sparse-upload variants (v1 rejected).
- **Evaluate `GPU3D_Compute` on Android** (in-tree, included from
  `MelonInstance.cpp`; confirm reachability from the Android frontend). Moving 3D
  rasterization off-CPU entirely outclasses any CPU-side rasterizer optimization.

---

## 8. Milestone 5 (optional) — ARM9 Relaxed Timing Mode

Flag-gated flat cycles-per-instruction for ARM9 only, removing per-access wait-state
computation from emitted code (DraStic-style). **ARM7 timing stays exact permanently
(WiFi).** Do this last — measure whether M1–M3 already hit target before spending
accuracy budget.

---

## 9. Keep / Drop Summary from v1

| Disposition | Items |
|---|---|
| **Port directly** | LiteProfile + harness; NEON 2D; identical-frame skip; dirty GL uploads; compositor skip; PBO upload; SPU fast interp; CLZ `NextTarget`; timer-overflow-as-events; ARM7 IPC/SPI idle detection; `MaxBlockSize=512`; divergence-log/flag discipline; rejected-experiments list |
| **Port the concept, rewrite the code** | ARM9 library HLE framework (→ compile-time hooks); DTCM/MainRAM fastmem thunks (→ new exit structure) |
| **Superseded — don't port** | `ARM9_ContinueBlock` chaining + chain budget; trace recipes / promoted traces (kept as the reference prototype for the conditional M1 hot-region recompilation follow-up); last-block cache; stack-PC / BX-LR special-casing; scheduler window slice; 96/128-cycle slices; ARM7 condvar thread |
| **Re-measure before porting** | Lazy div/sqrt completion; dedicated LCD scheduler path; DMA mode mask (all interact with the M2 scheduler rewrite) |
| **Drop** | WiFi HLE skeleton (risk with no demonstrated payoff; keep `docs/wifi-protocol/` — the protocol analysis is good reference) |

---

## 10. Sequencing

```
M0 (harness + baselines)
 └─ M1.1 (compile-time HLE)            — independent, immediate
     └─ M1.2–1.4 (dispatcher + linking) — one coherent change behind LITEV_JIT_LINKING
         └─ M2.1 (event-true slices)
             └─ M2.2 (ARM7 inline)
                 └─ M2.3 (ARM7 idle)
                     └─ M2.4 (ARM7 concurrency) — only if evidence gate triggers
         └─ M3 (memory fast paths)      — after M1 stabilizes
M4 (renderer track)                     — parallel with all of the above
M5 (ARM9 relaxed timing)                — last, optional
```

Every milestone: perf gate + MP gate + divergence log entry.

---

## 11. Risk Register

| Risk | Mitigation |
|---|---|
| Block linking vs. self-modifying code (games that rewrite hot code) | Incoming-link unlink in `InvalidateByAddr`/`RetireJitBlock`; interpreter-diff validation mode; invalidation-heavy games profiled explicitly — if link/unlink thrashes, fall back per-region to dispatcher-only |
| IRQ latency change from longer slices | IRQs already only fire at C++ boundaries; `ForceExecutionExit()` bounds latency to one block; interpreter-diff catches ordering bugs |
| WiFi/MP regression from scheduler restructure | ARM7 timing and WiFi event cadence untouched by design in M1–M2.3; automated MP gate on every merge; M2.4 is kill-on-failure |
| A64 `b` patch races with execution | Patching happens on the emulator thread while it owns execution (no concurrent JIT execution of the patched CPU's code); icache maintenance per patch |
| Profiled vs. production divergence (v1 Phase 10 lesson: +79% from compile-out) | All profiling/trace metadata compile-time gated from day one; production numbers recorded at every gate |
| Rebase burden vs. upstream | Fresh branch from the upstream sync point; divergence log per change; renderer track isolated from core |

---

## 12. Expected Shape of the Payoff

v1 spent ten phases extracting roughly 2× through the old architecture's ceiling.
M1+M2 remove the ceiling itself:

- ~1–2M block transitions/sec at ~100ns+ → ~2–15ns (dispatch tax eliminated)
- ~1,094 scheduler iterations/frame → event-count-driven (often several× fewer)
- ~2,200 futex crossings/frame → 0 (until/unless M2.4 reintroduces a thread with
  actual overlap)
- ARM7 idle time skipped rather than executed

This is the architecture region where Dolphin-class emulators — and, per the binary
evidence, DraStic — live.

---

# Appendix A — M1.2 / M1.3 Exact A64 Emission Design

Code references are to the current fork tree (`melonDS-android-lib/src/`); the
upstream baseline has the same structures minus the LITEV additions noted.

## A.0 Current exit anatomy and three load-bearing findings

Every ARM9 block today ends with (`ARMJIT_A64/ARMJIT_Compiler.cpp:954–976`):
`RegCache.Flush()` → fold `ConstantCycles` into `RCycles` → store Cycles and CPSR to
memory → C-ABI call to `ARM9_ContinueBlock` → `CBZ`/`BR` on the result → fall back to
`ARM_Ret`. Mid-block conditional exits (`Comp_BranchSpecialBehaviour`,
`ARMJIT_Compiler.cpp:783–807`) emit the same shape inline.

**Finding 1 — the in-JIT "can I continue?" check costs 3 loads.** The v1 inline
experiments (`Comp_JumpToARM9DynamicSameARM`, `ARMJIT_Branch.cpp:384–390`) emit:
load `NDS` pointer, load `ARM9Timestamp`, add `RCycles`, load `ARM9Target`, compare —
five instructions, three dependent loads, per check. The budget-slot design collapses
this to one load and one compare.

**Finding 2 — v1 already prototyped the dispatcher and linking, with hardcoded game
addresses.** `Comp_ExitToARM9FastLookupTarget` (`ARMJIT_Branch.cpp:414`) is a full
inline fast-lookup (~25 instructions, 8 loads) gated by a literal list of eight
Shrek PC values baked into the compiler (`ARMJIT_Branch.cpp:505–512`), plus two more
at `:539–540` for the BX-LR path. M1.2/1.3 is this machinery generalized to every
exit, with the address lists deleted and the 3-load check replaced by the budget
guard.

**Finding 3 — `Gen_JumpTo9/7` are already custom-ABI stubs.** They are JIT-emitted at
compiler init, called with `BL`, take the target in `W0`, clobber only `W0–W3`, and
leave `RCPU/RCPSR/RCycles` live (`ARMJIT_Branch.cpp:171–288`). The Tier B stub
pattern and the generate-stubs-at-`Reset()` pattern both already exist; the
dispatcher is a new stub in the same family.

Register/ABI context: `RMemBase=x26`, `RCPSR=w27`, `RCycles=w28` (counts **up** from
0 within a slice), `RCPU=x29`; `ARM_Dispatch`/`ARM_Ret` own the callee-saved frame
and the Cycles/CPSR memory commits (`ARMJIT_A64/ARMJIT_Linkage.S`).

## A.1 M1.2(a) — the budget slot

New field on `ARM` (offset added to `ARMJIT_x64/ARMJIT_Offsets.h`, hand-maintained,
3 entries today; add `static_assert(offsetof(...))` checks beside the existing ones):

```cpp
s32 CyclesBudget;   // slice budget in core cycles; 0 => exit ASAP
```

C++ side, before entering JIT code in `ARMv5::Execute<JIT>`:

```cpp
CyclesBudget = (s32)std::min<s64>(NDS.ARM9Target - NDS.ARM9Timestamp, INT32_MAX);
```

`ForceExecutionExit(cpu)` — called by ScheduleEvent-earlier-than-target, IRQ raise,
`CPUStop` writes, halt/idle setters — is `cpu->CyclesBudget = 0;`. Semantics are
identical to `ARM9Timestamp + Cycles >= ARM9Target` because `RCycles` accumulates
from 0 per slice. Emitted check everywhere:

```asm
ldr   w1, [x29, #ARM_CyclesBudget_offset]
cmp   w28, w1
b.ge  exit_path          ; 3 instructions, 1 load
```

## A.2 M1.2(b) — the dispatcher stub

Generated at `Compiler::Reset()` like `Gen_JumpTo9` — **not** in `ARMJIT_Linkage.S` —
so `GetRXBase()` and the `num` tag are baked as immediates. Calling convention:
**`W0 = instrAddr` on entry**, JIT context registers live. Every exit path already
has the target address in `W0` (it is `Gen_JumpTo9`'s argument), so the dispatcher
never re-derives the PC from `R[15]`/CPSR.

```asm
Dispatcher9:                                ; in: w0 = instrAddr
    ldr   w1, [x29, #ARM_CyclesBudget_offset]
    cmp   w28, w1
    b.ge  1f                                ; slice over / forced exit
    ldr   w2, [x29, #ARM_StopExecution_offset]
    cbnz  w2, 1f                            ; halt/IRQ/idle -> C++
    ldr   w5, [x29, #ARM_FastBlkStart_offset]
    subs  w6, w0, w5
    b.lo  1f                                ; before region -> C++ (SetupExecutableRegion)
    ldr   w7, [x29, #ARM_FastBlkSize_offset]
    cmp   w6, w7
    b.hs  1f                                ; past region -> C++
    ldr   x5, [x29, #ARM_FastBlkPtr_offset]
    lsr   w6, w6, #1
    ldr   x6, [x5, w6, uxtw #3]             ; lookup entry (u64)
    lsr   x4, x6, #32
    cmp   w4, w0                            ; tag == instrAddr | num (num baked)
    b.ne  1f                                ; miss -> C++ compiles, re-enters
    movz/movk x5, #<RXBase>                 ; baked at stub-generation time
    add   x0, x5, w6, uxtw                  ; w6 = low 32 of entry = sub-entry offset
    br    x0                                ; ~17 instructions, 5 loads hot path
1:  b     ARM_Ret
```

The tag/offset decode is lifted verbatim from `ARMJIT_Branch.cpp:454–464`, including
the detail that `w6` after the `ldr x6` *is* the entry's low word. `R[15]` is already
committed by `Gen_JumpTo9`/`Comp_JumpTo` state updates, so edge `1:` needs no PC
store — the C++ loop recomputes `instrAddr` from `R[15]` as today. `Dispatcher7` is
the same generator run with `num=1` (tag compare against `w0|1`); the
`FastBlockLookup*` fields live on the `ARM` base class (`ARM.h:201–203`) so offsets
are shared.

## A.3 M1.2(c) — CompileBlock tail rewrite

```
before:  Flush; add ConstantCycles; STR RCycles; STR RCPSR;
         mov x0,RCPU; QuickCall ARM9_ContinueBlock; cbz/br; tailcall ARM_Ret
after:   Flush; add ConstantCycles; <w0 := instrAddr, already there>;
         b Dispatcher9
```

`SaveCycles()/SaveCPSR()` disappear from every exit — `RCycles`/`RCPSR` stay
register-resident across the whole slice; only `ARM_Ret` commits them. Same rewrite
at the mid-block sites (`Comp_BranchSpecialBehaviour`, BX-LR path
`ARMJIT_Branch.cpp:593–602`). Deleted: both `ARM9_ContinueBlock` call sites,
`LastJitBlockAddr/Entry` and all hardcoded-PC users,
`Comp_ExitToARM9FastLookupTarget`, the address lists at `ARMJIT_Branch.cpp:505–512`
and `:539–540`.

**Trap:** the idle-branch emission (`Comp_BranchSpecialBehaviour`,
`ARMJIT_Compiler.cpp:777–781`) sets `cpu->IdleLoop` with an emitted `STRB` and flows
to the exit; the dispatcher checks budget, not `IdleLoop`. That emission gains
`str wzr, [x29, #ARM_CyclesBudget_offset]`. General invariant: **every setter of
`StopExecution`/`Halted`/`IdleLoop` also zeroes the budget** — that is
`ForceExecutionExit()`. The dispatcher's explicit `StopExecution` check covers
setters reached via helper calls mid-block.

## A.4 M1.3(a) — link-site emission

Only for exits where `Comp_JumpTo(u32 addr, ...)` runs — compile-time-constant
same-mode targets. Mode switches go to the dispatcher (the v1 chaining rule, kept).
`Comp_JumpTo(u32)` itself is unchanged — it already commits `RegionCodeCycles`, the
CPSR T-bit, `R[15]` when `Exit`, and folds branch cycles into `ConstantCycles`
(`ARMJIT_Branch.cpp:38–168`). Only the control transfer changes:

```asm
    ; RegCache flushed, ConstantCycles folded — as today
    ldr   w1, [x29, #ARM_CyclesBudget_offset]
    cmp   w28, w1
    b.ge  0f
link_slot:                       ; exactly one A64 instruction, 4-byte aligned
    b     0f                     ; UNLINKED: branch to own fallback
0:  movz  w0, #:lo16:targetAddr
    movk  w0, #:hi16:targetAddr
    b     Dispatcher9
```

Linked state patches `link_slot` to `b <target_entry>`. Cost per linked transition:
**4 instructions, 1 load.**

**Patch-slot discipline (load-bearing):** the slot only ever contains an
unconditional `B`, in both states. ARMv8 permits concurrent modification and
execution without synchronization only for `B`, `BL`, `NOP`, `BRK` and a few others;
the executing PE fetches either the old or the new branch — both valid program
states. This is what makes unlinking safe even though `InvalidateByAddr` can be
reached from a store helper while JIT frames are live on the stack. Never patch
anything but B→B.

Conditional branches produce two link sites per block (taken + fall-through), hence
`Outgoing[2]`.

Note: a linked jump enters the target block's full entry point, so per-block
prologue work (e.g. the `MOVP2R RMemBase` fastmem-base load when `hasMemInstr`) is
preserved — `res = GetRXPtr()` is taken before that emission in `CompileBlock`.

## A.5 M1.3(b) — bookkeeping

```cpp
struct OutgoingLink { u32 PatchOffset; u32 TargetAddr; };   // offset into RX region
struct LinkSite     { u32 SourceBlockAddr; u32 PatchOffset; };

// JitBlock gains:
u8 NumOutgoing;                 // 0..2
OutgoingLink Outgoing[2];
TinyVector<LinkSite> Incoming;

// ARMJIT gains (per CPU):
std::unordered_multimap<u32, LinkSite> PendingLinks9, PendingLinks7;
```

Life cycle (all on the emulator thread — no locking):

- **Compile end:** for each outgoing record, target block exists → patch now +
  append to `target->Incoming`; else insert into `PendingLinks`.
- **After any block compiles:** drain `PendingLinks.equal_range(newBlock.StartAddr)` —
  patch each site, move to `Incoming`.
- **Target dies** (`InvalidateByAddr`, `ARMJIT.cpp:2792`; `RetireJitBlock`): rewrite
  each `Incoming` site back to `b 0f` (fallback = `PatchOffset + 4`), **re-insert
  into `PendingLinks`** so recompilation at the same address re-arms it.
- **Source dies:** erase its sites from `PendingLinks` and from targets' `Incoming`
  (found via its `Outgoing` records) — otherwise later unlinks write into recycled
  code memory.
- **`ResetBlockCache`:** clear both structures.

Ordering inside `InvalidateByAddr`: unlink incoming sites *before* the block leaves
`JitBlocks9/7` and before `RetireJitBlock` recycles memory.

## A.6 Patch routine and W^X

```cpp
void PatchBranch(u32 rxOffset, ptrdiff_t targetRxOffset) {
    u32 instr = 0x14000000 | (((targetRxOffset - rxOffset) >> 2) & 0x03FFFFFF); // B
    JitEnableWrite();            // no-op on Android RWX; jit_write_protect on Apple
    *(u32*)GetRWPtr(rxOffset) = instr;
    JitEnableExecute();
    __builtin___clear_cache(rxPtr, rxPtr + 4);
}
```

Android maps the code region RWX (`ARMJIT_Global.cpp:108`); Apple dev builds use the
same `JitEnableWrite/Execute` bracket `MaybePromoteTrace` already uses
(`ARMJIT.cpp:1850–1857`). A64 `B` spans ±128MB ≫ code cache size — assert, don't
handle veneers.

## A.7 M1.4 — dynamic exits after this

`bx lr` / `pop {pc}`: `Gen_JumpTo9` (address stays in `W0`, updates R15/cycles) then
`b Dispatcher9`. The ~17-instruction dispatcher replaces both the hardcoded
`LastJitBlock` sites and the C++ helper. `Comp_JumpToARM9SameARMDirect`
(`ARMJIT_Branch.cpp:333`) survives as the inline state-commit before the dispatcher
jump.

## A.8 Cost table (per transition, production build)

| Path | Today | After M1.2/1.3 |
|---|---|---|
| Static branch, linked | C call: ~6 branches + HLE hash + `LookUpBlock` + 2 trace hashes (~50–150ns) | 4 instrs, 1 load (~1–2ns) |
| Static branch, unlinked/miss | same C call, or full `ARM_Ret` round trip | dispatcher (~17 instrs) or C++ compile path |
| Dynamic branch (`bx lr`) | hardcoded-PC hit, else C call / `ARM_Ret` | dispatcher, universally |
| Slice-end check | 5 instrs, 3 loads (where inlined) | 3 instrs, 1 load |
| Exit to C++ | every block boundary stores Cycles+CPSR | once per slice, in `ARM_Ret` |

---

# Appendix B — Full Implementation Plan to Compile-and-Test Fidelity

Scope: everything needed to build, run, and verify M0→M2 on real hardware, with
WiFi/MP validation explicitly deferred (the MP gate infrastructure is specified but
not blocking during this phase, per current project direction).

## B.0 Test vehicles and a discovered gap

**Vehicle 1 — macOS host (primary dev loop).** The development machine is Apple
Silicon: native AArch64, so the A64 JIT runs natively, and `ARMJIT_Global.cpp:63`
already carries the `MAP_JIT` path. The tree retains the desktop frontend sources
(`src/frontend/qt_sdl/`). Host-side compile+test iteration requires no Android
device.

**Vehicle 2 — Android device.** Gradle app build +
`tools/bench/run_android_harness.sh` / `run_android_simpleperf.sh`, as in v1.

**Gap:** `tools/bench/README.md` and `multiplayer_smoke.sh` reference a
`liteDS-headless` CMake target. That target does not exist — `LITEV_HEADLESS` only
adds a compile definition (`app/CMakeLists.txt:65–67`); there is no `main()` in the
tree. **Unit 0 must build it.** It is also the natural integration point for the
melonDS-profiler project.

## B.1 Ordering note vs. the milestone list

The milestones in the main plan are stated against the v1 fork. On the *fresh
upstream branch* two simplifications apply:

1. Upstream has no `ARM9_ContinueBlock`, traces, or last-block cache — block exits
   all go `ARM_Ret` → C++ loop. The "delete chain machinery" steps vanish;
   dispatcher + linking are built directly.
2. Upstream has no ARM9LibHLE. M1.1 (compile-time HLE) therefore has nothing to do
   on day one; the HLE framework arrives later (M3-era port) already in
   compile-time-hook form. The first code unit on the fresh branch is the budget
   slot, not HLE.

## B.2 Implementation units

Each unit ends in a state that **compiles, boots the ROM suite, and passes its
oracle**. No unit depends on a later one.

### Unit 0 — headless harness + baseline (est. ~600 LOC, mostly new files)

New files:

- `tools/headless/main.cpp` (~300 LOC): CLI runner. Args: `--rom`, `--savestate`,
  `--frames N`, `--mode jit|interp`, `--fb-hash-every N` (xxhash of both screen
  framebuffers, printed per sample), `--fb-dump-png <frame>`, `--profile-json out`
  (LiteProfile per-frame dump), `--audio-null`. Exit code 0 on completing N frames.
- `tools/headless/PlatformHeadless.cpp` (~300 LOC): minimal `Platform::*`
  implementation — POSIX file IO, std::thread/mutex/semaphore wrappers, stub
  camera/mic, `MP_*` backed by the existing local shared-mem channel (needed later
  by `multiplayer_smoke.sh`; stubs acceptable in Unit 0), null audio out.
  Reference: the interface list in `src/Platform.h`; crib from
  `src/frontend/qt_sdl/Platform.cpp` and `src/android/` equivalents.
- `tools/headless/CMakeLists.txt`: `liteDS-headless` executable linking the core
  library only (no Qt, no JNI). Wire into the core CMake behind
  `-DLITEV_HEADLESS=ON`. The core `src/CMakeLists.txt` is platform-clean; the
  `android/` sources must be excluded from the host build (verify the existing
  conditional; add one if absent).

Port from v1 (verbatim): `LiteProfile.h` + `LITE_PROFILE_*` scaffolding +
`LITEV_PROFILE` compile-out discipline; the `LITEV_*` flag block pattern for the
host CMake.

**Oracle / acceptance:**

- Builds on macOS host (`cmake -B build-host -DLITEV_HEADLESS=ON
  -DCMAKE_BUILD_TYPE=Release && cmake --build build-host -j`).
- Boot suite (see B.4) runs 300 frames in both `--mode jit` and `--mode interp`.
- Baseline recorded: per-ROM FPS + frame-hash sequence into
  `docs/baselines/host-v2/<date>.json`.

### Unit 1 — interpreter-lockstep verification mode (the "is it working" oracle)

New file `tools/headless/VerifyLockstep.cpp` (~200 LOC): `--verify-interp` runs two
`NDS` instances from the same ROM/savestate — one JIT, one interpreter — stepping
one frame at a time. After each frame, compare: R0–R15 + CPSR/SPSR of both CPUs,
`ARM9Timestamp/ARM7Timestamp` deltas, main-RAM xxhash, and both framebuffer hashes.
On divergence: print frame number, mismatching field(s), and the last 64 dispatched
ARM9 block addresses (small ring buffer in the `ARM` struct under `LITEV_PROFILE`).

Caveat to accept: identical inputs required (no RTC drift — force a fixed RTC epoch
in headless mode; both instances already share it via `Platform`).

**Oracle:** `--verify-interp --frames 600` clean on the boot suite *before any JIT
change lands*. This is the baseline sanity proof that the oracle itself is sound.

### Unit 2 — budget slot in shadow mode (M1.2a)

Changes:

- `src/ARM.h`: add `s32 CyclesBudget;` + dispatch ring buffer (profile-gated).
- `src/ARMJIT_x64/ARMJIT_Offsets.h`: add `ARM_CyclesBudget_offset`; add
  `static_assert(offsetof(ARM, CyclesBudget) == ARM_CyclesBudget_offset)` in
  `ARMJIT_A64/ARMJIT_Compiler.cpp` beside its includes (do the same for the three
  existing offsets while there).
- `src/ARM.cpp` `Execute<JIT>`: set budget before dispatch (formula in A.1).
- New `ForceExecutionExit(ARM*)` inline in `ARM.h`; call sites:
  `NDS::ScheduleEvent` (only when the new event precedes the current target),
  `TriggerIRQ`, all `CPUStop` bit-set sites in `NDS.cpp`/`DMA.cpp`/`GPU3D.cpp`,
  halt writes (`CP15.cpp` wait-for-IRQ, `NDS::ARM7IOWrite8(HALTCNT)`), and both
  `IdleLoop` C++ setters.
- **Shadow mode:** no emission change yet. The C++ loop still enforces
  timestamp/target; a debug assert verifies
  `(Cycles >= CyclesBudget) == (ARM9Timestamp + Cycles >= ARM9Target)` at every
  return.

**Oracle:** boot suite + verify-interp unchanged; shadow assert never fires over the
suite.

### Unit 3 — dispatcher stub, all exits rerouted (M1.2b+c)

Changes:

- `ARMJIT_A64/ARMJIT_Compiler.{h,cpp}`: `void* Gen_Dispatcher(u32 num)` emitted in
  the compiler constructor/`Reset()` after `Gen_JumpTo9/7` (emission per A.2); store
  `DispatcherEntry9/7`.
- `CompileBlock` tail per A.3; audit **every** exit for the `W0 = instrAddr`
  convention: `Gen_JumpTo9/7` returns (already `W0`-based), `Comp_JumpTo(u32)`
  static case (materialize the constant), block-end fall-through (materialize
  `R15`-derived address), `Comp_BranchSpecialBehaviour` both edges, BX-LR path,
  interpreter-fallback instruction exits.
- Idle-branch emission gains the budget-zero store (A.3 trap).
- `src/ARM.cpp` `Execute<JIT>` loop simplifies: region setup + `LookUpBlock` +
  compile-on-miss only (the dispatcher having already failed its inline lookup);
  IRQ/halt handling unchanged.
- New LiteProfile counters: dispatcher hits / misses / budget-exits / stop-exits.

**Oracle:** verify-interp clean (600 frames × suite); benchmark delta recorded;
counter sanity: dispatcher hit rate should immediately dominate misses.

### Unit 4 — direct linking (M1.3)

Changes per A.4–A.6:

- `src/JitBlock.h`: `NumOutgoing`, `Outgoing[2]`, `Incoming`.
- `src/ARMJIT.{h,cpp}`: `PendingLinks9/7`, `PatchBranch()`, `LinkBlock()`,
  `UnlinkIncoming()`; drain pending links at `CompileBlock` return; unlink hooks in
  `InvalidateByAddr` (before `JitBlocks` erase / `RetireJitBlock`) and
  `ResetBlockCache` (clear structures).
- `ARMJIT_A64/ARMJIT_Compiler.cpp` / `ARMJIT_Branch.cpp`: emit link sites for
  static same-mode exits; record patch offsets into the `JitBlock` under
  construction.
- Runtime kill switches for A/B and bisection: `LITEV_LINK_UNCOND`,
  `LITEV_LINK_COND`, `LITEV_LINK_FALLTHROUGH` (env-read once at init in headless;
  settings-plumbed on Android later). Land enabled in that order.

**Oracle:** verify-interp clean on the suite **plus the SMC stressors** (B.4);
benchmark delta per link kind; new counters: links made / unlinked / pending-drained;
`InvalidateByAddr` frequency per ROM to catch link-thrash.

### Unit 5 — event-true slices (M2.1)

Changes:

- `src/NDS.cpp`: `NextTarget()` uncapped (delete `kMaxIterationCycles` /
  `kIterationCycleMargin` usage); `ScheduleEvent` early-event case calls
  `ForceExecutionExit` on both CPUs (budget already exists from Unit 2 — this
  activates it as the *enforcing* mechanism; the shadow assert from Unit 2 flips to
  authoritative).
- Keep the GPU3D `HasPendingWork()` poll per iteration initially — iteration count
  is about to collapse, so its cost does too; make it event-driven only if profiles
  still show it.

**Oracle:** verify-interp clean; `SchedulerIterations` per frame drops from ~1,094
to approximately the per-frame event count; benchmark delta; GXFIFO-heavy ROM
(3D-heavy suite entry) behaves identically.

### Unit 6 — ARM7 inline + dispatcher/linking parity (M2.2) and idle port (M2.3)

- On the fresh branch ARM7 is already inline (the condvar thread was a fork
  divergence) — nothing to remove; give ARM7 the Unit 2–4 treatment
  (`Dispatcher7`, budget on `ARMv4`, link bookkeeping `num=1`).
- Port v1 IPC/SPI idle detection per the main plan M2.3.

**Oracle:** verify-interp includes ARM7 register/timestamp comparison (already in
Unit 1's design); audio regression check = WarioWare suite entry frame-hash +
listening test on device; benchmark.

### Unit 7 — Android device bring-up

- Mirror the host CMake flags in `app/CMakeLists.txt`; gradle build;
  `run_android_harness.sh` + `run_android_simpleperf.sh` baseline vs. v2.
- Devices: the v1 A55-class reference device first (it exposed guard-cost
  regressions v1 hit repeatedly), then a big-core device.

## B.3 File manifest summary

| File | Units | Nature |
|---|---|---|
| `tools/headless/main.cpp` | 0 | new |
| `tools/headless/PlatformHeadless.cpp` | 0 | new |
| `tools/headless/VerifyLockstep.cpp` | 1 | new |
| `tools/headless/CMakeLists.txt` | 0 | new |
| `src/LiteProfile.h` | 0 | ported from v1 |
| `src/ARM.h` | 2,3,6 | +budget field, ForceExecutionExit, ring buffer |
| `src/ARM.cpp` | 2,3,6 | budget setup; Execute<JIT> loop simplification |
| `src/NDS.cpp` | 2,5 | ForceExecutionExit call sites; uncapped NextTarget |
| `src/ARMJIT_x64/ARMJIT_Offsets.h` | 2,3 | +offsets |
| `src/ARMJIT_A64/ARMJIT_Compiler.h/.cpp` | 3,4 | Gen_Dispatcher; tail rewrite; link-site emission |
| `src/ARMJIT_A64/ARMJIT_Branch.cpp` | 3,4 | exit rewrites to W0-convention + link sites |
| `src/ARMJIT_A64/ARMJIT_Linkage.S` | 3 | unchanged except comments (dispatcher is generated, not static asm) |
| `src/JitBlock.h` | 4 | +link fields |
| `src/ARMJIT.h/.cpp` | 4 | PendingLinks, PatchBranch, unlink hooks |
| `src/CP15.cpp`, `src/DMA.cpp`, `src/GPU3D.cpp` | 2 | ForceExecutionExit call sites |
| `app/CMakeLists.txt` | 7 | flag mirror |

## B.4 Test ROM suite (WiFi excluded)

| ROM | Stresses | Why |
|---|---|---|
| Shrek – Smash n' Crash | the v1 benchmark | continuity with all v1 numbers |
| Pokémon Diamond/White (overworld + menu) | CPU-heavy, known v1 problem scene | `docs/pokemon-white-menu-performance-analysis.md` |
| New Super Mario Bros. (1-1) | 2D-heavy | renderer-track sensitivity |
| Mario Kart DS (single-player GP) | 3D/GXFIFO stalls | Unit 5 GXStall path |
| WarioWare: Touched | audio/IPC | ARM7 idle-detection safety (Unit 6) |
| A known SMC-heavy title + a homebrew SMC test | self-modifying code | Unit 4 link/unlink under invalidation churn |

Oracles per run: verify-interp divergence (primary), frame-hash sequence vs.
recorded baseline (secondary — catches "both modes wrong the same way" only via
visual spot check), FPS, and the unit-specific counters. MP smoke
(`multiplayer_smoke.sh`) becomes runnable once Unit 0's `MP_*` backend is real; it
gates merges again from M2.4 onward.

## B.5 Known unknowns to resolve during implementation

1. **Host build of the core without Android deps** — the core CMake looks
   platform-clean, but the `src/android/` subtree's inclusion conditions must be
   verified on the first Unit 0 configure. Fallback: build the core as a static lib
   with an explicit source list for the headless target.
2. **Exact `Platform::MP_*` shared-mem semantics** for two host instances — needed
   only for the smoke test, stubs fine until then.
3. **RegisterCache invariants at exits** — the dispatcher clobbers caller-saved
   `w0–w7` only, and `RegCache.Flush()/PrepareExit()` precedes every exit today, so
   guest state is memory-resident at transition time. Verify no exit path skips
   `PrepareExit` before a link site (the conditional mid-block exits are the ones to
   audit).
4. **Savestate compatibility** — `CyclesBudget` and link bookkeeping are transient
   (recomputed per slice / per compile); confirm nothing serializes `JitBlock` or
   `ARM` hot fields. Loading a savestate must `ResetBlockCache()` (upstream already
   does).
5. **`InvalidateByAddr` cost under link churn** — v1 games with heavy code-page
   writes will thrash link/unlink. The per-ROM invalidation counter (Unit 4 oracle)
   decides whether a per-region "never link" heuristic is needed.
6. **Apple vs. Android W^X divergence** — patching uses the `JitEnableWrite/Execute`
   bracket; on macOS confirm `pthread_jit_write_protect_np` interacts correctly with
   patching while JIT frames are on the stack (it protects the thread's view, not
   the frames — expected fine, verify with the SMC tests on host).

## B.6 Definition of "working" for this phase

The v2 core is *working* (WiFi aside) when, on both vehicles:

1. verify-interp runs 600+ frames divergence-free across the full B.4 suite with
   all link kinds enabled;
2. no shadow-assert/counter anomalies (unlink leaks, pending-link leaks — both
   structures must return to size 0 after `ResetBlockCache`);
3. FPS ≥ v1's best production numbers on Shrek and ≥ upstream baseline on every
   other suite entry (the honest early bar: Units 2–5 must beat *upstream*
   immediately; beating *v1's* accumulated wins is the M3-era goal once fastmem is
   re-ported);
4. 30-minute soak per ROM without crash, JIT-memory exhaustion regression, or
   visual divergence in spot checks.

---

# Appendix C — Implementation Record (2026-07-04)

Units 0–6, M3 Tier A, and the M4 software-renderer track are implemented on this
branch, each gated as specified. This appendix records outcomes and the load-bearing
deviations discovered during implementation (tree facts that supersede the plan text).

## C.1 Status and measured results (Shrek ROM, Apple Silicon host)

| Unit | Commits | Result |
|---|---|---|
| U0 headless harness | b2af6095..0b8641d4 | JIT 950 vs interp 399 FPS; modes converge byte-identical |
| U1 golden-trace oracle | 9b2e6bee..bf5d8794 | 600f baseline committed; double-record byte-identical |
| U2 CyclesBudget shadow | (Unit 2 commits) | bit-exact; zero shadow fires; offsets static_asserted |
| U3 A64 dispatcher | b7a40053 | bit-exact; FPS flat (+0.2%, expected — see C.2.2) |
| U4 direct linking | 7cc67bb4..f372d812 | bit-exact; 63% of exits link-eligible; FPS flat on host (renderer-bound) |
| U5 event-true slices | 833bf80e, ea6eaf48 | iterations 9424→3168/frame (=event count); **+13.5% FPS, +23.2% CPU-bound** |
| U6 ARM7 idle + decomposition | 7fef33d7, a54fadbc | ARM7 = 8.2% of frame, does real work; detector correct, no Shrek gain (as predicted) |
| M3 Tier A memory | 8d07aa9a, 6d256c17 | bit-exact; SlowBlockTransfer9 −98.4% calls; **+4.4% FPS, +7.1% CPU-bound** |
| M4 renderer (soft scope) | 6f4a6bf3..3f6fe05a (merged 3d31a20c) | NEON bit-exact; frameskip **+29–34% FPS**; GL items deferred to Android |

Flags (all default OFF): LITEV_HEADLESS, LITEV_PROFILE, LITEV_SHADOW_ASSERT,
LITEV_JIT_DISPATCH (+LITEV_LINK_UNCOND/COND/FALLTHROUGH), LITEV_EVENT_SLICES,
LITEV_ARM7_IDLE, LITEV_MEM_DTCM_BLOCK, LITEV_MEM_MAINRAM_LOAD,
LITEV_NEON_RENDERER, LITEV_SPU_FAST_INTERP, LITEV_AGGRESSIVE_SKIP.

## C.2 Load-bearing deviations from the plan text

1. **Oracle redesign (supersedes B.2 Unit 1):** JIT and interpreter legitimately
   diverge in timing (208/600 frames mid-boot, reconverging), so the authoritative
   gate is JIT-vs-JIT golden-trace comparison (`--verify-trace`), not interp
   lockstep. Two goldens exist: `shrek-600.trace` (exact-timing configs) and
   `shrek-600-eventslices.trace` (event-slices config, which is a deliberate
   timing change).
2. **Per-hop timestamp commit (supersedes A.2/A.4):** `ScheduleEvent` schedules
   relative to `ARMxTimestamp`, so the dispatcher AND every link site must commit
   `Timestamp += Cycles` per hop. Dispatcher is ~34 instrs, not ~17; link sites
   carry the same bookkeeping. This is why U3/U4 alone were flat on host and the
   payoff arrived with U5 (fewer, longer slices).
3. **Iteration cap is 64, not 512** (`kMaxIterationCycles`, src/NDS.cpp): flag-off
   baseline is ~9424 iterations/frame; the U5 win is proportionally larger than
   projected. U5 added timer-deadline bounding (`NextTimerDeadline`) because DS
   timers are per-iteration-polled, not event-scheduled.
4. **StopExecution is a union over {Halted, IRQ, IdleLoop}** — the dispatcher's
   single StopExecution check covers all loop-acted conditions; checked before the
   timestamp commit (ordering is trace-visible).
5. **Fastmem is OFF on macOS** (`IsFastMemSupported` returns false under __APPLE__)
   and ON on Android — but block LOADS never take the fastmem path on any platform,
   so M3's guard-based Tier A helps both. Tier B stubs deferred on evidence: the
   host residual is Tier-C-exact MMIO/VRAM; Android's residual needs device data.
6. **ARM7 parity came free:** Unit 3/4's per-CPU stubs and link registries covered
   ARM7; the plan's Unit 6 reduced to measurement + the IPC/SPI idle port.

## C.3 Remaining work (blocked on hardware/assets, or evidence-gated)

- **Unit 7 Android bring-up** — requires porting the melonDS-android glue onto this
  upstream base (the v1 app pins an older core) and an Android device for the
  A55-class guard-cost measurements. DTCM block-LOAD inlining is the specific
  Android win to re-measure.
- **B.4 test suite** — only the Shrek ROM is locally available; Pokémon/NSMB/Mario
  Kart/WarioWare and an SMC stressor are needed for the full compat oracle
  (Mario Kart especially: the U5 GXFIFO-interleave watch item).
- **MP smoke gate** — PlatformHeadless MP_* are stubs; wire the local channel
  before any M2.4 work.
- **Evidence-gated, intentionally not built:** M2.4 ARM7 concurrency (ARM7 is 8.2%
  ≪ the 15% gate), M5 relaxed ARM9 timing, M1-followup hot-region recompilation,
  M3 Tier B stubs (needs slowmem-miss region histogram from the profiler).

---

# Appendix D — Unit 7b Findings and the 60 FPS Campaign (2026-07-05)

## D.0 Why this appendix exists

The original plan (and Appendix C) was drafted from v1's CPU-profiler data and
validated against menu/boot workloads. Unit 7b (playable app) and the in-race
device work exposed bottlenecks the plan never contained. This appendix records
those findings and defines Milestone 6: sustained 60 FPS in-race on the RG DS
(4xA55 / Mali-G52) with no frameskip. Beating v1's ~40 is explicitly NOT the
bar.

## D.1 Findings the original plan overlooked

1. **The 3D geometry engine was never in scope.** The GL renderer offloads
   rasterization only; the DS geometry pipeline (matrix/vertex/clip/polygon
   setup in GPU3D.cpp) runs per-vertex on the CPU, fed by thousands of GXFIFO
   MMIO writes per frame (Tier C exact paths by design). Menus idle it; races
   hammer it. No unit ever measured or optimized it. DraStic's known answer:
   hand-optimized NEON geometry.
2. **The plan's renderer assumptions were v1-era.** Current upstream composites
   the 2D engines ON THE GPU (full-GL compositor: giant per-pixel shader +
   per-frame VRAM/palette/OAM texture mirroring + ~800 tiny draws + capture
   sync). v1 ran CPU 2D (NEON) + GL 3D + a trivial layer-blend compositor.
   On A55/Mali-class devices, upstream's architecture is structurally more
   expensive: "v2 has the faster engine and the slower car."
3. **App-process environment is its own platform.** LITEV_JIT_DISPATCH crashes
   in the untrusted_app domain (SEGV_ACCERR during NDS construction; runtime
   stub generation into JIT memory is the suspect) while running bit-exact in
   the adb shell domain on the same silicon. Ditto the GLES port class of bugs:
   signed/unsigned vertex-attribute mangling (the white-textures root cause),
   glReadPixels format restrictions, empirically-placed BGRA swizzles.
4. **Frame-time accounting, not FPS, is the decision variable.** In-race app
   frames are a real ~30ms (no vsync involvement; 60-cap only). Emulation-only
   in-race is ~13-16ms on the A55; the GL path adds ~14-17ms of CPU-side cost.
   Both sides must shrink AND overlap to reach 16.6ms.
5. **Measured-flat is workload-relative.** Dispatch/link/fastmem verdicts from
   menu workloads did not survive in-race measurement (fastmem: menu "wash" ->
   in-race +5-7%). All future verdicts must come from the in-race savestate
   workload.

## D.2 Milestone 6 — the 60 FPS campaign (task list)

Render side (owner: GL-perf track):
- M6.1 Frame phase breakdown in-app (RunFrame / GL-3D / compositor+uploads /
  blit+present), with per-frame VRAM-mirroring byte volume. IN PROGRESS.
- M6.2 Kill synchronous GL stalls (fence waits in present handshake; the
  GLES_Compat glMapBuffer READ|WRITE whole-buffer shim; upload stalls;
  capture-sync path).
- M6.3 Draw-call diet: GLES sampler objects (replaces per-batch
  glTexParameteri), consecutive same-state batch merging, redundant-state
  shadowing.
- M6.4 Dirty VRAM/palette/OAM uploads (v1 concept) if mirroring dominates.
- M6.5 Emulation/render pipelining: overlap RunFrame(N+1) with render/present(N)
  (v1 frame-queue + fences concept). Structural multiplier for everything else.
- M6.6 HYBRID ARCHITECTURE DECISION (gated on M6.1 numbers): if GPU-2D
  mirroring+compositor cost is structurally >5ms, evaluate soft-2D(NEON) +
  GL-3D + lean compositor — v1's architecture on v2's core.
- M6.7 Frameskip exposed as a user setting (real-time game speed at 30 visible
  FPS today; does NOT count toward the 60 bar).
- M6.8 Latent capture bug: glReadPixels 1555_REV write-back is still unfixed
  (Shrek race never exercises it; capture-using games will). Field-exact
  readback wrapper per the documented conversion.

Emulation side (owner: core track):
- M6.9 Dispatcher/linking in the app process: root-cause the untrusted_app
  crash (W^X sequencing of runtime stub generation suspected), fix properly,
  enable LITEV_JIT_DISPATCH+LINK in the app. IN PROGRESS. Worth +3-5%
  CPU-bound; mainline cherry-pick expected.
- M6.10 Fastmem ON in the app (handler-gating fix landed) + complete the
  in-race full±fastmem device cells (run-race.sh) to confirm the +5-7% signal
  and set the app default.
- M6.11 **GPU3D geometry engine optimization (NEW MAJOR FRONT):** profile
  GPU3D.cpp in-race on-device; NEON-ify vertex/matrix/clip math; batch GXFIFO
  command processing to cut per-write MMIO round-trips. The largest untouched
  slice of in-race emulation time.
- M6.12 M5 relaxed ARM9 timing (plan §8) — elevated from "optional" to
  "conditional on M6.9-M6.11 leaving a gap": flat cycles-per-instruction for
  ARM9 removes per-access timing math incl. every GXFIFO write's accounting.
  ARM7 timing stays exact (WiFi invariant).
- M6.13 ARM7 idle port re-measure on non-Shrek titles when suite ROMs arrive
  (WarioWare-class IPC polling was its target).

Process invariants for all M6 work: in-race savestate workload for every
measurement; golden-trace gates for every core change (both goldens + on-device
shell-domain verify); rendering-correctness screenshot compare for every GL
change; every flag defaults OFF upstream-identical.

## D.3 Standing facts for M6 implementers

- In-race device numbers (fs0): headless soft baseline 28.8 / full 32; app GL
  31-33 real (no vsync). Menus: app 56-60.
- Emulation-only in-race ~13-16ms (A55). 60 FPS budget: 16.6ms wall with
  pipelining, ~12-13ms without.
- App flags ON: EVENT_SLICES, MEM_DTCM_BLOCK, MEM_MAINRAM_LOAD, NEON, -O3.
  OFF pending M6: JIT_DISPATCH+LINK (M6.9), fastmem (M6.10), AGGRESSIVE_SKIP
  (M6.7 exposure), ARM7_IDLE (no Shrek value).
- White-textures root cause on Mali: glVertexAttribIPointer GL_UNSIGNED_INT
  into signed ivec3 mangles values >=2^31 (texcache sentinel). Fixed with
  GL_INT (core a2fa25a4). BGRA 3D-layer swizzles are empirically placed; the
  compensating swap's origin is undetermined — revalidate on capture-as-texture
  paths.
- App savestates wrap core states with a mandatory RetroAchievements section
  (RCHV) — headless-made states are rejected by the app (cross-embedder
  compatibility gap; make the wrapper optional someday).

## D.4 — M6.1 phase breakdown results (2026-07-05): render side CLOSED

On-device in-race frame (~30.5ms total, 1992MHz unthrottled): RunFrame (ARM
emulation incl. GXFIFO/geometry/DMA/scheduler) ~20ms; 3D GL submission ~3.3ms
(≤9ms dense); 2D compositor + VRAM/pal/OAM upload ~1.2ms (already dirty-tracked
upstream); app blit ~2.3ms; misc ~2.5ms; GPU hardware time ~1ms (near idle).

Decisions from evidence:
- M6.2 stall kill: NO STALL EXISTS (fenceWait 0; capture readback never called
  in-race; glMapBuffer is a small WRITE_ONLY UBO). Closed.
- M6.3 draw diet: renderer already batch-merges; <1ms upside. Rejected.
- M6.4 dirty uploads: already upstream. Closed.
- M6.5 pipelining: GPU ~1ms -> nothing meaningful to overlap. Rejected for FPS
  (may return for latency later).
- M6.6 hybrid architecture: REJECTED — compositor slice is 1.2ms << 5ms bar;
  soft-2D would ADD CPU raster to a saturated A55.
- M6.7 frameskip: shipped (debug.litev.frameskip 0-3); RunFrame −6ms at skip 1;
  net ~45 cap. M6.10 fastmem: shipped ON (stable in-race).
- NEW: gated in-app frame-phase profiler (debug.litev.prof, zero-overhead when
  off) — the seed of the melonDS-profiler on-device backend.
- THERMALS: sustained racing throttles 1.992->1.8GHz at ~83C, −20% FPS. On this
  passively-cooled device, emulation EFFICIENCY (fewer joules/frame) is part of
  the 60 FPS problem, not just speed.

The 60 FPS math: 16.6ms budget vs ~20ms of ARM emulation — even zero-cost
rendering caps at ~40. The campaign is now entirely emulation-side, in order:
M6.9 dispatcher-in-app (in progress), M6.11 geometry engine (requires
decomposing the 20ms RunFrame bucket — GXFIFO/geometry share unknown), M6.12
relaxed timing, hot-region recompilation (Appendix A follow-up).

## D.5 — M6.9 + M6.11 outcomes (2026-07-05): geometry front closed, M6.12 is the path

**M6.9 dispatcher-in-app: DONE.** Root cause was ABI, not W^X: LITEV_* macros
were directory-scoped, so the app's JNI glue compiled a smaller NDS layout than
the core constructed (SEGV at construction). Fix: LITEV_* exported as PUBLIC
usage requirements of core (core 1ca8b152, mainline 54c7ff5c's parent). App
with dispatcher+link ON: boots clean, in-race stable, median 33.0 FPS
(baseline 31-33). App branch cebcf95 repins core.

**M6.11 decomposition (host 54c7ff5c, device 4c166ca8):** A55 in-race,
app-matching config, per frame: ARM9 JIT 7.55ms (55.7% of emu-compute),
GPU3D geometry 2.29ms (16.9%), ARM7 1.95ms, DMA 1.76ms; 8,451 GX cmds/frame
@ 271ns. Menus: 22 cmds/frame (geometry idles, as D.1 predicted). Full detail:
docs/m6.11-runframe-decomposition-{host,device}.md.

**M6.11 NEON geometry (cbaaf32e): landed flag-gated (LITEV_NEON_GEOMETRY,
default OFF), bit-exact on all four gates, but the measured win is ~0.02ms
(~1%) on the A55 — order of magnitude under the 1.0-1.2ms projection.**
Verdict: the 271ns/cmd bucket is dominated by FIFO dispatch + SubmitPolygon/
clipping/vertex-RAM writes, not transform arithmetic; single 4-wide integer
dot products can't amortize NEON lane-move overhead on an in-order A55. The
D.2 M6.11 "NEON-ify the math" premise is CLOSED-NEGATIVE for math-only
vectorization; a material geometry win requires structural work (batched
GXFIFO drain, clipping restructure) — reclassified to the same reserve tier
as hot-region recompilation.

**60 FPS path forward (evidence-ranked):** ARM9 is 7.55ms and already carries
every landed optimization; geometry structural work is speculative. The next
sanctioned front is **M6.12 relaxed ARM9 timing (plan §8)** — flat
cycles-per-instruction removes per-access timing math including every GXFIFO
write's accounting (which the decomposition shows is where geometry cost
actually lives). Semantic change: new golden config required; ARM7 stays
exact (WiFi invariant).

## D.6 — M6.12 relaxed ARM9 timing (2026-07-05): CLOSED-NEGATIVE

**Implemented, flag-gated (`LITEV_RELAXED_ARM9_TIMING`, default OFF), correct,
deterministic, game runs a full live race under it — but a performance
REGRESSION on both host (−6.0% FPS) and the A55 (−4.8% FPS; ARM9 bucket 7.76 →
9.11 ms, +17.3%).** All gates pass (OFF byte-identical to both existing goldens;
new goldens `shrek-600-relaxed9{,-full}.trace` double-record byte-identical and
self-verify). Full report: `docs/m6.12-relaxed-arm9-device.md`.

Root cause — the D.5 premise did not survive the code: **in this JIT, ARM9
timing is baked at block-COMPILE time** (the decode loop runs the interpreter
once to fill `CurInstr.CodeCycles/DataCycles`; the emitted block just does
`ADD RCycles, #const`). There is **no per-access MemTimings walk in the ARM9
runtime hot path** to remove — the 8,451 GXFIFO writes/frame each cost one baked
constant, set at compile time; `SlowWrite9` performs the write without touching
cycles. Relaxing the model cannot delete runtime work; it only shrinks each
instruction's sim-time, which makes the game's status-poll/busy-wait loops
iterate MORE per real frame (measured: ARM9 u32-load helper calls +23.6%, ARM9
idle-loop hits +51–58%, scheduler iterations +21%) → ARM9 exec time GROWS. A
DraStic-style timing win requires DraStic's runtime-computed-timing structure,
which melonDS's compile-time-baking JIT does not have, so there is nothing to
reclaim. Reclassified to the same closed-negative tier as M6.11 NEON-geometry;
the emulation-side ARM9 bucket is not reachable by timing relaxation.

## D.7 — 60 FPS reanalysis (2026-07-05, post-M6.12): render side REOPENED with evidence

Three independent deep-dives (prior-art/DraStic mapping; on-device simpleperf of
the app's emu thread; A55 headless decomposition) converge on a reframing that
supersedes D.4's "the wall is ARM emulation" conclusion:

**Core emulation compute is ~13.5ms/frame — it already fits the 16.6ms budget.**
The app's ~22ms RunFrame = core (~13.5) + ~8.5ms of GL render submission that
melonDS's GL renderer issues INSIDE NDS::RunFrame on the emulation thread
(simpleperf: Mali userspace driver 25% self ≈ 7ms + GL renderer C++ ≈ 1.7ms).
On top: ~2.8ms ART/JNI tax purely from the debuggable build (CheckJNI on), and
the 2.1ms "blit" bucket is a GPU-completion stall (0% CPU), not work. D.4's
M6.5 rejection judged GPU *hardware* idle (~1ms) and missed the CPU-side
submission mass. Three of four A55 cores are idle.

DraStic's shape confirms the path: one cooperative core thread + a render
thread. Its 60fps recipe is NOT more core speed — its own ceiling is realtime.
120fps verdict: no documented path on in-order A55 short of per-game
hot-region recompilation + HLE; out of scope for this fork.

### The four render-side workstreams (exact plans)

**R1 — Release build (S; ~1.5-2.5ms; app repo).** Build the existing gitHubProd
RELEASE variant (debug-keystore signing acceptable for the RG DS). Verify the
native cmake flags are identical to the debug production set; verify
minify/R8 keeps JNI symbols (existing proguard rules); confirm
android:debuggable=false kills CheckJNI (logcat "CheckJNI is ON" absent).
Gate: same-scene FPS A/B vs debug build, expect +1.5-2.5ms cpu_loop reduction.
Note: run-as stops working on release builds — device debugging via root only.

**R2 — Deferred blit (M; ~1-1.5ms wall; app glue MelonInstance.cpp).**
blitAcceleratedFrame() blits the array texture the 3D renderer wrote THIS
frame -> driver blocks on tiler completion. Change: keep N-buffered (2) frame
textures; blit the PREVIOUS frame's texture (guaranteed complete, zero stall)
and present it — one frame of added display latency, acceptable on this
device. Keep a fence check to assert completeness rather than stall. Flag:
runtime-selectable (debug.litev.deferblit or setting), default ON after gates.
Gates: screenshot-compare top+bottom vs baseline (identical content, allowing
the 1-frame shift), blit bucket -> ~0 in LITEV_PROF, no flicker over 3-lap
race.

**R3 — GL draw/state diet (M-L; ~2-4ms of the 7ms Mali time; core GL renderer,
android branch).** Step 1 MEASURE: temporary LITEV_PROFILE counters for
glDraw*/glBindTexture/glUseProgram/glUniform*/sampler-state calls per frame
in-race (the compositor's ~800 tiny draws + per-batch glTexParameteri are the
suspects). Step 2 implement in cost order: (a) redundant-state shadow cache
(skip no-op binds/uniforms), (b) merge consecutive draws sharing full state
(the 2D compositor's per-scanline/per-layer quads -> instanced or
vertex-appended batches), (c) GLES sampler objects to end per-batch
glTexParameteri churn. Gates: per-frame GL call count before/after (target
>5x reduction), screenshot-compare exactness, in-race FPS.

**R4 — Render-thread offload (L; collapses wall toward ~13.5ms core floor +
R1-R3 savings; core+glue).** The structural fix: emulation thread never talks
to GL. Design: (1) RunFrame produces a frame packet — 3D polygon/vertex RAM
snapshot (the GL 3D renderer's input), 2D compositor inputs (VRAM/palette/OAM
dirty ranges), capture requests; (2) double-buffered packet queue, depth 1
(render N while emulating N+1); (3) render thread owns the GL context: 3D
submission, 2D compositor, blit, present, fences; (4) emu thread blocks only
when the queue is full (render slower than emu) — wall = max(emu, render);
(5) savestate/pause/reset drain the queue first (coherency point); (6)
LITEV_RENDER_THREAD flag, default OFF, app setting to enable. Correctness
gates: golden traces untouched (render is downstream of traced state);
screenshot-compare parity flag-ON vs OFF; 3-lap stability; input latency
check. Bench gate: wall/frame -> max(core, render) measured by LITEV_PROF.
Deliver design doc first (docs/r4-render-thread-design.md) reviewed against
melonDS GL renderer object lifetimes before code.

Sequencing: R1 ships independently now. R2 next (small, app-only). R3 after
its measurement step. R4 design in parallel; implementation lands last and
benefits from R2/R3 (less to move). Device measurement is serialized through
one verification queue (dispatcher A/B first). Projection if all four land at
midpoints, on top of the ~30ms baseline: ~30 - (2 + 1.2 + 3 + remaining
serialization ~6) => ~17-18ms wall => ~55-58fps at 3x resolution, better at
1x; with thermal headroom restored by fewer joules/frame. Reserve tier
(ARM9 idle-skip NEW-GOLDEN, Tier-B memory stubs, hot-region recompilation)
remains if a gap persists.

### D.7 addendum — ARM9 deep-dive results (same day)

On-device hot-block histogram + slow-memory classification (bit-exact
instrumented runs, in-race window): ARM9's 7.55ms is well-distributed genuine
game execution (~46 host-cycles/guest-instr; top-50 blocks = 59% but all
diverse mainRAM game code). Busy-wait/poll share is <0.15ms (DISPSTAT 0,
GXSTAT 1.5, IPCSYNC 0.5 polls/frame; VBlank is a HALT already fast-forwarded
1,373x/frame) — ARM9 idle-skip is CLOSED before implementation, and D.6's
relaxed-timing regression is fully explained. JIT churn near zero; dispatch
84% in-asm. fastmem: neutral on the ARM9 bucket in matched A/B (keep ON,
not a lever). THE one core pickup: 86% of slow reads (23,162/frame) are
mainRAM words from 8,204 SlowBlockTransfer9 LDM calls — the M3 Tier B
"block-LOAD inline tier for mainRAM" deferred in C.3 now has its evidence:
~0.5-0.7ms, bit-exact, effort M (task M6.14). Secondary: mainRAM u16/u8
inline + div/sqrt result-read shortcut ~0.1-0.2ms. ARM9 floor ~6.6ms; core
best-case ~11.9ms — 60fps remains render-side + pipelining per D.7.

### D.7 addendum 2 — R0 pacing-floor diagnosis (same day): no wait exists; the floor is real serialized CPU

Off-CPU tracing + schedstat on the app's emu thread (in-race, 3x GL): ~90%
on-CPU at 1.992GHz, no audio/limiter/present wait (all three suspects
exonerated with code+trace evidence; SPU drops-oldest and never blocks). The
~30.5ms floor decomposes as ~18-19ms ARM/SPU/2D emulation + ~11ms Mali GL
serialized on the SAME thread: ~5ms per-polygon glDrawElements submission
(GPU3D_OpenGL RenderSceneChunk, scales with 3x resolution), ~2.6ms blit,
~3.4ms glFlush/sync/save-check glue. The dispatcher A/B's "absorbed savings"
were the Mali driver's fixed async submission cost redistributing between
profiler buckets when frames arrive faster — an illusion of pacing.
Implications: (1) R4 render-thread offload is confirmed as THE ceiling-raiser
(frame -> max(ARM ~18, GL ~11) => ~50-55fps at 3x, more at 1x, before R1-R3
and core wins); (2) R3 draw batching directly attacks the ~5ms submission;
(3) once R4 overlaps rendering, ARM becomes the critical path and the
dispatcher's 1.2ms + M6.14's ~0.6ms surface as FPS — ship dispatcher ON
after R4 lands; (4) GPU hardware remains ~idle (0.1-0.7ms) even at 3x.

### D.7 addendum 3 — DraStic audio teardown (binary RE): no audio-slaved pacing; audio is free once fast

Decompiled libdrastic_arm64.so (OpenSL ES Simple Buffer Queue). Proven from the
binary: DraStic's audio is the SAME non-blocking model melonDS already uses —
callback contains zero pthread calls, enqueues an all-zeros silence buffer on
underrun, and the producer (emu thread) DROPS-ON-FULL (branch at 0x1de98
returns immediately, no usleep/cond_wait/spin). No sync primitive is shared
between the audio path and the frame path; DraStic's frame limiter is a condvar
shared only emu<->render-thread (waitScreen/signalScreen) — which is exactly
the R4 architecture we're building. Rate 44100 stereo, DS 32768->44100
fixed-ratio resample, user-selectable output-buffer depth {1470..5880 samples,
up to ~4 video frames of slack}. VERDICT: liteDS-v2 needs NO audio-architecture
change; the crackle at 33fps is pure underrun from running at ~55% realtime and
resolves automatically when R4 reaches 60fps. Two cheap copy-worthy ideas:
(1) user-selectable output-buffer depth to mask transient spikes; (2) since
audio never back-pressures emulation, instrument underrun/silence-fill events
as a clean realtime-miss signal for the profiler. Full teardown:
docs/drastic-audio-teardown.md.

### D.7 addendum 4 — R2/R3 device verification: R2 shelved, R3 diet re-aimed at redundant state

**R2 deferred blit = REGRESSION on-device (3x GL, verified).** Toggle ON vs OFF
medians: blit 2.53->3.60ms (did NOT collapse), cpu_loop 31.26->33.13, fps
31.9->30.0. Correctness all-pass (screenshots identical, savestate/pause/3-min
stability clean, 0 crashes). Root cause = R0's floor: the frame is CPU-bound
serialized, no idle-GPU window for a deferred blit to hide in, so deferral only
shuffles cost between buckets (other 3.38->0.73, runFrame 25.3->28.8) and
slightly worsens. VERDICT: R2 provides no benefit pre-overlap. Keep the toggle
default OFF / SHELVE the commit until R4 creates a real overlap window, then
re-measure (the blit belongs on the render thread, which subsumes R2 anyway).

**R3 GL counters aim the diet — it's redundant STATE, not draw count.** Per-frame
in-race medians (menu->race): draws 5->124, binds 411->529, texparam 17->251,
uniforms 4->29, uploadKB 79->121, progs 6->7. The ~411 binds present at a STATIC
menu prove a large fixed redundant-rebind baseline independent of scene. Ranked
diet targets: (1) binds 529/frame — redundant-state shadow cache (skip no-op
glBindTexture/glBind*), (2) texparam 251/frame — cache glTexParameter per
texture / GLES sampler objects (steepest race scaler, +234), (3) draws
124/frame — batching, far lower leverage. So R3 = redundant-state shadowing
FIRST (biggest win, lowest risk), draw batching last. Upload 121KB/frame is
modest, not a target. This is the ~5ms Mali-submission slice from R0.

### D.7 addendum 5 — R3 diet implemented (redundant-state shadow cache); latent Android build bug fixed

Landed on liteDS-v2-android (60af1f87): LITEV_GL_STATE_CACHE (default OFF,
Android-only). New src/LiteGLStateCache.h shadows bound GL state and wraps the
renderer's bind/param calls so redundant ones (bind to already-bound object,
same texparam on same object) are skipped — a driver-level identity. Reset at
END of every VBlank (not frameskip-gated) so no assumption survives into the
app-glue blit/present or next frame => transparent to the app. Attacks binds
529/frame (the ~411 static-menu baseline is per-frame re-binds of a fixed small
object set — collapses toward the count of DISTINCT bind points, tens) and
texparam 251/frame (SetupPolygonTexture's 2 WRAP calls/polygon). Per-object
param cache chosen over a sampler object because wrap mode is per-polygon.
Deliberately NOT cached (ambiguous ownership): UNIFORM/ELEMENT_ARRAY buffer
binds (VAO/BufferBase aliasing), VAO binds, the compute-3D renderer. Gates:
host golden bit-exact flag OFF and ON (inert in headless); Android app builds
flag OFF and ON exit 0. Standalone FPS value (unlike R2): the eliminated calls
are Mali userspace command-construction CPU on R0's serial critical path.
Staged apk-r3-diet.apk for device counter-verify (expect binds/texparam LITEV_GL
counters to drop OFF->ON by exactly the redundant count).

LATENT BUG FOUND + FIXED (41cdf730): LiteProfileGL.h had `*/` inside a comment
(the text "glTexSubImage*/") that closes the block comment early — breaks EVERY
Android LITEV_PROFILE build. It never showed on the host golden (OGLRENDERER=OFF
never compiles the header), meaning the earlier profile-gl instrumentation
commit was host-verified only, never Android-built. Process note: GL/renderer
changes must be Android-compile-gated, not just host-golden-gated.

### D.7 addendum 6 — R3 diet + R1 release: BOTH closed-negative on-device; only R4 remains

Two device A/Bs, both clean negatives, both reshape the plan:

**R3 GL diet: cuts calls, does NOT cut frame time.** Device (3x GL, in-race):
binds 529->436 (-18%), texparam 251->74 (-70%) — the cache works — but fps
30.75->30.90 (noise), cpu_loop -0.11ms. GL *call count* is NOT a serial-CPU
cost. This RETRACTS the R0/simpleperf read that Mali command construction was
~5ms of the ~11ms GL slice: the Mali on-CPU time is proportional to draw/vertex
CONTENT (124 draws, geometry, fragments), not bind/param call count, so
deduping calls can't reclaim it. Correctness PASS (visually identical, stable).
Keep LITEV_GL_STATE_CACHE flag default OFF (correct, harmless, may reduce
render-thread work post-R4); no standalone value.

**R1 release build: 0ms CheckJNI win.** Non-debuggable release vs debug, same
race scene, -O3 matched: cpu_loop 31.15 vs 31.15, fps 32 vs 32. The emulation
hot loop makes too few JNI calls/frame for CheckJNI/ART validation to register;
the ~2.8ms simpleperf "ART/JNI" was the Java driver-loop's real work, not the
debuggable flag. Release build has no perf value (still worth shipping for size/
production hygiene, not FPS).

**Roadmap impact — the cheap wins are exhausted.** Every incremental lever tried
(dispatcher wash, NEON geom +1%, relaxed timing -5%, R2 blit regression, R3 diet
0%, R1 release 0%) is closed. The frame is ~30.5ms of serial work that does not
yield to call-count/CPU-micro cuts. The ONLY remaining structural lever is R4
render-thread offload: run the ~11ms GL-content CPU on a spare A55 concurrent
with the ~18.5ms emulation, wall -> max(~18.5, ~11) ~= 18.5ms => ~54fps at 3x.
R4 is now 60fps-or-bust; its value rests on the ~11ms GL CPU being genuinely
parallelizable (it is CPU per R0's 90%-on-CPU finding, just content-bound not
call-bound). Post-R4, the banked core wins (dispatcher 1.2ms, M6.14 mainRAM,
event-slices) become the ARM-side critical path and surface as FPS. If R4's
overlap does not materialize the win on-device, the honest conclusion is 60fps
at 3x is not reachable on this A55 without dropping internal resolution.

### D.7 addendum 7 — DraStic full teardown (Ghidra, 3285 fns): findings for the campaign

Complete decompile of libdrastic_arm64.so r2.6.0.4a → docs/drastic-teardown/
(12 subsystem docs, 4131 lines). Campaign-relevant conclusions:

**CONFIRMS closed levers (do not reopen):**
- DraStic ALSO bakes cycle cost at compile time (per-instr base + LDM/STM popcount
  + waitstate tables, one subtract/block against a signed down-counter). M6.12
  relaxed-timing regression was correct; DraStic would regress identically.
- DraStic's #1 speed technique — per-block backward liveness → dead-flag AND
  dead-register elimination — is ALREADY in melonDS's JIT (FloodFillSetFlags +
  ARM_InstrInfo ReadFlags/DstRegs/SrcRegs/NotStrictlyNeeded). Not a new lever;
  explains why the ARM9 bucket is dense (~46 cyc/instr), not naive-codegen bloat.

**VALIDATES R4 as the right move:** DraStic runs emulation on the caller thread
and renders FULLY off the critical path — double-buffered software framebuffer,
emu's only present cost is a buffer-index flip + condvar signal; a separate GL
thread does upload+post-FX. This is exactly R4's design. Frame pacing = audio
back-pressure + GLSurfaceView vsync, no sleep-to-60 (matches our R0/audio
findings). R4 is DraStic's proven architecture.

**THE DEEPER ARCHITECTURAL DIVERGENCE (the real ceiling question):** DraStic does
NOT use GL for rendering at all — GLES2 only blits+post-processes a software
framebuffer (no glDrawElements/VBO). Its entire 2D compositor and 3D rasterizer
are software-NEON on DEDICATED helper threads (2D engine-B on its own thread; 3D
raster split 12×16-line bands across 4 threads). melonDS-v2's ~11ms GL-content
CPU (R0) is its full-GL renderer submission — an architecture DraStic proves is
NOT required for DS 60fps on this silicon. So:
- R4 (offload GL submission to a thread) → the sanctioned next step, gets ~54fps.
- IF R4's overlap is insufficient, the DraStic-proven ceiling-raiser is the M6.6
  HYBRID reconsidered with in-race data: soft-2D(NEON) + soft-or-GL-3D + GL-as-
  dumb-blit, rendering on dedicated A55 helper threads — v1's architecture, which
  is literally DraStic's. D.4 rejected M6.6 on 1.2ms MENU compositor data; in-race
  the GL cost is ~11ms, so the rejection no longer holds and M6.6 is REOPENED as
  the reserve behind R4.

**Other portable ideas (logged, not yet actioned):** branchless 2KB software
pointer-table fastmem (vs our SIGSEGV-handler fastmem — may suit A55 better;
big rearchitecture); deferred/batched GXFIFO threaded-code interpreter (our
GXFIFO is already deferred per M6.11); LLE clean-room custom BIOS + synthesized
firmware direct-boot (compat/legal, not perf). Full detail per subsystem in
docs/drastic-teardown/.

### D.7 addendum 8 — R4 single-thread split landed (device bit-exact); threaded tranche is next

Pushed liteDS-v2-android 7e54405d: the 2D final-composite capture/submit split
body behind SubmitFrame(), still SINGLE-THREADED (submit runs on the emu thread
after RunFrame). This is the correctness foundation for the actual thread.
- Capture phase (GLRenderer::VBlank): non-capture deferred frames snapshot the 3D
  color output into a shadow tex and defer the 2D composite + buffer swap.
- Submit phase (SubmitFrame after RunFrame): replay per-engine 2D composite ->
  final pass (consuming OutputTex2D) -> swap, reading the 3D shadow.
- The ONLY boundary crossing in single-thread was OutputTex3D (ColorBufferTex)
  being overwritten by the next frame's Start3DRendering at VCount 215 before
  SubmitFrame runs — resolved with a glBlitFramebuffer snapshot to SubmitShadow3DTex
  (~tens of us on Mali, no CPU packet copy this tranche). Config/VRAM stay
  live-valid because submit completes before the next RunFrame mutates them.
- Capture-active frames run inline (Tier 1 fallback, SyncVRAMCapture edge preserved);
  Reset() drains pending state.

GATES: host golden bit-exact flag OFF and ON; app builds both flags; DEVICE
screenshot-parity PASS — same savestate deferred vs inline: top(3D) maxdiff=0
(pixel-exact), bottom 98.9% (diff only animated timer/minimap, <=1-frame),
pause/save/load correct, no corruption. Perf flat as expected (single-thread).
Artifacts: apk-r4-flagON.apk, r4-app-glue.diff (MelonInstance calls SubmitFrame;
debug.litev.renderthread toggle), r4shots/.

NEXT (threaded tranche): add the render thread + depth-1 queue so SubmitFrame(N)
runs concurrent with RunFrame(N+1). THEN the per-span 2D config packet
(Layer/Compositor/Scanline/OAM snapshots, double-buffered) + VRAM/palette shadow
flat-mirror become necessary (live state is no longer submit-before-mutate), plus
deferring Start3DRendering/mid-frame composites. This is the tranche that
realizes the wall -> max(emu ~18.5, render ~11) => ~54fps win. Design in
docs/r4-render-thread-design.md; the single-thread seam proven here de-risks it.

### D.7 addendum 9 — THE DraStic gap, quantified: we waste 3 of 4 cores

Direct analysis of docs/drastic-teardown/ against our measured numbers. The
gap to DraStic is NOT emulation speed — it is the rendering architecture.

**Our emulation already fits 60fps.** M6.11 decomposition: pure emu-compute
13.56ms + scheduler residual 2.53 = 16.09ms < the 16.6ms budget. The reason
we're at 32fps is the ~11ms of full-GL renderer submission SERIALIZED into
RunFrame on the emu thread (R0), so frame = 13.5 + 11 + glue on ~1 CPU core.

**DraStic renders in software-NEON across all 4 A55 cores; GL is a dumb blit**
(docs 07/05, proven): emu on caller thread, 2D engine-B on its own thread, 3D
rasterizer in 12x16-line bands across 4 threads, GL thread does only
texSubImage2D+drawArrays+post-FX. Its wall = slowest of ~6 overlapped threads.
We use ONE core and serialize. That is the entire gap.

**R4 threaded is the fix and is the make-or-break test.** If GL submission
overlaps emulation on a second core: wall -> max(emu ~13.5, GL ~11) ~= 13.5ms
=> ~60fps, emulation-bound. Running now; the FPS delta is the campaign verdict.
Kill-criteria if R4 underdelivers (packet-copy >2.5ms, sync overhead, or
2-core memory-bandwidth contention on the shared A55 L3): fall to the M6.6
HYBRID = DraStic's exact model (soft-NEON 2D compositor + banded 3D raster on
dedicated helper threads, GL as blit) — reopened in addendum 7, this is the
proven-on-weaker-silicon ceiling and uses all 4 cores like DraStic does.

**Secondary JIT gaps (real, but only matter AFTER R4 makes us emu-bound):**
- Compile-time idle-loop detector (doc 01 §9): overview calls it the biggest
  CPU gap, BUT our ARM9 deep-dive measured Shrek-race busy-wait <0.15ms (we
  have HALT + branch-to-self detection already). Helps WarioWare-class IPC-poll
  titles, NOT the Shrek target. Port for general compat, not for this number.
- Fixed static register allocation (doc 01 §6): DraStic pins guest r0-r14 ->
  host x13-x27 and CPSR flags 1:1 -> host NZCV, so guest ALU ~1:1, zero
  per-block spill; melonDS uses a dynamic register cache. Genuine per-instr win
  on the 13.5ms emu bucket, but a large risky JIT rearchitecture — only worth
  it once R4 makes emulation the wall and we need to push 13.5 -> lower.
- melonDS ALREADY has: backward liveness/dead-flag+reg elimination
  (FloodFillSetFlags), deferred GXFIFO, HALT idle, NEON 2D/geometry. Those
  DraStic techniques are NOT gaps.

Bottom line: one architectural fix (R4, use the other cores) closes the DraStic
gap; the JIT micro-gaps are a distant second and some are already closed.

### D.7 addendum 10 — R4 threaded: precise NEGATIVE on the current seam; core-floor measurement gates the full split

Device A/B (in-race 3x): inline vs single-thread-deferred → wall 32.8ms both,
fps 30.5 vs 30.4 (Δ≈0). The landed seam (7e54405d) defers ONLY the VBlank final
composite (~2.83ms into a new `submit` bucket); RunFrame stays 28.4ms and still
issues ALL per-scanline GL (DrawScanline VRAM/palette glTexSubImage, prerender
draws, mid-frame RenderScreen composites). Threading THIS seam ceiling =
max(28.4, 2.83+1.29) ≈ 28.4ms ⇒ ~33fps — fails the 45-54 target. NOT a
sync/bandwidth failure (design kill-criterion #2) — the render work simply
isn't isolated from RunFrame. The agent correctly refused to ship a threaded
flag-ON (false win + unverifiable data-race surface).

THE GATE ON THE 4-6 DAY FULL SPLIT: RunFrame 28.4ms = core_emu + inline_GL. Our
13.5ms "emu-compute" is HEADLESS (software renderer, no GL); the APP core floor
is unmeasured. Decide before investing:
- core ~13.5ms + inline_GL ~15ms → full per-scanline split → wall max(13.5,~18)
  ≈18ms ⇒ ~55fps. Full R4 split IS worth it.
- core ~20ms+ → even a perfect split caps <60 ⇒ pivot to M6.6 hybrid (DraStic
  software-render, addendum 9) or drop internal resolution.
Cheap decisive test: measure app RunFrame with the 3D/2D renderer disabled (or
frameskip sweep, which skips rasterization) on-device. Do this BEFORE the split.
Artifacts (not pushed, HEAD still 7e54405d): r4-app-glue-profsplit.diff (the
emu/render submit-bucket profiling split — safe/useful regardless),
apk-r4-prof-split.apk, r4t_{defer,inline}.log.

### D.7 addendum 11 — core-floor measurement: frameskip invalid, but decomposition leans PROCEED

Attempted app core-only RunFrame via frameskip sweep (in-race 3x). Result:
frameskip is INVALID for isolating core — confirmed in GPU.cpp: frameskip gates
the software 2D compositor (if !SkipThisFrame, ~3-5ms) but the GL 3D render
(VCount215 CurrentRenderer->RenderFrame, synchronous in RunFrame) is gated by
!SkipThisFrame || !RenderFrameIdentical, and RenderFrameIdentical is forced
false on every geometry flush → in a racing scene the GL 3D submission runs
EVERY frame regardless of frameskip. So the movable GL-3D cost stays in RunFrame
and can't be skipped away.

Decomposition obtained (in-race 3x medians): full RunFrame ~26ms; 2D compositor
~3-5ms; floor with 2D removed (core + GL-3D-submit) ~20-22ms. Cross-ref headless
core ~13.5ms ⇒ GL-3D-submit ~6-7ms, core ~13-15ms (core~20/GL3D~0 is impossible
— GL 3D provably costs several ms). This LEANS PROCEED (core in the go-zone),
not pivot — but is not proof. Authoritative number = the per-section profiler
(LiteProfile GPU3DRunNs/ARM9ExecNs/ARM7ExecNs summed) which R4 STAGE A produces
directly: Stage A makes RunFrame emit ~zero GL, so its gated RunFrame reading IS
the app core floor from the target build. No separate measurement needed.
Also confirmed: debug.litev.renderthread is inert in the installed e3badc8 build
(expected — pre-R4-seam); the R4 seam lives only in unpushed local commits.

### D.7 addendum 12 — R4 Phase 1 (RIR) COMPLETE: monolith broken, full split surface proven bit-exact

Pushed liteDS-v2-android c55d261d..050eaa42 (5 batches). The RIR unlock worked:
every recipe §1.2 per-scanline GL site (BG/OBJ VRAM + palette uploads, PrerenderLayer/
Sprites, DoRenderSprites, per-engine RenderScreen composite, final-pass FinalPass,
Start3DRendering→Render3D) now routes through the GLLogRecord command log via
record→immediate-replay, DEVICE-verified pixel-identical to flag-OFF per batch
(counter proof inlineGL=0). The hard error-prone 80% — byte-exact payload snapshots
for every op — is done and proven on-device, incrementally, no all-or-nothing.
Host goldens bit-exact both flags. Runtime prop debug.litev.rir; flag-OFF byte-
identical at every commit. APK apk-r4-rir.apk, glue diff r4-app-glue-rir.diff.

PHASE 2 (launching): flip replay from immediate → deferred (SubmitFrame, STILL
single-thread) + add the Stage-B VRAM/palette shadow (recipe §2) for the only two
ops that read live VRAM (UploadBGVRAM/OBJVRAM, Render3D) — the config-driven ops
already fully snapshot. After Phase 2, RunFrame emits ~zero GL ⇒ its flag-ON
RunFrame reading IS the authoritative app core floor (the go/pivot number),
still single-thread (no perf win yet — that's Phase 3's thread). PHASE 3: render
thread + depth-1 queue = the wall→max(core,render) FPS win.
