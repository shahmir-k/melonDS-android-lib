# DraStic Teardown — DOC 09: DMA / Timers / RTC / IO / Interrupts / Cart / Backup

Scope: the DS device subsystems — DMA (4 ch/CPU), timers (4/CPU), RTC, the IO
register dispatch (device-semantics side; the memory agent covers the routing
mechanism), the interrupt controller (IE/IF/IME), cartridge/ROMCTRL + KEY1, and
backup memory (EEPROM/FLASH/SRAM/NAND).

Evidence base: `scratchpad/decomp/all_decomp.c`, `strings_arm64.txt`,
`sym_imports.txt`. Confidence: [proven-from-binary] / [inferred] / [unknown-too-stripped].

State-struct convention: nearly all core functions take `param_1` = pointer to the
big per-machine state block. IO register shadows live at `param_1 + 0x23070` (and a
second CPU view at `+0x1b070`). The per-CPU "live" hardware block is reached via
`DAT_01000010 + *(param_1 + 0xfba88/0xfba90)` (the two CPUs). [proven-from-binary]

---

## 1. IO register read/write dispatch (device-semantics overview)

DraStic dispatches IO by the **register offset within the 0x04000000 page**, using
large `switch` statements plus a backing **shadow array**. [proven-from-binary]

- Read path: `FUN_00123280(state, offset)` (16-bit read) and siblings. Structure:
  ranged `if (offset < 0x1a0) … else if (offset < 0x204) switch … else …`, with a
  fallthrough `return shadow[offset & 0x7fff]` for plain registers. Special
  registers (timers, ROMCTRL, IPC) are **computed on demand** rather than stored.
- Write path: one big `switch(offset)` (the function containing the DMA/timer/IRQ
  cases at `all_decomp.c:15062-15400`). Most writes update the shadow and, for
  registers with side effects (DMA enable, IF acknowledge, IME/IE, IPC, timers),
  run device logic inline.

Because reads of volatile registers are computed from scheduler state, DraStic
avoids maintaining a live counter for every timer/ROM transfer each cycle — it
reconstructs the value only when the guest reads it (see Timers §3). [proven]

Actionable: this is the standard fast approach (shadow array + on-demand volatile
regs). melonDS already does similar; the notable bit is that the shadow array is a
flat `offset & 0x7fff` window shared with fastmem, so the common non-side-effect IO
read is a single indexed load.

---

## 2. DMA — 4 channels per CPU

### 2.1 Register writes and triggering — `FUN_00125da0` (32-bit IO write switch)

DMACNT (control, 32-bit) writes land on offsets **0xB8 (DMA0), 0xC4 (DMA1),
0xD0 (DMA2), 0xDC (DMA3)** in `FUN_00125da0`; the 16-bit-write path `FUN_00125588`
handles the CNT-high halves (0xBA/0xC6/0xD2/0xDE). Note: `param_2` here is the
**already-decoded low IO offset** (0x000–0x6xx), not a full 0x040000xx address.
[proven-from-binary] On each 32-bit CNT write:

```
write_DMAxCNT(state, value):
    shadow[cnt] = value
    if (value & 0x80000000)                 # enable bit set
       and channel_not_already_active:
        start_mode = (value >> 28) & 3       # start timing: 0=imm,1=VBlank,2=HBlank/slot,3=special
        channel.mode = start_mode
        channel.SAD  = *chan.sad_ptr         # latch source
        channel.DAD  = *chan.dad_ptr         # latch dest
        channel.CNT  = value
        if start_mode == 0:                  # IMMEDIATE
            run_dma_transfer(state, &channel)   # FUN_0012d9fc — executed SYNCHRONOUSLY
    else:
        channel.CNT = value                  # just store (disable / mode-latch)
```

Evidence: `all_decomp.c:15265-15335` — the four cases each test `(int)param_3 < 0`
(bit31 = enable), extract `param_3 >> 0x1c & 3` (start-timing), latch SAD/DAD from
the channel's register-pair pointer (`fd368/fd390/fd3b8/fd3e0`), and, **only when
mode==0**, call `FUN_0012d9fc` immediately (`LAB_0012613c`). Non-immediate modes
are latched and fired later by the matching hardware event (VBlank/HBlank/DS-cart/
etc.) which re-invokes the same transfer engine. [proven-from-binary]

The four DMA channel descriptors sit at `state + 0xfd348 + 0x28*ch` (bases 0xd358,
0xd380, 0xd3a8, 0xd3d0 relative to +0xf0000), each holding latched SAD/DAD/CNT/mode.
[proven-from-binary]

### 2.2 The transfer engine and the DMA fast path — `FUN_0012d9fc` (10 856 B)

This large function performs the actual transfer. Two phases: **timing** then
**copy**. [proven-from-binary]

Timing model:
```
count = (CNT & 0x1FFFFF) ? (CNT & 0x1FFFFF) : 0x200000   # 21-bit ARM9 count; 0 → max
width = (CNT & 0x4000000) ? 32bit : 16bit                # bit26 = transfer type
src_region = SAD >> 24 ; dst_region = DAD >> 24
cyc_per_unit = DMA_TIMING_TABLE[src_region][dst_region][width][cpu]  # UNK_0020d8a0 / UNK_0020d9a0
scheduler_cycles += cyc_per_unit * count * clock_multiplier
```
The two tables `UNK_0020d8a0` (src+dst differ) and `UNK_0020d9a0` (src==dst) encode
per-memory-region access cycles, indexed by region, 16/32-bit, and CPU. So DMA cost
is charged to the event scheduler up front. [proven-from-binary]

**Fast-path copy (the DraStic-specific optimization):** there is **no literal
`memcpy`/`memset`** in the engine. Instead it does a **region-chunked, descriptor-
dispatched** transfer. Memory is described by a table of 0x60-byte region descriptors
(`param_1[1] + region*0x60`, region = `addr >> 0x17`, i.e. 0x800000-granular) with
slots: `+0x08` src pre-access hook, `+0x20` dst pre-access hook,
`+0x48` **dirty-invalidation hook**, `+0x50` **region-specialized mover**,
`+0x58/0x59` addressing-mode codes. [proven-from-binary]

```
run_dma_transfer:
    while units_left:
        # how many units until SRC or DST crosses its 0x800000 region boundary
        run = min(units_left, units_to_src_region_end, units_to_dst_region_end)
        dst_desc = region_desc[DAD >> 0x17]
        dst_desc.dirtymark(state, DAD)         # +0x48 : JIT/GPU coherency invalidate
        dst_desc.move(state, buf, DAD)         # +0x50 : region-specialized bulk move
        advance SAD/DAD per addressing mode (inc / inc-reload / dec / fixed)
        units_left -= run
    if CNT.irq: raise IRQ(DMAx)
    if not repeat: clear enable bit
```

Each memory region installs its own mover at descriptor `+0x50`, so RAM→RAM chunks
move with host pointers while VRAM/palette/IO route through special handlers; the
`+0x48` dirty hook (backed by `FUN_00120b44`, a dirty-word scan) invalidates any
already-JIT-compiled ARM code or cached GPU data the write clobbers — DMA-into-code
coherency. **This per-region specialization IS the fast path** (the innermost copy
thunks are hand-tuned asm behind Ghidra-unrecovered jump tables — the exact loop body
is `unknown-too-stripped`, but it is provably not one flat memcpy). The
region-boundary check (`uVar9 < (uVar9 & mask) + run`) keeps each chunk inside one
region. 16-bit vs 32-bit = two mirror branches on bit26 (`*2/>>1` vs `*4/>>2`); a
fixed-source **fill** (src_ctrl==2) holds the source pointer constant but reuses the
same region machinery — **no separate memset fast path**. [proven-from-binary]

**Timed-mode dispatch — `FUN_0012d350`.** Non-immediate channels are only *armed* at
the CNT write. When a triggering hardware event fires (VBlank/HBlank/scanline wrap/
FIFO), `FUN_0012d350(controller, condition)` scans all 4 channels and runs
`FUN_0012d9fc` for those whose enable is set and whose latched start-timing byte
matches the condition. DMA completion cost is pushed into the event queue via
`insert(mgr, cost, chan_event_id)` (cost from the per-region timing tables
`UNK_0020d8a0/UNK_0020d9a0` via `FUN_0012d974`). ARM7 DMA (`FUN_00124f3c`) has a
thinner trigger set. [proven-from-binary]

Actionable for melonDS: the highest-value DMA trick is **region-chunked movers routed
through the same fastmem region table the CPU uses, with a dirty-invalidation hook for
DMA-into-code coherency**, plus charging timing as `count × per-region-cost` once
rather than cycle-by-cycle. Immediate (mode-0) DMA runs synchronously at the CNT write
(no event round-trip); timed modes are armed and swept by a single condition scan.

### 2.3 DS-specific DMA start modes [inferred from mode field]

`(CNT>>28)&3` distinguishes immediate / sync-to-display / special. On ARM9 the DS
extends start-timing to a 3-bit field (bits 27-29) for HBlank/VBlank/DS-cart/GXFIFO/
main-mem-display DMA; DraStic reads `>>0x1c & 3` here (a 2-bit slice) and resolves
the finer mode when the triggering event fires. The exact per-event trigger table is
in the video/scheduler code (not fully isolated in the stripped decomp). [inferred]

---

## 3. Timers — 4 per CPU

### 3.1 On-demand counter reconstruction [proven-from-binary]

Timer counter reads (offsets 0x100/0x104/0x108/0x10C) are handled in the read
dispatcher `FUN_00123280` (`all_decomp.c:14614-14631`). The selector
`(offset-0x100) < 0xd && (1<<idx & 0x1111)` isolates the four timer slots (stride 4).
For a **running** timer (control bit 7 set) the current value is reconstructed:

```
read_timer_counter(timer):
    if not running:            return timer.latched_counter
    now      = scheduler_now                       # (cpu_cycles_base + delta) - phase
    value    = ((timer.overflow_deadline - now) >> timer.prescaler_shift)
             + timer.reload
    return value & 0xFFFF
```

i.e. the value is derived from the timer's scheduled overflow deadline, its
prescaler shift (1/64/256/1024 → shift 0/6/8/10), and reload. Fields:
`DAT_025d3358`=reload, `DAT_025d335a`=control (bit2 select, bit7 enable),
`DAT_025d335c`=prescaler shift, per-timer stride 0x20. [proven-from-binary]

**Implication:** timers are NOT ticked every cycle. Each running timer has a single
scheduled overflow event; the counter is computed only on guest read. This is the
key timer optimization. [proven-from-binary]

### 3.2 Overflow, cascade, IRQ scheduling — `FUN_0012d3f8` [proven-from-binary]

The 4 timers are a **0x20-byte-stride array** reached via `state+0xfba68` (init
`FUN_00132904`, serializer `FUN_00132ba4` loops `+=0x20` to 0x80). Per-timer fields:
`+0x08` start_bias, `+0x14` period (cycles to next overflow), `+0x18` reload,
`+0x1a` control (bit7 enable, bit6 IRQ, bit2 cascade, bits0-1 prescaler), `+0x1c`
prescaler shift. The on-demand counter (§3.1) is the same array via read handlers
`FUN_0012294c` (16-bit) / `FUN_00122cd4` (32-bit):
`count = ((now - start_ts - start_bias) >> shift) + reload & 0xFFFF`.

The overflow event callback is **`FUN_0012d3f8`**. On overflow:
```
overflow(timer):
    if control & 0x40:                       # bit6 IRQ
        IF |= timer_irq_bit
        active = IE & IF & -IME              # same raise idiom as the IRQ controller (§5)
    next = timer + 0x20
    if next.control (at +0x3a) & 0x04:        # bit2 cascade (guarded so timer3 doesn't chain)
        next.counter += 1                     # cascade increments, may itself overflow
    start_ts = now
    period = (0x10000 - reload) << shift
    scheduler.insert(this, period, event_id = (cpu<<2 | 3) + idx)
```
So each timer is a single pre-scheduled future event; time-based timers reschedule
their own overflow, and cascaded timers carry **no** time event — they advance only
when the lower timer overflows. Prescaler is applied as a **right-shift** by the byte
at `+0x1c`; the literal shift values {0,6,8,10} (÷1/64/256/1024) sit behind a
jump-table in the control-write path [inferred, forced by the hardware ratios], but
the shift *mechanism* is proven. [proven-from-binary]

---

## 4. Event scheduler / CPU interleave

Strings `recompiler_cpu_next_action_arm9_to_arm7` and
`recompiler_cpu_next_action_arm7_to_event_update` reveal the loop: **run an ARM9
time-slice → switch to ARM7 → run the event/hardware update**, then repeat.
[proven-from-binary] (Those two strings are JIT `.rodata` stub names with no C
xrefs — the CPU→CPU handoff is hand-emitted asm; the event side it returns into is
the C below. [proven by absence])

### 4.1 The event queue — a delta-encoded sorted doubly-linked list [proven-from-binary]

This is the backbone all volatile subsystems plug into (timers, DMA, video). It was
independently rediscovered by the timer, DMA and scheduler investigations — same node
layout, high confidence.

- **Node = 0x30 bytes**: `+0x00 u32 delta` (cycles relative to the *previous* node),
  `+0x08 callback`, `+0x10 arg`, `+0x18 next`, `+0x20 prev`, `+0x28 id`. Recurring
  events are pre-allocated fixed slots at `manager + id*0x30`; the event-manager
  sub-struct is at `system+0x18` (head list at `manager+0x300`).
- **Dispatcher `event_update` = `FUN_0012d6f8`** (the routine the recompiler returns
  into, called from outer loop `FUN_00185184`):
  ```
  elapsed = mgr.slice_cycles; mgr.accum64 += elapsed
  if elapsed <= head.delta and head.delta - elapsed != 0:
      head.delta -= elapsed; return                 # O(1) fast path: nothing due
  do: pop head; head.callback(mgr, arg)             # fires; handler RE-INSERTS itself
      head = mgr.head
  while head.delta == 0                              # drain simultaneous events
  ```
  **The JIT slice budget = `head.delta`, a single u32.** Classic next-event skipping:
  idle cycles are never stepped, and no deadline array is scanned.
- **insert `FUN_0012c7a8`, remove `FUN_0012c838`, node-init `FUN_0012c89c`.** Insert
  walks subtracting each node's delta, splices, and decrements the successor's delta
  (keeps the list relative); remove folds the removed delta back onto the successor.
- Slot map: slot0 = HBlank (`FUN_0012cf2c`), slot1 = scanline/VCOUNT (`FUN_0012c8f8`),
  slot2 = ~128-unit fractional tick (SPU, inferred), slots 12–19 = the 8 DMA-channel
  completions, slot11 = IPC (inferred).

### 4.2 Video timing (scanline `FUN_0012c8f8`, HBlank `FUN_0012cf2c`) [proven-from-binary]

VCOUNT is a u16 at `system+0x14`; there are **two DISPSTAT shadows** (ARM9
`+0x35fc9a4`, ARM7 `+0x35f49a4`) matching the DS per-CPU DISPSTAT. Phase table:
VCOUNT 191 → enter VBlank (set DISPSTAT bit0 on both, IF bit0 if enabled by bit3,
flush GPU, increment frame counter, fire VBlank-timed DMA); 261 → VBlank end;
262 → frame wrap (fire mode-3/main-mem DMA), VCOUNT→0. LYC/VCount-match compares the
9-bit field and raises IF bit2 if bit5 set. The scheduler time base decodes exactly:
HBlank re-inserts with delta **1188** (0x4a4), scanline with **3072** (0xc00);
`3072 + 1188 = 4260 = 355 dots × 12` → DraStic counts **12 units/dot, 4260/scanline**
(visible 3072 / hblank 1188). [constants proven; phase alternation inferred]

---

## 5. Interrupt controller (IME / IE / IF) — `FUN_00125da0` IO write switch

Fully recovered from `all_decomp.c:15062-15140` (in `FUN_00125da0`). There are **two
independent interrupt clusters**: ARM9 at `machine+0x1b070`, ARM7 at `machine+0x23070`,
each with `+0x208` **IME**, `+0x210` **IE**, `+0x214` **IF** (so the concrete ARM7
fields are 0x23278/0x23280/0x23284; ARM9 are 0x1b278/0x1b280/0x1b284). Cluster identity
is nailed by DraStic's own crash-dump `printf` "Emulated ARM9:\n Mode %02d, IRQ %08x,
CPSR %08x, PC…", which proves the ARM9 deliverable-IRQ **active-mask** global is
`DAT_015cee58` (ARM7's is `DAT_025d5448`). The recompiler's per-CPU **pending latch**
also lives at `(*(DAT_01000010 + cpu_base)) + 0x2108`, and the **"exit block / next
action" signal** is `+0x22a8` (bit 1). [proven-from-binary]

```
# IME write (0x208)
if IME newly enabled and (IE & IF) != 0:
    pending = IE & IF                 # latch to +0x2108
    wake |= 2                          # force recompiler to leave current block
elif IME disabled:
    pending = 0

# IE write (0x210-0x213, byte-wise)
IE = merge_byte(IE, value)
if newly_set_bits:
    pending = IE & IF & (IME ? 0xFFFFFFFF : 0)   # note: -IME as mask
    latch pending -> +0x2108 ; if pending: wake |= 2

# IF write (0x214-0x217): write-1-to-clear acknowledge
IF     &= ~value | 0x200000            # note: bit21 kept STICKY (DraStic-internal latch)
active &= ~value                       # drop acknowledged bits from the active mask
```

The `-*(IME)` trick turns IME∈{0,1} into a 0x00000000/0xFFFFFFFF mask.
[proven-from-binary] IE writes carry a micro-opt: `if ((value & ~oldIE)==0) return` —
skip re-evaluation when the write only clears enable bits. **Raising IF** is not a
single helper; the idiom above is inlined at every source (scanline, timer overflow
`FUN_0012d3f8`, DMA completion, IPC, gamecard), gated by the CPU's halt flags.

**How an IRQ reaches a CPU [proven-from-binary]:** the raise idiom recomputes
`active = IE & IF & -IME` into the cluster's active-mask (`DAT_015cee58` ARM9 /
`DAT_025d5448` ARM7) and sets the exit signal. Delivery does **not** poll IE&IF
between blocks — the recompiler consumes the precomputed active-mask, gated by the
guest **CPSR I-bit (bit7)** (checked in `FUN_00185184` and timing handler
`FUN_001327bc`). To **force the current slice to end**, the cycle budget
(`DAT_015cefe0`) is set to `0xFFFFFFFF` (-1) so the next block's cycle check fails and
bails to the dispatcher, which performs the mode switch and vectors to 0x18 → the BIOS
IRQ handler (DOC 08 §2.4) → the game's handler at DTCM-end. ARM9 and ARM7 run the
whole path independently on their own clusters. **Gap:** the ARM7 BIOS IRQ-check
mirror at 0x0380FFF8/0x03FFFFF8 is **not** emulated — DraStic HLEs dispatch via this
native active-mask path instead. [proven by absence]

Actionable: the pending-latch + block-exit flag is exactly how a JIT should deliver
IRQs cheaply — no per-instruction IRQ poll; only recompute the latch on IME/IE/IF
writes and on IF-raising events, and force a block exit when it becomes nonzero.

---

## 6. Cartridge / ROMCTRL / KEY1

### 6.1 ROMCTRL read [proven-from-binary]

Offset 0x1A4 = ROMCTRL. On read (`all_decomp.c:14641-14649`) DraStic returns the
stored ROMCTRL with the **data-word-ready bit (0x00800000)** and **block-busy /
0x20000000** set/cleared by comparing the transfer position against the command's
byte count (`... 0x5998` = transfer length vs current offset). So cart transfer
progress is likewise computed on demand from the scheduler position rather than
bit-banged. AUXSPICNT/AUXSPIDATA at 0x1A0/0x1A8/0x1AC read straight from shadow.
[proven-from-binary]

### 6.2 Secure-area / KEY1 Blowfish decryption — `FUN_0017433c` + `FUN_00173d98`

Cart boot loads the 0x4000-byte secure area; if it begins with the encrypted magic
**0xE7FFDEFF** (seen as `-0x18002101` twice at `all_decomp.c:16*`), DraStic decrypts
it with the DS **KEY1 Blowfish** cipher. [proven-from-binary]

`FUN_0017433c` seeds the id block with `0x72636e65,0x6a624f79` ("encr","yObj"),
copies the 0x1048-byte KEY1 table (18-word P-array + 4×256-word S-boxes = 4168 B) to
`state+0x4940`, applies the key schedule (`FUN_00173d98` = apply-keycode, run 3× with
the modulus halving/doubling steps), then runs Blowfish Feistel rounds:

```
for each 64-bit block:
    for i in 0..15:
        x ^= P[i]
        y ^= F(x) where F = ((S0[b3]+S1[b2]) ^ S2[b1]) + S3[b0]
        swap
    apply P[16],P[17]
```

The S-box lookups at `+0x12/+0x112/+0x212/+0x312` word offsets (P-array = 18 words,
then four S-boxes) confirm textbook Blowfish. The KEY1 table itself comes from the
ARM7 BIOS image (offset 0x30). So DS KEY1/secure-area is emulated exactly, using the
real Blowfish key from BIOS. KEY2 (the hardware LFSR stream on the cart bus) is not
separately visible — with direct-boot and decrypted secure area, KEY2 streaming is
effectively bypassed/unneeded for most commands. [proven for KEY1; KEY2
unknown-too-stripped]

---

## 7. Backup memory (EEPROM / FLASH / SRAM / NAND)

### 7.1 Save-type detection — game-database XML parser

DraStic ships a per-game save database (XML: `<cartridge title=…><slot1>…<save
type='…' size='…' id='0x…'/>…`) parsed at `all_decomp.c:95770-95850`. Recognized
`type` → internal code: **`eeprom`→2, `flash`→1 (+ chip `id`), `nand`→3**, absent →0
(auto/SRAM). It also reads per-cart `size`, and capability flags `irport`,
`bluetooth`, and `slot1`/`slot2`. Unknown types log `"Unknown save type %s."`.
[proven-from-binary]

At game load (`FUN` around `all_decomp.c:92750-92850`) DraStic:
- picks save path `…/backup/<game>.dsv` (with header) or `<game>.sav` (raw) per a flag;
- allocates the backup buffer at the DB size (fallback `malloc(0x80000)`);
- for GBA slot-2 gamepaks, detects `SRAM_V110` / `"PASS"` (0x53534150) signatures and
  loads `…/slot2/<name>.sav` / `.gba`. [proven-from-binary]

### 7.2 AUXSPI backup command state machine — `FUN_00178010`

Runtime EEPROM/FLASH access is a faithful SPI command FSM (`all_decomp.c:94407-…`),
state in `state+0x242a`, status byte (WEL/WIP) in `+0x242b`, address accumulator in
`+0x2404`, address-byte count in `+0x242c`. Commands decoded:

| Opcode | Command | Effect |
|--------|---------|--------|
| 0x06 | WREN | set WEL (status |= 2) |
| 0x04 | WRDI | clear WEL |
| 0x05 | RDSR | read status register |
| 0x03 | READ | enter address phase → stream data out |
| 0x02 | PP / WRITE | enter address phase → stream data in |
| 0x0A/0x0B | PW (small EEPROM page write, with A8 in opcode) | for 512 B/64 Kb EEPROM |
| 0x9F | RDID | return JEDEC chip id (FLASH) |
| 0xAB/0xB9/0xD8/0xDB | FLASH deep-power-down / sector-erase family | |

Address width and page size are chosen from the detected size (`param_4 < 0x201` →
tiny EEPROM 1-byte addr; larger → 2/3-byte). Writes are masked by the size
(`addr & 0x2408`). [proven-from-binary] `.dsv` vs `.sav` selects whether a DraStic
header is prepended to the persisted file.

---

## 8. RTC — `FUN_001777bc` (serial engine) [proven-from-binary]

RTC register **0x138** (the ARM7 IO dispatch `FUN_00124f3c`, sole `case 0x138`) is
serviced by `FUN_001777bc`; the RTC sub-struct pointer is `state+0xfd4c8`. One
function handles both write and read-back (returns the byte with the RTC's SIO output
merged into bit0).

**Bit-banged serial state machine** (bit1 = SCK, bit0 = SIO, bit2 = CS):
- On CS deassert, reset the engine (`FUN_00177568`).
- On SCK edges, shift **LSB-first**: rising edge samples SIO into the shift register,
  increments a bit counter; at 8 bits a byte completes.
- **Phase** (`+0x18`): 0 = command byte, 1 = read (drive data out on falling edge),
  2 = write. Command byte validated `(byte & 0x0F) == 6`, R/W in bit7, register index
  `= (byte >> 4) & 7` (bit-reversed).
- Struct: `+0x10..0x16` = 7-byte BCD buffer (sec/min/hour/wday/day/mon/year),
  `+0x1e` shift reg, `+0x1f` bit count, `+0x20` byte count.

**Registers emulated:** only Status1, Status2, Date&Time (7 bytes), Time (3 bytes);
Alarm1/2, Clock-Adjust and Free fall through `default` (bits clocked, no logic).
Status1 bit1 = 24h/12h; hour bit6 = AM/PM. BCD via `(x/10<<4)|(x%10)`. [proven]

**Time source — `FUN_00177764` (get) / `FUN_0017779c` (set).** A flag at
`state+0x8AB28` selects **real host clock** (`time()`/`localtime()`) vs a
**deterministic emulated clock** = stored offset + `cycles/60` — a TAS/determinism
feature worth noting. Calendar mapping: month `+1`, year `tm_year-100` (2000-based),
weekday raw 0-6; writes invert through `mktime` and are stored back as an offset.
Imports `time`, `localtime`, `mktime`, `gettimeofday` confirm the host-time path.
[proven-from-binary]

Actionable: like melonDS, DraStic serves host wall-clock through the RTC serial
interface — negligible profiler cost. The optional deterministic `cycles/60` clock is a
nice touch for reproducible benchmarking/TAS, and cheap to add.

---

## 9. Summary — actionable for melonDS

1. **DMA fast path**: coalesce transfers into region-bounded block copies via the
   fastmem region-descriptor block read/write hooks (RAM→RAM/VRAM becomes memcpy);
   charge timing once as `count × per-region-cost`; run immediate (mode-0) DMA
   synchronously at the CNT write. This is DraStic's biggest DMA win.
2. **Timers**: never tick per cycle — schedule one overflow event per running timer
   and reconstruct the counter from the deadline + prescaler on guest read; cascaded
   timers carry no time event.
3. **IRQ delivery**: recompute `pending = IE & IF & IMEmask` only on IME/IE/IF writes
   and IF-raising events; signal the JIT via a pending latch + block-exit flag rather
   than polling — matches the `recompiler_cpu_next_action` design.
4. **IO dispatch**: flat shadow array (`offset & 0x7fff`) for inert registers; only
   volatile regs (timers, ROMCTRL, IPC) are computed on demand.
5. **Cart/backup**: KEY1 Blowfish from BIOS for secure-area; ROM transfer progress
   computed from scheduler position; backup is a compact AUXSPI FSM with a per-game
   XML save-type/size database — a robust alternative to autodetection.

---

## 10. Function quick-reference (addr → inferred role)

- **Scheduler:** dispatcher `FUN_0012d6f8`, insert `FUN_0012c7a8`, remove
  `FUN_0012c838`, node-init `FUN_0012c89c`, scanline `FUN_0012c8f8`, HBlank
  `FUN_0012cf2c`, outer JIT loop `FUN_00185184`.
- **DMA:** engine `FUN_0012d9fc`, timed-trigger scan `FUN_0012d350`, cost table
  `FUN_0012d974`, completion `FUN_0012d5bc`, dirty-word scan `FUN_00120b44`,
  register writes `FUN_00125da0` (32-bit) / `FUN_00125588` (16-bit); ARM7 DMA
  `FUN_00124f3c`.
- **Timers:** overflow `FUN_0012d3f8`, read `FUN_0012294c` (16) / `FUN_00122cd4` (32),
  init `FUN_00132904`, serialize `FUN_00132ba4`; array ptr `state+0xfba68`.
- **Interrupts:** IO write / IF-ack `FUN_00125da0`, slice-kill / timing handler
  `FUN_001327bc`, delivery `FUN_00185184`, event-advance `FUN_0012d6f8`; clusters
  ARM9 `machine+0x1b070` / ARM7 `machine+0x23070`; active-mask `DAT_015cee58` (ARM9) /
  `DAT_025d5448` (ARM7); cycle budget `DAT_015cefe0`.
- **IO dispatch:** 16-bit read `FUN_00123280`; write switch `FUN_00125da0`; ARM7 IO
  `FUN_00124f3c`.
- **Cart/backup:** KEY1 decrypt `FUN_0017433c`, key-schedule `FUN_00173d98`, backup
  SPI FSM `FUN_00178010`, save setup/loader `FUN_0017870c`, save-DB XML parser near
  `all_decomp.c:95770`.
- **RTC:** serial engine `FUN_001777bc`, get/set time `FUN_00177764` / `FUN_0017779c`,
  reset `FUN_00177568`; sub-struct `state+0xfd4c8`.

Cross-corroboration note: the 0x30-byte event node + `manager+0x300` head, the
`IE & IF & -IME` raise idiom, and the DMA per-region cost tables were each independently
recovered by two or three separate investigation passes, which raises confidence in
those three structures specifically.
