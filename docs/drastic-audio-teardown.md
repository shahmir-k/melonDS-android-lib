# DraStic Audio Architecture Teardown (arm64)

Reference-only, legally-owned static analysis of
`reference/universal/lib/arm64-v8a/libdrastic_arm64.so`
(1.35 MB, ELF aarch64, stripped; BuildID 2318f180…).
Tools: `nm -D`, `objdump -d/-R/-s`, `strings`. No device touched.
All addresses are file/virtual offsets in the arm64 `.so`.
Labels: **[proven]** = read directly from the binary; **[inferred]** = deduced from structure/constants.

---

## 0. Bottom line first

DraStic's audio path is a **non-blocking OpenSL ES buffer queue**: drop-on-full,
silence-on-empty. **Emulation never blocks or throttles on audio buffer state** —
in either direction. There is **no sync primitive shared between the audio path and
the frame path**. Frame pacing is slaved to the **display/GL render thread**
(a `pthread_cond` at `0x3f2db84`), which the audio callback never touches.

**Therefore DraStic's smooth audio is verdict (i): it simply runs fast enough that the
ring rarely underruns**, helped by a generous, user-selectable output buffer
(up to ~4 video-frames of slack). It is architecturally the *same* model melonDS already
uses. Our fork does **not** need an audio-architecture change — audio comes good "for free"
once R4 render-thread offload sustains 60 fps. (Details + the one cheap win worth copying in §7.)

---

## 1. Audio API + callback model  **[proven]**

Dynamic symbols (`nm -D`) present:
`slCreateEngine`, `SL_IID_ENGINE`, `SL_IID_PLAY`, `SL_IID_BUFFERQUEUE`,
`SL_IID_ANDROIDSIMPLEBUFFERQUEUE`, `SL_IID_VOLUME`, `SL_IID_RECORD`; needs `libOpenSLES.so`.
No Java `AudioTrack` — pure native OpenSL ES. Confirms the prior pass.

Audio engine init function at **`0x1d760`**:
- `0x1d810` `slCreateEngine` → Realize → `GetInterface(SL_IID_ENGINE)`
- `0x1d848` `CreateOutputMix` → Realize
- `0x1d8b0` `CreateAudioPlayer` with an `SLDataLocator_AndroidSimpleBufferQueue`
  source + `SLDataFormat_PCM` (built on stack from rodata at `0x10a084`) → Realize
- `0x1d924` `GetInterface(SL_IID_PLAY)`; `0x1d944` `GetInterface(SL_IID_BUFFERQUEUE)`
- **`0x1d974` `RegisterCallback`** (SLAndroidSimpleBufferQueueItf vtable+0x18):
  callback fn = **`0x1d650`**, context = **NULL**.
- `0x1d990` `SetPlayState(SL_PLAYSTATE_PLAYING=3)`
- `0x1d9d8` primes the queue: Enqueues N buffers up-front, then playback is self-sustaining.

There is a *second* OpenSL setup at `0x1da98` using `SL_IID_RECORD` + 44100 Hz — the
**microphone recorder** (DS mic input), not playback. Ignore for pacing.

### The buffer-queue callback `0x1d650` — the decisive object  **[proven]**
Full disassembly of the callback (it is tiny, ends at `0x1d6b4`):
```
0x1d650  ldr  w8,[0x3c7d074]        ; pause/mute guard
0x1d658  cbz  -> continue           ; if set, just RET (no audio)
0x1d668  ldr  w8,[0x3c7d068+0x18]   ; stride
0x1d66c  ldr  x11,[0x3c7d068]       ; read pointer
0x1d670  ldr  w9,[0x3c7d070]        ; queued-buffer counter
0x1d674  add  x11,x11,stride ; str back
0x1d680  b.le 0x1d694              ; counter <= 0  -> UNDERRUN path
0x1d684  sub  w9,w9,#1 ; str [0x3c7d070]   ; consume one buffer, RET
--- underrun path 0x1d694 ---
0x1d698  ldr  x0,[0x3c7d038]        ; the buffer-queue interface
0x1d6a4  add  x1,#0x107290          ; <-- fixed rodata buffer
0x1d6b0  br   Enqueue(bq, 0x107290, stride*2)   ; tail-call, then RET
```
- **No `pthread_*` call anywhere in the callback.** It signals nothing, waits on nothing.
- On underrun (counter hits 0) it Enqueues a **fixed read-only buffer at `0x107290`**,
  which I dumped and verified is **all zeros = silence**. This is the melonDS-equivalent
  "fill silence on underrun" behaviour — it does NOT stall or notify the emulator.

---

## 2. Threading model  **[proven counts / inferred roles]**

6 `pthread_create` sites: `0x3d53c`, `0x5f718`, `0x5f79c`, `0x5f820`, `0x7ab18`, `0xc1a98`.
The three at `0x5f7xx` share one entry (`0x5f53c`) → a **3-thread worker pool** [inferred:
DS core / renderer workers].

```
 ┌────────────────────────────────────────────────────────────────────┐
 │ EMULATION / FRAME thread                                            │
 │  per-frame update fn 0x1dd6c:                                       │
 │    - run DS core                                                    │
 │    - MIX + resample DS SPU 32768Hz -> 44100Hz  (0x1de00 loop)      │
 │    - volume/soft-clip (NEON, 0x1e130)                              │
 │    - memcpy into current output slot (0x1ded4)                     │
 │    - Enqueue full slot to OpenSL  (0x1def0, via [0x3c7d038])       │
 │    - if ring FULL: DROP + return  (0x1de98 -> 0x1e100) NON-BLOCKING│
 │                                                                    │
 │  frame pacing: waitScreen 0x1ce40 / signalScreen 0x1ce78          │
 │   cond=0x3f2db84+0x28, mutex=0x3f2db84   <── shared with:          │
 └───────────────┬──────────────────────────────────────────┬────────┘
                 │ pthread_cond (DISPLAY sync)               │ Enqueue()
                 ▼                                           ▼
 ┌───────────────────────────┐         ┌──────────────────────────────┐
 │ GL / SCREEN render thread │         │ OpenSL ES callback thread     │
 │ waits on 0x3f2db84 cond   │         │ (inside libOpenSLES.so)       │
 │ = the real frame limiter  │         │ callback 0x1d650:             │
 └───────────────────────────┘         │  advance ptr / consume buffer │
                                        │  underrun -> Enqueue silence  │
   NO cond/sem/mutex shared             │  NO pthread primitive         │
   between audio and anything ────────► │  NO back-pressure to emu      │
                                        └──────────────────────────────┘
```

- **Audio is *generated* on the emulation thread** (inside the per-frame update, `0x1dd6c`).
- **Audio is *played out* on the OpenSL callback thread**, owned by `libOpenSLES.so`
  (DraStic does not `pthread_create` it).
- **Frame pacing is on a display cond var** (`0x3f2db84`), between emu and the GL/screen
  thread (`waitScreen`/`signalScreen`, matching JNI names `waitScreen`/`signalScreen`).

---

## 3. THE KEY QUESTION — discipline when emu runs slower than realtime

### (a) Callback underruns to silence — **YES [proven]**
Callback `0x1d650`: counter `[0x3c7d070] <= 0` ⇒ Enqueue the zero buffer `0x107290`.
Verified zeros via `objdump -s`.

### (b) Frame pacing slaved to audio (emu blocks on audio) — **NO [proven]**
The producer `0x1dd6c` checks ring occupancy at **`0x1de98`**:
```
0x1de90  ldr  w9,[0x3c7d070]      ; write/used index
0x1de94  ldr  w8,[0x3c7d07c]      ; buffer-count limit
0x1de98  cmp  w9,w8
0x1de9c  b.hs 0x1e100             ; ring FULL -> jump to drop path
...
0x1e100  ldr  x9,[0x3c7d068] ; sub samples ; str back ; str wzr,[..] ; RET
```
The full path (`0x1e100`) only adjusts the sample accounting and **returns immediately**.
There is **no `usleep`, no `pthread_cond_wait`, no spin** anywhere on this path.
(The binary's single `usleep` — `0x17014`, `usleep(50000)` — is in the *pause/menu* idle
loop, gated by pause flags at `0x14c04bd`; it is unrelated to audio pacing.)
⇒ When the ring is full the emulator simply **discards** that chunk and keeps running.
Emulation is never throttled by audio.

### (c) Dynamic resampling to output rate — **YES, but STATIC ratio [proven rates / inferred loop role]**
DS SPU native 32768 Hz is up-converted to the 44100 Hz OpenSL output. Resample loop at
`0x1de00–0x1de78` reads the source with a wrapping index (`w14` = source-length limit,
`0x139270 >> 1`) and emits `w22` output samples, plus a companding/volume curve. The main
NEON volume/clip loop at `0x1e130` uses **equal input/output stride** (1:1), confirming the
rate conversion happens only in the `0x1de00` loop and the ratio is **fixed** (not adapted
to buffer fill). It is *not* a buffer-driven adaptive resampler.

### (d) Ring buffer drop / duplicate — **DROP on overflow [proven]**
Overflow ⇒ drop (the `0x1de98` → `0x1e100` path). Underflow ⇒ **silence**, not sample
duplication. No pitch-preserving stretch, no duplicate.

**Summary of discipline:** non-blocking **both** ways — *drop-on-full, silence-on-empty*.
This is philosophically identical to melonDS's audio buffer.

---

## 4. Sample rate + buffer / period sizing  **[proven]**

- **Output format:** `SLDataFormat_PCM`, **44100 Hz**, **stereo (2ch)**, **16-bit**.
  rodata `0x10a08c` = `0x02A0E9A0` = 44 100 000 milli-Hz = 44100 Hz; numChannels=2, bits=16.
- **Buffering is a user-selectable latency preset** — a 4-entry table indexed by a config
  value (`0x143eb8`), read at `0x1d7d4`:
  - buffer **count**  (table `0x10a0d4`): `{4, 4, 3, 4}`
  - **samples/buffer** (table `0x10a0e4`, int16 stereo): `{1470, 2940, 5880, 5880}`
  - default fallback (config out of range): 3 buffers × 5880.
  - Note `1470 = 735 stereo frames = exactly one 60 Hz video frame at 44100 Hz`;
    2940 = 2 frames, 5880 = 4 frames. Per-slot stride in the code is `0x2df0 = 11760 bytes = 2940 samples`.
- The producer accumulates in **1470-sample (`0x5be`) increments** — one video frame's worth —
  and Enqueues a slot once it fills (`0x1def0`).
- **Sizing is preset, not runtime-adaptive.** Total output latency ranges from
  ~4 frames (preset 0, ≈66 ms) up to ~larger buffers; chosen once at init, never resized on the fly.

---

## 5. Shared sync primitive between audio and frame paths?  **NO [proven]**

- Frame path cond var: `0x3f2db84` (+0x28 cond, base mutex) — `waitScreen 0x1ce40`
  (`mutex_lock; cond_wait; mutex_unlock`) and `signalScreen 0x1ce78`
  (`mutex_lock; cond_signal; mutex_unlock`). Shared only between the **emu thread and the
  GL/screen thread**.
- Audio callback `0x1d650`: touches **zero** pthread primitives and a *different* global
  region (`0x3c7d000`), never `0x3f2db84`.
- ⇒ **Audio and emulation share no lock, cond, semaphore, or futex.** Audio is fully decoupled.
  The "audio-slaved pacing" signature the task was hunting for is **absent**.

---

## 6. Answers, one line each

1. **API/callback:** OpenSL ES Android Simple Buffer Queue; callback `0x1d650`, ctx NULL,
   registered `0x1d974`. [proven]
2. **Threading:** audio *generated* on emu thread (producer `0x1dd6c`), *played* on the
   OpenSL-owned callback thread; 6 threads total incl. a 3-worker pool; frame pacing on a
   separate display cond. [proven/inferred]
3. **Slower-than-realtime discipline:** (a) callback → **silence** [proven];
   (b) emu **does NOT** block on audio [proven]; (c) fixed 32768→44100 resample [proven];
   (d) **drop** on overflow [proven]. Non-blocking both ways.
4. **Rate/buffer:** 44100 Hz / stereo / 16-bit; preset buffering {count 3–4}×{1470–5880 samples};
   not runtime-adaptive. [proven]
5. **Shared audio↔frame primitive:** **None.** [proven]

---

## 7. Verdict + implication for liteDS-v2

**Verdict = (i) "simply fast enough / never underruns."**
Not (ii) audio-slaved pacing (there is provably no coupling); the frame limiter is the
*display* thread, not audio. Resampling (iii) exists but is cosmetic (fixed ratio), not the
source of smoothness. DraStic sounds smooth because its core reliably out-runs realtime, so a
44.1 kHz ring with ~2–4 frames of slack almost never empties; on the rare miss it plays a
few ms of silence and moves on.

**Implication for our fork:**
- **No audio-architecture change is required to match DraStic.** DraStic proves that a plain
  non-blocking OpenSL buffer queue (drop-on-full / silence-on-empty) — which melonDS already
  has — is sufficient. The lever is **emulation speed**, not an audio rewrite.
- **Audio "comes good for free" once R4 render-thread offload sustains 60 fps.** Underruns are
  a *symptom* of falling below realtime; fix the speed and the audio follows. Do **not** spend
  R4 effort building an audio-slaved pacing loop — DraStic, the gold standard here, doesn't have one.
- **One cheap win worth copying:** DraStic's **user-selectable output-buffer depth** (up to
  ~4 video frames / 5880 stereo samples). A deeper output buffer masks *transient* lag spikes —
  the exact "lag spike" hot-path scenario in this profiler's mandate — without any pacing change,
  by riding out brief dips below 60 fps before the ring empties. If liteDS-v2 currently runs a
  shallow audio buffer, widening it (and exposing a latency preset) is a low-risk smoothness win
  that buys headroom while R4 lands.
- Corollary for the profiler: since audio never back-pressures emulation, **audio underruns are a
  clean, cheap realtime-miss signal** — instrument the ring's silence-fill events as a proxy for
  "frame ran long," and correlate with the frame-section timings.

*(Not committed — left untracked for orchestrator review, per instructions.)*
