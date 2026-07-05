# DraStic Teardown 06 — SPU / Audio Subsystem

Reverse-engineered from `libdrastic_arm64.so` (arm64, stripped except JNI). Ghidra image
base = `0x100000`, so **file vaddr = Ghidra addr − 0x100000**. All data-table values below
were read directly out of the ELF (`.data` / `.rodata`) and are byte-exact.

Confidence tags: **[proven]** = read from disassembly/data; **[inferred]** = strong
structural deduction; **[unknown]** = too stripped to be sure.

---

## 0. TL;DR verdicts

| Question | Answer | Evidence |
|---|---|---|
| Pacing model | **Drop-on-full, NOT audio-slaved.** Emu never blocks on audio. | `FUN_0011dd6c` enqueue guard `pending < numBuffers`; BQ callback `FUN_0011d650` only re-queues silence; the one blocking throttle `FUN_001807dc` has **0 callers (dead code)**. [proven] |
| Output format | **44100 Hz, 16-bit, stereo (2ch), interleaved.** | `SLDataFormat_PCM` in `FUN_0011d760`: `samplesPerSec=44100000` mHz, `bitsPerSample=16`, `numChannels=2`; state field `+0x40014=0xac44`. [proven] |
| Resampler | Per-channel **nearest-neighbour** (point sampling) via 32.32 fixed-point phase accumulator; NO interpolation. | `FUN_00171bf0` uses `pos>>0x20` as integer index. [proven] |
| Mix precision | 16 channels summed into **int32** L/R accumulators, then `>>12` and clamp to int16. | `FUN_00171bf0` + `FUN_00172764`. [proven] |
| Mixer thread | **Emulation thread**, at the frame/VBlank boundary. No dedicated audio pthread. | `FUN_00172764`→`FUN_00171bf0` and `FUN_0011dd6c` all called from frame handler `FUN_0012c8f8`. OpenSL's own callback thread only feeds silence. [proven] |
| DS SPU coverage | All 16 channels, PCM8 / PCM16 / IMA-ADPCM / PSG / Noise, per-channel vol+pan, **2 sound-capture units**, mic in. | `FUN_00171bf0` format switch (cases 0–4); `FUN_001724f4`×2; ADPCM `FUN_00171740`. [proven] |

---

## 1. Key functions

| Addr (Ghidra) | Inferred name | Role |
|---|---|---|
| `FUN_0011d760` | `audio_init` | Builds the whole OpenSL ES graph (engine → output-mix → player), registers BQ callback, primes buffers, `SetPlayState(PLAYING)`. Called once from the startGame/boot path (`…(param_1, base+0x8a658)`). |
| `FUN_0011d650` | `bq_play_callback` | OpenSL buffer-queue completion callback. **The pacing tell.** |
| `FUN_0011d6b4` | `bq_record_callback` | Mic recorder BQ callback (re-enqueues capture buffers). |
| `FUN_0011dd6c` | `audio_flush_frame` | Per-frame: builds mic-in buffer, then copies the frame's mixed SPU output into an OpenSL buffer and enqueues **iff room**. The drop-on-full site. |
| `FUN_0011da98` | `mic_record_start` | Creates the OpenSL AudioRecorder (44100, mono) or loads `microphone.wav`. |
| `FUN_0011dcfc` / `FUN_0011e320` / `FUN_0011e3e0` / `FUN_0011e4a0` / `FUN_0011e1ec` | mic-stop / pause / pause+ / resume / teardown | Play-state transitions; all `memset` the buffer pool and `Clear()` the queue. |
| `FUN_0011e61c` | `set_volume` | `Java_…_setAudioVolume` → log-scaled millibel. |
| `FUN_0011d754` | `set_whitenoise` | `Java_…_setWhitenoiseFeed` → toggles synthetic-mic feed. |
| `FUN_00172764` | `spu_render_frame` | Computes #output samples via fixed-point ratio, calls the mixer + 2 capture units, then `>>12`+clamp→int16 into the output ring. |
| `FUN_00171bf0` | `spu_mix_channels` | **The 16-channel mixer.** Per-channel resample + format decode + vol/pan + int32 accumulate. |
| `FUN_00171740` | `adpcm_decode_block` | Decodes 8 IMA-ADPCM nibbles (one 32-bit word) per call into a 64-sample ring. |
| `FUN_001724f4` | `spu_capture_unit` | DS sound-capture unit (invoked for unit 0 and unit 1). |
| `FUN_00173104` | `spu_reset` | Zeroes 16 × 200-byte channel structs; sets up mic/`microphone.wav`. |
| `FUN_001807dc` | `audio_throttle_UNUSED` | Blocking `usleep(10)` spin while ring >3/4 full. **0 callers — dead code.** |

---

## 2. OpenSL ES graph (`FUN_0011d760`) [proven]

Standard Android OpenSL ES non-blocking buffer-queue player:

```
slCreateEngine(&engineObj,0,NULL,0,NULL,NULL)
engineObj->Realize(async=false)
engineObj->GetInterface(SL_IID_ENGINE,&engineItf)
engineItf->CreateOutputMix(&outMixObj,0,NULL,NULL); outMixObj->Realize()

// source = SLDataLocator_AndroidSimpleBufferQueue{ numBuffers }
//          + SLDataFormat_PCM{ fmt=PCM, ch=2, 44100000 mHz, 16-bit, 16 container, L|R, little-endian }
// sink   = SLDataLocator_OutputMix(outMixObj)
engineItf->CreateAudioPlayer(&playerObj, &src, &sink, 2,
                             {SL_IID_BUFFERQUEUE, SL_IID_VOLUME}, {req,req})
playerObj->Realize()
playerObj->GetInterface(SL_IID_PLAY,        &playItf)
playerObj->GetInterface(SL_IID_BUFFERQUEUE, &bqItf)
bqItf->RegisterCallback(bq_play_callback, NULL)   // FUN_0011d650, context=NULL
playerObj->GetInterface(SL_IID_VOLUME,      &volItf)
playItf->SetPlayState(SL_PLAYSTATE_PLAYING)

for i in 0..numBuffers-1:                          // prime the queue
    writeOff[i] = 0
    bqItf->Enqueue(&pool[i], bufSamples*2 /*bytes*/)
```

- **Object/interface globals** (`.bss` ~`0x3d7d0xx`): engineObj `d010`, engineItf `d018`,
  outMix `d020`, playerObj `d028`, playItf `d030`, **bqItf `d038`**, recorderObj `d040`,
  recordItf `d048`, recordBqItf `d050`, **volItf `d058`**.
- **Buffer pool**: `DAT_03d7d084`, `memset` size `0x16f80` = room for **8** buffers of stride
  `0x2df0` (11760 bytes = 5880 int16 = 2940 stereo frames). Per-buffer write offset in
  `DAT_03d9b008[]`.
- The player is created with a **`SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE`** — the classic
  fire-and-forget path. No `SL_IID_ANDROIDCONFIGURATION` stream-type pinning, no PBQ pull model.

### 2.1 Latency presets [proven — byte-exact]

`FUN_0011d760` indexes two `.rodata` tables by config `DAT_00243eb8` (0–3; ≥4 → default):

| preset | numBuffers (`@0x20a0d4`) | bufSamples int16 (`@0x20a0e4`) | frames/buf | total buffered | latency @44100 |
|---|---|---|---|---|---|
| 0 | 4 | 1470 | 735 (1 video-frame) | 4 × 735 | ~66 ms |
| 1 | 4 | 2940 | 1470 | 4 × 1470 | ~133 ms |
| 2 | 3 | 5880 | 2940 | 3 × 2940 | ~200 ms |
| 3 / default | 4 / **3** | 5880 | 2940 | — | ~266 / **200 ms** |

`1470 int16 = 735 stereo frames = 44100/60` → **one 60 fps video frame's worth of audio**.
The default (index out of range) is **3 buffers × 5880 samples ≈ 200 ms**.

---

## 3. Pacing — the crucial proof [proven]

### 3.1 The playback callback does NOT block or signal the emu

```c
// FUN_0011d650  bq_play_callback(void) — runs on OpenSL's internal thread
if (mute_flag /*d074*/) return;
playClock /*d068*/ += bufSamples;      // advance a play-position counter only
if (pending /*d070*/ > 0) {            // a real buffer just drained
    pending--;                          // …decrement in-flight count
    return;
}
// UNDERRUN: no real audio queued → keep the stream alive with silence
bqItf->Enqueue(&SILENCE /*UNK_00207290*/, bufSamples*2);
```

No condvar, no semaphore, no write to any emu-thread wakeup. On starvation it enqueues a
**static silence buffer** (`UNK_00207290`). This is the definitive proof the emulator is
**never** paced by audio.

### 3.2 The producer drops when the queue is full

```c
// FUN_0011dd6c audio_flush_frame(audioState) — runs on the EMU thread, once per frame
n = audioState[0x4000c];                 // int16 samples produced this frame (=1470)
... build mic-input buffer at +0x20000 (see §7) ...
if (outputDisabled /*d9b048*/ == 0 && pending /*d070*/ < numBuffers /*d07c*/) {
    b = curBuf /*d060*/;
    memcpy(&pool[b] + writeOff[b]*2, audioState /*mixed output @+0*/, n*2);
    writeOff[b] += 1470;                  // 0x5be — exactly one frame
    if (writeOff[b] >= bufSamples) {      // buffer full → hand to OpenSL
        bqItf->Enqueue(&pool[b], bufSamples*2);
        writeOff[b] = 0;
        pending++;                         // in-flight++ (callback will --)
        curBuf = (curBuf+1) % numBuffers;
    }
}
playClock /*d068*/ -= n;
audioState[0x4000c] = 0;                  // reset frame sample count
```

`pending` (`d070`) is the in-flight-buffer count: **++ here on enqueue, −− in the callback**
on drain. When `pending == numBuffers` the whole `memcpy`+enqueue is **skipped — the frame's
audio is silently discarded.** No spin, no wait. `d9b048` is a hard output-disable (turbo /
fast-forward) that drops everything.

### 3.3 The latent audio-slaved path that DraStic chose NOT to use

```c
// FUN_001807dc — callers = 0  (present in the binary, never called)
while ( (bufSize*3)>>2 <= ((writePtr - readPtr) & 0xffff) )
    usleep(10);                           // block producer until ring < 3/4 full
```

A textbook audio-slaved throttle exists in the code but is **dead**. DraStic deliberately
runs the OpenSL player free and lets its **own frame limiter** own pacing.

> **Actionable for melonDS:** DraStic's model = *frame-limiter owns time; audio is a lossy
> sink.* The buffer-queue is primed with N buffers of silence, the emu appends exactly one
> video-frame of samples per frame and drops if the ring is full, and underruns are papered
> over with a static silence buffer inside the callback. There is **no** `cb→emu` feedback.
> If our fork currently blocks the emu in the SDL/audio callback (audio-slaved), matching
> DraStic means: (1) size the queue to the desired latency (≈200 ms default / down to ~66 ms),
> (2) never block the emu on the audio device, (3) on underrun emit silence rather than stall,
> (4) let the existing vsync/sleep frame limiter set speed. This removes audio from the
> critical path so a lag spike drops a few ms of sound instead of stalling the whole frame.

---

## 4. SPU render pipeline — thread & cadence [proven/inferred]

Call chain per emulated frame, all on the **emu thread**:

```
FUN_0012c8f8  (ARM7 frame/VBlank event handler)
  └─ FUN_00172764  spu_render_frame(audioState)
       ├─ compute nSamples from 32.32 phase ratio (DAT_015ccd00 pos, _d20 rate, _d18 step)
       ├─ memset int32 accumulator (stack aiStack_8070[8192])
       ├─ if (mode<2) FUN_00171bf0  spu_mix_channels(...)   // sum 16 channels → accum
       ├─ FUN_001724f4(...,0)   capture unit 0              // DS sound capture
       ├─ FUN_001724f4(...,1)   capture unit 1
       └─ accum >>12, clamp int16, write interleaved L/R to output ring @+0
  └─ FUN_0011dd6c  audio_flush_frame(audioState+…)          // enqueue-or-drop (§3.2)
```

Per frame the renderer emits **735 stereo frames (1470 int16) = 44100/60**, matching the
enqueue's fixed `writeOff += 1470`. There is **no separate mixer pthread**; the only audio
thread is OpenSL's internal callback thread, which does nothing but re-queue silence.

The audio state block lives at `master + 0x2b1800`. Notable offsets:
`+0` mix/output ring · `+0x20000` mic-input buffer · `+0x4000c` frame sample-count (write) ·
`+0x40008` read ptr · `+0x40010` mix-rate divisor · `+0x40014 = 0xac44` (44100) ·
`+0x40018` buffer size · `+0x40028` channel[0]; **16 channels × 200 bytes (0xc8) stride**.

---

## 5. The 16-channel mixer `FUN_00171bf0` [proven]

Iterates channels; channel struct = 200 bytes. Split into two passes: channels **0–3**
(these additionally store their raw per-channel output to a side buffer at `+0x4000` for the
capture units — DS capture taps ch1/ch3), then channels **4–15** via a `switch(format)`.

### 5.1 Per-channel state (offsets into the 200-byte struct)

| off | field |
|---|---|
| `+0x80` | phase accumulator, **32.32 fixed** (`>>0x20` = integer sample index) |
| `+0x88` | phase increment, 32.32 = `0x1006f43_00000000 / ((0x10000 − TMR) · mixrate)` |
| `+0x90` | ADPCM decoded-window end |
| `+0x98` | pointer to channel HW registers (`[0]`=CNT/SOUNDxCNT, `[+8]`=TMR timer reload) |
| `+0xa0` | source waveform pointer (into emulated DS memory) |
| `+0xac` | loop end (LOOPSTART+LEN, in samples) |
| `+0xb0` | loop start length |
| `+0xb4/0xb6` | precomputed **left / right** volume multipliers (int16) |
| `+0xb8/0xba` | ADPCM predictor saved at loop point / current |
| `+0xbc` | **format** (0..4) |
| `+0xbd` | dirty flags (recompute vol / recompute timer) |
| `+0xc0/0xc1` | ADPCM index saved at loop / loop-saved flag |

### 5.2 Volume & pan (recomputed when dirty) [proven]

```
chVol   = (CNT & 0x7f)==0x7f ? 0x80 : CNT & 0x7f          // per-channel volume, cap 0x7f→0x80
volShift = ((CNT>>8)&3)==3 ? 0 : 4-((CNT>>8)&3)           // volume divider 0/1/2/4 → left shift
mstVol  = (mixReg[0x100]&0x7f)==0x7f ? 0x80 : mixReg&0x7f // SOUNDCNT master volume
pan     = (CNT>>16)&0x7f                                  // 0..0x7f
base    = mstVol * chVol << volShift
Lvol    = (base * (pan ^ 0x7f)) >> 13
Rvol    = (base *  pan       ) >> 13
```

Faithful DS SPU: per-channel volume + 2-bit volume divider, master volume, 7-bit pan, folded
into two int16 L/R gains.

### 5.3 Per-sample loop (point-resample + accumulate) [proven]

```
for i in 0..nSamples-1:
    idx = pos >> 32
    s   = decode(format, src, idx)               // §5.4  (NEAREST NEIGHBOUR — pos truncated)
    accumL[i] += s * Lvol                          // int32 accumulate (L in low32, R in high32)
    accumR[i] += s * Rvol
    pos += inc
    if (pos>>32 >= loopEnd):                        // loop / one-shot / stop per CNT bits 27,28,29
        if (repeat) pos -= loopLen<<32
        else        clear CNT.bit31 (mark inactive)
```

**No linear interpolation** anywhere — DraStic uses point sampling (truncated phase) for
speed, identical in spirit to hardware "no-filter" DS output.

### 5.4 Formats [proven — from the switch + ADPCM tables]

- **case 0 — PCM8:** `s = (int8)src[idx] << 8`.
- **case 1 — PCM16:** `s = (int16)src[idx*2]`.
- **case 2 — IMA-ADPCM:** read `s` from a 64-entry decoded ring `struct+(idx&0x3f)*2`; when the
  play position passes the decoded window (`+0x90`), decode the next block via `FUN_00171740`.
  Loop points save/restore predictor (`+0xb8`) and step index (`+0xc0`) — matching DS ADPCM
  loop semantics.
- **case 3 — PSG square wave** (DS channels 8–13): table-indexed duty waveform.
- **case 4 — Noise** (DS channels 14–15): PCM8-style read from the noise/LFSR source.

Channels 0–3 also mirror their decoded sample to the capture side buffer (`+0x4000`).

### 5.5 IMA-ADPCM decoder `FUN_00171740` [proven — byte-exact tables]

Decodes 8 nibbles (one aligned 32-bit word) per call into the channel's 64-sample ring, then
saves index+predictor. Uses the **exact DS/IMA tables**, read from `.data`:

- **Step table** `DAT_00243eec` — 89 × int16: `7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, …,
  27086, 29794, 32767`. (Canonical IMA step table.)
- **Index-adjust table** `DAT_00243f9e` — 8 × int8: **`{-1, -1, -1, -1, 2, 4, 6, 8}`**.

Reconstruction per nibble (standard IMA): `diff = step>>3 + (b0? step) + (b1? step>>1) + (b2?
step>>2)` with sign from bit3; predictor clamped to ±0x7fff; index `+= adjust[nibble&7]`,
clamped `[0, 88]`.

---

## 6. Output stage `FUN_00172764` [proven]

- **Sample count** = `((ptr8*0x400 − fracPos) · rate) >> 32` — a 32.32 fixed-point ratio
  between the SPU timeline and the 44100 Hz output clock. This is the *global* resample
  cadence that keeps output locked to 44100/60.
- **Downshift & clamp** (NEON, 4-wide): `out16 = clamp( accum32 >> 12, −0x8000, +0x7fff )`,
  written interleaved L/R into the output ring at `+0`. The `>>12` is the fixed mix headroom
  (16 channels × 13-bit vol precision → int32, brought back to int16).

So the **only** resampling is: (a) per-channel point resample source→mix rate in `FUN_00171bf0`,
and (b) the frame-level fixed-point count in `FUN_00172764`. There is **no polyphase / FIR /
linear filter** — cheapest possible resampler.

---

## 7. Volume, mic, whitenoise [proven]

### 7.1 Master volume — `setAudioVolume` → `FUN_0011e61c`
```
vol = min(vol,100)
millibel = (vol==0) ? SL_MILLIBEL_MIN(0x8000)
                    : (short)(log10(vol/100.0) * 2000)     // → DAT_03d9b04c
```
Applied via `volItf->SetVolumeLevel(millibel)` on resume (`FUN_0011e4a0`). Hardware-mixed by
OpenSL, log-scaled to millibels; 0x8000 = mute.

### 7.2 Microphone in — `FUN_0011da98` / capture callback `FUN_0011d6b4`
- Creates an OpenSL **AudioRecorder** (`SL_DATALOCATOR_IODEVICE` mic → simple BQ, 44100 mHz,
  **mono**, 16-bit) with `SL_IID_RECORD` + `SL_IID_ANDROIDSIMPLEBUFFERQUEUE`, enabled only when
  `DAT_00243ebc` (mic-enable) is set. Falls back to loading `%s/microphone/microphone.wav`.
- Mic gain preset `DAT_00243ec0` from `.rodata` `@0x20a080 = {2, 4, 8, 16}` (index 0–3).
- In `FUN_0011dd6c`, live mic samples are read from the capture ring (`DAT_03d9a006`), converted
  to float `×(1/32768)`, **non-linearly companded** (`s·gain·s·32768` — a squared/soft-gate
  transfer), clamped to int16, and written to the emulated mic-input buffer at `state+0x20000`.

### 7.3 Whitenoise feed — `setWhitenoiseFeed` → `FUN_0011d754` (`DAT_03d9b03c`)
When enabled, `FUN_0011dd6c` fills the mic-input buffer from a **44100-sample noise table**
(`DAT_03d9b030 = &DAT_00239274`, length `DAT_00239270 = 44100` [proven]), modulo-indexed, with a
soft-clip transfer (`>>2` for small magnitudes, `<<1` for large). This lets games that require
mic input (e.g. "blow into the mic") pass their checks without a real microphone.

---

## 8. Sound capture `FUN_001724f4` [proven/inferred]

Called for capture unit 0 and 1 each frame. Reads the mixer accumulator / channel side buffer
and writes resampled captured samples back into DS memory at the capture destination, with its
own 32.32 phase accumulator, format bit (16-bit vs 8-bit), and loop-wrap — i.e. the DS
`SNDCAP0/1` units (loop or one-shot). Two units, matching DS hardware. [proven the two units
exist and resample/loop; exact add-to-channel routing is [inferred].]

---

## 9. DraStic-specific optimizations (relevant to our fork)

1. **Audio off the critical path** — free-running OpenSL BQ, drop-on-full, silence-on-underrun.
   The blocking throttle exists but is disabled. *(§3)*
2. **Cheapest resampler** — per-channel nearest-neighbour + a frame-level fixed-point sample
   count. No FIR/linear/polyphase. *(§5.3, §6)*
3. **NEON-vectorized** volume application (mic path) and the int32→int16 downshift/clamp. *(§5, §6)*
4. **One-shot decode caching** — ADPCM decoded 8 samples at a time into a 64-entry ring, so
   the hot per-sample loop is a table read, not a decode. *(§5.5)*
5. **Precomputed L/R gains** — vol×pan×master folded into two int16 multipliers, recomputed only
   on a dirty flag, not per sample. *(§5.2)*
6. **Latency is a user knob** — 4 presets from ~66 ms to ~266 ms by trading buffer count/size;
   default ≈200 ms. *(§2.1)*
7. **Exactly one video-frame of audio per frame** (735 stereo samples), so audio and video share
   the same 60 Hz cadence and the frame limiter transitively paces audio. *(§3.2, §4)*

---

## 10. Open items [unknown / inferred]

- Exact meaning of the `0x1006f43` numerator in the channel-increment formula (DS SPU clock is
  33.51 MHz; the constant is a pre-scaled ratio — not reconstructed to the last bit). [inferred]
- The `mode<2` gate on `FUN_00171bf0` (`DAT_025d5450`) — likely a "disable-channel-mix / capture
  only" or interpolation-mode selector. [unknown]
- Capture-unit → mixer-add-back routing (DS "add to channel 1/3" bit) not fully traced. [inferred]
- Whether PSG duty/noise use a shared LFSR advanced elsewhere, or a static table. [inferred:
  static-table read in the mixer].
