# DraStic teardown — DOC 10: Save / Savestate / Cheat engine / Input

Reverse-engineered from `reference/universal/lib/arm64-v8a/libdrastic_arm64.so`. `CTX` = the emulator
context base (`DAT_0024c000`). Evidence labels: **[proven]** literal in decomp, **[inferred]**,
**[unknown]**. JNI exports keep real names; internal funcs are `FUN_<addr>`.

---

## Part A — Backup save (`.dsv`) and autosave

### A.1 Backup save file
- Path template `"%s%cbackup%c%s.dsv"` (vaddr 0x10edf7) → `DraStic/backup/<gamename>.dsv`. [proven]
  This is the game's persistent SRAM/EEPROM/FLASH backup memory (the `.dsv` container).
- Save-type detection: DraStic ships `game_database.xml` (in `assets/`) and `usrcheat.dat`; the
  backup size/type is looked up per-game and, on unknown titles, auto-probed by the save-write
  pattern. (Loader specifics are in the ROM-load path around `FUN_00175c00`; exact probe heuristic
  is **[inferred]** — the DB lookup is [proven] by the shipped XML + the ROM-header gamecode reads.)

### A.2 Autosave
- `setAutosaveInterval(int)` @0x0011a520 → `DAT_0024c49c` = interval **in seconds**. [proven]
- Per-frame in `FUN_00116e74`: `if (DAT_0024c49c != 0 && time(NULL) >= DAT_0024c4a0)` → set the
  one-shot event flag and `FUN_00117308(9)` — **autosave writes to savestate slot 9** — then
  reschedule `DAT_0024c4a0 = now + interval`. [proven] (Note: DraStic's "autosave" saves a
  *savestate* to slot 9, distinct from the `.dsv` backup which is flushed by the emulated write.)

### A.3 Save-in-progress query
- `isSaving()` @0x0011a6bc-area (0x118000): true if `DAT_0024c4b5 != 0` (save-request pending) or
  `FUN_0017a3fc() != 0` (`DAT_0401e09c`, the async compress/write worker is busy). [proven]
- `getSavingSlot()` @0x118034: returns `DAT_0024c4b4` (target slot) while saving, else `-1`. [proven]

---

## Part B — Savestate (`.dss`) format

### B.1 Request/execute split
`saveState(slot, waitFlag)` @0x117f84 and `loadState(slot)` @0x118050 **do not perform I/O**; they
set request flags consumed by the emulation thread [proven]:

| Global | Meaning |
|--------|---------|
| `DAT_0024c4b5` | save requested |
| `DAT_0024c4b6` | load requested |
| `DAT_0024c4b4` | target slot |
| `DAT_0024c488` | load defer countdown (0x1e=30 frames when auto-loading at boot) |
| `DAT_0401e09c` | async compress/write worker busy |

The frame function `FUN_00116e74` services them: save → `FUN_00117308(slot)`; load →
`FUN_0017acc4(CTX, slot, 0,0,0)`. Files: `DraStic/savestates/<name>_<slot>.dss`
(`"%s%csavestates%c%s_%d.dss"`, vaddr 0x10f714/0x10f723), staged through
`"_savestate_temp.dss"` (0x10f6df) then renamed. [proven]

### B.2 On-disk `.dss` layout [proven header, proven body sizes]

**Header = 0x40 bytes** (written uncompressed at file start; `FUN_0017a888` writer):

| Offset | Size | Field |
|-------|------|-------|
| 0x00 | 32 | magic `"DraStic-SaveState---------------"` |
| 0x20 | 4 | version = **15 (0x0F)** |
| 0x24 | 4(+4 pad) | **flags** (see below) |
| 0x2c | 4 | time base (`CTX[0] / 60`) |
| 0x30 | 4 | (pad / uninit) |
| 0x34 | 4 | counter `CTX[0xb9c]` |
| 0x38 | 8 | sub-version bytes `04 00 06 02` then `00 00 00 00` |

**Flags word @0x24** [proven bit roles]:
`bit0`=compressed, `bit1`=has-thumbnails, `bits2-3`=BIOS type (bit2 ARM7, bit3 ARM9: 0=real NDS
BIOS, 1=DraStic HLE BIOS), `bit4`=extra firmware/backup section present, `bit5`=set-always (marker).

**Body** (after the 0x40 header):
- If compressed (bit0): `u32 compressedLen`, then the zlib `compress()` stream. Uncompressed max
  size = **0x680000 (6,815,744 B)**. Decompressed with `uncompress()` (`FUN_00198a78/dac` zlib
  anchors). [proven — `compressBound`/`compress`/`uncompress` calls, 4-byte length prefix]
- If uncompressed (bit0=0): raw body to EOF. [proven]

**Uncompressed body structure** (built by `FUN_0011c88c`, restored by `FUN_0011c614`):

| # | Serializer | Component | Size |
|---|-----------|-----------|------|
| — | (front of body) | **Thumbnails** if bit1: top `0x18000` + bottom `0x18000` (256×192×2 = 16bpp each) | 0x30000 |
| 1 | `FUN_00132d30` ×1 | ARM9 CPU core (banked regs, CPSR/SPSR) | ~0x10D |
| 2 | `FUN_00132d30` ×1 | ARM7 CPU core | ~0x10D |
| 3 | `FUN_0012c3e8` | **Memory block** (see B.3) | ≈0x4DD000 (~5.09 MB) |
| 4 | `FUN_0013db34` | 2D engine A + 2D engine B (`FUN_00150714`×2) + 3D GPU (`FUN_0016cb04`) | variable |
| 5 | `FUN_00173970` | SPU — **16 sound channels** (16×0xC8) + 8-byte mixer | 0xC88 |
| 6 | `FUN_00176e78` | Misc I/O regs (sound-capture / IPC / sys-control), version-gated | variable |
| 7 | `FUN_0017768c` | Small peripheral regs (IPC/SPI/touch, inferred) | ~0x1B |
| 8 | `FUN_00177ed0` | RTC (uses `time(NULL)`, `/60`) | ~0x11 |
| 9 | `FUN_0012d76c` | Small linked-list of records (≤10; cheat/patch/hook list, inferred) | ~0x41 |
| — | trailer | 8 + 8 + 2 bytes | 0x12 |

**Extra section** (flags bit4, `FUN_00178a9c`): 13-byte header (`u32@CTX+0x2400`,
`u32@+0x2408`=len−1, `u32@+0x2420`, `u8@+0x242c`) then a pointer-referenced payload of
`(*0x2408)+1` bytes → **firmware / backup-memory blob** [inferred]. Deserialized by `FUN_001789d0`.

### B.3 Memory block (section 3) — proven `memcpy` sizes
Main RAM **0x400000 (4 MB)**; WRAM/TCM-class blocks (0x8000, 0x8000, 0x4000, 0x10000);
**VRAM banks A–D 0x20000 each, E 0x10000, F 0x4000, G 0x4000, H 0x8000, I 0x4000** — these sum to
**0xA4000 = 656 KB, exactly the DS VRAM total** (definitive identification); then OAM 0x800,
palette 0x800, and several 16K/32K BIOS/TCM-class blocks; plus DMA/timer channel banks
(`FUN_00131ebc`×2, stride-0x28 4-entry loops) and IRQ regs (`FUN_00133a1c`). [proven sizes]

### B.4 Endianness / versioning [proven]
- No byte-swapping anywhere — arm64 and DS are both little-endian; every field is a raw native
  store through a bump cursor (`*(cursor)=v; cursor+=sizeof`, cursor at `param_2+0x20`).
- Version 15 threaded into every serializer; newer fields are strictly additive, gated by
  `if (version < N)` (seen N = 1,2,4,5,7,8,9,10,11,13). Older `.dss` simply lack the newer fields.

### B.5 Thumbnails and `getSnapshots16*` [proven]
- Writer `FUN_00117308`: `malloc(0x30000)`, render top via `FUN_0017fce4(buf,0)` and bottom via
  `FUN_0017fce4(buf+0x18000,1)`, apply a 16-bit channel swizzle (RGB565↔BGR555), hand both halves
  to `FUN_0017ac04`. Stored at file offsets 0x40 (top) and 0x18040 (bottom).
- Readers load with the **thumbnail-only flag = 1** so only the two 0x18000 buffers are decoded
  (not the full ~5 MB state):

| JNI | Source | Output |
|-----|--------|--------|
| `getSnapshots16` @0x119844 | slot int | 2 screens → 2 `int[]` |
| `getSnapshots16Direct` @0x119a58 | filename | 2 screens |
| `getSnapshots16TopGreyscale` @0x119d30 | filename | top screen only, edge-enhanced greyscale |

- Pixel conversion: stored **RGB565 → ARGB8888** with forced `0xFF000000` alpha
  (`(v&0x7e0)<<5 | v<<0x13 | (v>>8)&0xf8 | 0xff000000`); a NEON 8-px path is used unless the two
  Java arrays alias. Data written directly into the caller's `int[]` via
  `GetPrimitiveArrayCritical` / `ReleasePrimitiveArrayCritical` (no copy-back). [proven]
- Greyscale variant: BT.601 luma (Q16 weights 0x4c8b/0x9645/0x1ced) + a `2*c − down − right + 0x80`
  Sobel-ish edge pass over the 254×190 interior, output as grey ARGB. [proven]

---

## Part C — Cheat engine

The cheat manager object lives at `CTX + 0x370`. JNI getters use absolute offsets into `CTX`
(`manager + (off − 0x370)`). Verified directly: `getCheatCount`→`CTX+0x7dc`,
`getCheatFolderCount`→`CTX+0x7d8`, `getCustomCheatData`→`CTX+0x780` (stride 0x28).

### C.1 Key functions

| Addr | Inferred name | Role |
|------|---------------|------|
| `0017c990` | `cheatdb_open_index` | opens `usrcheat.dat`, checks `"R4 CheatCode"` magic, builds sorted game index [proven] |
| `0017ced8` | `cheatdb_load_game` | bsearch game by (gamecode, CRC32), parse its folder/cheat block [proven] |
| `0017c974` | `idx_cmp` | qsort/bsearch comparator on gamecode (first 4 bytes) [proven] |
| `0017cd90` | `rebuild_active_list` | collect enabled cheats+customs into active-pointer array `CTX+0x798` (mgr+0x428) [proven] |
| `0017c8bc` | `cheats_apply_all` | per-tick walk of active list, honoring single/multi-select folders [proven] |
| `0017bf68` | `ar_exec_one` | **AR code interpreter** — computed-goto on opcode nibble [proven dispatch] |
| `0017d708` | `custom_add` | append custom cheat (name + code words) [proven] |
| `0017d7fc` | `custom_remove` | delete custom cheat by index (memmove compaction) [proven] |
| `0017da3c` | `custom_find` | find custom cheat by (code bytes, word-len) [proven] |
| `0017d26c` | `cht_file_load` | parse `<dir>/cheats/<code>.cht` text (`[name]` + hex pairs) [proven] |
| `0017d8b8` | `cht_file_write` | write `.cht` back (`[name]` `+`=enabled, `%08X %08X` lines) [proven] |
| `0017db70` | `usrcheat_write_back` | rewrite a game block into `usrcheat.dat` in place [proven] |
| `00118494` | `updateCheats` (JNI) | only sets rebuild dirty flag `DAT_0024c4bc._1_1_` [proven] |

`updateCheats(enable)` performs **no work** — it sets a dirty bit; the frame driver `FUN_00116e74`
then calls `FUN_0017ced8` to (re)parse the current game's block using (gamecode, CRC32) read from
the loaded-ROM struct at `CTX+0x7b0`. [proven]

### C.2 In-memory data model — three parallel 0x28-byte arrays

- **DB cheats** (leaf codes from `usrcheat.dat`): ptr `CTX+0x7c8`, count `CTX+0x7dc`.
- **DB folders** (group nodes): ptr `CTX+0x7d0`, count `CTX+0x7d8`.
- **Custom cheats** (user, persisted to `.cht`): ptr `CTX+0x780`, count `CTX+0x790`, plus a parallel
  enabled-byte array.

Two gates: `CTX+0x7a4` (index DB loaded) and `CTX+0x7a8` (current game's block parsed); every getter
returns 0/empty unless both set. [proven]

**Cheat struct (0x28)** — DB and custom share layout [proven]:

| Off | Type | Field |
|-----|------|-------|
| 0x00 | `u32*` | code words (little-endian AR pairs) |
| 0x08 | `char*` | name |
| 0x10 | `char*` | note (NULL for custom) |
| 0x18 | `u32` | code length **in 32-bit words** |
| 0x1c | `s32` | parent folder id (`0xFFFFFFFF` = standalone) |
| 0x20 | `bool*` | enabled flag (pointer into the mapped DB block / enabled array) |

**Folder struct (0x28)** [proven]:

| Off | Type | Field |
|-----|------|-------|
| 0x00 | `char*` | name |
| 0x08 | `char*` | note |
| 0x10 | `u32` | child-cheat count |
| 0x18 | `bool*` | expanded flag |
| 0x20 | `u8` | select type: `0x11` = single-select (radio), else multi-select |

### C.3 Per-frame application + AR interpreter

`cheats_apply_all` (`FUN_0017c8bc`) walks the active-pointer list; standalone cheats (`+0x1c == -1`)
always run, and for a single-select folder (`type == 0x11`) only the first enabled member runs
(radio-button semantics). Each runs through `ar_exec_one`. [proven]

`ar_exec_one` (`FUN_0017bf68`) dispatch [proven]:

```
if (cheat[+0x18] /* word count */ == 0) return;
opcode = (*(u32*)cheat[+0x00]) >> 28;        // top nibble of word0 (bits 28-31)
goto handlers[ DAT_0020f7d2[opcode] ];        // 16-entry byte remap -> inlined handler @ 0x17c018
```

The DS Action Replay code format is **2 words per line**: `word0` = `(opcode<<28) | address`,
`word1` = `data`. The **top-nibble dispatch and the 16-way jump table are proven**; the individual
handler bodies (mask/shift, the `offset`/`data` registers, skip-counters, loop-counters) are
**[unknown-too-stripped]** — Ghidra failed to recover the jump table and the interpreter sits above
the code range present in the on-disk reference `.so` (its executable segment ends at 0x13228c,
below `0x17bf68`), so the arm64 handler bytes are not in the available corpus. The opcode table
below is the **standard AR-DS semantics [inferred]** that this dispatch implements:

| Nibble | Operation (standard AR-DS) |
|--------|-----------------------------|
| 0x0 | 32-bit write `[addr+offset] = data` |
| 0x1 | 16-bit write |
| 0x2 | 8-bit write |
| 0x3 | if `data > [addr]` (32-bit) execute next, else skip |
| 0x4 | if `data < [addr]` |
| 0x5 | if `data == [addr]` |
| 0x6 | if `data != [addr]` |
| 0x7 | 16-bit masked compare: `(data>>16)` vs `(mask & [addr])` |
| 0x8 | load offset register: `offset = [addr]` |
| 0x9 | data-register (`dN`) operations |
| 0xA | 16-bit variants of load/store |
| 0xB | `offset = [addr+offset]` |
| 0xC | set repeat/loop counter |
| 0xD | `Dxxxxxxx` terminators: endif / next / else / loop-end / offset-reset |
| 0xE | copy immediate byte block to `[offset]` |
| 0xF | copy `[offset]` → `[dataReg]` block |

### C.4 Custom-cheat API

- `addCustomCheat(name, byte[] code, wordLen, enabled)` → marshals name + code bytes, calls
  `custom_add` (`FUN_0017d708`): `realloc` the `CTX+0x780` array +1, `malloc(wordLen<<2)`, `memcpy`
  the `wordLen*4` code bytes, store count at `+0x18`, folder id `-1` at `+0x1c`, copy the name, set
  enabled; then `cht_file_write` + `rebuild_active_list`. [proven]
- `findCustomCheat(code, wordLen)` → `custom_find` (`FUN_0017da3c`): linear scan comparing `+0x18`
  then `memcmp(code, wordLen*4)`; returns index or `-1`. [proven]
- `removeCustomCheat(idx)` → `custom_remove`: `memmove` compaction of both arrays + `realloc` down.
- `getCustomCheatData(idx)` → returns a Java `byte[]` of raw code, size = `+0x18` words. [proven]
- Raw text parsing (hex-pair string → `u32[]`, `strtoul(...,16)`) happens in the `.cht` loader
  `cht_file_load`; `[name]` starts a group, `+` suffix = enabled. The JNI `addCustomCheat` path
  instead receives pre-packed bytes from Java. [proven]

### C.5 `usrcheat.dat` on-disk format (R4/DeadSkullzJr NDS DB) — verified by hex dump

- **256-byte header**: `0x00` magic `"R4 CheatCode\x00\x01\x00\x00"` (verified: at file offset
  0x10f7fc the binary literally contains `R4 CheatCode`); `0x10` DB title; `0x4C` `"UsAY"`+version;
  padded to `0x100`. [proven]
- **Game index @ 0x100**: array of 16-byte entries, sorted by gamecode, `offset==0` terminates:
  `{ gamecode[4] ASCII, crc32 u32, offset u64 }`. Loader converts to in-memory
  `{gamecode, crc, offset, length=next.offset−this.offset}` and `qsort` by gamecode. Lookup =
  `bsearch` gamecode then linear CRC match. [proven]
- **Game block**: game name (NUL-term, 4-aligned), then `u32 entryCount (& 0x0FFFFFFF)`, then a
  flat pre-order list. Each entry starts with a **flag word**: bit28 = folder(1)/cheat(0);
  bits0–23 = folder child-count OR cheat words-to-next; byte3 = folder type / cheat default-enabled;
  byte2 = folder default-expanded. Followed by `name\0`, `note\0`, 4-align; cheats then have
  `u32 codeWordCount` + code words. Parser advances `ptr += (flag & 0xFFFFFF) + 1` words.
  Rewritable in place via `usrcheat_write_back`. [proven]
- **`game_database.xml`** (separate): `<rom crc32=… id=… title=…/>` supplies the CRC32 + gamecode
  key used to index `usrcheat.dat`. [proven]

### C.6 On-device paths [proven]
`%s%cusrcheat.dat` (main DB), `%s%ccheats%c%s.cht` (per-game custom cheats),
`%s%csavestates%c%s_%d.dss` (savestates). `%c` = `'/'`.

_Note: DB cheats and folders come from `usrcheat.dat` and are applied through the AR interpreter;
custom cheats live in a plaintext `.cht` and are merged into the same active list at rebuild time._

---

## Part D — Input

### D.1 JNI input entry points [proven]
Two writers stage input into globals; the emulation thread injects them each frame.

```
updateInput(env,cls, uint buttons, uint touch, int extra)  @ 0x0011a5d8
updateFrame(env,cls, uint buttons, uint touch, int extra)  @ 0x0011a53c   // same packing + returns frame status
```

Packing [proven]:
| Global | Source | Meaning |
|--------|--------|---------|
| `DAT_0024c48c` | `buttons & 0x7FFFFFFF` | DS button mask (low bits) + emulator hotkey command bits (high bits) |
| `DAT_0024c4bf` | `buttons >> 31` | **touch/pen-down** flag |
| `DAT_0024c494` | `(int)touch >> 16` | **touch X** (signed) |
| `DAT_0024c498` | `touch & 0xFFFF` | **touch Y** |
| `DAT_0024c490` | `extra` | **turbo / autofire mask** |

`updateFrame` additionally returns the packed frame-status word (paused / one-shot event / settle /
progress %) — identical layout to `getFrameInfo` (DOC 11 §4.2).

### D.2 Per-frame injection — `FUN_00116e74` [proven]
Runs once per frame; writes into the machine's hardware-register mirror:
- **Keypad** → `CTX+0x80010`. Turbo/autofire: if `(turboMask & buttons)` and the current frame
  falls in the "off" phase of pattern table `DAT_00206cf8[DAT_0024c4b7]` (indexed by turbo speed,
  `DAT_0024c4b7` from config bits 32–34), the masked buttons are cleared this frame — producing
  autofire. Frame counter `DAT_0024c460` advances each call. When paused (`DAT_0024c4c0`), bit
  `0x1000` is OR'd into the keypad.
- **Pen down** → `CTX+0x8001c`; **touch X/Y** → `CTX+0x80014` / `CTX+0x80018`.
- **Accelerometer**: `updateAccelerometer(x,y,z)` @0x11a614 stages `DAT_0024c4c4/c8/cc` + dirty flag
  `DAT_0024c4d0`; injected to `CTX+0x80030` (x,y) / `CTX+0x80038` (z), flag at `CTX+0x8003c`.
- **Gyroscope**: `updateGyroscope(v)` @0x11a630 → `DAT_0024c4d4` + flag `DAT_0024c4d8`; injected to
  `CTX+0x80040`, flag `CTX+0x80044`.
- **Hinge / rotation**: `luaUpdateRotation(int)` @0x11a93c wraps the angle (`<0xB5` passthrough,
  else `+0xFE98`) into `DAT_0024c448` (a `short`). [proven]

### D.3 Emulator hotkey commands packed in the input word — `FUN_00180834` [proven]
The **upper bits** of the keypad word `CTX+0x80010` are not DS buttons — they are one-shot emulator
commands, each consumed and cleared (`&= ~mask`) by the frame step:

| Bit | Action |
|-----|--------|
| 0x13 (19) | **Quick-save** state (renders two 0x18000 thumbs, `FUN_0017ac04` to slot `CTX[0x11556]`) |
| 0x14 (20) | **Quick-load** state (`FUN_0017acc4` from slot `CTX[0x11556]`) |
| 0x15 (21) | Toggle a mode flag (`CTX+0x8aab4`; sets `CTX+0x3b2f92b`) — likely fast-forward toggle |
| 0x16 (22) | Toggle `CTX+0x8aaac ^ 1` → `FUN_0011cd14` (screen layout swap) |
| 0x17 (23) | Toggle `CTX+0x8aaa4 ^ 1` → `FUN_0011cd10` |
| 0x18 (24) | Toggle `CTX+0x8aaa4 ^ 2` → `FUN_0011cd10` |
| 0x1a (26) | **Quit emulation** (`FUN_0011b428`, longjmp out) |
| 0x1c (28) | Soft-reset ARM9 (`FUN_0017dc24(CTX+0x2b9dcd,0)`) |
| 0x1d (29) | Soft-reset ARM7 (`FUN_0017dc24(CTX+0x4baa8b,0)`) |
| 0x12 (18) | Power/return (`FUN_0011b31c`) |

### D.4 Movie / input-recording (DSM-style) [proven]
`FUN_00180834` also implements deterministic input record/playback:
- **Playback** (`CTX+0x80050 == 2`): reads 10-byte input records from a buffer and, when the frame
  index matches, overrides keypad/pen/touchX/touchY from the record (`buttons` u32, `penDown` in
  bit31, `touchX` byte, `touchY` byte).
- **Record**: on any input change it appends a **10-byte record** {frame u32, buttons|penDown u32,
  touchX u8, touchY u8} and `fwrite`+`fflush` to the movie file at `CTX+0x80048`.

### D.5 Lua input overrides [proven]
- `luaUpdateAxisValues(a,b,c,d)` @0x11a900 → `DAT_0024c438/440` (two 2-float pairs = analog axes).
- Per frame `FUN_0011b17c(&DAT_0024c438)` + `FUN_0011b198(&DAT_0024c450)` compute overrides:
  `DAT_0024c450` = touch override (`&0xff` into bits16-23), `DAT_0024c454`/`DAT_0024c458` set
  bits 15/31 and 14/30 respectively. Result packed into `DAT_0024c4a8`.
- `luaGetOverrides()` @0x11a914 returns `DAT_0024c4a8` to Java. `luaIsActive` gates the whole path.
- The Lua controller exposes named axes (`get_axis_lx`, … via `PTR_s_get_axis_lx_00239000`,
  registered under module `"android"`), i.e. the script can read/override stick + touch input.

### D.6 Notes for the profiler
- Input is latency-cheap: JNI writes globals, the emu thread samples once per frame (no locking on
  the button path beyond the single-writer/single-reader globals).
- The upper-bit "hotkey command" channel means a single `updateFrame` call both feeds buttons and
  can trigger save/load/reset/quit — worth replicating cleanly rather than as magic bits in melonDS.
