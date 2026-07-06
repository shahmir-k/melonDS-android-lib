# DraStic Teardown — 02: Memory Subsystem

Reverse-engineered from `libdrastic_arm64.so` (arm64, stripped except JNI).
Evidence labels: **[P]** proven-from-binary · **[I]** inferred · **[U]** unknown/too-stripped.
Cross-refs the CPU-recompiler doc (`01-cpu.md`) for the JIT memory fast-path; this
doc owns the memory model, page table, TCM/VRAM mapping, and MMIO dispatch.

All function addresses are in the arm64 image. State lives in one large per-machine
struct (`state`); a second near-identical struct exists for the ARM7 side. Byte
offsets below are into that struct. There are **two CPUs** (ARM9, ARM7), each with
its own page table and MMIO handler tables.

---

## 0. TL;DR verdict

- **Fast-path = software page table of packed host-pointer deltas, 2 KB granularity**,
  one table per CPU (ARM9 `state+0xfba88`, ARM7 `state+0xfba90`). Entry =
  `(host_ptr − guest_addr) >> 2`; `bit 62` set ⇒ unmapped ⇒ MMIO slow path. **[P]**
- **Backed by an mmap'd, ashmem-fd contiguous region** so the DS's natural RAM
  mirrors are realized by the *kernel* (same fd offset mmap'd at several virtual
  addresses), not by pointer fixups. Hybrid: page table + OS aliasing. **[P]**
- **MMIO dispatch = per-region function-pointer table** in the state struct
  (read/write × 8/16/32 × region), *not* a monolithic switch. Default handler =
  open-bus (returns 0). Palette/OAM handlers self-swap to a copy-on-write shadow. **[P]**
- **ARM9 caches are NOT emulated** (CP15 c7 op only sets a WFI/halt flag). TCM **is**
  emulated as a page-table overlay. **[P]**

---

## 1. Physical backing store & memory init

### `FUN_0012b048` — master memory allocator **[P]**
Called once at boot (from the machine-init chain). Sequence:

1. **Ashmem fd via `FUN_0011a96c → FUN_0011b338`** (see §7). Creates a shared-memory
   file **`drastic_mapped_memory.dat`**.
2. **Main-RAM backing.** Ashmem sized `0x414000` (~4.27 MB = 4 MB main RAM + WRAM +
   palette/OAM working set). It is mmap'd into a **`0x4000000` (64 MB) contiguous
   host window** (`mmap(NULL, 0x4000000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)`),
   stored at `state[0]` and `state[0x1fa9c]`. On failure it retries with a full
   64 MB ashmem. Region base pointers:
   - `state[0]` = **main RAM** base (4 MB, `0x400000`)
   - `state[1]` = base `+0x400000`, `state[2]` = `+0x408000`, `state[3]` = `+0x410000`
     (WRAM / shared-WRAM working buffers) **[I on exact role]**
3. **Mirrors.** A *second* 64 MB mmap of the same fd (`state[0x1fa9e]`) is used to
   lay down the DS main-RAM mirror image: loops `munmap`+`mmap` the same fd offset
   (`uVar12 & 0xffffc000`, 16 KB pages) at host `region+0x2000000`, `+0x2400000`,
   `+0x2800000`, `+0x2c00000`. This is the DS main-RAM mirror band
   (guest `0x02000000–0x02FFFFFF`, a 4 MB RAM mirrored 4×). **The aliasing is done by
   the kernel via repeated mmap of one fd — the classic host-VM mirror trick.** **[P]**
4. **VRAM backing.** Second ashmem sized `0xa8000` (688 KB ≈ total DS VRAM 656 KB),
   mmap'd as **`0x800000` (8 MB) ×2 views** (`state[0x1fa9f]`, `state[0x1faa0]` — a CPU
   view and a renderer/ARM7 view). Per-bank base pointers `state[0x2a04..0x2a0d]`:

   | idx | offset | bank | size |
   |-----|--------|------|------|
   | 0x2a04 | 0x00000 | A | 128 KB |
   | 0x2a05 | 0x20000 | B | 128 KB |
   | 0x2a06 | 0x40000 | C | 128 KB |
   | 0x2a07 | 0x60000 | D | 128 KB |
   | 0x2a08 | 0x80000 | E | 64 KB |
   | 0x2a09 | 0x90000 | F | 16 KB |
   | 0x2a0a | 0x94000 | G | 16 KB |
   | 0x2a0b | 0x98000 | H | 32 KB |
   | 0x2a0c | 0xa0000 | I | 16 KB |
   | 0x2a0d | 0xa4000 | (end/spare) | — |

   Offsets match the DS VRAM bank layout exactly. **[P]**
5. **BIOS / firmware** loaded into the state struct via `FUN_0011b4d4(asset, dst, size)`:
   - `nds_bios_arm9.bin` (4 KB, `0x1000`) → `state+0x2004·8`; fallback `drastic_bios_arm9.bin`
   - `nds_bios_arm7.bin` (16 KB, `0x4000`) → `state+0x2204`; fallback `drastic_bios_arm7.bin`
   - `nds_firmware_modified.bin` / `nds_firmware.bin` (256 KB, `0x40000`) → `state+0x560e`;
     if absent, zero-filled and synthesized by `FUN_0012a9fc`. **[P]**

Everything is `PROT_READ|PROT_WRITE, MAP_SHARED` (`prot=3, flags=1`). Region remaps
use `munmap`+`mmap` (not `mprotect`). `mprotect` in this binary is JIT W^X code-page
protection — see `01-cpu.md`. **[P]**

### `FUN_0012b8a4` — memory reset **[P]**
`memset`s every buffer to 0 (main RAM `0x400000`, WRAM `0x8000`, VRAM banks, palette
`0x800`, OAM `0x800`), sets **WRAMCNT = 3** (`state+0x1b2b7`), seeds a few control
regs, then rebuilds the page tables via `FUN_00121be0` (WRAM/main) + `FUN_00129944`.

---

## 2. The software page table (fastmem core) **[P]**

### Layout
- **One table per CPU**: ARM9 pointer at `state+0xfba88`, ARM7 at `state+0xfba90`.
- **Granularity = 2 KB** (`index = guest_addr >> 0xb`). Full 32-bit space →
  `2^21 = 2,097,152` entries × 8 bytes = **16 MB per table**.
- **Entry encoding**: `entry = (int64)(host_ptr − guest_addr) >> 2` (arithmetic).
  Reconstruct host address as `host = guest_addr + (entry << 2)`. The `>>2`/`<<2`
  keeps the low bits free and lets the sign/`bit 62` act as a flag.
- **Unmapped / MMIO marker**: `entry = 0x4000000000000000` (**bit 62 set**). A load/store
  whose page entry is negative-ish (bit 62/63) diverts to the slow MMIO path. **[P]**
  (Evidence: every remap routine writes `0x4000000000000000` to clear a page, and
  writes `(host−guest)>>2` to map one — e.g. lines around `FUN_001217ec`/`FUN_0012122c`.)

### Auxiliary bitmaps (JIT-code invalidation) **[P]**
Two companion bitmaps live just below the table base (`DAT_01000018` = base+0x18,
`DAT_01004018` = base+0x4018):
- **`DAT_01000018`**: 1 bit per 2 KB page — "this page currently maps somewhere /
  holds translated code".
- **`DAT_01004018`**: coarse 1 bit per 32 pages (a summary word) so a remap can skip
  empty 64 KB spans without scanning.

`FUN_0012122c` (generic region (re)map) walks these to (a) write the new packed
pointers for each 2 KB page in a range and (b) punch out (`= bit62`) any pages whose
translated JIT code must be dropped. This is how a VRAM/TCM remap invalidates stale
recompiled blocks. **[P]**

### Table-build / remap routines
| Fn | Role | Granularity |
|----|------|-------------|
| `FUN_00121be0` | Build WRAM + main-RAM mappings (incl. shared-WRAM split per WRAMCNT) | 2 KB / 32 KB blocks |
| `FUN_001217ec` | Map **ITCM** (fixed base 0), size arg | 2 KB (0x800 step) |
| `FUN_00121da8` | Map **DTCM** (movable base, size) | 2 KB |
| `FUN_0012122c` | Generic range map + JIT-block invalidate | 2 KB step |
| `FUN_0012177c` | ARM7-side generic map (used for VRAM C/D → ARM7) | 2 KB |
| `FUN_00120a08/aac` | Low-level mmap-mirror helpers (16 KB pages of the fastmem fd) | 16 KB |

Fixed regions are mapped once (in `FUN_0012a72c` and its sibling): **palette
`0x05000000`** and **OAM `0x07000000`** are mapped *direct* to their buffers;
**VRAM `0x06000000`** is mapped per-bank (§4). Unmapped tail ranges are filled with
the `bit 62` sentinel. **[P]**

### How the JIT consumes it (cross-ref `01-cpu.md`) **[I, strong]**
Codegen symbol names present in the image: `arm64_load_memory8_unsigned/…_signed`,
`arm64_load_memory16/32/64`, `arm64_store_memory{8,16,32}_arm9/arm7`. The recompiler
bakes, per guest load/store: index the CPU's page table by `addr >> 11`, test the
entry sign; if clear, `host = addr + (entry<<2)` and emit a native `ldr/str`; if set
(bit 62), branch to the C MMIO handler. Store helpers additionally consult the
presence bitmap to self-invalidate translated code when the guest writes into a code
page (SMC / DMA-to-code). Per-CPU tables (`_arm9`/`_arm7` suffixes) confirm one table
each. No SIGSEGV/fault-based fastmem is used — the checks are in-band via the entry
sign, so a melonDS port does not need signal handlers to copy this. **[I]**

---

## 3. TCM emulation **[P]**

Handled through the ARM9 CP15 coprocessor path:
- **Write CP15**: `FUN_001336ac(state, CRn, op1, CRm, value)`.
  - `CRn==9, CRm==1, op2==1` → **ITCM control**: size `= 0x200 << (val>>1 & 0x1f)`,
    then `FUN_001217ec(pt, size_rounded)`. ITCM base is fixed at guest `0x0`.
  - `CRn==9, CRm==1, op2==0` → **DTCM control**: base `= val & 0xFFFFF000`, size as
    above, then `FUN_00121da8(pt, base, size)`. DTCM base is movable.
  - `CRn==7` → **cache maintenance / WFI**: only sets `state[0]+0x2110 = 1`
    (a halt/wait flag). **No cache lines, no cache tags, no invalidation cost
    modeled.** ⇒ ARM9 I-cache/D-cache are ignored for speed. **[P]**
  - `CRn==1` (control reg) → updates enable bits then `FUN_00133438` re-derives TCM
    mappings (ITCM/DTCM enable + size).
- **Read CP15**: `FUN_0013362c` returns stored TCM control/size words; also returns
  fixed MIDR/cache-type IDs (`0x0f0d2112`, `0x00140180`, `0x41009561`). **[P]**
- Control snapshot fields live at `state+0x14..0x3c` in a small CP15 sub-struct
  (base/size/enable per ITCM & DTCM). `FUN_00133838` resets it (ITCM enabled, 32 KB
  default per `0x12078` control seed). **[P]**

TCM is thus a **page-table overlay**: enabling/moving/resizing TCM just rewrites the
affected 2 KB page entries to point at the on-chip TCM buffers (`state+0x8…` region),
shadowing whatever main-RAM/BIOS mapping was there. Fast, exact, zero per-access cost.

---

## 4. VRAM bank mapping **[P]**

VRAMCNT A–I (guest `0x04000240`–`0x04000249`) drive a large dispatcher (bank-remap
switch around `FUN_0012a…`/`0x29500+`). Per bank + MST mode it:
- computes the target guest base (`bank_slot * 0x4000 + 0x06000000` for engine-A BG,
  `0x06200000` engine-B BG, `0x06400000`/`0x06600000` OBJ, `0x06800000` LCDC, plus
  texture/tex-palette slots), and
- calls `FUN_0012122c(ARM9_pt, guest_base, size<<14)` — or `FUN_0012177c(ARM7_pt, …)`
  for banks C/D mapped into ARM7 (`state+0xfba90`) — writing page-table entries at
  **16 KB (`0x4000`) granularity** that point into the VRAM bank buffer
  (`state[0x2a04..]`). **[P]**
- Maintains per-bank "mapped" bitmasks (`state+0x2224`, `state+0x21e0`, `state+0x444`)
  so the software renderer knows which banks are LCDC-visible / texture-bound and can
  invalidate its texture cache. **[P]**

So VRAM remaps are *page-table edits* — a bank move is O(bank pages) pointer writes,
and reads/writes to mapped VRAM then hit the buffer directly through the fast path.
Overlapping/unmapped VRAM reads fall to open-bus via the sentinel. **[P]**

---

## 5. MMIO dispatch **[P]**

**Design: a per-region function-pointer table embedded in the state struct**, not a
giant `switch`. Setup routines (`FUN_0012a72c` and its sibling near `0x1857c`) install
handler pointers into fields spanning `state+0xfbe58 … 0xfc090`, in groups of:
- a **base pointer + limit mask** (`0x7ff`) for the region's backing buffer, and
- **read/write handler code-pointers per access width** (8/16/32), for both CPUs.

Observed handlers: `FUN_001278f8`, `FUN_001279fc`, `FUN_00127b34`, `FUN_00127c38`,
`FUN_00127d6c`, `FUN_00127e70`, and defaults `FUN_00128220/28/50`.

- **`FUN_00128220` = open-bus default**: `return 0`. Unmapped reads yield 0. **[P]**
- **Palette/OAM handlers do copy-on-write shadowing** (`FUN_001279fc`): on the first
  CPU access into the 2 KB palette after a frame boundary, it `memcpy`s the live
  buffer (`state+0x15070`) to a shadow (`state+0x15870`), repoints the region base to
  the shadow, and **hot-swaps its own handler pointers** to a "dirty" variant
  (`FUN_00127874/ab0/cec`). This gives the renderer a stable palette/OAM snapshot for
  the frame while the CPU keeps writing — lazy double-buffering with no per-write
  branch once dirtied. **[P]**

**Routing.** I/O-space pages carry the `bit 62` sentinel in the page table, so a JIT
load/store to `0x04000000+` misses the fast path and calls the region's C handler.
The handler masks the address into its region window (`& 0x7ff` etc.) and either
touches the backing buffer or invokes a register-specific side-effect function:
CP15/TCM (§3), VRAMCNT (§4), WRAMCNT (`FUN_00121be0`), DMA, timers, IPC FIFO. The
`get_ds_memory_arm9_{8,16,32}` / `set_ds_memory_arm9_{8,16,32}` / `_arm7_*` symbol
names (in `.rodata`) are the width-specialized entry points the recompiler and DMA
engine call for the slow path. **[P for names; I for exact call graph]**

Register-level per-register decoding inside a handler is a compact `switch`/`if`
ladder on `addr & 0xfff` (evidenced by the CP15 ladder `FUN_001336ac` and the VRAM
switch); DraStic keeps the *region* selection O(1) via the pointer table and only
switches within a region. **[I]**

---

## 6. Address-space model summary **[P/I]**

| Guest region | Addr | Backing | Mapping mechanism |
|---|---|---|---|
| Main RAM 4 MB (+mirrors) | 0x02000000–0x02FFFFFF | ashmem `drastic_mapped_memory.dat`, mmap-aliased | kernel mirror + page table (direct) |
| Shared WRAM / ARM7 WRAM | 0x03000000 | `state[1..3]` buffers | page table per WRAMCNT (`FUN_00121be0`) |
| I/O registers | 0x04000000 | none (handlers) | page-table sentinel → fn-ptr table |
| Palette | 0x05000000 | `state+0x15070` (+shadow) | direct + CoW handler |
| VRAM A–I | 0x06000000 | `state[0x2a04..0x2a0d]` (ashmem VRAM) | per-bank page table, 16 KB (`FUN_0012122c`) |
| OAM | 0x07000000 | `state+…` (+shadow) | direct + CoW handler |
| GBA slot / cart | 0x08000000+ | — | sentinel/open-bus (DS mode) **[I]** |
| BIOS9 / BIOS7 | 0xFFFF0000 / 0x0 | `state+0x2004 / +0x2204` | page table (read-only region) |
| ITCM / DTCM | overlay | `state+0x8…` buffers | page-table overlay (§3) |

---

## 7. Shared-memory / ashmem usage **[P]**

`FUN_0011b338(name, size, api_level)`:
- API level `> 0x1c` (≥ 29): `dlopen("libandroid.so")` →
  `ASharedMemory_create(name, size)`.
- else: `open("/dev/ashmem")` + `ioctl(ASHMEM_SET_NAME)` + `ioctl(ASHMEM_SET_SIZE)`.
(`FUN_0011a96c` is the thin wrapper passing the cached API level `DAT_0024c4dc`.)

**Only two callers, both inside `FUN_0012b048`**: the main-memory backing and the VRAM
backing. **Ashmem here is NOT an IPC channel to Java** — it is used purely because an
fd-backed mapping can be `mmap`'d at multiple virtual addresses to realize DS RAM/VRAM
**mirrors** cheaply (anonymous mmap can't be aliased that way on older Android). The
framebuffer path to Java is GL texture upload (see GL doc), a separate mechanism. **[P]**
(`drastic_mapped_memory_vram.dat` / `geometry_log_vram.bin` strings are debug dumps of
these regions, not live IPC.) **[I]**

---

## 8. Savestate interaction **[P]**

`FUN_0012c3e8` serializes memory by linear `memcpy` of each buffer into a scratch
region: main RAM `0x400000`, WRAM banks `0x8000`/`0x4000`, the `0x10000` I/O block,
VRAM banks (`0x20000` each), palette/OAM. The blob is then zlib-compressed to `.dss`
(`FUN_00198a78/dac`; header `DraStic-SaveState---------------`). Load path reverses it
and rebuilds page tables. No pointer fixups are stored — buffers are position-
independent and tables are regenerated. **[P]**

---

## 9. Actionable for the melonDS fork

1. **melonDS already uses a JIT fastmem** (`ARMJIT_Memory.cpp`, host-VM `mmap` +
   fault handler on desktop; on Android it falls back to a software region). DraStic's
   scheme is a **middle path worth profiling against**: a 2 KB-granularity 16 MB
   pointer table (per CPU) baked inline, *plus* kernel-aliased mirrors — no SIGSEGV
   handler, no per-access bounds branch on the hot path (only a sign test on the
   loaded entry). On Android arm64 (where melonDS's true fault-based fastmem is
   fragile) this is likely the bigger win. **Measure: fraction of frame in memory
   stubs.** The profiler should tag load/store slow-path callouts distinctly from
   fast-path inline hits.
2. **No cache emulation** — if the melonDS fork ever adds ARM9 cache timing for
   accuracy, know DraStic pays *zero* here; keep cache modeling behind a flag or it
   will dominate the memory budget.
3. **TCM & VRAM as page-table overlays** — remaps are O(pages) pointer writes + JIT
   block invalidation, done rarely. In profiling, VRAMCNT/WRAMCNT/TCM writes should be
   cheap; if melonDS shows spikes there, it's over-invalidating the JIT. DraStic's
   coarse summary bitmap (`DAT_01004018`, 1 bit/32 pages) is the trick to skip empty
   spans during invalidation — cheap to replicate.
4. **Lazy CoW palette/OAM shadowing** decouples CPU writes from the renderer without a
   per-write branch after the first dirty. melonDS copies palette/OAM per scanline;
   DraStic's frame-granular snapshot may cut memcpy traffic — a candidate optimization,
   though it trades mid-frame palette-change accuracy.
5. **Mirror-via-mmap** (one ashmem fd mapped at 4 mirror addresses) is a clean way to
   get main-RAM mirroring for free on Android without per-mirror pointer tables — if
   the fork's Android fastmem is the weak point, adopt this aliasing directly.

---

### Open / unverified
- Exact per-register `switch` interiors of the I/O handlers beyond CP15/VRAM/WRAM
  (DMA, timers, SPI, IPC) not fully traced — handler *table* is proven, individual
  register decode is **[I]**.
- GBA-slot (0x08000000) mapping in DS mode assumed open-bus; not directly confirmed **[U]**.
- Whether `state[0x1faa0]` (2nd VRAM view) is the renderer's read view vs an ARM7
  alias is inferred from usage, not proven **[I]**.
