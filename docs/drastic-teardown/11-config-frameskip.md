# DraStic teardown — DOC 11: Config, Frameskip / Frame-pacing, Performance Counters

Reverse-engineered from `reference/universal/lib/arm64-v8a/libdrastic_arm64.so` (arm64, stripped
except JNI exports). Addresses are file/vaddr as they appear in the Ghidra decomp corpus. Evidence
labels: **[proven]** = the constant/opcode is literally in the decompiled body; **[inferred]** =
strongly implied by surrounding code; **[unknown]** = too stripped to resolve.

The machine/emulator context object is a single large allocation whose base pointer lives in the
global `DAT_0024c000` (call it `CTX`). Almost every config knob is decoded into a field of `CTX`.

---

## 1. `applyConfig(J)` — the native config word

```
Java_com_dsemu_drastic_DraSticJNI_applyConfig(env, cls, long cfg)   @ 0x0011a4a0
```

**Key finding [proven]:** the Java `long` is **NOT a pointer to a struct**. It is a **packed
64-bit bitfield**. `applyConfig` stores it whole into the 64-bit global `_DAT_0024c468`
(low 32 bits = `DAT_0024c468`, high 32 bits = `uRam_0024c46c`) and then re-derives the live engine
state from its bits. The same word is also passed as the `param_5` argument to `startGame`
(`_DAT_0024c468 = param_5` at `startGame`+init), i.e. config is delivered both at boot and live.

`applyConfig` itself does only four things [proven]:
1. `DAT_0024c4b7 = (cfg >> 32) & 7` — **fast-forward / turbo speed index** (0..7).
2. Selects framebuffer texel format: `(cfg & 0x800000) ? 0x10 : 0x20` written to
   `CTX[0x03b2f931]`, then `FUN_0011cbc0(that)` sets the GL upload format:
   - `0x10` → `GL_RGB` / `GL_UNSIGNED_SHORT_5_6_5` (16-bit color)
   - `0x20` → `GL_RGBA` / `GL_UNSIGNED_BYTE` (32-bit color)
   So **config bit 23 = "high color / 32-bit framebuffer"** vs 16-bit. [proven]
3. `FUN_0011cbc0()` — pushes that GL format to the render thread globals
   (`DAT_0402db54/58/5c`).
4. `FUN_0011d728((cfg >> 37) & 3)` — **audio interpolation / quality** (0..3), see §2.

The bulk of the decode happens in **`FUN_00117c58`** (`config → CTX GPU-config block`, called from
both `applyConfig`'s callee chain and `startGame`) and in `startGame` itself.

### 1.1 Recovered config bit map

Low 32 bits = `DAT_0024c468` (`uVar4` in `FUN_00117c58`); high 32 = `uRam_0024c46c` (`uVar8`).
Field targets are byte offsets inside `CTX` (the `0x440–0x4d0` "graphics/config" register block)
unless noted.

| Bit(s) | Meaning (inferred) | Decoded to | Confidence |
|-------|--------------------|-----------|-----------|
| 0–3   | 3D internal-scale / render-mode selector (`cfg & 0xf`) | `CTX+0x444` | [proven layout / inferred meaning] |
| 5–6   | 3-way display/filter mode (`(cfg&0x60)>>5`; 1→0, 2→1, else→2) | `CTX+0x440` | [proven] |
| 8–9   | **Audio buffer / latency preset** (0..3) → `FUN_0011d704` → `DAT_00243eb8` | audio subsys | [proven] |
| 12–15 | **Target-framerate / frameskip LUT index** (0..5), gated by bit 29; indexes `DAT_002070c0[]` | `CTX+0x48c` | [proven gate, inferred=frameskip] |
| 16–19 | 4-bit renderer sub-option (`>>0x10 & 0xf`) | `CTX+0x490` | [proven] |
| 23    | **32-bit color framebuffer** (see above) | GL format `0x10`/`0x20` | [proven] |
| 24    | 1-bit renderer flag (`>>0x18 & 1`) | `CTX+0x488` | [proven] |
| 26    | 1-bit toggle → `FUN_0011d71c` → `DAT_00243ebc` (audio-related enable) | audio subsys | [proven] |
| 27    | 1-bit renderer flag | `CTX+0x478` | [proven] |
| 28    | 1-bit renderer flag | `CTX+0x468` | [proven] |
| 29    | **Enable** for the bits-12–15 framerate LUT lookup | `CTX+0x45c` + gates `CTX+0x48c` | [proven] |
| 30    | 1-bit renderer flag | `CTX+0x448` | [proven] |
| 31    | **Frame limiter / throttle** (inverted): `FUN_0011d710(~cfg>>31)` → `DAT_03d9b048`. Set = disable audio-sync throttle = fast-forward path. Also re-checked in `pauseSystem` as `if (DAT_0024c468 < 0)` | throttle global | [proven] |
| 32–34 | **Fast-forward / turbo speed index** (0..7) | `DAT_0024c4b7` | [proven] |
| 37–38 | **Audio interpolation / quality** (0..3) → `FUN_0011d728` → `DAT_00243ec0` (float) | audio subsys | [proven] |

High-word (bits 32+) renderer flags also decoded in `FUN_00117c58`:
`>>7&1 → CTX+0x4d0`, `>>10&1 → CTX+0x4a8`, `>>11..14 → CTX+0x4bc` (4-bit),
`>>15&1 → CTX+0x4ac`, `>>16&1 → CTX+0x480`, `>>18&1 → CTX+0x4b8`, plus two more bits packed via
NEON into `CTX+0x494/0x49c`. Their individual UI meanings are **[unknown]** (stripped), but they
are all 1–4-bit renderer/post-process toggles.

**Auxiliary globals set from other startGame args** (not part of the `cfg` word):
`DAT_0024c4be` (a boolean, arg7 → written to `CTX+0x8aad4`), `DAT_0024c4b8`
(`(arg5>>0x18)>>1 & 1`), `DAT_0024c478` (arg8 = a time/limit value → `CTX+0x8ab20 = /1000`),
`DAT_0024c49c` (autosave interval, see DOC 10).

### 1.2 The small config setters (config → live-engine globals)

| Fn | Global | Role |
|----|--------|------|
| `FUN_0011d704` @0x11d704 | `DAT_00243eb8` | audio buffer preset; if `<4` selects thresholds `DAT_03d7d07c/080` from `UNK_0020a0d4[]`/`UNK_0020a0e4[]` |
| `FUN_0011d710` @0x11d710 | `DAT_03d9b048` | **throttle enable** (0 = pace to audio; nonzero = free-run) |
| `FUN_0011d71c` @0x11d71c | `DAT_00243ebc` | audio effect/enable toggle |
| `FUN_0011d728` @0x11d728 | `DAT_00243ec0` (float) | audio interpolation coefficient, from `DAT_0020a080[]` |
| `FUN_0011d754` @0x11d754 | `DAT_03d9b03c` | misc a/v flag (init 0) |

---

## 2. Audio / frame pacing (the throttle path)

DraStic has **no busy-wait frame limiter and no `sleep(16ms)` in the render loop**. Frame pacing is
**audio-buffer driven** [proven]. The OpenSL ES buffer-queue callback (in the SPU mixer, around
`0x10462`) contains:

```
if (DAT_03d9b048 == 0 /* throttle enabled */ && DAT_03d7d070 < DAT_03d7d07c /* buffer not full */)
    ... enqueue / block ...
```

- `DAT_03d9b048` (config bit 31) = **throttle switch**. Throttle on → the emulator only advances
  as fast as the audio consumer drains buffers (locks emulation to ~60 Hz / real-time audio).
- Fast-forward (`fast-forward`, the single relevant string @ vaddr 0x10722b) simply sets bit 31 →
  `DAT_03d9b048 != 0` → the audio-underrun gate is bypassed → emulation runs unthrottled.
- `DAT_0024c4b7` (turbo index, config bits 32–34) selects the *degree* of fast-forward.

**Implication for our melonDS profiler:** DraStic's "how long did the frame take" is governed by
audio-buffer occupancy, not a wall-clock sleep. If we want faithful comparison we should measure
CPU-busy time, not wall-time-including-throttle.

---

## 3. Frameskip

**Honest status: [inferred / partially proven].** There is no `frameskip` string and no explicit
skip-counter loop found. What is proven:

- Config **bits 12–15** (a 0..5 index, gated by bit 29) index a small ROM table `DAT_002070c0[]`
  and the result is written to `CTX+0x48c`. The gating (`if enable && idx<6`) and the small range
  strongly match a **fixed-frameskip / target-FPS selector** (skip 0..5 frames). [inferred]
- The per-frame entry `FUN_00180834` (emulator step; calls `FUN_00116e74` for input) always runs
  the CPU cores; GPU **composition** is a separate call in the render path
  (`renderFrame`→`FUN_0011ceac`, software raster → GL blit). DraStic's architecture (software
  raster into a framebuffer texture, then GL blit — see ANCHORS) means a **skipped frame runs the
  ARM9/ARM7 + scheduler but omits the 2D/3D compose + texture upload**. This matches the classic
  "run CPU, skip draw" frameskip model. [inferred from architecture]
- Auto vs fixed: the LUT-index form (bits 12–15) is a **fixed** frameskip. An auto mode, if present,
  would live in the audio-underrun gate (drop a compose when behind) — not conclusively located.
  **[unknown-too-stripped]** for the exact auto-skip decision.

Do **not** treat the frameskip mechanics here as fully proven; the config *selector* is proven,
the *skip execution* is inferred from DraStic's known software-raster + GL-blit design.

---

## 4. Performance counters (directly relevant to our profiler)

### 4.1 `getPerformanceCounters()` @ 0x0011a6bc  [proven]

```
uint getPerformanceCounters():
    if (DAT_0024c4b0 != 0) return 0xFFFFFFFF          // invalid during post-load settle
    a = (uint)( *(float*)(CTX + 0x03b2f70c) * 16.0 )   // counter A
    b = (uint)( *(float*)(CTX + 0x03b2f710) * 16.0 )   // counter B
    clamp a,b to 0xFFFF
    return (a << 16) | b
```

- Returns **two independent per-frame timing measurements** as **Q12.4 fixed-point milliseconds**
  (float ms × 16, clamped to 0xFFFF → max ≈ 4095.9 ms), packed as two `u16`. [proven]
- The two floats live at `CTX+0x3b2f70c` and `CTX+0x3b2f710`, immediately adjacent to the CPU
  context region (`CTX+0x3b2f800` is the CPU `longjmp` buffer; `CTX+0x3b2f929..92c` is CPU run
  state). **Most likely = per-frame busy time of the two CPU cores (ARM9 vs ARM7)**, or emulation
  time vs idle/render time. Identity is **[inferred]**; the *format* (two Q12.4-ms values) is
  **[proven]**.
- `DAT_0024c4b0` is a **post-load settle countdown** (set to 10 at `startGame`/loadState,
  decremented each frame in `FUN_00116e74`). While nonzero, both `getPerformanceCounters` and
  `getFrameInfo` suppress/flag their output so timing isn't polluted by the load spike. [proven] —
  worth mirroring in our profiler (discard first ~10 frames after a state load).

### 4.2 `getFrameInfo()` @ 0x0011a650  [proven]

```
uint getFrameInfo():
    r = DAT_0024c4b0 & 0xFFFF                          // settle countdown (low 16)
    if (DAT_0024c4c0 != 0) r |= 0x80000000             // bit31 = PAUSED
    if (DAT_0024c4c1 != 0){ r |= 0x40000000; DAT_0024c4c1 = 0 }  // bit30 = one-shot event (edge)
    if (DAT_0024c4b0 != 0) r |= min(FUN_001974a4(),100) << 16    // bits16-23 = progress %
    return r
```

- **bit 31** = system paused (`DAT_0024c4c0`).
- **bit 30** = one-shot event edge, auto-cleared on read — fires when a save/load/autosave
  completes (set at `DAT_0024c4c1 = 1` right after `FUN_00117308` save and after the autosave
  timer trips). [proven]
- **low 16** = settle-frame countdown (`DAT_0024c4b0`, counts 10→0 after a load).
- **bits 16–23** = a **percentage 0–100** from `FUN_001974a4` = `DAT_0401e0d0 * 100 / DAT_0401e0c8`.
  Those two globals are advanced by the **savestate compress/write worker thread** (`FUN_0017a1bc`
  region, `DAT_0401e0xx` = the async-save block) → this is **savestate save/load progress %**,
  surfaced only during the settle window. [proven]

`updateFrame` (DOC 10) returns the **same** packed word as `getFrameInfo` (paused/event/settle/%),
so a caller that uses `updateFrame` gets frame status for free each frame.

### 4.3 Timing primitives available in the binary

- `FUN_0011b26c` @0x11b26c → `gettimeofday`, returns **microseconds** (`sec*1e6 + usec`). Used to
  seed RNG at `startGame` and as the base clock.
- `FUN_0011b2b0` @0x11b2b0 → `gettimeofday`, returns **milliseconds**. Used for the debug memdump
  filename and general timing.

There is no `clock_gettime(MONOTONIC)` usage — DraStic times everything off `gettimeofday`.

---

## 5. Pause / resume and a hidden debug hook

`pauseSystem(bool)` @ 0x0011a75c [proven]:
- Toggles `DAT_0024c4b9._1_1_`. When **config bit 31 is set** (`DAT_0024c468 < 0`), pause/unpause
  routes GL/audio teardown/restore via `FUN_0011e3e0` (pause) / `FUN_0011e4a0` (resume).
- **Debug memdump**: if a file `DraStic/config/dbg_mo.de` exists (probed once at `startGame`,
  result cached in `DAT_0024c4c2`), pausing dumps `/proc/self/maps` filtered regions into
  `DraStic/<...>/memdump_<ms>.txt`. Not a normal user feature — a developer diagnostic. [proven]

---

## 5b. Threading configuration

Thread/worker counts are **not** part of the `applyConfig` bitfield — they are established at
`onInit` and pushed into the machine when settings are applied [proven/inferred]:
- `onInit` sets the packed global `ram0x0024c010 = 0x0000000700000007` = **{worker count = 7,
  cpu/job count = 7}** (see ANCHORS), and `_DAT_0024c008`.
- On a settings-apply pass (`FUN_00116e74`, the `DAT_0024c4bb` branch) these are copied into the
  machine context at `CTX+0x8a684` (`_DAT_0024c008`) and `CTX+0x8a68c` (`ram0x0024c010`), alongside
  the screen-layout rectangles (`DAT_0024c018/020/028/02a`).
- The actual worker pool is `FUN_001c196c` (creates up to 32 detached workers running
  `FUN_001c1b14`; "Thread pool initialization failed"). The GL/emu/audio threads are separate
  (`pthread_create` at `FUN_0013d3c4`, `FUN_0015f614`, `FUN_0017a888`-save, `FUN_001c196c`-pool).

So "renderer threading" in DraStic is a fixed 7-way software-raster job pool seeded at init, not a
per-config toggle exposed through `applyConfig`.

---

## 6. Cheat sheet for the profiler project

- Two ready-made per-frame timers exist (`getPerformanceCounters`, Q12.4 ms ×2) — likely ARM9/ARM7
  busy time. Our profiler should expose the analogous split plus render/compose time, which DraStic
  does **not** separately expose.
- Frame pacing is **audio-driven**, so wall-clock frame time is meaningless under throttle; measure
  CPU-busy and render-busy separately.
- Discard the first ~10 frames after a savestate load (DraStic's `DAT_0024c4b0` settle window).
- Fast-forward = disable the audio-sync gate (config bit 31); turbo degree = bits 32–34.
- Frameskip selector is config bits 12–15 (gated by bit 29); skip omits GPU compose but still runs
  the CPUs (inferred).
