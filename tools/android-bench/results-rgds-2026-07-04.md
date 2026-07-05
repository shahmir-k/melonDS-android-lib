# liteDS-v2 Unit 7 (measurement half) — Android/A55 benchmark results

Date: 2026-07-04
Device: Anbernic **RG DS** (`rk30sdk`, Rockchip RK3568), 4x Cortex-A55 (CPU part
0xd05) @ 1.992 GHz, Android 14 (API 34), kernel 6.1.141, aarch64.
Workload: Shrek ROM (`.tmp-shrek.nds`, 32 MiB, game code A4IE), direct boot,
FreeBIOS + generated firmware, software renderer, no input.

Approach: the **liteDS-headless CLI itself** was cross-compiled with the Android
NDK and run on the device over adb from `/data/local/tmp/liteds/` — no app/UI
port. This gives the A55-class architecture measurements with full feature
parity with the host harness (same binary source, same golden traces).

## 1. Build

`tools/android-bench/build.sh` — NDK r27 (27.0.12077973) CMake toolchain,
`ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-34`, `ANDROID_STL=c++_static`
(standalone binary, no libc++ dependency), Release, LTO off. Common flags:
`BUILD_QT_SDL=OFF ENABLE_OGLRENDERER=OFF ENABLE_GDBSTUB=OFF ENABLE_JIT=ON
LITEV_HEADLESS=ON LITEV_AGGRESSIVE_SKIP=ON` (frameskip is a runtime knob,
inert at 0).

**Build friction: none.** All five configs cross-compiled first-try; teakra,
fatfs, and the A64 JIT (including `ARMJIT_Linkage.S`) needed no patches. The
core already carries the `__ANDROID__` ashmem/ASharedMemory fastmem path.

Stripped binary sizes (llvm-strip):

| config   | LITEV flags (cumulative)                          | size (bytes) |
|----------|---------------------------------------------------|-------------:|
| baseline | none (upstream-equivalent core)                   | 2,544,800 |
| dispatch | +JIT_DISPATCH (LINK_* off)                        | 2,546,208 |
| link     | +LINK_UNCOND/COND/FALLTHROUGH                     | 2,552,416 |
| es       | +EVENT_SLICES                                     | 2,553,184 |
| full     | +MEM_DTCM_BLOCK +MEM_MAINRAM_LOAD                 | 2,554,528 |

The whole liteDS stack costs **&lt;10 KB** of code size.

## 2. JIT / fastmem engagement proof

`--fastmem on|off` was added to the harness (`tools/headless/main.cpp`), plus a
stderr line reporting the engine that *actually* engaged:

```
engine: JIT  fastmem_requested=on  fastmem_active=yes
```

- JIT vs interpreter on device (baseline, 300f): **68.5 vs 33.4 FPS** — JIT
  clearly engaged, no silent interpreter fallback.
- `mmap` PROT_EXEC rwx anon mappings work in the adb shell domain on API 34;
  no wx-alternation fallback needed.
- Fastmem allocates via `ASharedMemory_create`; the boot-time log line
  `[E] Failed to allocate memory using ftruncate! (Invalid argument)` is a
  benign quirk — ASharedMemory already sizes the region and `ftruncate` on an
  ashmem fd returns EINVAL; the subsequent `MAP_FIXED` mmap succeeds and
  fastmem is functional (proven bit-exact by the trace gate below, which ran
  with fastmem active).

## 3. Cross-platform golden-trace gate — PASS (all four)

The macOS-recorded golden traces replay **bit-exactly** on the A55 device
(600 frames: ARM9/ARM7 R0–R15+CPSR, timestamps, full MainRAM hash, both
framebuffer hashes per frame). All exit 0:

| device build | golden trace                 | result |
|--------------|------------------------------|--------|
| baseline     | shrek-600.trace              | OK, 600/600 identical |
| link         | shrek-600.trace              | OK, 600/600 identical |
| es           | shrek-600-eventslices.trace  | OK, 600/600 identical |
| full         | shrek-600-eventslices.trace  | OK, 600/600 identical |

No platform-dependent nondeterminism. This also validates the M3 Tier A
memory inlines and the Android fastmem path (active during these runs) as
bit-exact on in-order silicon.

## 4. Benchmark matrix

Methodology: 600-frame runs, 3 reps per cell, **median** reported. Runs were
interleaved across configs (one full pass over all cells per rep, never the
same config back-to-back) with a 30 s cooldown between runs. SoC temperature
and A55 clock sampled before every run: temperature stayed **45.0–47.8 °C**
and the clock stayed pinned at **1.992 GHz for all 63 runs — no thermal
throttling observed.** Max spread within any cell: 3.6 %. Raw data:
`tools/android-bench/raw-rgds.tsv`.

### Median FPS (RG DS, Cortex-A55)

| config   | fm off, fs0 | fm on, fs0 | fm off, fs9 | fm on, fs9 |
|----------|------------:|-----------:|------------:|-----------:|
| interp (scale) | 36.02 | —     | —      | —      |
| baseline | 76.79 | 76.75 | 122.21 | 121.91 |
| dispatch | 77.84 | 76.86 | 126.00 | 122.18 |
| link     | 76.72 | 79.01 | 124.29 | 126.16 |
| es       | 83.98 | 84.70 | 139.99 | 146.31 |
| full     | 87.17 | 86.44 | 148.51 | 146.02 |

(fm = fastmem, fs = frameskip. fs9 = render 1 of every 10 frames.)

### Per-stage deltas, device vs host

Host = Apple Silicon (M-series), same harness/ROM/600f, single runs from the
committed host build dirs (fastmem unsupported on macOS; frameskip 0):
baseline 1046.9 → dispatch 1065.2 → link 1065.6 → es 1186.4 → full 1254.0 FPS.

Stage-over-stage gain (each vs the previous stage):

| stage added        | host fs0 | A55 fm off fs0 | A55 fm on fs0 | A55 fm off fs9 | A55 fm on fs9 |
|--------------------|---------:|---------------:|--------------:|---------------:|--------------:|
| dispatch           | +1.8 %   | +1.4 %         | +0.1 %        | +3.1 %         | +0.2 %        |
| linking            | +0.0 %   | −1.4 %         | +2.8 %        | −1.4 %         | +3.3 %        |
| event slices       | +11.3 %  | +9.5 %         | +7.2 %        | +12.6 %        | +16.0 %       |
| mem Tier A         | +5.7 %   | +3.8 %         | +2.1 %        | +6.1 %         | −0.2 %        |
| **full vs baseline** | **+19.8 %** | **+13.5 %** | **+12.6 %** | **+21.5 %** | **+19.8 %** |

### Bottom line (baseline → full stack)

| fastmem | frameskip | baseline | full  | gain |
|---------|-----------|---------:|------:|-----:|
| off     | 0         | 76.79    | 87.17 | **+13.5 %** |
| on      | 0         | 76.75    | 86.44 | **+12.6 %** |
| off     | 9         | 122.21   | 148.51 | **+21.5 %** |
| on      | 9         | 121.91   | 146.02 | **+19.8 %** |

## 5. What the numbers say

1. **Event slices are the dominant win on A55, exactly as on the host.** U5
   contributes 7–16 % alone, and its share grows at frameskip 9 (when the run
   is more CPU-bound) — mirroring the host's +13.5 % overall / +23.2 %
   CPU-bound split. The scheduler-iteration overhead (9424 → 3168
   iterations/frame) is the architecture-level cost that dominates, not the
   per-block dispatch instructions.
2. **Dispatch/link alone are near-flat on A55 too** (±3 %, several cells within
   noise). The "dispatch-cost dominance on in-order cores" thesis in its
   strong form — that the C++ dispatch round-trip would be *proportionally
   more* expensive on an in-order A55 than on a wide OoO M-series — is **not
   supported at this stage granularity**: A55 stage deltas track host deltas
   closely. The dispatcher's value on both platforms is as the *enabler* for
   longer slices (U5), per Appendix C.2.2.
3. **Fastmem is a wash on this workload/core** (on vs off within ~1–3 % in
   every config, direction inconsistent). With Tier A inlines covering DTCM
   block transfers and MainRAM u32 loads, the mmap-trap fastmem path buys
   nothing measurable on Shrek on A55 — relevant for the app port, since
   fastmem carries signal-handler complexity and address-space cost on
   Android.
4. **Mem Tier A pays 2–6 % on A55** — comparable to the host's +4.4 %; the
   A55's simpler load pipeline does not amplify the guard-elimination win
   beyond what the host showed for this ROM. (The plan's "DTCM block-LOAD
   inlining is the specific Android win" expectation is *met but not
   exceeded*.)
5. **Absolute scale:** the RG DS runs Shrek at 76 → 87 FPS (full stack,
   frameskip 0) and 148 FPS at frameskip 9, i.e. comfortable full speed
   (>60 FPS) headroom on a single A55 core with the software renderer. Interp
   is 36 FPS — the JIT is a hard requirement on this class of device.

## 6. Device quirks

- adb shell (non-root, `shell` domain) can read `/sys/class/thermal/*` and
  `cpufreq` state; `/data/local/tmp` is fully usable as a workspace; rwx anon
  mmap and ashmem both work. No root needed for any part of this unit.
- Benign `ftruncate` EINVAL log line on fastmem init (see §2).
- No thermal throttling in ~40 min of interleaved runs at 45–48 °C SoC —
  either the box's passive cooling is adequate for single-core loads, or the
  fan/thermal budget is generous. Multi-core app-port loads may differ.
- Scheduling: FPS spread ≤3.6 % per cell with no pinning (`taskset` exists on
  the device if tighter control is wanted; all 4 cores are identical A55 so
  big.LITTLE migration noise does not apply).

## 7. Recommended next steps for the full app port

1. Ship the **full** config (dispatch+link+ES+mem) as the Android core; it is
   bit-exact against the golden traces and +13–22 % on A55.
2. **Do not block the port on fastmem**: default it off (or make it a hidden
   toggle) until a workload where it wins is demonstrated; it simplifies the
   signal-handler story inside an app process (seccomp, JNI, GC threads).
3. Frameskip is the biggest single lever on this hardware (+60 % at fs9);
   surface the LITEV_AGGRESSIVE_SKIP runtime target in the app UI.
4. Re-run this matrix with the app's real render path (GL/Vulkan present) —
   the headless numbers exclude present/composition cost, which on RK3568
   may shift the CPU/GPU balance and re-weight the ES win.
5. Wire `tools/android-bench/run.sh` into CI-on-device (it is already
   idempotent over adb) and extend the ROM set once B.4 test ROMs land.
