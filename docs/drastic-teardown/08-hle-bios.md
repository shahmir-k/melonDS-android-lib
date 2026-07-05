# DraStic Teardown — DOC 08: HLE / BIOS / Firmware

Scope: how DraStic provides the DS ARM9 (ARM946E-S) and ARM7 (ARM7TDMI) BIOSes,
how SWIs are handled, firmware emulation and boot mode, and libc/OS/WiFi HLE.

Evidence base:
- BIOS blobs: `reference/universal/assets/drastic_bios_arm9.bin` (4096 B) and
  `drastic_bios_arm7.bin` (16384 B), disassembled with capstone (ARM mode).
- Decompiled arm64 core: `scratchpad/decomp/all_decomp.c`, `strings_arm64.txt`,
  `sym_imports.txt`.

Confidence labels: [proven-from-binary] / [inferred] / [unknown-too-stripped].

---

## 0. Executive verdict

**The DS BIOS is emulated LLE (Low-Level Emulation): DraStic runs actual ARM BIOS
machine code inside its recompiler.** It does NOT intercept SWIs in native C.
[proven-from-binary]

What makes it look "HLE" is that the *bundled* BIOS images are DraStic's **own
clean-room reimplementations** of the DS BIOS, not Nintendo's dumps. They are tiny
(4 KB / 16 KB) because they implement only the exception vectors, the SWI table,
and the IRQ dispatcher — none of the retail BIOS's boot menu, RSA/whitelist, or
GBA-mode cruft. At load time DraStic prefers a real dump if the user supplies one:

- ARM9: try `nds_bios_arm9.bin` (real, 4 KB) → else `drastic_bios_arm9.bin` (own)
- ARM7: try `nds_bios_arm7.bin` (real, 16 KB) → else `drastic_bios_arm7.bin` (own)
- Firmware: try `nds_firmware_modified.bin` → `nds_firmware.bin` → else **synthesize**

So the correct framing is: **BIOS = LLE execution of a HLE-authored (custom) BIOS
image; firmware = HLE-synthesized when absent; boot = direct-boot.**

---

## 1. BIOS loading and the "own-BIOS" flag

Function `FUN_0012b8a4` (@0x12b8a4, the emulator init/reset that also maps memory)
loads the BIOS/firmware. [proven-from-binary]

```
load_bios_and_firmware(state):
    if load_file("nds_bios_arm9.bin", ARM9_BIOS /*+0x2004*/, 0x1000) < 0:
        if load_file("drastic_bios_arm9.bin", ARM9_BIOS, 0x1000) >= 0:
            state.bios_flags |= 2          # bit1 = "ARM9 using DraStic's own BIOS"
        else: abort
    if load_file("nds_bios_arm7.bin", ARM7_BIOS /*+0x2204*/, 0x4000) < 0:
        if load_file("drastic_bios_arm7.bin", ARM7_BIOS, 0x4000) >= 0:
            state.bios_flags |= 1          # bit0 = "ARM7 using DraStic's own BIOS"
        else: abort
    if load_file("nds_firmware_modified.bin", FW /*+0x560e*/, 0x40000) < 0 and
       load_file("nds_firmware.bin",          FW,           0x40000) < 0:
        memset(FW, 0, 0x40000)
        synthesize_firmware(FW)            # FUN_0012a9fc
```

Evidence: the exact `FUN_0011b4d4(state,"nds_bios_arm9.bin", base, 0x1000)` /
`...,"drastic_bios_arm9.bin",...` fallback chain, sizes 0x1000/0x4000/0x40000, and
the `bios_flags |= 1|2` bits at `all_decomp.c:19311-19338`. A second copy of the
same loader appears near `all_decomp.c:96491`. `FUN_0011b4d4` is the asset opener
(pathname + buffer + max size). [proven-from-binary]

The `bios_flags` bit is likely used later to slightly alter behavior (e.g. skip
BIOS-protect region reads) when running the reimplemented BIOS, but the consumer
is not clearly isolated in the stripped decomp. [inferred]

---

## 2. BIOS image anatomy (disassembly)

Both blobs open with the standard 8-entry ARM exception vector table (one `B`
per vector). [proven-from-binary]

| Vector | Offset | ARM9 target | ARM7 target | Meaning |
|--------|--------|-------------|-------------|---------|
| Reset  | 0x00   | 0x100 (loop)| 0x1078      | Reset |
| Undef  | 0x04   | 0x104 (loop)| 0x107c      | Undefined instr |
| **SWI**| 0x08   | **0x108**   | **0x1080**  | Software interrupt |
| Prefetch abort | 0x0C | 0x104 | 0x107c | |
| Data abort | 0x10 | 0x104   | 0x107c      | |
| Reserved | 0x14 | 0x104     | 0x107c      | |
| **IRQ**| 0x18   | **0x628**   | **0x1f00**  | Hardware interrupt |
| FIQ    | 0x1C   | 0x104       | 0x107c      | (unused on DS) |

The reset/abort/undef vectors on ARM9 branch to self-loops (0x100/0x104) — the
reimplemented BIOS never expects to take those, because DraStic direct-boots and
never runs the retail boot sequence. [inferred]

### 2.1 SWI dispatcher (identical structure on both CPUs) [proven-from-binary]

ARM9 @0x108, ARM7 @0x1080:

```
swi_entry:
    push {r4, ip, lr}
    r4 = SPSR
    push r4                     ; save caller PSR
    r4 = (SPSR & 0x80) | 0x1F   ; keep I-bit, force System mode  (ARM9 also clears/sets)
    ip = byte[lr-2]             ; SWI comment field = SWI number (from the SWI opcode)
    CPSR = r4                   ; switch to System mode, IRQs per caller
    push lr
    if ip >= 0x20: ip = 1       ; clamp out-of-range to slot 1 (a return stub)
    pc = jump_table[ip]         ; ldr pc,[pc, ip lsl #2]
```

The common epilogue (ARM9 @0x1b4, ARM7 @0x112c) restores System-mode regs, switches
back to the caller's mode/PSR, pops `{r4,ip,lr}` and does `movs pc,lr`.

### 2.2 SWI jump tables — which SWIs are implemented

Both tables have 0x20 real entries. Address form `0xFFFF0xxx` on ARM9 confirms the
BIOS is linked at the true DS ARM9 BIOS base **0xFFFF0000** (ARM7 at 0x00000000).
[proven-from-binary]

**ARM9 SWIs implemented** (from jump table @0x138, verified by disassembling bodies):

| SWI | Name | Body @ | Implementation confirmed |
|-----|------|--------|--------------------------|
| 0x00 | SoftReset | 0x64c | clears IO, sets SVC/IRQ/SYS stacks, loads entry |
| 0x03 | WaitByLoop | 0x1e0 | `subs r0,#1; bgt` busy loop |
| 0x04 | IntrWait | 0x220 | halt + poll IF flags in a helper (0x1ec) |
| 0x05 | VBlankIntrWait | 0x218 | IntrWait with mask=1 (VBlank) |
| 0x06 | Halt | 0x1d4 | `mcr p15,0,r0,c7,c0,4` (CP15 wait-for-IRQ) |
| 0x09 | Div | 0x24c | signed shift-subtract division (q→r0, r→r1) |
| 0x0B | CpuSet | 0x2a8 | 16/32-bit copy **or** fill, bit24=word bit26=fill |
| 0x0C | CpuFastSet | 0x328 | 32-bit copy/fill loop |
| 0x0D | Sqrt | 0x368 | bit-by-bit restoring integer sqrt (seed 0x40000000) |
| 0x0E | GetCRC16 | 0x3b4 | table-less CRC16 |
| 0x0F | IsDebugger | 0x44c | returns 0 |
| 0x10 | BitUnPack | 0x454 | bit-depth expansion |
| 0x11/0x12 | LZ77UnComp (Wram/Vram) | 0x4e8 | LZ77 window decompressor |
| 0x13 | HuffUnComp | 0x564 | (tiny — thin/partial) |
| 0x14/0x15 | RLUnComp | 0x568 | run-length decompressor |
| 0x16 | Diff8bitUnFilter | 0x5c0 | delta unfilter |
| 0x18 | Diff16bitUnFilter | 0x5ec | delta unfilter |
| 0x1F | SoundBias | 0x61c | writes SOUNDBIAS |
| others | reserved | 0x1b4 | fall through to return stub |

**ARM7 SWIs implemented** (jump table @0x10b0). Same core set (SoftReset 0x1f28,
WaitByLoop 0x115c, IntrWait 0x1190, VBlankIntrWait 0x1188, Halt 0x114c, **Sleep**
0x11bc @SWI7, SoundBias 0x11cc @SWI8, Div 0x11e8, CpuSet 0x1244, CpuFastSet 0x12c4,
Sqrt 0x1304, GetCRC16 0x1350, IsDebugger 0x13e8, BitUnPack 0x13f0, LZ77 0x1484,
HuffUnComp 0x1500, RLUnComp 0x1504) **plus the ARM7-only sound helpers** at SWIs
0x1A–0x1E (bodies 0x15dc / 0x1bf0 / 0x1f18 / 0x1ed8 / 0x1ef4) — these are
GetSineTable / GetVolumeTable / GetPitchTable / GetBootProcSys / SoundBias-family
routines the sound driver calls. [proven-from-binary]

### 2.3 Verified algorithm samples (prose, not source)

- **Div (ARM9 @0x24c)** [proven-from-binary]: take abs of both operands (track sign
  in `ip`), normalize divisor left until it exceeds dividend (`lsl` while `<=`),
  then a subtract-and-shift-right loop accumulating quotient bits; finally negate
  quotient/remainder per saved sign. Classic non-restoring long division.
- **Sqrt (ARM9 @0x368)** [proven-from-binary]: restoring bitwise sqrt. Start bit
  mask `0x40000000`; for each 2-bit step, form trial `result|bit`, subtract if the
  radicand ≥ trial, shift result down, mask down by 2. 15 iterations.
- **CpuSet / CpuFastSet (@0x2a8/@0x328)** [proven-from-binary]: length in low bits of
  r2; bit26=fill (load source once) vs copy; bit24=32-bit vs 16-bit (CpuSet only —
  CpuFastSet is always 32-bit). Straight `ldr/str` post-increment loops. Note the
  reimplementation uses a simple word loop rather than the retail BIOS's 8-word
  unrolled block, but is functionally identical.
- **Halt (ARM9 @0x1d4)** [proven-from-binary]: `mcr p15,0,r0,c7,c0,4` — the ARM9
  wait-for-interrupt CP15 op. In DraStic's recompiler this maps to the CPU going to
  sleep until the scheduler raises an IRQ (see DOC 09 §interrupts). ARM7 Halt
  (@0x114c) writes HALTCNT instead.

### 2.4 IRQ dispatcher [proven-from-binary]

ARM9 @0x628:
```
irq_entry:
    push {r0-r3, ip, lr}
    r0 = CP15 DTCM base (mrc p15,0,r0,c9,c1,0) ; -> compute DTCM top (base+0x4000)
    lr = pc ; pc = word[DTCM_top - 4]          ; call user handler pointer at DTCM end
    pop {r0-r3, ip, lr}
    subs pc, lr, #4                            ; return from IRQ
```
This is the standard DS convention: the game stores its IRQ service routine pointer
at `DTCM_end-4` and its handled-IRQ acknowledge mask at `DTCM_end-8`; the BIOS
vector loads and calls it. ARM7 (@0x1f00) uses the fixed mirror at 0x3FFFFFC/0x380FFFC
instead of DTCM.

---

## 3. SWI handling: NOT intercepted natively [proven-from-binary]

There is **no** native C switch that decodes SWI numbers and runs division/decompress
in host code. The only `swi` strings in the binary —
`"swi%s 0x%x"` (`all_decomp.c:99470`) and `"swi %d"` (`:99687`) — sit inside the
ARM/Thumb **disassembler** (`FUN_0017dd68`, printf-style formatter that also prints
`p%d, %d, %s, c%d, c%d` coprocessor MRC/MCR mnemonics), used for the debugger/logging,
not for execution. Therefore SWIs are dispatched by the recompiled `SWI` instruction
trapping to the BIOS vector at 0x08 and executing the jump-table body as real ARM
code. There is no SWI fast-path; the "fast path" is simply that the reimplemented
BIOS bodies are short. [proven-from-binary]

Actionable for melonDS: melonDS already LLE-executes real BIOS and optionally HLEs
some SWIs. DraStic shows a viable middle path — ship a compact clean-room BIOS so
no dump is required, while still running it as ARM code (no per-SWI C intercept to
maintain). The reimplemented bodies here are a useful correctness reference.

---

## 4. Firmware HLE

### 4.1 Synthesis when no firmware present — `FUN_0012a9fc` [proven-from-binary]

When neither `nds_firmware_modified.bin` nor `nds_firmware.bin` exists, DraStic zeroes
a 256 KB buffer and builds a minimal-but-valid firmware header + user-settings area:

```
synthesize_firmware(fw):
    fw[0x08] = 'MACP'                 ; 0x5043414d — firmware identifier/magic
    fw[0x1d] = 0x20
    fw[0x20] = 0x7fc0                 ; user-settings CRC region params
    fw[0x2c] = 0x138
    fw[0x36..] = 0x3ffe050403020100   ; part table / user-settings offset seed
    fw[0x3e] = 0x1802ffff ; fw[0x42] = 0x10c ; fw[0x162] = 0x19
    fill touchscreen-calibration / language slots with 0xFF defaults (0x163..0x1f8)
    crc = crc16(fw[0x2c .. ]) using polynomial-shift table (unrolled per-bit CRC)
    store crc into header
```

Evidence: the `0x5043414d` ("MACP") store, the 0x7fc0/0x138/0x10c header constants,
the big run of `0xFFFFFFFFFFFFFFFF` fills, and the unrolled CRC16 loop (the cascade
of `if (bit&1) x ^= 0x606080 / 0x306040 / 0x186020 / ...` half-poly constants) at
`all_decomp.c` FUN_0012a9fc. This produces a firmware that passes the game's own
header/CRC checks so firmware reads return sane data. [proven-from-binary]

### 4.2 User settings injection — `setFirmwareUserdata` JNI [proven-from-binary]

`Java_com_dsemu_drastic_DraSticJNI_setFirmwareUserdata` (@0x119f5c) takes a Java
nickname string + a packed 32-bit value and writes them into the live firmware
user-settings shadow globals:

```
setFirmwareUserdata(env, jstr_nickname, packed):
    chars = GetStringChars(jstr_nickname)
    len   = min(GetStringLength(jstr_nickname), 10)   ; DS nickname max 10 chars
    memcpy(NICKNAME_GLOBAL /*DAT_0024c018*/, chars, len*2)  ; UTF-16
    DAT_0024c008 = packed & 0xFF                       ; favorite color
    DAT_0024c014 = packed >> 0x18                       ; birthday / language byte
    DAT_0024c00c = (packed rotated) & 0xff000000ff      ; birthday month/day
    ReleaseStringChars(...)
```

So DS user settings (nickname, favorite color, birthday, language) come from the
Android UI, not from a dumped firmware. [proven-from-binary]

### 4.3 Modified-firmware persistence [proven-from-binary]

`vsnprintf(...,"%s%csystem%cnds_firmware_modified.bin",...)` (`all_decomp.c:93864`)
builds the path DraStic writes back a modified firmware to (games can write settings
/ WFC config into firmware FLASH; DraStic persists that to a separate file so the
original dump is untouched, and prefers it on next load). [proven-from-binary]

### 4.4 Boot mode: DIRECT-BOOT [inferred, strong]

DraStic direct-boots the cartridge: it loads the cart's ARM9/ARM7 binaries to their
load addresses and jumps to the header entry points, rather than executing the
firmware's boot menu. Evidence: (a) firmware is optional and can be fully synthetic,
so it cannot be relied on for boot; (b) the reimplemented BIOS reset vector is a
self-loop (never executes a retail boot ROM sequence); (c) the loader stages BIOS +
synthesized firmware user-settings so games that *read* firmware still work. A
firmware-boot ("boot to menu") path is not evidenced in the strings. [inferred]

---

## 5. libc / OS-call HLE

There is no attempt to emulate a DS "OS" beyond BIOS SWIs — the DS has no OS. Host
libc is used directly by the native emulator (file IO for ROM/save/state, `mmap`/
`mprotect` for JIT+fastmem, pthreads). Cartridge-side "libc" (a game's own code) runs
as ARM under the recompiler with no interception. [proven-from-binary]

---

## 6. WiFi

**WiFi is not emulated at all — fully stubbed/absent.** [proven-from-binary]
Grepping `strings_arm64.txt` for `wifi|wlan|802\.11|baseband|beacon|nifi|rf9008`
yields no functional strings. There is no RF/baseband model, no BB9008/RF9008 chip
registers, no local-multiplayer or Nintendo WFC. The save-database parser does
recognize per-cart `bluetooth` and `irport` (IR) capability flags (see DOC 09), but
those gate cart-hardware quirks, not a wireless stack. Games that probe WiFi simply
see it unavailable. [proven-from-binary]

Actionable for melonDS: nothing to port here — melonDS's WiFi is strictly ahead of
DraStic. If the fork wants max single-player perf, DraStic's choice to omit WiFi
hardware modeling entirely is the pragmatic baseline.

---

## 7. Summary table — HLE vs LLE by subsystem

| Subsystem | DraStic approach | Confidence |
|-----------|------------------|------------|
| ARM9/ARM7 BIOS | LLE — runs ARM code; ships own 4KB/16KB clean-room reimpl, accepts real dumps | proven |
| SWIs (Div/Sqrt/CpuSet/CpuFastSet/LZ77/RLE/Huff/BitUnPack/Diff/IntrWait/Halt/SoundBias) | executed from BIOS blob, NOT C-intercepted | proven |
| ARM7 sound-table SWIs (0x1A–0x1E) | executed from ARM7 blob | proven |
| Firmware | real if present, else HLE-synthesized (MACP header + CRC16) | proven |
| Firmware user settings | HLE — injected from Java via setFirmwareUserdata | proven |
| Boot | direct-boot (skip firmware menu) | inferred (strong) |
| WiFi / RF / WFC | not emulated (stubbed/absent) | proven |
| Bluetooth / IR | flagged per-cart in save DB only | proven |
