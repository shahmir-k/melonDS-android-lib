# DraStic Teardown — 04: 3D GPU Geometry / Vertex Pipeline

Reverse-engineered from the stripped arm64 build `libdrastic_arm64.so`.
The DS 3D engine is **software** in DraStic (no GL 3D — GLES2 only blits the finished
software framebuffer; see doc 05). This doc covers the **geometry/vertex stage**:
GXFIFO command processing, matrix stack, vertex transform, lighting, clipping, viewport,
polygon setup, and the swap-buffers boundary. The **rasterizer** (span/pixel fill) is doc
covered separately (`FUN_001913dc` and the 0x18a000–0x191000 NEON cluster).

Method: Ghidra decompilation cross-checked against raw `objdump -d` and ELF relocations.
Confidence tags: **[P]** proven-from-binary · **[I]** inferred · **[U]** unknown / too-stripped.

> Address-base note: `FUN_00XXXXXX` = Ghidra address (image based `0x100000`). objdump
> address = Ghidra − `0x100000` (e.g. NEON matmul `FUN_001904e8` → objdump `0x904e8`).
> The geometry per-frame state struct lives at `emu_base + 0x356cb0`.

---

## 0. TL;DR verdicts

| Question | Verdict | Confidence |
|---|---|---|
| Batching model | **Deferred display-list (batched).** GXFIFO words are unpacked into two ring buffers (command-byte stream + parameter-word stream); the list is executed later at drain/SWAP points — NOT per-write. | **[P]** |
| Command dispatch | **Threaded 128-entry jump table** at Ghidra `0x233c58`: `ldrb cmd; ldr handler=[table+cmd*8]; br handler`. Handlers are inline threaded code (no call/ret) that fall through to the next command. | **[P]** |
| Matrix multiply | **NEON fixed-point** `FUN_001904e8`: `ld1 {v4-v7}`, `smull/smlal2 .2d` (32×32→64), `shrn/shrn2 #0xc` (>>12 = 20.12). Column-major. A secondary **scalar** unrolled 4×4 (`FUN_001625bc`) also exists. | **[P]** |
| Vertex transform | Vertices decoded (VTX_16/10/XY/XZ/YZ/DIFF), multiplied by a lazily-recomputed **clip matrix** (proj × position), then clipped/viewport-transformed. Vertex stride `0x10`, count reg `+0x330`. | **[P]** |
| Lighting | Up to 4 lights; NORMAL 10-bit sign-extend, normal × vector-matrix, dot products `>>9`. | **[P]** |
| Vertex/poly RAM | **Double-buffered**, banks of stride `0x10008`, selected by `*(+0x9ac0) ^ 1`, flipped at SWAP_BUFFERS. HW caps enforced: **6144 vertices (0x1800), 2048 polygons (0x800)**. | **[P]** |

---

## 1. Key functions

| Ghidra addr | Role | Size | Evidence |
|---|---|---|---|
| `FUN_001314cc` | GXFIFO MMIO burst-write entry (`0x04000400`) | 804 | dispatched from CPU store handler `FUN_0012d9fc` on `addr==0x4000400` **[P]** |
| `FUN_001694c8` | **GXFIFO packed-command decoder** (splits cmd/param streams) | 992 | 32-bit word → 4× `&0x7f` cmd bytes; param count from `DAT_0020e94c[cmd]` **[P]** |
| `FUN_001698a8` | Same decoder, cycle-counting variant | 1056 | identical structure, returns GX cycle cost **[P]** |
| `FUN_00163c34` | **Command executor** (threaded jump-table interpreter) | 340 | `adrp x21,0x133000`; `ldrb w8,[x24]`; `ldr x8,[x21,x8,lsl#3]`; `br x8` **[P disasm]** |
| `FUN_00163aec` | Display-list **drain/flush + double-buffer reset** | 328 | rewinds cmd buf `+0x79b00`, param buf `+0x81b00` **[P]** |
| `FUN_001904e8` | **4×4 fixed-point matrix multiply — NEON** | 176 | `ld1 {v4-v7}`; `smull/smlal/smull2/smlal2 .2d`; `shrn/shrn2 #0xc` **[P disasm]** |
| `FUN_001625bc` | Scalar unrolled 4×4 matrix multiply (secondary/fallback) | 956 | 16 outputs, each `Σ4 products >> 0xc`; referenced once (indirect) **[P]** |
| `FUN_00162978` | Scalar 4×3 / 3×3 matrix multiply variant | 624 | same 20.12 form, fewer terms **[P]** |
| `FUN_001667ac` | **Vertex + normal transform + lighting** | 1796 | 10-bit normal sign-extend `<<0x16>>0x36`; dots `>>9`; vector-matrix `+0x9a60` **[P]** |
| `FUN_0016dc1c` | Per-vertex transform helper (vertex × clip matrix) | — | called from VTX_16 handler **[P]** |
| `FUN_00166eb0` | **Primitive assembly / near-plane clip** (BEGIN_VTXS modes) | 5408 | `switch(mode)` cases 0-3 = tri/quad/tristrip/quadstrip; `local_f8[68]` clip scratch **[P]** |
| `FUN_0016dee8` | Polygon assembly (tri/quad winding, vertex-index build) | 1056 | HW caps: `if(0x1800<verts || polys==0x800)` (6144/2048); `local_370[384]` clip scratch **[P]** |
| `FUN_0016a070` | Polygon setup / color pack (NEON integer) | 5548 | `auVar[16]` NEON; 6→5-bit color; `add/mul/and/orr` vectors **[P]** |
| `FUN_00166754` | **SWAP_BUFFERS body** | 88 | transform+light → assemble/clip → flip bank `+0x9ac0` **[P]** |
| `FUN_00169074` | Geometry-state debug dumper (`geometry_log_*.bin`) | 856 | reveals struct offsets **[P]** |
| `FUN_0013d90c` | Frame-level 3D render orchestrator (per-region finalize) | 552 | iterates 9 regions, calls geometry finalize `+0x356cb0` and `FUN_00139880`; from frame path `FUN_0011c614` **[P/I]** |

### Opcode → handler table (resolved via `.rela.dyn`, Ghidra addrs) **[P]**
Jump table base **`0x233c58`** (objdump `0x133c58`), 128 × 8-byte slots.

| Op | Cmd | Handler | Op | Cmd | Handler |
|---|---|---|---|---|---|
| 0x10 | MTX_MODE | `FUN_00163d54` | 0x28 | VTX_DIFF | `FUN_0016558c` |
| 0x11 | MTX_PUSH | `FUN_00163d7c` | 0x29 | POLYGON_ATTR | `FUN_00165720` |
| 0x12 | MTX_POP | `FUN_00163fcc` | 0x2a | TEXIMAGE_PARAM | `FUN_00165630` |
| 0x13 | MTX_STORE | `FUN_00164134` | 0x2b | PLTT_BASE | `FUN_001656ac` |
| 0x14 | MTX_RESTORE | `FUN_001642cc` | 0x30 | DIF_AMB | `FUN_0016593c` |
| 0x15 | MTX_IDENTITY | `FUN_001644cc` | 0x31 | SPE_EMI | `FUN_00165b1c` |
| 0x16 | MTX_LOAD_4x4 | `FUN_00164628` | 0x32 | LIGHT_VECTOR | `FUN_00165ca4` |
| 0x17 | MTX_LOAD_4x3 | `FUN_00164948` | 0x33 | LIGHT_COLOR | `FUN_00165e18` |
| 0x18 | MTX_MULT_4x4 | `FUN_00164be4` | 0x34 | SHININESS | `FUN_001655e0` |
| 0x19 | MTX_MULT_4x3 | `FUN_00164c8c` | 0x40 | BEGIN_VTXS | `FUN_00165f90` |
| 0x1a | MTX_MULT_3x3 | `FUN_00164d2c` | 0x41 | END_VTXS | `FUN_001660cc` |
| 0x1b | MTX_SCALE | `FUN_00165148` | 0x50 | SWAP_BUFFERS | `FUN_001660e4` |
| 0x1c | MTX_TRANS | `FUN_00164dd4` | 0x60 | VIEWPORT | `FUN_00166154` |
| 0x20 | COLOR | `FUN_00165744` | 0x70 | BOX_TEST | `FUN_001661dc` |
| 0x21 | NORMAL | `FUN_001657b4` | 0x71 | POS_TEST | `FUN_001664f0` |
| 0x22 | TEXCOORD | `FUN_00165898` | 0x72 | VEC_TEST | `FUN_00166618` |
| 0x23 | VTX_16 | `FUN_0016544c` | 0x24 | VTX_10 | `FUN_0016548c` |
| 0x25 | VTX_XY | `FUN_001654d0` | 0x26 | VTX_XZ | `FUN_00165510` |
| 0x27 | VTX_YZ | `FUN_00165550` | — | (unused) | `FUN_001666f4` (no-op, advances ptr) |

Parameter-count table **`DAT_0020e94c`** (objdump `0x10e94c`, `.rodata`), 128 bytes indexed by
cmd — matches DS GX hardware exactly: MTX_MODE=1, MTX_PUSH=0, MTX_LOAD_4x4=16,
MTX_LOAD_4x3=12, MTX_MULT_3x3=9, SCALE/TRANS=3, VTX_16=2, VTX_10=1, SHININESS=32,
BEGIN_VTXS=1, END_VTXS=0, SWAP_BUFFERS=1, BOX_TEST=3, POS_TEST=2, VEC_TEST=1, etc. **[P]**

---

## 2. Batching model — deferred display-list [P]

DS GX commands are received via CPU/DMA writes to the GXFIFO (`0x04000400`) and command
ports (`0x04000440–0x040005CC`). DraStic does **not** execute each command at write time.
The decoder `FUN_001694c8` unpacks each 32-bit GXFIFO word into **two separate ring
buffers** in the geometry state struct:

- **Command-byte stream** at `+0x79b00` (write ptr `+0x9a68`, end `+0x9a78`), capacity `0x400`.
- **Parameter-word stream** at `+0x81b00` (write ptr `+0x9a70`, end `+0x9a80`).

Partial commands straddling a 32-bit word are carried in a pending count at `+0x9ac1`.
The accumulated display list is executed later by the threaded interpreter `FUN_00163c34`
at drain points — on buffer-full (`0x400`) and at the frame/SWAP boundary — then the buffers
are rewound by `FUN_00163aec`. (The split-stream design is independently corroborated by
DraStic's own `geometry_log_commands.bin` / `geometry_log_parameters.bin` debug dumps.)

**Implication for melonDS:** this is the same "build a per-frame display list, execute at
swap" model melonDS uses, but with the command *bytes* and *parameter words* physically
de-interleaved into two contiguous streams — cache-friendly for the threaded interpreter.

---

## 3. Command dispatch — threaded jump table [P disasm]

Executor inner loop (`FUN_00163c34`, objdump `0x63d24`):
```asm
adrp x21, 0x133000          ; x21 = table base 0x133c58 (Ghidra 0x233c58)
...
ldrb w8, [x24]              ; next command byte from +0x79b00 stream
ldr  x8, [x21, x8, lsl #3]  ; 8-byte stride table lookup
br   x8                     ; jump to handler
; each handler consumes its params from the +0x81b00 stream,
; then re-loads the next cmd byte and br's again  -> "threaded code", no call/ret
```
Handlers are **inline threaded code**: no `bl`/`ret`, each falls through to the next command
by repeating the `ldrb`/`ldr`/`br`. Static table slots are pre-relocation; true targets come
from `R_AARCH64_RELATIVE` addends in `.rela.dyn` (resolved table in §1). Unused opcodes route
to a common no-op `FUN_001666f4`.

---

## 4. Matrix stack [P]

Geometry state offsets (from dumper `FUN_00169074` + reset path):

| Item | Offset | Notes |
|---|---|---|
| Matrix-mode byte (`& 3`) | `+0x49a` / `+0x9ac2` | `MTX_MODE` writes `and w8,#3; strb [x28,#0x49a]` |
| Projection matrix (16×i32, 20.12) | `+0x9824` | depth-1 stack |
| Position stack current-top ptr | `+0x9a58` → base `+0x9764` | coordinate/position stack |
| Vector/direction stack ptr | `+0x9a60` → base `+0x97a4` | for normals/lighting |
| Texture matrix | `+0x9864` | |
| Clip matrix (proj × position), cached | `+0x97e4` | recomputed lazily when dirty `+0x9ad0 != 0` |
| Vertex count register | `+0x330` | |

- **MTX_MULT_4x4** (`FUN_00164be4`) reads the mode, indexes the parameter stream, and calls
  the NEON matmul `FUN_001904e8` **twice in coordinate mode** (updates both position and
  vector matrices) — hardware-accurate. It sets the clip-dirty flag `+0x9ad0`.
- The clip matrix is recomputed on demand via
  `FUN_001904e8(clip=+0x97e4, proj=+0x9824, position=*(+0x9a58))` only when a vertex is
  submitted and the dirty flag is set — a **lazy** evaluation optimization.

### Matrix multiply — NEON 20.12 fixed-point [P disasm]
`FUN_001904e8` (objdump `0x904e8`), column-major, i64 accumulation, `>>12` narrow:
```asm
ld1   {v4.4s,v5.4s,v6.4s,v7.4s},[x1]   ; matrix A (4 columns)
ld1   {v0.4s,v1.4s,v2.4s,v3.4s},[x2]   ; matrix B
smull  v20.2d, v4.2s, v0.s[0]          ; 32×32→64 widening, low lanes
smlal  v20.2d, v5.2s, v0.s[1]
smlal  v20.2d, v6.2s, v0.s[2]
smlal  v20.2d, v7.2s, v0.s[3]
smull2 v21.2d, v4.4s, v0.s[0]          ; high lanes
smlal2 v21.2d, v5.4s, v0.s[1] ... smlal2 v21.2d, v7.4s, v0.s[3]
shrn   v16.2s, v20.2d, #0xc            ; >>12 back to 20.12
shrn2  v16.4s, v21.2d, #0xc
... (repeat for the other columns) ...
st1   {v16.4s..v19.4s},[x0]
```
A secondary fully-unrolled **scalar** 4×4 matmul `FUN_001625bc` exists (16 outputs, each
`(a·b+c·d+e·f+g·h) >> 0xc`); it is referenced only indirectly and is likely the fallback /
box-and-position-test path, while the NEON `FUN_001904e8` is the one wired to the live
MTX_MULT handlers and the clip-matrix recompute. **[P for both; primary=NEON]**

---

## 5. Vertex transform [P]

- **VTX_16** (`FUN_0016544c`): `ldp w20,w8` then `sxth`/`asr #16` splits two signed 16-bit
  4.12 coords (x,y from word 0; z from word 1) → transform helper `FUN_0016dc1c`.
- **VTX_10 / VTX_XY / VTX_XZ / VTX_YZ** have dedicated handlers (`0x16548c…0x165550`);
  **VTX_DIFF** (`0x16558c`) adds a signed 10-bit delta to the previous vertex.
- Each submitted vertex is multiplied by the cached clip matrix (proj × position). Vertex
  records are `0x10` bytes; the transformed position lands in the vertex RAM at `+0x650`
  region (per-vertex index × `0x10`).

### Clipping / viewport
- **Near-plane / frustum clipping against ±w** and new-vertex interpolation are performed in
  `FUN_00166eb0` (primitive assembly; `local_f8[68]` = clipped-vertex workspace) and
  `FUN_0016dee8` (`local_370[384]`/`local_1f0` clip scratch).
- **VIEWPORT** (`FUN_00166154`) unpacks x0/y0/x1/y1 bytes (`ubfx`) and clamps y to `0xbf`
  (191) → 256×192 screen mapping.
- Depth: the reciprocal/division needed for perspective and edge interpolation uses a
  **512-entry reciprocal LUT** (`DAT_0402e128`/`DAT_0402f128`, built in `FUN_0015f614`) plus a
  fixed-point reciprocal `((v | 0x4000000000000000) - 1) / v` seen in the geometry region.

---

## 6. Lighting (up to 4 lights) [P]

`FUN_001667ac` decodes the 10-bit signed NORMAL components (`iVar << 0x16 >> 0x36`
sign-extend), transforms the normal by the **vector matrix** (`+0x9a60`), then accumulates up
to 4 lights via dot products scaled `>> 9`. Per-command state handlers:
`DIF_AMB` (`0x16593c`), `SPE_EMI` (`0x165b1c`), `LIGHT_VECTOR` (`0x165ca4`),
`LIGHT_COLOR` (`0x165e18`), `SHININESS` (`0x1655e0`, consumes the 32-word specular table —
param count 32, matching hardware).

---

## 7. Polygon setup & the SWAP_BUFFERS boundary [P]

- **BEGIN_VTXS** (`FUN_00165f90`): `and w20,w27,#0xf` extracts primitive type (0=tri, 1=quad,
  2=tri-strip, 3=quad-strip) and compares to the current mode to decide flushing.
- **POLYGON_ATTR** (`FUN_00165720`) latches polygon attributes; **primitive assembly**
  (`FUN_00166eb0` / `FUN_0016dee8`) builds triangles/quads with correct strip winding and
  enforces the DS RAM caps: **6144 vertices (0x1800)** and **2048 polygons (0x800)** — excess
  primitives are dropped.
- **SWAP_BUFFERS body** (`FUN_00166754`): runs transform+lighting (`FUN_001667ac`) then
  assemble/clip (`FUN_00166eb0`), returns the fresh polygon+vertex counts, and flips the
  **double buffer** (banks stride `0x10008`, index `*(+0x9ac0) ^ 1`). The rasterizer threads
  (doc: rasterizer) then consume the completed bank.

### Threading & handoff [P]
Geometry runs on the **emulator/CPU thread** (the GXFIFO decode + threaded command
interpreter execute inline as the CPU time-slice writes commands / hits SWAP). The completed,
double-buffered polygon+vertex bank is then rasterized by a **separate 4-way thread group**
set up in `FUN_0015f614` (3 worker threads `FUN_0015f53c` + dispatcher `FUN_0015f2c8`, plus
the caller), which also builds the 512-entry reciprocal LUT. So per frame: 1 emu thread does
CPU + geometry; up to 4 threads rasterize; a separate thread does engine-B 2D (doc 03). This
4-way rasterizer group is distinct from the generic 32-thread pool `FUN_001c196c` (util).

---

## 8. Data structures (geometry state @ `emu_base + 0x356cb0`) [P]

- Command-byte ring `+0x79b00` (cap `0x400`), param-word ring `+0x81b00`.
- Ring pointers: cmd wr `+0x9a68`/end `+0x9a78`; param wr `+0x9a70`/end `+0x9a80`; pending
  `+0x9ac1`.
- Matrices: proj `+0x9824`, position stack `+0x9764`(ptr `+0x9a58`), vector stack
  `+0x97a4`(ptr `+0x9a60`), texture `+0x9864`, clip cache `+0x97e4`, dirty flag `+0x9ad0`,
  mode `+0x49a`.
- Vertex RAM record stride `0x10`; vertex count `+0x330`; transformed-vertex area near `+0x650`.
- Double-buffer bank stride `0x10008`, active-bank selector `+0x9ac0`.

---

## 9. DraStic-specific speed tricks

1. **Deferred display-list with de-interleaved streams** — command bytes and parameter words
   accumulate into two separate contiguous ring buffers, executed by a branchless threaded
   interpreter (no call/ret per command). **[P]**
2. **NEON fixed-point matrix multiply** — `smull/smlal2 .2d` widening + `shrn #12`, whole 4×4
   in a handful of vector ops; column-major so a matrix loads as four `.4s` registers. **[P]**
3. **Lazy clip-matrix recompute** — proj × position is recombined only when dirty and only
   when a vertex actually needs it, avoiding redundant matmuls on matrix-heavy command bursts. **[P]**
4. **Reciprocal LUT** (512 entries) for the divisions in perspective/edge math, shared with
   the rasterizer. **[P]**
5. **Geometry on emu thread, rasterization on a 4-way thread group** — clean producer/consumer
   split at the double-buffered polygon bank. **[P]**
6. **Threaded jump-table opcode dispatch** with a hardware-exact 128-byte parameter-count
   table — minimal per-command overhead. **[P]**

---

## 10. Actionable for melonDS

- melonDS's `GPU3D.cpp` executes GX commands through a `switch`/function-pointer per command.
  DraStic's **branchless threaded-code interpreter** (handlers fall through to the next
  command with no return) is a measurable win on ARM for command-heavy scenes; worth
  prototyping if GX dispatch shows up hot.
- melonDS's matrix multiply (`MatrixMult4x4` etc.) is scalar 64-bit. DraStic's **NEON
  `smull/smlal2 + shrn #12`** column-major 4×4 is a directly portable drop-in — the exact
  instruction template is in §4. This is the single most transplantable geometry optimization.
- **De-interleaving command bytes from parameter words** into two streams improves the
  interpreter's I-cache/D-cache behavior; melonDS keeps them interleaved in the FIFO entry.
- **Lazy clip-matrix caching** (dirty flag + recompute-on-vertex) mirrors melonDS's own
  approach but is worth verifying melonDS invalidates as narrowly as DraStic (only on
  proj/position change).
- Keep geometry on the emu thread and rasterization on worker threads; DraStic's 4-way split
  at a double-buffered polygon bank is a good template (melonDS's threaded soft-renderer
  already does something similar per-scanline).

---

## 11. Open items / limits

- `FUN_0016a070` (polygon color-pack) and `FUN_001913dc` (14 KB **rasterizer**, other doc)
  fall past the objdump dump's end (objdump ≤ `0x106cbc`); their NEON was confirmed via Ghidra
  vector types (`auVar[16]`, `NEON_ushl`) rather than raw disasm. **[I-strong]**
- Exact per-vertex clip-polygon interpolation math inside `FUN_00166eb0` (large; structurally
  identified, cases 0-3 for primitive types, but the interpolation inner loop not transcribed
  instruction-by-instruction). **[I]**
- Role overlap on `FUN_0013d90c`/`FUN_0016bbf4`: appears in a frame-render-orchestration path
  (per-region finalize) and has also been read as a geometry-state (savestate) serializer;
  the exact split is not fully disambiguated. **[U]**
