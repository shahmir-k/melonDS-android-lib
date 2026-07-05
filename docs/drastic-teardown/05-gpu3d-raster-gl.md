# 05 — DS 3D Software Rasterizer + GL Screen/Post-Process Path (DraStic arm64 teardown)

Scope: how DraStic renders the Nintendo DS 3D engine in **software**, how the
resulting framebuffers reach OpenGL ES, and the `.dfx`/`.dsd` shader-chain
post-process runtime. Evidence is from the decompiled `libdrastic_arm64.so`
(stripped except JNI exports). Every non-trivial claim is labelled
`[proven-from-binary]`, `[inferred]`, or `[unknown-too-stripped]`.

Companion docs: 01 (JNI/boot), 02 (CPU recompiler), 03 (memory), 04 (audio).

---

## 0. TL;DR verdicts (for the melonDS fork)

- **DS 3D is 100% software-rasterized.** There is **no GL 3D** — no
  `glDrawElements`, no VBO/`glBufferData`, no `glVertexAttribPointer` with
  geometry. GL is used *only* to blit two software-produced framebuffer
  textures and run a post-process shader chain over them. `[proven-from-binary]`
- **The rasterizer is multithreaded**, but **not** via the generic 7/32-thread
  worker pool (`FUN_001c196c`). It has its **own set of ~3–4 dedicated
  render-band threads** (`FUN_0015f614`), splitting the 192 scanlines into
  twelve 16-line blocks assigned round-robin. `[proven-from-binary]`
- **NEON is used heavily** in the shading/blend/AA/fog kernels and the
  perspective-gradient setup (branchless `cmeq/cmhi/bsl` compare-select,
  `umull` texel/color multiply, `frecpe/frecps` for 1/w). The coverage
  edge-walk + depth-compare inner loop is **scalar**. `[proven-from-binary]`
- **GL upload is deferred + dirty-masked** `glTexSubImage2D` into a persistent
  texture (never re-`glTexImage2D` per frame), from a **double-buffered**
  software framebuffer, behind a **single mutex+condvar** producer/consumer
  handshake. `[proven-from-binary]`

---

## PART A — SOFTWARE RASTERIZER

### A.1 Threading model — dedicated render-band threads

Spawner `FUN_0015f614` (called once at init, decomp line ~31326)
`[proven-from-binary]`:

- Creates **3 render-worker threads** via raw `pthread_create`, each running
  `FUN_0015f53c`, with per-thread context blocks at base `+0x2c1740` and
  strided copies `+0x2c1840 / +0x2e5940 / +0x309a40` (stride ≈ `0x24100`).
  Each thread has its own mutex/cond and a hard-coded **band index** written
  into the context (`…2e5912=1`, `…309a12=2`, `…32db12=3`; index 0 = base) —
  giving up to a **4-way split** (3 spawned + the calling thread). `[proven-from-binary]`
- Creates **1 coordinator** thread `FUN_0015f2c8` over `&DAT_0402e110`.
- Worker body `FUN_0015f53c`: condvar-wait on go-flag `ctx+0x240d0`; on wake
  dispatch to `FUN_001596a4(ctx)` (normal path) or `FUN_0015e648(ctx)`
  (alternate path, gated on `*(gpu+0x4a0)`); then set done-flag `ctx+0x240d1`
  and signal cond `ctx+0x240a0`. Classic **barrier handshake**. `[proven-from-binary]`

**Contrast: the generic worker pool `FUN_001c196c` (count = 7) is NOT used for
raster.** Its job-enqueue `FUN_001c1e6c(pool, fn, arg)` has exactly two call
sites, neither 3D `[proven-from-binary]`:
1. `FUN_001b6aec`→`FUN_001b6b48` — a canonical **Huffman/bitstream decoder**
   (3-byte read, `>> (8-bitoff)`, threshold/code-length/symbol tables) used by
   savestate/asset paths (dispatcher `FUN_001c1084`). `[proven-from-binary; "decompression" inferred]`
2. `FUN_001c860c`/`FUN_001c8534` — a **multi-buffer hash** over 0x40-byte
   blocks across 8 lanes. `[proven-from-binary]`

> **Actionable for melonDS:** DraStic's 3D speed comes from a *purpose-built*
> band-parallel software raster with a static, cache-friendly 16-line-block
> interleave and its own barrier — not a general task pool. melonDS's software
> renderer is already scanline-threaded; the takeaway is the *static
> block-interleave* (12 blocks of 16 lines, round-robin across N threads) and a
> **deferred coverage → NEON-shade two-phase** structure (§A.3).

#### Per-band work split (`FUN_001596a4`, decomp ~line 69152) `[proven-from-binary]`

```
nbands            = ctx[0x240d3];        // active render bands (<= 13)
blocks_per_thread = 0xc / nbands;        // 12 blocks * 16 lines = 192 scanlines
for (i = 0; i < blocks_per_thread; i++) {
    blk = ctx[0x240d2] + i*nbands;       // this band's start block, stride nbands
    y0  = blk * 0x10;                    // 16-scanline block
    // 1. OPAQUE pass:      per-scanline polygon buckets @ gpu+0x2856c0 (list off 0x39ae0)
    // 2. DEPTH clear:      FUN_0018d178(ctx+0x20000.., 0xff, 0x100)  // 16 lines -> far
    // 3. TRANSLUCENT pass: buckets @ gpu+0x2916f0 (list off 0x59af0)
    // 4. POST/SHADE pass:  AA / edge-mark / fog composite (config-selected variant)
}
```

Interleaved (not contiguous) block assignment balances load when polygon
density varies vertically. `[proven-from-binary]`

### A.2 Coverage / edge-walk inner loop (scalar)

- **Polygon span rasterizer** `FUN_00155220` (1580 B, **0 NEON, scalar**)
  `FUN_00155220(ctx, poly, vtx_base, y0, y1)`. Walks the two polygon edges by
  calling the edge-walker twice — direction `+1` then `-1`
  (`0xffffffff`) — to produce left/right span extents, then fills the span,
  testing a packed depth/attribute word (`& 0x7fff` depth, `& 0x8100` flag)
  per pixel. `[proven-from-binary]`
- **Edge walker / attribute DDA** `FUN_00153f68` (1060 B, **scalar**).
  Interpolates z/w, u/v texcoords, and vertex RGB in fixed point along each
  edge (`attr*0x40000`, `attr<<0x27`), storing per-scanline start values and
  step lengths. `[proven-from-binary]`
- **Perspective gradient setup (NEON)** `FUN_001908a4` / `FUN_00190c28`
  (chosen by the hi-res flag `*(gpu+0x8aaf8)`): computes per-polygon
  attribute gradients with `NEON_umull`/`NEON_ushl` and fixed-point
  `(x + 0x7fff) >> 0xf` (÷32768) rounding over vertex arrays
  `+0x17f0`(xy), `+0x3070`(z), `+0x48f0`, `+0x6170`(color). `[proven-from-binary]`

Representative NEON (from `FUN_001908a4`) `[proven-from-binary]`:
```c
v = NEON_umull(CONCAT44(g1+dy, g0+dx), w, 4);   // 4-lane: attr * w
v = NEON_ushl(v, shift, 8);                       // perspective normalize
v = v - ((v + 0x7fff) >> 0xf);                    // /32768 rounding
```

### A.3 Two-phase deferred structure `[inferred from call graph]`

The scalar `FUN_00155220`/`FUN_00153f68` pass appears to build **per-pixel
index + depth buffers** per band; a separate **NEON shading/composite pass**
(the `FUN_0018e358` kernel family, §A.5) then resolves colour, blend, AA, and
fog. This "coverage first, shade later" split is consistent with the call
graph (scalar builders feed NEON resolvers) but is not proven to the
instruction level. `[inferred]`

### A.4 Texturing — decode-to-cache, hash-chained

- **Texture cache** `FUN_0017132c` `[proven-from-binary]`:
  512-bucket hash, `key = (texparam >> 7) & 0x1ff`, table at `vram_ctx+8`;
  entries `malloc(0x50)`, collision chain at entry `+0x8`; MRU/LRU list head
  `vram_ctx+0x8008`, entry link `+0xc`; identity key `(texparam & 0x3ff0ffff,
  palette_base)`; dirty flag entry `+0x4a` forces re-decode. Bound once per
  polygon during binning and cached at `poly+0x10`.
- **Format decoder / sampler** `FUN_00170f5c` `[proven-from-binary]`:
  `fmt = (texparam >> 0x1a) & 7`; S/T sizes `8 << ((p>>0x14)&7)` /
  `8 << ((p>>0x17)&7)`.
  - `fmt==5` → **4×4-block compressed**: two VRAM slots (`+0x2180` texels,
    `+0x2188` palette-index block), decompressed by `FUN_001708f0`.
  - `fmt==7` → **direct 16-bit colour** (no palette).
  - `fmt∈{1,2,3,4,6}` → **palette formats** A3I5 / 2bpp / 4bpp / 8bpp / A5I3,
    using tables `DAT_0020ec06[fmt]` (palette-entry count),
    `DAT_0020ebfc[fmt]` (bytes/texel), `DAT_0020e9f0[fmt]` (shift).
  - **Whole texture is expanded once into a linear malloc'd buffer** and
    reused while `fmt` is unchanged (`if (fmt == entry[0x4b]) skip realloc`).
    Because of this, the per-pixel sampler is **format-agnostic** — it reads
    the pre-decoded linear buffer, so there is no per-texel format switch in
    the hot loop. `[proven-from-binary]`

> **Actionable for melonDS:** the big win is **decode-once-per-cache-entry**:
> DS texture-format unpacking (palette lookups, A3I5/A5I3 alpha expansion, 4×4
> decompression) happens on a cache miss, not per sampled texel. melonDS's
> soft renderer samples DS-format texels directly each pixel; a decoded-texel
> cache keyed on `(texparam & 0x3ff0ffff, palbase)` with a dirty flag would
> mirror this.

### A.5 Per-pixel features (shading kernels)

The NEON shade/composite pass is a family of six 868-byte twins —
`FUN_0018e358`, `FUN_0018e6c0`, `FUN_0018ea28`, `FUN_0018f948`,
`FUN_0018fcb0`, `FUN_00190018` — plus a SIMD-dense sibling `FUN_0018e2f0`
(so dense Ghidra failed to decompile it), one variant per pixel-format/mode,
~80 NEON intrinsics each. Their driver `FUN_00157364` runs a **3-scanline
sliding window** (buffers 0x400 = 256px×4B apart) with top/bottom edge
special-casing = edge-marking / anti-aliasing composite. `[proven-from-binary]`

| Feature | Status | Evidence |
|---|---|---|
| **Depth test** (z/w buffer) | present | per-band depth buffer `ctx+0x20000..0x20f00` cleared to `0xff` (far) each 16-line block via `FUN_0018d178`; per-pixel compare in `FUN_00155220`. Compare direction not fully decoded. `[proven-present; direction inferred]` |
| **Z-buffer vs W-buffer** | present, per-frame select | alt render path `FUN_0015e648` vs `FUN_001596a4` gated on `*(gpu+0x4a0)` — likely the W-buffer/rear-plane variant. `[inferred]` |
| **Translucent polygon sort** | present | separate opaque list (off `0x39ae0`) and translucent list (off `0x59af0`), two passes/block; sort setup `FUN_00190630` (56 NEON). `[proven-from-binary]` |
| **Alpha blending** | present | `bsl/bit` select + `umull` multiply in the shade kernels = per-pixel blend/combine. Exact blend formula not decoded. `[proven-present; formula inferred]` |
| **Alpha test** | likely present | `cmhi/cmeq` masks in shade kernels consistent with a threshold test; threshold register not pinned. `[inferred / partly unknown]` |
| **Edge marking + AA** | present | 3-line-window post pass `FUN_00157364` (+ variants `FUN_0015870c/00159110/0015829c/00158ca0`) selected by config bits `DAT_0034eb40`; disabled path `FUN_0018e070` = plain copy. `[proven-present; which-bit inferred]` |
| **Fog** | present | one of the post-pass config variants (same switch); fog-table lookup not isolated. `[inferred]` |

Whole-binary NEON intrinsic histogram (top): `cmeq×315, cmhi×294, ushl×150,
bsl×146, ext×134, umull×67, frecps×12, frecpe×6, urecpe×4`. The
`cmeq/cmhi/bsl/bit` cluster = **branchless per-pixel compare-and-select**
(masked depth/alpha/blend); `frecpe/frecps/urecpe` = **reciprocal for
perspective 1/w**. `[proven-from-binary]`

### A.6 Geometry front-end (emu thread) & command log

Vertex transform / clipping run on the emulator thread, upstream of the raster
bands: `FUN_0016a070` (transform, 26 NEON), `FUN_0016dee8` (clip),
`FUN_00166eb0` (command processing), `FUN_00159bb4`/`FUN_001559bc`
(per-region polygon→scanline bucketing, Y clamp `0xc0`=192). `[proven-from-binary]`
The `geometry_log_commands.bin` / `_parameters.bin` / `_vram.bin` /
`_video_io.bin` capture files are written by `FUN_00169074` — a debug GPU
command logger. `[proven-from-binary]`

---

## PART B — GL SCREEN + POST-PROCESS PATH

### B.1 The software framebuffer (double-buffered, host-format)

Init `FUN_0011cbec` `[proven-from-binary]`:

```
posix_memalign(&fb_base, 0x10, 0x300000);   // 3 MB, 16-byte aligned
buffer[0] = fb_base;                          // DAT_0402d1f8
buffer[1] = fb_base + 0x180000;               // DAT_0402d200  (double buffer)
memset(fb_base, 0, 0x300000);
pthread_mutex_init(&db84); pthread_cond_init(&dbac);
```

Layout: each of the 2 buffers = **2 screens × 0xc0000 bytes**. `0xc0000` =
512×384×4 = **2× internal-resolution RGBA8888**. The buffer is sized for max
internal scale up front. `[proven-from-binary]`

Pixel format is switchable by `FUN_0011cbc0(bpp)` `[proven-from-binary]`:
- `bpp == 16`: type `0x8363` = `GL_UNSIGNED_SHORT_5_6_5`, format `0x1907` = `GL_RGB`.
- else (32): type `0x1401` = `GL_UNSIGNED_BYTE`, format `0x1908` = `GL_RGBA` (default).

Per-screen **internal-resolution scale** lives in `DAT_0402db60[screen]`;
upload dims are `(scale+1)*256 × (scale+1)*192`. So the DS 2D+3D compositor
writes **host-native RGB565/RGBA8888** into these buffers (colour conversion —
including the NEON `ushll v.4s,v.4h,#3/#5/#8` RGB555→RGBA8888 expansion, 4
px/iter — happens on the CPU), and GL never sees native DS 15-bit pixels.
`[proven-from-binary]`

Accessors:
- `FUN_0011cd18(screen)` → **write pointer** for the *current* buffer:
  `buffer[db50] + (screen&1)*0xc0000`. 10 callers = the 2D compositor + 3D
  blit. `[proven-from-binary]`
- GL side always reads the **other** buffer: `buffer[~db50 & 1]`.

### B.2 Producer/consumer present handshake

One mutex `DAT_0402db84` + one condvar `DAT_0402dbac` `[proven-from-binary]`:

| Function | Role |
|---|---|
| `FUN_0011cb14` | **buffer swap**: `db50 = ~db50 & 1`; `cond_signal`. Emu → "new frame ready". |
| `FUN_0011cb5c(screen)` | **mark dirty**: `db80 \|= 1<<screen`; `cond_signal`. |
| `JNI waitScreen` (0011a64c) | GL/UI thread: `cond_wait(dbac, db84)` — blocks until a frame is signalled. |
| `JNI signalScreen` (0011a648) | `cond_signal` only (wake without state change — pause/shutdown). |

Emu thread renders into `buffer[db50]`, marks the changed screens dirty, swaps,
and signals. The GL thread wakes in `waitScreen`, then uploads only the dirty
screens and presents. `[proven-from-binary]`

The top-level frame compositor (~`FUN_0013c8xx`, decomp ~30880) selects which
physical buffer maps to top/bottom (DS screen-swap), calls `FUN_0011cde4` (set
scale), `FUN_0011cd18` (get write ptrs), runs the 2D engines
(`FUN_0014b2b0`/`FUN_0014ef0c`), then marks both dirty (`FUN_0011cb5c`) and
swaps (`FUN_0011cb14`). `[proven-from-binary]`

### B.3 Upload strategy — deferred, dirty-masked `glTexSubImage2D`

The texture is allocated **once**; frames are uploaded with `glTexSubImage2D`
into it (never a fresh `glTexImage2D` per frame), and only when the dirty bit
is set. `[proven-from-binary]`

Three JNI present paths (all thin wrappers) map to three natives:

| JNI | native | draws |
|---|---|---|
| `renderFrame` (00119030) | `FUN_0011ceac` | both screens (quad @ vtx 0 and @ 6) — raw blit |
| `renderFrameTex` (00119044) | `FUN_0011d008` | one quad @ vtx 0x12 |
| `renderFrameTexExt` (00119050) | `FUN_0011d0d0` | one quad @ vtx 6 |

Core of `FUN_0011ceac` (raw blit path — no shader chain) `[proven-from-binary]`:
```c
if (fb_base != 0) {
  lock(db84);
  screen  = screen & 1;
  scale0  = db60[screen];  scale1 = db60[screen^1];
  src     = buffer[~db50 & 1];               // completed front buffer
  unlock(db84);
  glBindTexture(GL_TEXTURE_2D, tex0);
  if (db80 & 1)                               // top dirty?
    glTexSubImage2D(GL_TEXTURE_2D,0, 0,0, (scale0+1)*256,(scale0+1)*192,
                    fmt, type, src + screen*0xc0000);
  glDrawArrays(GL_TRIANGLES, 0, 6);           // top quad (2 tris)
  if (tex1) {
    glBindTexture(GL_TEXTURE_2D, tex1);
    if (db80 & 2)                             // bottom dirty?
      glTexSubImage2D(..., (scale1+1)*256,(scale1+1)*192, ..., src + (screen^1)*0xc0000);
    glDrawArrays(GL_TRIANGLES, 6, 6);         // bottom quad
  }
  db80 = 0;                                   // clear dirty mask
}
```

The `glDrawArrays` offsets (0, 6, 0x12, …) index a **prebuilt client-array
vertex buffer** holding several full-screen quad layouts (top, bottom,
FBO-flipped, ext) — set once at init; DraStic uses client arrays, no VBO.
`[proven-from-binary]`

### B.4 The `.dfx` / `.dsd` shader-chain format

Two file types under `reference/universal/assets/shaders/` (~250 shaders: xBR,
FXAA, nds_color, CRT, LCD3x, SABR, scanlines…) `[proven-from-binary from assets]`:

- **`.dsd`** = a raw GLSL shader: `<vertex>…</vertex>` + `<fragment>…</fragment>`
  blocks. Built-in uniforms/attributes: `a_vertex_coordinate`,
  `a_texture_coordinate`, `u_texture_size` (vec4 = `1/w,1/h,w,h`),
  `u_target_size` (vec2), `u_time` (float), `u_texture` (sampler).
- **`.dfx`** = a multi-pass *chain config* referencing `.dsd` shaders. Section
  tags (parser tokens at strings `0x10a2xx`): `<options>` (`name=`,
  `textures=N`), `<header>/<vheader>/<fheader>` (GLSL prologues),
  `<include>`, `<texture:N>`, `<pass>`.

`<texture:N>` fields `[proven-from-binary]`: `input=framebuffer` (the DS
screen texture) or `input=null` (an FBO-backed intermediate), plus
`width=`, `height=`, `internalformat=`, `format=`, `type=`, `min_filter=`,
`mag_filter=` (all mapped from GL enum name strings by `FUN_0011fb68`).

`<pass>` fields `[proven-from-binary]`: `shader=<file.dsd>`,
`sampler:NAME=<slot>` (bind a sampler uniform to texture slot N),
`output=<slot>:<scale>` (render this pass into texture slot `slot` at integer
`scale`; parsed by `sscanf("=%d:%d")`). Default scale = 1; final pass with no
`output` renders to the screen.

Worked example — `5XBR_v3.7A + LCD3x_2x.dfx` (2 passes) `[proven-from-binary]`:
```
textures=2
<texture:0> input=framebuffer          # DS screen
<texture:1> input=null                  # intermediate FBO target
<pass> shader=5XBR....dsd  sampler:u_texture=0  output=1:3   # pass0: screen -> tex1 @3x
<pass> shader=LCD3x.dsd    sampler:u_texture=1              # pass1: tex1  -> screen
```

### B.5 Chain runtime — parse / setup / render

| Stage | native | JNI |
|---|---|---|
| **fxLoad** (parse file → chain descriptor) | `FUN_0011ff78` | `fxLoad` (00118ea8) |
| **fxSetup** ((re)allocate pass FBO targets) | `FUN_00120418` | `fxSetup` (00118f24) |
| **fxRender** (upload + execute passes) | `FUN_00120690` (via `FUN_0011d1d8`) | `fxRender` (00118f40) |

There are **two independent chain descriptors**: `DAT_0402d208` (primary, `fx*`)
and `DAT_0402d6a8` (secondary, `extfx*` — a second display/target). Both are
destroyed by `FUN_001202b8` in teardown `FUN_0011cc84`. `[proven-from-binary]`

**Parser `FUN_0011ff78`** dispatches by tag (`strstr`) to per-section handlers:
`<options>`→`FUN_0011edd8`, `<header>/<vheader>/<fheader>`→`FUN_0011ef70`,
`<include>`→`FUN_0011f0a0`, `<texture>`→`FUN_0011f260`, `<pass>`→`FUN_0011f734`.
It also compiles a built-in **identity/pass-through** program into `DAT_0402dbe0`
(the `gl_FragColor = vec4(color.rgb, 1.0)` shader seen in strings) used when no
effect is selected. `[proven-from-binary]`

**Shader compile `FUN_0011e688`** `[proven-from-binary]`:
`glCreateShader(GL_VERTEX_SHADER=0x8b31)` + `glCreateShader(GL_FRAGMENT_SHADER=0x8b30)`,
compile, `glCreateProgram`/`glLinkProgram`/`glDetach`/`glDeleteShader`, then
cache locations into the pass struct:
`[0]=program, [1]=a_vertex_coordinate, [2]=a_texture_coordinate,
[3]=u_texture_size, [4]=u_target_size, [5]=u_time`.

**Pass struct** (built by `FUN_0011f734`, 0x178 bytes) key offsets
`[proven-from-binary]`: `+0x56`=FBO, `+0x57/0x58`=pass output w/h,
`+0x59`=output texture-slot index, `+0x5a`=scale, `+0x5b`=sampler count,
`+0x5c`=next-pass pointer (singly-linked list), `+0x164`=output slot,
`+0x2d`=output scale.

**fxSetup `FUN_00120418`** (re)creates each pass's render-target texture +
FBO when the target size changes `[proven-from-binary]`:
```
for each pass:
  outW = pass.scale * targetW;  outH = pass.scale * targetH;
  if (size changed or no texture):
     glDeleteFramebuffers(old); (maybe glDeleteTextures)
     glGenTextures; glBindTexture; glTexImage2D(0, ifmt, outW,outH, 0, fmt,type, NULL);
     glTexParameteri(min/mag from block, WRAP_S/T = GL_CLAMP_TO_EDGE=0x812f);
     glGenFramebuffers; glBindFramebuffer(GL_FRAMEBUFFER);
     glFramebufferTexture2D(COLOR_ATTACHMENT0, texture); glCheckFramebufferStatus;
  glUseProgram(pass); glUniform4f(u_texture_size = 1/W,1/H, W,H);
```

**fxRender `FUN_00120690`** `[proven-from-binary]`:
- If `chain.effect == 0` (no post): bind default program `DAT_0402dbe0`, FBO 0,
  set the two client vertex arrays (`DAT_0402dbf8`=positions,
  `DAT_0402dc00`=texcoords), bind the DS screen texture, `glDrawArrays(GL_TRIANGLES,
  offset, 6)` — one quad, done (the "None"/point/linear fast path).
- Else walk the pass linked list:
  ```
  time_ms = (now - first_seen) / 1e6;         // for u_time
  for (pass = head; pass; pass = pass.next) {
    glUseProgram(pass.program);
    glBindFramebuffer(GL_FRAMEBUFFER, pass.fbo);   // intermediate FBO, or 0 for final
    if (final)  glViewport(dst_x, dst_y, dst_w, dst_h);   // on-screen target rect
    else        glClear; glViewport(0,0, pass.w, pass.h);
    bind a_vertex_coordinate / a_texture_coordinate client arrays;
    if (pass.u_target_size >= 0) glUniform2f(targetW, targetH);
    if (pass.u_time        >= 0) glUniform1f(time_ms);
    for (s = 0; s < pass.sampler_count; s++) {
       glActiveTexture(unit_s); glBindTexture(texture_slot[s]);
    }
    glDrawArrays(GL_TRIANGLES, (final ? screen_offset : fbo_offset), 6);
  }
  ```
  The vertex offset differs for FBO vs screen passes to flip Y (FBO is
  bottom-up). `[proven-from-binary]`

**fxRender is per-screen**: `FUN_0011d1d8` uploads screen A
(`glActiveTexture(GL_TEXTURE0)` + dirty `glTexSubImage2D`) and runs the whole
chain, then does the same for screen B with a different destination rect — each
DS screen passes through the shader chain **independently**. `[proven-from-binary]`

---

## C. Consolidated function map

| Addr | Inferred name | Confidence |
|---|---|---|
| `FUN_0015f614` | spawn 3D render-band threads + coordinator | proven |
| `FUN_0015f53c` / `FUN_0015f2c8` | render-band worker body / coordinator | proven |
| `FUN_001596a4` | per-band renderer (16-line-block loop, opaque+translucent) | proven |
| `FUN_0015e648` | alternate render path (W-buffer variant?) | inferred |
| `FUN_00155220` | polygon span rasterizer (scalar) | proven |
| `FUN_00153f68` | edge-walk / z,u,v,rgb DDA interpolation (scalar) | proven |
| `FUN_001908a4` / `FUN_00190c28` | perspective gradient setup (NEON) | proven |
| `FUN_0018e358` (+5 twins), `FUN_0018e2f0` | per-scanline shade/blend/AA kernels (NEON) | proven |
| `FUN_00157364` (+variants) | AA/edge/fog post-pass driver (3-line window) | proven |
| `FUN_0018d178` | per-band depth-buffer clear (→0xff far) | proven |
| `FUN_0017132c` | hash-chained texture cache | proven |
| `FUN_00170f5c` / `FUN_001708f0` | texture format decoder / 4×4 decompressor | proven |
| `FUN_0016a070` / `FUN_0016dee8` / `FUN_00166eb0` | geometry transform / clip / command proc | proven |
| `FUN_00169074` | geometry command logger (`geometry_log_*.bin`) | proven |
| `FUN_0011cbec` | software framebuffer init (3 MB double buffer) | proven |
| `FUN_0011cbc0` | framebuffer GL format select (RGB565/RGBA8888) | proven |
| `FUN_0011cd18` | current-buffer screen write pointer | proven |
| `FUN_0011cb14` / `FUN_0011cb5c` | buffer swap / mark-screen-dirty (+signal) | proven |
| `FUN_0011ceac` / `FUN_0011d008` / `FUN_0011d0d0` | renderFrame / …Tex / …TexExt (raw blit) | proven |
| `FUN_0011ff78` | fxLoad — `.dfx`/`.dsd` chain parser | proven |
| `FUN_0011f260` / `FUN_0011f734` | `<texture>` / `<pass>` block parsers | proven |
| `FUN_0011e688` | GLSL vertex+fragment compile/link | proven |
| `FUN_00120418` | fxSetup — pass FBO/texture (re)allocation | proven |
| `FUN_00120690` / `FUN_0011d1d8` | fxRender — multi-pass FBO chain executor | proven |
| `DAT_0402d208` / `DAT_0402d6a8` | primary / secondary (extfx) chain descriptors | proven |

---

## D. Honesty / stripped-out gaps

- Raster/GL functions live in `0x11b000–0x1a0000`, which is **outside** the
  provided `drastic_disasm.txt` (stops ~`0x107000`), so raw `.4s`/`ld1` SIMD
  opcodes could not be quoted for the render kernels; NEON is proven via
  Ghidra's `NEON_*` intrinsics in the decomp instead (plus the `ushll`
  RGB-expansion opcodes that *are* in range). `[stated limitation]`
- Not fully decoded: exact depth-compare direction, alpha-test threshold
  register, the specific config-bit→(AA vs edge vs fog) mapping, and the exact
  alpha-blend formula. All flagged `[inferred]`/`[unknown]` above.
- The "deferred coverage → NEON shade" two-phase model is inferred from the
  call graph, not proven instruction-by-instruction. `[inferred]`

---

## E. What to steal for the melonDS fork (summary)

1. **Decode-once texture cache** keyed on `(texparam & 0x3ff0ffff, palbase)`
   with a dirty flag — unpack A3I5/A5I3/palette/4×4 on cache miss into a linear
   RGBA buffer so the per-texel sampler has no format branch.
2. **Static 16-line block interleave** across N raster threads with a barrier
   (12 blocks round-robin), rather than dynamic per-scanline task stealing.
3. **Two-phase raster**: scalar coverage/depth pass building index+depth
   buffers, then a NEON compare-select (`cmeq/cmhi/bsl`) + `umull` shade/blend
   pass — vectorize the *shade*, keep the *walk* scalar.
4. **Host-format software framebuffer + deferred dirty `glTexSubImage2D`** into
   a persistent texture (never per-frame `glTexImage2D`), double-buffered
   behind one mutex/condvar. This is the cheap, portable present path — and the
   `.dfx`/`.dsd` chain shows how post-FX layers on top without touching the
   emulation core.
