# DraStic ARM64 Teardown — 01: CPU Recompiler (the crown jewel)

Reverse-engineered from the stripped arm64 build
`reference/universal/lib/arm64-v8a/libdrastic_arm64.so` (v `r2.6.0.4a`) via Ghidra
decompilation + raw objdump. Every claim is labelled:

- **[proven]** — directly readable in the decompiled/disassembled binary (addr / string / xref given)
- **[inferred]** — strong deduction from proven facts, but the exact emitted code is runtime-generated and not in the image
- **[unknown]** — too stripped / not conclusively found

> The companion `libdrastic_cpu.so` (10 KB) is **NOT** the JIT. It exports exactly one
> symbol, `Java_..._getCpuType`, and only reads `/proc/cpuinfo`,
> `/sys/devices/system/cpu/present|possible` + `getauxval`. It is a **CPU-model detector**
> (used to pick tuning at startup). All recompiler code lives in the main `.so`. **[proven]**
> (nm -D, strings, objdump of libdrastic_cpu.so)

---

## 0. TL;DR verdicts (for the melonDS 60 fps campaign)

- **Timing model = BAKED AT COMPILE TIME, batched per straight-line segment.** DraStic sums
  per-instruction cycle costs into a compile-time accumulator (`local_ac` in `FUN_0019558c`)
  and flushes it as a **single `SUB w12, w12, #imm12`** (opcode `0x5100018c`, plus
  `0x5140018c` = `SUB …, LSL#12` for the high bits) at each block-boundary/exit instruction,
  then resets the accumulator — **not** a per-instruction decrement. Costs come from static
  tables: per-instruction base byte at `instr_desc+0x1b`, block-entry base = 2 (ARM) / 4
  (Thumb), LDM/STM count via popcount table `0x0402dc08`, memory waitstates via
  `UNK_0020d8a0/0020d9a0` (`[region][seq][cpu]`). `w12` is the live cycle **down-counter**
  (spilled to `cpu+0x2290`); the slice budget = delta to the next scheduler event. This
  matches melonDS's proven model — **relaxing per-instruction timing would regress DraStic
  the same way it regressed our fork.** **[proven — emitted SUB opcodes + accumulator + tables]**
- **Idle skip = TWO mechanisms.** (a) **Compile-time idle-loop detection:** the pre-compile
  scanner (`FUN_00136584`) recognises a block whose first branch jumps back to its own start
  and whose body only recirculates its own state (a pure poll); the epilogue emitter
  (`FUN_00195c78`) then bakes `MOVN w12,#0` (opcode `0x1280000c`, sets the cycle counter
  negative) at block entry so the loop **immediately yields to the scheduler instead of
  spinning natively** → time fast-forwards to the next event. (b) **Runtime HALT/WFI:** CP15
  WFI (`FUN_001336ac`) / NDS HALTCNT reg 0x301 (`FUN_00124f3c`) set `cpu+0x2110` and unlink
  the CPU from the scheduler; the run loop only enters the JIT when that flag is 0. **[proven]**
- **Translation cache = one contiguous RWX mmap, 3 arenas + 2-level page table.** 16 MB
  "main" + 1 MB "itcm" + 2 MB "alternate", bump-allocated. Guest-PC→host-block dispatch uses
  a **2-level page-indexed table** (2 KB pages: L1 index `(pc>>11)&0x1fffff`, entry =
  `hostPageBase>>2 | SMC-bit`, then `*(u32*)(hostPageBase+pc)` = 32-bit host block offset).
  A parallel C resolver (`FUN_00135fe8`) also keeps direct arrays (`cpu+0x2270/0x2278`) +
  hash tables and does `DC_CIVAC`/`IC_IVAU`/`ISB` on freshly-written code. **[proven]**
- **Directly actionable:** DraStic's speed on weak ARM comes from (1) **liveness analysis**
  (per-block *live register* + *live flag* masks → dead NZCV/dead-reg elimination),
  (2) **condition folding** (rewriting condition codes / NOP-ing redundant ones at decode),
  (3) **specialised LDM/STM stubs** (`arm64_load_block1..16` / `arm64_store_block1..16`,
  one per register count), (4) **fixed global register allocation** (guest r0–r14 pinned to
  host x13–x27, cheap ARM9↔ARM7 swap via spill to context), (5) **software SMC bitmap +
  software pagetable memory stubs** (no per-write mprotect faults), (6) **compile-time
  idle-loop detection**. Items (1)(2)(3)(6) are the highest-value ideas to port.

---

## 1. Address / symbol anchor table

All "names" are inferred unless they are real exported/​string symbols.

| Addr | Inferred name | Evidence |
|------|---------------|----------|
| `FUN_0011be00` | `cpu_boot / start_game_native` | mprotect RWX + `setjmp`; JNI `startGame` [proven] |
| `FUN_0011bff4` | `emu_init` | calls cpu-init ×2 + cache-init [proven] |
| `FUN_00132904` | `cpu_init(ctx, base, id, other_ctx)` | sets `+0x210c` id, lookup tables, timing table [proven] |
| `FUN_00196194` | `tcache_init` | `mprotect(base,16MB)`,`mprotect(base+16MB,1MB)` [proven] |
| `FUN_00139560` | `tcache_flush / reset` | mprotect all 3 arenas, memset lookup tables [proven] |
| `FUN_001327bc` | `recompiler_entry` (ARM9-primary dispatch) | stored into ctx run-ptr; sets budget; indirect-jumps to generated stub [proven] |
| `FUN_00132794` | `dispatch_tail_arm7` | `arm7_cycles += executed`; jump generated stub [proven] |
| `FUN_0012d6f8` | `scheduler_tick` (delta queue) | advances time, fires event callbacks [proven] |
| `FUN_0012d264` | `scheduler_insert_event` | sorted delta-list insertion [proven] |
| `FUN_00132268` | `raise_exception(ctx, vec)` | banked mode switch, SPSR save, vector [proven] |
| `FUN_001325b0` | `cpu_halt` (block_halt handler) | sets `+0x2110=1`, deactivates scheduler slot [proven] |
| `FUN_001325cc` | `cpu_wake_on_irq` | clears halt, raises IRQ [proven] |
| `FUN_00135fe8` | `block_lookup_or_compile(ctx, pc)` | 14 callers; direct arrays + hash; miss→compile; DC_CIVAC/IC_IVAU/ISB [proven] |
| `FUN_00136584` | `analyze_block(ctx, pc&~1, thumb)` | decode + IR build + liveness + **idle-loop detection** [proven] |
| `FUN_0019558c` | `compile_block` (per-block emit loop) | picks arena cursor, calls per-instr translator, bakes cycle SUB, patches local branches [proven] |
| `FUN_001913dc` | `translate_instr` (single ARM instr → arm64) | 14 KB; writes arm64 words via cursor `ctx+0x408`; fixed reg map [proven] |
| `FUN_00195c78` | `emit_block_epilogue` | terminator/dispatch branch + idle-yield `MOVN w12,#0` [proven] |
| `FUN_001911a0` | `emit_pc_immediate` | materialises r15/PC constant (MOVZ/MOVK) into scratch [proven] |
| `FUN_00126a40` | `mem_slow_resolver / compile-on-miss` | region-table walk, MMIO dispatch, lazy pagetable fill [proven] |
| `FUN_00182794` | `arm64_load/store stub` (canonical) | 2 KB-page pagetable probe; fast direct vs slow call [proven] |
| `FUN_00139880` | `liveness/lookup-table bookkeeping` (NOT the emitter) | maintains per-reg liveness bitmaps + block-ptr tables (SMC/invalidation) [proven] |
| `FUN_001494a4`,`FUN_00141cdc`,`FUN_00140f48`,`FUN_0013f074` | codegen helpers | batched 16-byte instruction-word stores [proven] |
| `FUN_00137af4` | `alloc_liveness/lookup entry` | bookkeeping entries (NOT executable inter-block patches) [proven] |
| `FUN_001336ac` / `FUN_00124f3c` | CP15-WFI / HALTCNT(0x301) handlers | set `cpu+0x2110` halt/stop [proven] |
| `FUN_001397d8` | `smc_invalidate_page` | per-page tag mismatch → retranslate [proven] |
| `FUN_00133d0c` | `mark_code_pages` | sets code-presence bitmap `+0xef1b8` [proven] |
| `FUN_001340d0` | `smc_check` | tests code bitmap on access [proven] |
| `FUN_00117460` | `crash_dump` (SIG_SEGV) | dumps cpu state + tcache layout [proven] |
| `FUN_00195e98`/`FUN_00195420` | `block_profile_dump` | per-block exec/instr stats [proven] |
| `FUN_00185184` | `interpreter_dispatch` (debug path) | manual ARM9/ARM7 interleave, reg swap [proven] |
| `FUN_00185464` | `arm64_disassembler` | names stubs via symtab `DAT_0024b200` [proven] |
| `FUN_0017e3f4`/`FUN_0017ef28` | ARM / THUMB disassemblers | used by block profiler [proven] |
| `FUN_001d2168` | `setjmp` (libc) | CPU-exit longjmp target = `DAT_03d7c800` [proven] |

Runtime **fixed stubs** (names are real strings @ `0x1104c8..0x11097f`): `recompiler_entry`,
`recompiler_cpu_next_action_arm9_to_arm7`, `recompiler_cpu_next_action_arm7_to_event_update`,
`block_indirect_branch`, `block_itcm_branch_arm`, `block_itcm_branch_thumb`, `block_halt`,
`arm64_load_block1..16`, `arm64_store_block1..16`. These are emitted once at init into the
cache and referenced by generated code. **[proven — strings]**

---

## 2. The CPU context struct (`cpu_state`)

Two instances exist: ARM9 at global `DAT_015ccd50` (`+0x91000`-relative base), ARM7 at
`DAT_025d3340`. Offsets proven from `FUN_00117460` (crash dump), `FUN_001327bc`,
`FUN_00132904`, `FUN_00185184`:

```
cpu+0x0010  : last-slice executed cycles (fed to scheduler)          [proven]
cpu+0x2088  : translation-cache base pointer (= global + 0x91000)    [proven]
cpu+0x2108  : IRQ pending                                            [proven]
cpu+0x210c  : cpu id (1 = ARM9, 0 = ARM7)                            [proven]
cpu+0x2110  : HALT/run state  (&6 != 0 ⇒ halted/stopped)             [proven]
cpu+0x2258  : pointer to scheduler / global emu base                 [proven]
cpu+0x2270  : ITCM block table, ARM   (ARM9 only) → global+0x1551038 [proven]
cpu+0x2278  : ITCM block table, THUMB (ARM9 only) → global+0x1559038 [proven]
cpu+0x2290  : CYCLE DOWN-COUNTER (signed; <0 = budget remaining)     [proven]
cpu+0x2298  : current dispatch fn ptr (generated recompiler_entry)   [proven]
cpu+0x2360  : cache/lookup base ptr handed to generated code         [proven]
cpu+0x2370  : guest register file r0..r15 (16 × u32)                 [proven]
cpu+0x23b8  : NZCV / host flags mirror                               [proven]
cpu+0x23bc  : guest PC (next)                                        [proven]
cpu+0x23c0  : CPSR                                                   [proven]
cpu+0x23d0  : instruction-fetch region base (used by FUN_00127f5c)   [proven]
```

Key correction (from emit-side analysis): during block execution the guest registers are
**pinned in fixed host registers** (see §6 — guest r0..r14 = host x13..x27, x28 = context
pointer, w12 = cycle counter). The in-memory register file at `cpu+0x2370` is the
**spill / context-swap home**, written back on block exit / when switching CPUs. ARM9↔ARM7
switch = point `x28` at the other context + spill/reload the pinned regs; the cycle counter
`+0x2290` is saved/restored (`FUN_001327bc` lines 23550/23669). **[proven]**

---

## 3. Boot, the RWX region, and the setjmp/longjmp exit

`FUN_0011be00` (JNI `startGame`): **[proven]**

```c
if (code_region == NULL) {
    code_region = &DAT_0024d000;
    if (mprotect(&DAT_0024d000, 0x3b30000, PROT_READ|WRITE|EXEC/*7*/)) return -1;
}
...
setjmp(&DAT_03d7c800);              // FUN_001d2168 == libc setjmp
if (exit_flag /*DAT_03d7d000*/ != 1) {
    ... FUN_001327bc(&DAT_0024d000);   // = recompiler_entry (or interpreter path)
    return 0;
}
```

- One giant **~59 MB RWX** mapping (`0x0024d000 + 0x3b30000`) holds emulator state + the JIT
  cache + fastmem. W^X is **not** enforced (RWX = 7). **[proven]**
- `setjmp` here is the **single CPU-exit trampoline**. Any deep exception / "stop the world"
  event (`longjmp` to `DAT_03d7c800`) unwinds straight back to the run loop without manually
  popping the generated-code call stack. `DAT_03d7d000` distinguishes "clean return" from
  "abort". **[proven]**
- The JIT cache proper is (re)armed separately by `FUN_00196194`:
  `mprotect(base,0x1000000,7)` (16 MB main) + `mprotect(base+0x1000000,0x100000,7)` (1 MB
  itcm). `FUN_00139560` (full flush) additionally mprotects `base+0x1100000, 0x200000` (2 MB
  alternate). **[proven]**

---

## 4. Translation cache: structure & lookup

### 4.1 Arenas (contiguous, bump-allocated)
From `FUN_00117460` crash dump + `FUN_0019558c` cursor selection: **[proven]**

```
[ base ................ base+0x1000000 )  main       (16 MB)  ← 0x02xxxxxx main-RAM blocks
[ base+0x1000000 ...... base+0x1100000 )  itcm       ( 1 MB)  ← ARM9 <0x02000000 blocks
[ base+0x1100000 ...... base+0x1300000 )  alternate  ( 2 MB)  ← everything else
[ base+0x1300000 ...... ]                 hash-tables + link/patch scratch
```

Each arena is a **double-ended bump allocator**: the crash dump prints, per arena,
`(cursor - start)` used and `(end - other_cursor)` free, from cursor pairs
`DAT_01420000/08` (main), `DAT_01420010/18` (itcm), `DAT_01420020/28` (alternate). Blocks
grow from one end; secondary "stub" instructions grow from the other. When an arena fills,
the whole cache is flushed (`FUN_00139560`) rather than doing fine-grained eviction. **[proven]**

### 4.2 Lookup — `FUN_00135fe8(ctx, pc)` **[proven]**
```
cache = *(ctx + 0x2088)
if (pc < 0x02000000 && ctx.id == ARM9):        // ITCM / low ARM9 code — DIRECT table
    tbl  = thumb ? *(ctx+0x2278) : *(ctx+0x2270)
    idx  = thumb ? (pc>>1)&0x3fff : (pc>>2)&0x1fff
    off  = tbl[idx];  if (off) return cache+off        // 32 KB directly indexed
else:                                            // HASHED buckets
    is_mainram = (pc & 0xff000000)==0x02000000
    mask   = is_mainram ? 0x7fff : 0x1fff              // 32K or 8K buckets
    table  = is_mainram ? cache+0x1300000 : cache+0x1380000
    bucket = table + ((mask & (pc>>2)) << 2)*4         // 16-byte bucket
    // bucket = { tag0,u32 off0, tag1,u32 off1 }  ; off1 doubles as chain head
    if (bucket.tag0==pc) return cache+bucket.off0
    if (bucket.tag1==pc) return cache+bucket.off1
    walk chain via block-header word at (off-4) ...
if miss: block = FUN_00136584(ctx, pc&~1, pc&1); insert (tag,off) into table/bucket
```

- **Block pointers are 32-bit offsets** from `cache` (keeps the cache relocatable and halves
  pointer footprint). **[proven]**
- Main-RAM & ITCM get their own fast tables because they are the hot code regions; VRAM /
  ARM7-RAM / BIOS share the smaller hashed table. **[proven]**
- `DAT_014d8038[...]` is a **per-page code-flag byte array** touched on insert
  (`if (8 < flag) flag |= 0x80`) — marks pages that now contain compiled code so writes to
  them are trapped (§8). **[proven]**

### 4.3 Block descriptor (parallel metadata array @ `global+0x91000`)
From `FUN_00195e98` (block profiler): **[proven]**
```
+0x91004 : guest start address (u32)
+0x9100c : live-registers mask (u16)   ← liveness result
+0x9100e : live-flags mask     (u16)   ← liveness result
+0x91010 : flags: bit15=cpu, bit12=thumb, low12=instruction count
+0x91012 : secondary-stub instruction count (u16)
```
The profiler can print each block's guest disassembly, its translated size, and live
reg/flag masks — DraStic ships a **self-profiler** (`profiles/..._translation_post.txt`,
`Block exec`, `Block * ins exec`, `Block size` counters). Directly relevant to your
melonDS-profiler project as a reference feature set. **[proven]**

The **compile-time** descriptor (transient, `FUN_0019558c` param) and its per-instruction
slot array (stride **0x20**) are separate from the persistent metadata above: **[proven]**
```
block header:  +0x08 instr-slot array ptr   +0x20 (u16) instr count
               +0x29 flags (bit2 = idle-loop marker)   +0x2a terminator type (1/2 normal, 4 return)
instr slot:    +0x08 host code addr for this instr (local-branch back-patch target)
               +0x12 (u16) read-set mask     +0x14 (u16) mask; BIT15 = cycle-flush boundary
               +0x16 (u16) combined live mask (= 0x14|0x12|0x16)     +0x1b (u8) cycle cost
in-cache prefix: 0x18-byte header before entry code (guest start PC+flags, code offset,
               PC-mirror u16, flags, link) — writer/reader offsets consistent; exact bit
               layout partly inferred. [proven offsets / partly inferred bit layout]
```

### 4.4 Block linking = dispatcher-return, **no direct block-to-block patching** **[proven]**
- A block epilogue (`FUN_0019558c`/`FUN_00195c78`) materialises the *next* guest PC as an
  immediate then emits `B` (`…|0x14000000`) to a **fixed shared dispatch stub**
  (`&LAB_001822f0` normal, `&LAB_001822c0` return-type, chosen by terminator byte). It never
  branches straight into another block's body. **[proven]**
- The block **prologue** emits the budget guard `TBZ w12,#31,+8` (`0x36f8004c`) + `BL`
  exit-helper. **[proven]**
- The **only** self-modifying patching is **intra-block** local-branch resolution
  (`LAB_00195b58`): the previously-emitted branch's imm26 is rewritten to the recorded host
  address (`instr_slot+0x08`) of a same-block target. **No inter-block patch site exists** —
  so `FUN_00137af4`'s "link" entries are liveness/lookup bookkeeping, not executable edges.
  **[proven]**
- Re-entry into the JIT is always through the code pointer at `cpu+0x2298`
  (= `FUN_00135fe8(...) + 8`, skipping the 8-byte in-cache header). **[proven]**

---

## 5. Dispatch, scheduler & the timing/cycle model  ⭐

### 5.1 Delta-queue scheduler — `FUN_0012d6f8` **[proven]**
Events form a singly-linked delta list; each node = `{u32 countdown, ?, fn callback,
arg, next, ...}`. Per tick: `global_time += executed_cycles`; subtract executed from head
countdown; while head fires, invoke callback and advance. `FUN_0012d264` inserts a new event
keeping the list sorted by cumulative delay. This is a textbook cycle-accurate event queue.

### 5.2 The run loop — `FUN_001327bc` (recompiler_entry) **[proven]**
```c
FUN_0012d6f8(sched);                       // fold last slice into scheduler, fire events
// ... service ARM9 IRQ (cpu+0x2108) and ARM7 IRQ, raise_exception if unmasked ...
budget = *(sched + 0x318)->countdown;      // cycles until next event  (int)
cpu.executed(+0x10) = budget;
cpu.cycle_ctr(+0x2290) += budget;          // replenish the down-counter
if (halt/irq pending /*cpu+0x2110*/)  cpu.cycle_ctr = 0xffffffff;   // = -1 → exit at once
(*(code**)(cpu+0x23b0))(cpu_or_block);     // JUMP into generated recompiler_entry stub
```
ARM7 mirror = `FUN_00132794` (`arm7.cycle_ctr += executed; jump arm7 stub`). **[proven]**

### 5.3 Cycle-counter semantics **[proven]** — down-counter, host reg `w12`
`w12` (home `cpu+0x2290`) is a signed **down-counter**: **positive = cycles remaining**;
generated code **subtracts** each segment's baked cost (`SUB w12,w12,#imm`); the slice ends
when it goes **negative**. Proven:
- Block prologue emits the budget test `TBZ w12,#31,+8` (opcode `0x36f8004c` — "branch if
  sign bit clear, i.e. still ≥0") falling through to a `BL exit-helper` when exhausted
  (`FUN_0019558c`, `piVar9[1]=0x36f8004c`, `piVar9[2]=…|0x94000000`). **[proven]**
- Idle-loop / IRQ / halt force-exit set the counter negative: idle bakes `MOVN w12,#0`
  (`0x1280000c`, = −1); IRQ writes `0xffffffff` (= −1) at `FUN_001327bc` line 23419. **[proven]**
- Run loop replenishes with the event budget and re-enters while still solvent:
  `FUN_00185184` `t = cycle_ctr + next_event_delta; if (t >= 0 && !halt) run_block(); …`. **[proven]**
- Absolute-cycle helpers compute `(base_time + slice_len) − cycle_ctr` (13785/13959/…). **[proven]**
- `w12` is spilled to `cpu+0x2290` on stub calls / context switch (`FUN_00182794`:
  `*(u32*)(x28+0x2290)=w12`). **[proven]**

### 5.4 Where the cost comes from — **BAKED at translate time, emitted as batched SUBs** ⭐
The compiler holds a compile-time integer accumulator `local_ac` in `FUN_0019558c` and
flushes it into the block as a **`SUB w12,w12,#imm12`** at each boundary/exit instruction,
then resets it — proven from the emitted opcodes:

- **Init / block-entry base cost** (`FUN_0019558c` ~149785): `local_ac = 0;` then a small
  base constant (2 vs 4 depending on ARM/Thumb — the two evidence passes disagree which is
  which; the base-constant behaviour is certain). **[proven; which-is-which minor uncertainty]**
- **Per-instruction add** (~149861): cost = descriptor byte `*(byte*)(instr_slot+0x1b)`;
  for ARM9 scaled by a mode/wait multiplier keyed on `ctx+0x2258`, else `<<1`; then
  `local_ac += cost`. **[proven]**
- **Gated flush** (~149872): only when the instruction's flags word `*(short*)(instr+0x14) < 0`
  (bit15 = boundary/can-exit) →
  `*hi = (local_ac>>2 & 0x3ffffc00) | 0x5140018c;` (`SUB w12,w12,#imm,LSL#12`, if `local_ac>0x1000`)
  and `*lo = ((local_ac & 0xfff)<<10) | 0x5100018c;` (`SUB w12,w12,#imm12`), then `local_ac=0`. **[proven]**
- **Tail flush** after the decode loop (~149930): same two-opcode SUB for the residual. **[proven]**
- Cost **inputs** are static tables built once at `cpu_init` (guarded by `DAT_0402dd07`,
  `FUN_00132904`): per-instruction base bytes; LDM/STM register-count via popcount table
  `DAT_0402dc08` (`cost += table[reglist>>8] + table[reglist&0xff]`, `FUN_00136584` @26923 /
  `FUN_001913dc` @148370); memory waitstates via `UNK_0020d8a0/0020d9a0` (`[region][seq][cpu]`,
  also used by the DMA cost path @20970/21045). **[proven]**

**Verdict: BAKED, and now proven at the instruction-encoding level.** A block subtracts its
per-segment cycle cost with one/a few `SUB w12,#imm` — never a per-instruction decrement.
This is exactly melonDS's model; **do not move DS per-instruction timing to runtime.**

> **melonDS takeaway:** DraStic confirms your finding. Its whole design assumes the block
> cost is a compile-time constant; the only runtime timing work is (a) `+=budget` at slice
> start and (b) the memory-access path adding region waitstates. Do **not** move DS
> per-instruction timing to runtime.

---

## 6. Register allocation, liveness & condition folding (the speed core)

### 6.1 FIXED global register allocation (host ABI) **[proven]**
The arm64 build uses a **static** guest→host mapping, applied unconditionally at 32 sites in
`FUN_001913dc` (guest reg N → host reg **N+0xd**: Rn field `(N+13)<<5`, Rm `(N+13)<<16`,
Rd `(N+13)`):

| Host arm64 reg | Holds |
|---|---|
| `x13 … x27` | **guest r0 … r14** (pinned) |
| — | **r15/PC**: not pinned — materialised as an immediate constant (MOVZ/MOVK via `FUN_001911a0`) into a scratch reg wherever used |
| `x0 … x4` | scratch / temps (PC materialisation, flag save) |
| `w12` | **cycle down-counter** (spill home `cpu+0x2290`) |
| `x28` | **guest CPU context pointer** (base for all `cpu+…` offsets) |
| `x9` | block-lookup pagetable base |
| `x30` | host LR |
| host **NZCV** | **guest CPSR condition flags** (translator emits `MRS x,NZCV`=`0xd53b4200` / `MSR NZCV,x`=`0xd51b4200`; spill slot `cpu+0x2354`) |

Consequences: guest ALU ops map almost 1:1 to arm64 ops on the pinned registers; guest
condition flags *are* the host flags (no emulated NZCV word on the hot path); the register
file at `cpu+0x2370` is only touched to spill on stub calls / CPU switch. The `FUN_00139880`
"liveness bitmaps" therefore drive **dead-code / writeback elimination**, not host-register
assignment (which is fixed). **[proven]**

### 6.2 Liveness analysis — `FUN_00136584` backward pass **[proven]**
The compiler builds an IR array (32-byte entries), each carrying **used** / **defined**
register masks (`+0x14`/`+0x16`) and **used**/​**defined** flag masks (`+0x18`/`+0x19`), then
walks the block **in reverse** computing:
```
live_regs_in  = used_regs  ∪ (live_regs_out  − defined_regs)
live_flags_in = used_flags ∪ (live_flags_out − defined_flags)
```
(seen as `bVar1 & (bVar1>>4 ^ 0xff)` = use & ~def, propagated up). Branch/terminator
instructions reset liveness to "all live" (`0x7fff` regs, `0xf` flags) conservatively for
cross-block edges. The per-block results are stored in the descriptor (`+0x9100c`/`+0x9100e`).
This lets the emitter **skip generating NZCV updates and register writebacks that are
provably dead** — the single biggest reason DraStic is fast on weak ARM. **[proven]**

### 6.3 Condition-code folding — decode pass **[proven]**
During instruction fetch/decode (`FUN_00136584` @ ~26700): when consecutive instructions
share a condition it rewrites them — either to `0xe1a00000` (canonical `MOV r0,r0` NOP) or by
OR-ing `0xe0000000` (force `AL`/always) into the opcode. Effectively it **collapses
conditional runs into unconditional straight-line code with one guard**, eliminating repeated
flag tests. **[proven]**

---

## 7. Memory access: pure helper-call + software pagetable

**No inline fast path is ever emitted into a block.** Every guest load/store emits address
setup (`ADD/SUB`-imm, `MOV`) + a `BL` to a **size/sign/CPU/reg-count-specialised stub**; the
fast/slow decision happens at runtime *inside* the stub. This keeps blocks tiny (good for
i-cache on weak ARM) at the cost of one `BL` per access. **[proven]**

### 7.1 Stub taxonomy **[proven — strings + PTR dispatch tables]**
- Loads: `arm64_load_memory{8s,8u,16s,16u,32u,64}`, `arm64_load_ext{8,16,32}`.
- Stores (per-CPU): `arm64_store_memory{8,16,32}_{arm9,arm7}` — the ARM9/ARM7 variant is
  chosen at **compile time** from `ctx[0x11e]` (offset 0x478): `FUN_001913dc` ~147105
  `p=&LAB_00182ac0; if (ctx[0x11e]!=1) p=&LAB_00182fd0; BL p`.
- LDM/STM: `arm64_load_block1..16` / `arm64_store_block1..16` — **index = register count**,
  selected via `PTR_LAB_0024b100[n]` / `PTR_LAB_0024b180[n]` (`FUN_001913dc` @148563/148591/148633).
  One straight-line stub per N instead of an emitted loop. **[proven]**

### 7.2 Inside the stub — software pagetable (`FUN_00182794`, canonical) **[proven]**
```c
entry = *(long*)(x9 + ((addr >> 11) & 0x1fffff) * 8);   // 2 KB pages, 2M-entry L1
host  = entry * 4;                                        // entry stores host_ptr >> 2
if (host != 0)  return *(uint*)(host + addr);            // FAST: direct RAM access
// else SLOW:  spill w12–w18,x30,NZCV; call FUN_00126a40(addr)
```
- Bit `0x4000000000000000` in an entry marks an **SMC-tracked** page. **[proven]**

### 7.3 Slow resolver `FUN_00126a40` **[proven]**
- `addr>>0x1c != 0` → open-bus `0xffffffff`.
- Region table at `ctx+0x1000000`, index `(addr>>0x17)&0x1ff` (**512 regions × 8 MB**, stride
  `0x60`). Type byte `desc[0x16]`: `2` = **MMIO** → indirect call `desc+0x18`; `0/1` = RAM →
  **lazily install** the 2 KB pagetable entry (so subsequent accesses hit the fast path) and
  set the SMC bitmaps.
- So the region descriptor table (96-byte entries, direct-base vs handler) is the *slow-path*
  structure; the 2 KB pagetable is the *fast-path software TLB* it populates on demand.
  (This is the same region table surfaced elsewhere as `DAT_010023d0`; my earlier
  `+0x58/+0x59` type-byte reading is the same mechanism at a slightly different field offset.)

---

## 8. Self-modifying-code (SMC) handling — software bitmaps, no guest mprotect

DraStic does **not** write-protect guest RAM with mprotect (that would fault on every game
write). Instead: **[proven]**

1. **On compile**, `FUN_00133d0c` marks every guest page the block was translated from in a
   **code-presence bitmap** at `mem_ctx + 0xef1b8` (indexed `addr>>6`, bit `(addr>>1)&0x1f` —
   ~2-byte granularity) and sets per-page code flags (`DAT_014d8038`, `DAT_014da138`, indexed
   `addr>>7 & 0x7fff` bit `(addr>>2)&0x1f`). **[proven]**
2. **On guest store**, the write helper tests that bitmap (`FUN_001340d0` @ 24820/24936:
   `if (code_bitmap[page] & (1<<bit))`). A hit means the write lands on compiled code. **[proven]**
3. **Invalidate**: `FUN_001397d8` compares a per-page **tag** (`page*0x10 + 0x14`); on
   mismatch it zeroes the affected lookup-table entries (`*entry = 0xffffffff`) and
   retranslates. Fine-grained per-page invalidation, not a full flush. **[proven]**

This is cheaper than fault-based SMC for the DS's write-heavy main RAM, and is the reason
code-containing pages are forced onto the checked store path (the `|0x80` page flag in §4.2).

---

## 9. Idle / halt fast-forward — TWO mechanisms

### 9.1 Compile-time idle-loop detection (software spin loops) **[proven]**
DraStic **does** recognise guest busy-poll loops at translate time — the famous "idle skip":
- The pre-compile scanner `FUN_00136584` records the block start (`local_88`) and tests
  whether the block's first branch jumps **back to its own start**
  (`(*(uint*)(branchDescr+3) & ~1) == local_88`, ~L972) and that the body only recirculates
  its own state (ORing per-instruction effect masks, field `instr+0x14`); a pure poll sets
  the **idle flag** `local_77 |= 4` (~L1012). **[proven]**
- The epilogue emitter `FUN_00195c78`, when that idle bit is set (`descriptor[0x450].+0x29
  bit2`) and the branch self-links, bakes **`MOVN w12,#0`** (`0x1280000c`, w12 = −1) at block
  entry. Result: on entry the budget guard (`TBZ w12,#31`) immediately fails and the block
  **yields straight back to the scheduler** instead of spinning natively — the event queue
  then fast-forwards to the next IRQ/timer. **[proven]**

This is the single biggest reason DS games with software idle loops (poll `IF`/`VCOUNT`)
cost DraStic almost no host time. **melonDS does not do this compile-time detection — a
high-value port target.**

### 9.2 Runtime HALT / WFI (hardware halt) **[proven]**
- CP15 c7 WFI handler `FUN_001336ac` and NDS HALTCNT I/O reg `0x301` handler `FUN_00124f3c`
  set `cpu+0x2110` (1 = HALT/WFI, 2 = STOP; tested `& 6`). The `block_halt` symbol (string
  `0x110575`) is the hand-asm entry for this path. **[proven]**
- Halt entry `FUN_001325b0` sets `0x2110 = 1` and unlinks the CPU's scheduler event
  (`FUN_0012c838`) so time fast-forwards; the run loop only dispatches into the JIT when the
  halt flag is 0 (`FUN_00185184` L102169: `if (cpu+0x2110 == 0) run_block(); else skip`).
  **[proven]**
- Wake `FUN_001325cc` clears halt on a pending IRQ (`cpu+0x2108`), raises the IRQ vector, and
  re-arms scheduling; a `>1` (STOP) value also gates the companion CPU (`cpu+0x22a0`). **[proven]**

---

## 10. ARM9↔ARM7 interleave & exceptions

- `recompiler_cpu_next_action_arm9_to_arm7` / `..._arm7_to_event_update` (strings) name the
  round-robin: **run ARM9 for a slice → switch to ARM7 → run scheduler/event update → repeat**.
  The two CPUs run to the same wall-clock event boundary; the primary counter is ARM9's, ARM7
  is stepped in its own dispatch tail (`FUN_00132794`). **[proven strings + dispatch code]**
- Exceptions (`FUN_00132268`): vector `< 8` uses a jump table of hardwired handlers; otherwise
  it performs the ARM banked-mode switch — save CPSR→SPSR (`+0x20e8[mode]`), load new mode
  bits (`DAT_0020db08[vec]`), set PC = `vec*4` (+ VBAR/exception base for ARM9), set the
  IRQ-disable bit `0x80`. IRQ = vector 6. **[proven]**

---

## 11. DraStic-specific optimisations worth porting (ranked for a 60 fps melonDS)

1. **Per-block liveness → dead-flag & dead-register elimination.** melonDS already elides some
   flags; DraStic's explicit *live-flags* + *live-regs* bitmask per block is more aggressive.
   Highest ROI. **[proven mechanism]**
2. **Condition folding at decode** (collapse conditional runs, NOP redundant guards). **[proven]**
3. **Compile-time idle-loop detection** (§9.1) — self-loop poll → bake `MOVN w12,#0` to yield
   immediately. melonDS lacks this; very high value for DS games that software-spin. **[proven]**
4. **Specialised LDM/STM stubs per register count** (`arm64_(load|store)_block1..16`) — avoids
   emitted loops on the DS's very common block-copy code; selected via `PTR_LAB_0024b100/180`.
   **[proven]**
5. **Fixed global register allocation** (guest r0–r14 → host x13–x27, guest CPSR flags = host
   NZCV) — near-1:1 ALU translation, no emulated flag word on the hot path. **[proven]**
6. **Software pagetable memory stubs** (2 KB-page TLB filled lazily; no inline fastmem in
   blocks → tiny blocks) + **software SMC bitmap** instead of mprotect faults. **[proven]**
7. **Dispatcher-return linking with a 2-level page table** (no fragile inter-block patching to
   invalidate on SMC). **[proven]**
8. **Baked compile-time cycle cost emitted as batched `SUB w12,#imm`** (popcount + waitstate
   tables). *Confirmation to stay the course*, not a change. **[proven]**

---

## 12. Open items / honest gaps (post emit-side analysis)

Most earlier gaps are now **closed** by reading the encoded opcodes the emitter writes
(register map, cycle-SUB emission, idle-yield, dispatch model — all proven above). Remaining:

- **Block-entry base cost polarity**: which of {2,4} is ARM vs Thumb — two evidence passes
  disagree; the base-constant behaviour itself is certain. **[minor uncertainty]**
- **In-cache block prefix bit layout** (the 0x18-byte header): writer/reader field offsets are
  consistent, but a few sub-fields show Ghidra self-assignment artifacts. **[partly inferred]**
- **`recompiler_cpu_next_action_*`** exact ARM9↔ARM7 hand-off state machine beyond the
  proven "run ARM9 slice → ARM7 → events" order — the stubs are hand-asm with no C xref.
  **[partially-unknown]**
- **Sign-direction footnote**: `w12` is a down-counter (positive = remaining, `SUB` consumes,
  exit when negative). An earlier pass phrased it inverted; §5.3 is the corrected, opcode-
  backed version. **[resolved]**
