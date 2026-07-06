# DraStic Teardown — 03: 2D GPU Engine (Engines A & B)

Reverse-engineered from the stripped arm64 build `libdrastic_arm64.so`.
Method: Ghidra decompilation (`FUN_<hexaddr>` names, image based at `0x100000`)
cross-checked against raw `objdump -d` (objdump address = Ghidra address − `0x100000`).
Confidence tags: **[P]** proven-from-binary · **[I]** inferred · **[U]** unknown / too-stripped.

> Address-base note (matters for reproduction): every `FUN_00XXXXXX` here is the
> Ghidra address. To find the same code in `drastic_disasm.txt` (raw objdump)
> subtract `0x100000` (e.g. `FUN_001494a4` → objdump `0x494a4`). An earlier pass
> mistakenly grepped Ghidra addresses in the objdump file and wrongly concluded the
> 2D region was "not in the dump"; it is — the NEON evidence below is real.

---

## 0. TL;DR verdicts

| Question | Verdict | Confidence |
|---|---|---|
| CPU scanline vs GPU shader | **CPU/software scanline compositor.** GLES2 only uploads+blits the finished software framebuffer (see doc 05). | **[P]** |
| Per-scanline or per-frame | **Per-scanline-accurate but batched:** whole visible frame (lines 0–191) rendered in one burst at line 191; mid-frame register writes trigger scanline-exact catch-up flushes via a replay queue. | **[P]** |
| CPU thread vs worker pool (**key**) | **Two-thread split: engine A (top) on the emulator/CPU thread, engine B (bottom) on a *dedicated* 2D render thread `FUN_0013cca0`**, joined by a condvar each frame. NOT the generic `FUN_001c196c` 32-thread pool. | **[P]** |
| NEON | **Inner pixel-composite kernels are NEON-vectorized** (color-format convert / BGR555 pack / blend, 4–8 px per iteration). Sprite OAM iteration and priority sort are scalar. | **[P]** |
| Internal upscaling | Software renderer can render above native res; per-screen framebuffer slot is `0xc0000` bytes, GL uploads `(scale+1)*256 × (scale+1)*192`. | **[P]** |

---

## 1. Key functions

| Ghidra addr | Inferred role | Size | Evidence |
|---|---|---|---|
| `FUN_0012344c` | **Top-level MMIO I/O write router** | 360 | jump table `DAT_0020a6e8[reg]` for offsets `0x000–0x604`; writes I/O shadow RAM at `base+0x1b070`; VCOUNT compared to `0xc0` (192) to pick replay vs immediate **[P]** |
| `FUN_0014f440` | **2D register-effect decode** (DISPCNT/BGxCNT/scroll/affine/WIN/BLD/MOSAIC/MASTER_BRIGHT + PAL/OAM) | 3028 | switch on `reg & 0xfff` = exact DS 2D register map (§3); writes engine-A/B context; calls FB getter | **[P]** |
| `FUN_00150014` | Enqueue register-write tagged with current scanline (per-line replay queue) | 56 | 15 callers; entered from MMIO path when `line < 0xc0` **[P]** |
| `FUN_0013d3c4` | **2D subsystem init** — spawns engine-B render thread, builds both engine contexts + condvars | 604 | `pthread_create(…, FUN_0013cca0, …)`; inits ctx at `+0x5cf` and `+0x10853` **[P]** |
| `FUN_0013cca0` | **Dedicated engine-B render thread** (bottom screen) | 216 | `pthread_cond_wait(ctx+0x4588b0)`; `FUN_0015004c(ctx+0x84298,0,0xbf,0)`; sets done flag + signals **[P]** |
| `FUN_0012c8f8` | Per-scanline scheduler / HBlank-VBlank-IRQ state machine | 1588 | keys on lines `0xbf`(191) `0xc0` `0xd6` `0x105`/`0x106`(262) **[P]** |
| `FUN_0013cf88` | End-of-visible-frame (line 191): trigger full render, FPS counter, GL signal | 912 | calls render-flush, FPS ring, `FUN_0011cb5c` (signalScreen) **[P]** |
| `FUN_0013cd78` | **Render-flush dispatcher** (renders cursor..N; forks engine B when full-frame) | 528 | VRAM flush + cond signal/wait + `FUN_0015004c` per engine **[P]** |
| `FUN_0015004c` | **Scanline loop** (`for line=a..b`) + per-line reg-write replay | 288 | `while(line<=end){ FUN_0014c610(ctx,buf,line); replay FUN_0014f440 }` **[P]** |
| `FUN_0014c610` | **Per-scanline master-output compositor** (display-mode mux + master brightness + BGR555 pack) | 3076 | DISPCNT `>>0x10 & 3` switch; MASTER_BRIGHT tables; **35 NEON** ops (BGR555 masks) **[P]** |
| `FUN_001494a4` | **BG-layer scanline renderer + priority composite + color-special-effects** (mode 1) | 7692 | BG-enable `DISPCNT>>8 & 0xf`, window masks, blend; **927 NEON** ops **[P]** |
| `FUN_00141a40` | Per-BG dispatch + MOSAIC; calls per-BG render callback `bgctx+0xf0` | 668 | indirect call, MOSAIC reg `0xa8` (mostly scalar) **[P]** |
| `FUN_00141cdc` | Affine / extended-rotoscale / bitmap BG + 3D-layer line merge | 6264 | DISPCNT bit `0xc` gate; **516 NEON** ops (integer) **[P]** |
| `FUN_0014b2b0` | **OBJ / sprite engine** — 128-OAM iterate, sizes, affine, per-line binning | 2820 | loop bound `0x80`; attr0/1/2 decode; bins per line×priority (**scalar**, 1 NEON) **[P]** |
| `FUN_0014e078` | Sprite/BG affine PA/PB/PC/PD → per-line fixed-point increments | 332 | called ×2 from OBJ affine path **[P]** |
| `FUN_00131a08` | Affine BG reference-point per-scanline advance (BG2/BG3) | 280 | per-line call from `FUN_0014c610` **[P]** |
| `FUN_0014e1c4` | BG priority-sort rebuild (on DISPCNT/BGxCNT change) | 2916 | reads BGxCNT priorities at `+0x158/0x208/0x2b8/0x368` → 4 buckets (**scalar**) **[P]** |
| `FUN_0018bc98` | VRAM/main-mem BGR555 direct-bitmap line copy (display modes 2/3) | 132 | 256-halfword unrolled copy **[P]** |
| `FUN_0013c938` | VBlank display-swap + **display-capture (DISPCAPCNT)** setup | 872 | engine↔screen map; `memalign(0x10,0x60000)` capture buf; EVA/EVB `&0x1f` cap `0x10` **[P]** |
| `FUN_0011cd18` | Per-screen software-framebuffer pointer getter | 32 | `(&DAT_0402d1f8)[dblbuf] + (screen&1)*0xc0000` **[P]** |
| `FUN_0011cd38` | Framebuffer stride getter (`0x200`/`0x400`/`0x800`/`0x1000` by scale) | 60 | scale-dependent stride select **[P]** |
| `FUN_0017fce4` | Internal-res framebuffer → native 256×192 RGB565 downscale/convert | 452 | reads 32-bit FB, packs `(v>>5)&0x7e0|(v>>3)&0x1f|(v>>8)&0xf800`, 192 rows (**scalar**) **[P]** |
| `FUN_0011ceac` | GL blit of software framebuffer (glTexSubImage2D + glDrawArrays) | 348 | see doc 05; uploads `(scale+1)*0x100 × (scale+1)*0xc0` **[P]** |

---

## 2. Threading & data-flow model (**key verdict**) [P]

DraStic's two DS 2D engines render **in parallel on two threads**:

```
emulator/CPU thread                         dedicated 2D render thread (FUN_0013cca0)
────────────────────                        ─────────────────────────────────────────
CPU recompiler runs to scanline 191
scheduler FUN_0012c8f8 hits line 0xbf
  └─ FUN_0013cf88 ─ FUN_0013cd78:
        flush dirty VRAM banks (16 KB blocks)
        ctx[0x8b122]=1; cond_signal(ctx+0x8b116) ───────────►  wakes on cond ctx+0x4588b0
        render_lines(engineA, 0..191)  [FUN_0015004c]           render_lines(engineB, 0..191)
                                                                 [FUN_0015004c(ctx+0x84298,...)]
        cond_wait(ctx+0x8b11c) until engineB done ◄──────────── set done; cond_signal(ctx+0x8b11c)
        FPS ring update; signalScreen (FUN_0011cb5c) → GL thread blits
```

Address arithmetic proves both sides share the same context/condvars:
`0x8b116*8 = 0x4588b0`, `0x10853*8 = 0x84298`. **[P]**

Nuances **[P]**:
- The engine-B thread is created **once** at GPU init (`FUN_0013d3c4`) with its own two
  mutexes + two condvars. It is **not** the generic `FUN_001c196c` 32-thread worker pool
  (that pool's consumers live at `0x1b7664`/`0x1c8660` — util, not rendering; see doc 07).
- Parallelism applies only to the **full-frame flush** (`N == 0xbf`). Mid-frame partial
  catch-up flushes render **both engines inline on the emu thread** (serial) — the `else`
  branch of `FUN_0013cd78` calls `FUN_0015004c` twice back-to-back.
- Net effective 2D speedup ≈ **2×** on the common path (one engine per thread). Everything
  is CPU/software; GLES2 (doc 05) never composites — it only blits the finished FB.

Separately, the DS **3D software rasterizer** uses a *different*, larger thread group
(4-way, `FUN_0015f614`; see doc 04). So DraStic runs, concurrently: 1 emu thread (CPU +
geometry + engine A), 1 engine-B 2D thread, and up to 4 3D-raster threads.

---

## 3. MMIO register decode [P]

`FUN_0012344c` is the write router for all I/O registers. For 2D-GPU offsets it either
dispatches through the jump table `DAT_0020a6e8[reg]` (per-register handler) or, when a
mid-frame write occurs (`VCOUNT < 0xc0`), tags it into the scanline replay queue via
`FUN_00150014`. Every write also updates an I/O shadow-RAM image at `emu_base + 0x1b070`.

`FUN_0014f440` decodes the register *effect* (switch on `reg & 0xfff`), matching the DS
2D map exactly (two engine contexts: A at `0x4000xxx`, B at `0x4001xxx`):

```
0x00       DISPCNT            0x40/42  WIN0/1H
0x08–0x0e  BG0..3CNT          0x44/46  WIN0/1V
0x10–0x1e  BGx H/V scroll     0x48/4a  WININ / WINOUT   (masked & 0x3f3f3f3f)
0x20–0x26  BG2 PA/PB/PC/PD    0x4c     MOSAIC
0x28/0x2c  BG2 X/Y ref        0x50     BLDCNT
0x30–0x3f  BG3 affine+ref     0x52     BLDALPHA (EVA/EVB)
                              0x54     BLDY     (& 0x1f)
                              0x6c     MASTER_BRIGHT
else-branch: palette-RAM / OAM byte|half|word writes (ctx[3]=PAL base, ctx[6]=OAM base)
```

---

## 4. Layer composition pipeline [P unless noted]

### Scanline compositor (`FUN_0014c610`, mode mux)
Per line: advance affine BG references (`FUN_00131a08`), then switch on
`DISPCNT.display_mode` (bits 16–17):
- **0** display off → white line.
- **1** BG+OBJ graphics → `FUN_001494a4`.
- **2** VRAM bitmap → `FUN_0018bc98` (`ctx[2] + (line<<8)*2`, 256-px BGR555 line).
- **3** main-memory bitmap.

Then apply **master brightness** (jump table on the `MASTER_BRIGHT` reg) and pack to the
internal framebuffer format. `FUN_0014c610` carries **35 NEON** ops whose constants
(`movi v.8h,#0x1f`, `#0x3f`, `#0xf8,lsl #8`) are the R/G/B masks of a vectorized BGR555
pack, plus `dup`+`st1` splats to clear/fill line buffers. **[P]**

### BG + OBJ + effects (`FUN_001494a4`, mode 1)
The workhorse. Reads BG-enable bits (`DISPCNT>>8 & 0xf`), renders text BGs 0–3 via the
per-BG callback (`FUN_00141a40`, callback ptr at `bgctx+0xf0`, MOSAIC applied), renders
the affine/extended/bitmap/3D layer via `FUN_00141cdc`, builds WIN0/WIN1/OBJ-window masks
(`FUN_00147b74`), then composites by DS priority (0–3) applying color-special-effects
(alpha blend from BLDCNT/BLDALPHA, brightness inc/dec from BLDY).

**NEON:** `FUN_001494a4` contains **927 vector instructions**. The dominant kernel is a
4-pixel-wide color-format converter/compositor:
```asm
ld4  {v0.4s,v1.4s,v2.4s,v3.4s},[x10],#64   ; deinterleave 4 px × 4 lanes
ushr v4.4s,v0.4s,#0xf   ; ushr v5.4s,v1.4s,#0xd ; ushr v6.4s,v2.4s,#0xb ; ushr v0.4s,v3.4s,#0x9
and  v1.16b,v4.16b,v7.16b (movi #1) ; and ...v16(#4)... ; and ...v17(#0x10)...
orr  v1.16b,v2.16b,v1.16b ; orr v1,v1,v3 ; orr v0,v1,v0
xtn  v0.4h,v0.4s ; xtn v0.8b,v0.8h          ; narrow 32→16→8, pack
```
i.e. the DS ↔ internal color conversion and blend are **vectorized 4 pixels at a time**. **[P]**

### Affine / extended / bitmap BG (`FUN_00141cdc`)
Handles rotate/scale text BGs, 256×N extended-palette bitmaps, direct-color bitmaps, and
the merge of the 3D-layer scanline (DISPCNT bit `0xc` gates BG0 as the 3D layer). **516
NEON** integer ops (`add/orr/and/shl` + `mul`) — bit-exact fixed-point BG sampling and
attribute packing, no float. **[P]**

### Sprite / OBJ engine (`FUN_0014b2b0`) [P]
Iterates all **128 OAM** entries (loop bound `0x80`, 8 bytes each). `attr0=*p`, `attr1=p[1]`,
`attr2=p[2]`. Sprite dimensions from `DAT_0020e528`(w)/`DAT_0020e529`(h) indexed by
`(shape<<2)|size`. OBJ mode (attr0 bits 10–11): 0 normal, 1 semi-transparent/alpha
(sets a `0x80` flag), 2 OBJ-window, 3 bitmap-OBJ (computes direct VRAM address). Rotoscale
(attr0 bit 8) reads the 32-byte affine group (PA/PB/PC/PD at OAM `+0x06/0x0e/0x16/0x1e`)
and calls `FUN_0014e078` twice for X/Y fixed-point increments. Sprites are **binned into
per-scanline buckets** (`ctx+0x20c00` = per-line counts, `ctx+0x2f80` = per-line×priority
index lists, 192 lines × 4 priorities). **Essentially scalar** (1 NEON op) — a candidate
optimization target.

### Priority sort (`FUN_0014e1c4`) [P]
On DISPCNT/BGxCNT change, rebuilds 4 priority buckets (0–3) over the ≤4 BGs, keyed on
`BGxCNT & 3` with BG index as tiebreak; DISPCNT bit `0xc` marks BG0 as the 3D layer.
Scalar.

---

## 5. Framebuffer, upscaling & output [P]

- **FB layout:** `FUN_0011cd18(screen)` = `(&DAT_0402d1f8)[dblbuf] + (screen&1)*0xc0000`.
  Two screens, `0xc0000` (786432) bytes each — sized for internal upscaling
  (512×384×4 = `0xc0000`, i.e. up to 2× native by default allocation). `0x180000` clears
  both. Double-buffered via `DAT_0402db50`.
- **Stride:** `FUN_0011cd38` returns `0x200/0x400/0x800/0x1000` depending on scale.
- **Downscale/convert:** `FUN_0017fce4` converts the (possibly upscaled) 32-bit internal FB
  down to a native 256×192 **RGB565** buffer (`0x18000` bytes), applying the integer
  downsample factor — scalar. Used for the Java `getScreenBuffers` path and screenshots.
- **GL upload/blit:** `FUN_0011ceac` `glTexSubImage2D`s `(scale+1)*256 × (scale+1)*192` then
  `glDrawArrays(GL_TRIANGLES,0/6,6)` per screen (doc 05). No 2D compositing on the GPU.

### Display capture (DISPCAPCNT) `FUN_0013c938` [P]
Decodes capture source A/B, EVA/EVB blend factors (`& 0x1f`, clamped to `0x10`=16), capture
size (tables `DAT_0020ddd0`/`0020dde0`), and allocates a `0x60000`-byte capture buffer via
`memalign(0x10, …)`. Runs at end-of-frame (scheduler line ~262).

---

## 6. Data structures [P]

- **Engine context:** engine A rooted at `+0x5cf` (long index), engine B at `+0x10853`.
  Fields: `ctx[2]`=VRAM/main-mem display source, `ctx[3]`=palette base, `ctx[4]`=char/screen
  base table, `ctx[6]`=OAM base, `ctx[7]/[8]`=output FB ptr/stride.
- **Per-BG context:** stride `0xb0`; render callback at `+0xf0`; BGxCNT at
  `+0x158/0x208/0x2b8/0x368`.
- **Register-write replay queue:** entries `0xc` bytes at `ctx+0x21418`, scanline field at
  `+0x14`; cursor pair `ctx+0x81418`/`+0x8141c`.
- **Line buffers:** on the emu/render-thread stack in `FUN_0014c610`/`FUN_001494a4`
  (768 B = 256 px × 3 layer/priority buffers; plus ~7 KB scratch in `FUN_001494a4`).
- **I/O shadow RAM:** `emu_base + 0x1b070` (all 2D registers mirrored here on write).

---

## 7. DraStic-specific speed tricks

1. **Two-engine thread parallelism** — engine A on the emu thread, engine B on a dedicated
   render thread, joined once per frame. ~2× on the common full-frame path. **[P]**
2. **Deferred/batched scanline rendering with exact catch-up** — the frame is rendered in a
   single burst at line 191, but a scanline-tagged register-write replay queue
   (`FUN_00150014` + `FUN_0014f440`) keeps mid-frame raster effects scanline-exact without
   re-rendering the whole frame per write. **[P]**
3. **NEON pixel kernels** — color-format conversion / BGR555 pack / blend vectorized 4–8 px
   per iteration (`ld4`/`ushr`/`and`/`orr`/`xtn` in `FUN_001494a4`; masked pack in
   `FUN_0014c610`; fixed-point BG sampling in `FUN_00141cdc`). **[P]**
4. **Internal-resolution upscaling in software** — FB slots pre-sized to `0xc0000`; GL just
   uploads the larger texture. **[P]**
5. **Sprite line-binning** — 128 OAM pre-binned per scanline×priority so the composite loop
   only touches sprites present on the line. **[P]**
6. **Dirty-VRAM-bank flush** — only changed 16 KB VRAM blocks are re-synced before render
   (`FUN_0013cd78` bitmask). **[P]**

---

## 8. Actionable for melonDS

- melonDS's software 2D renderer (`GPU2D_Soft.cpp`) is single-threaded per engine.
  DraStic's **engine-A-on-emu-thread / engine-B-on-render-thread** split is a cheap,
  low-risk parallelization worth mirroring (the two DS engines are independent except for
  shared VRAM reads and display capture).
- DraStic's **NEON 4-px color-convert/blend kernels** are the hottest 2D path; melonDS's
  scalar per-pixel BGR555 → RGBA and alpha-blend loops are prime NEON targets. The
  `ld4`/`ushr`/`and`/`orr`/`xtn` pattern in `FUN_001494a4` is a directly portable template.
- DraStic keeps 2D **sprite iteration scalar** — so melonDS's own scalar OBJ path is not the
  bottleneck DraStic optimized; focus NEON effort on the BG/blend composite, not OBJ.
- The **deferred-render + scanline-replay queue** is a good model for melonDS to avoid
  per-write full-frame invalidation while keeping mid-frame effects correct.
- Internal-res upscaling lives entirely in the software renderer + a larger GL upload; no
  GL 2D pipeline needed.

---

## 9. Open items [U]

- Exact NEON inner loops of `FUN_00141cdc` (affine/ext-BG) and the alpha-blend path of
  `FUN_001494a4` — structurally identified, not instruction-by-instruction transcribed.
- The writer that populates the per-BG render callback (`bgctx+0xf0`) — set in a BGCNT-decode
  helper not fully isolated.
- Precise palette-lookup inner loop for extended-palette bitmap modes.
