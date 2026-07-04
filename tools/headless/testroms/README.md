# Test ROMs (not committed)

This directory holds third-party test ROMs used by the liteDS-v2 headless
oracle. ROM binaries are **git-ignored** (see `.gitignore` — `*.nds/.gba/.bin/
.srl`) and are **never committed**. The user authorized downloading and using
these ROMs locally; none has a license that clearly permits redistribution, so
only the *scripts, traces, and metadata* (this file) are committed.

To repopulate this directory on a fresh checkout, run the fetch commands in the
inventory below (and copy the Shrek game ROM from its local path).

## Scripted input (`--input-script`)

Menu-driven test ROMs need input to reach their tests. The headless runner
accepts `--input-script <file>`: newline directives `<frame> <keys>` meaning
"hold exactly these keys from this frame onward" (level, not edge). `<keys>` is
a comma list from `{A,B,SELECT,START,RIGHT,LEFT,UP,DOWN,R,L,X,Y}`, the word
`NONE`, or a `0x` hex pressed-mask (bit layout `0:A 1:B 2:SELECT 3:START
4:RIGHT 5:LEFT 6:UP 7:DOWN 8:R 9:L 10:X 11:Y`). The DS keymask is active-low;
the runner inverts for you. Scripts apply to every run mode (benchmark, record,
verify, converge). Because scripted input is part of the deterministic input, a
golden trace recorded with a script embeds the script's xxhash in its header and
`--verify-trace` warns + mismatches if replayed under a different/absent script.

Committed input scripts live in `../baselines/*.script`.

## Inventory

| ROM | Source URL | sha256 | License | Script (`../baselines/`) | Validates | Oracle result |
|---|---|---|---|---|---|---|
| `shrek.nds` | local: `/Users/shahmir/Documents/GitHub/quickmelonDS/.tmp-shrek.nds` | `7428867c…ec9e8` | commercial (Activision); user's own dump, local only | `shrek-menu-advance.script` (demo) | input-script feature proof (A on menu → "SELECT GAME TYPE") | A-press diverges FB hash vs no-script (proves scripted input reaches emulation); see also `shrek-600*.trace` |
| `armwrestler.nds` | https://raw.githubusercontent.com/Atem2069/armwrestler-fixed/main/armwrestler.nds | `1512de79…12531` | no LICENSE (all-rights-reserved); mic- ARMWrestler DS + Atem2069 hardware fixes | `armwrestler-arm-600.script` | ARM9 instructions: ARM ALU pt1, ALU pt2/MISC, ARM LDR/STR, ARM LDM/STM | interp: **all OK**. jit-full-stack: **byte-identical** final screen |
| `rockwrestler.nds` | https://raw.githubusercontent.com/RockPolish/rockwrestler/master/rockwrestler.nds | `f905e165…6069a` | no LICENSE (all-rights-reserved); RockPolish DS tester | `rockwrestler-600.script` | ARMv4 condition-code / instruction sub-tests (driven subset) | interp: **OK**. jit-full-stack: **byte-identical** final screen |

Full sha256:
- shrek:        `7428867c7447e73eaf2dc10653582a009b99cee019fc903c2084217e5ffec9e8`
- armwrestler:  `1512de7953f9e4eb8376f54e3476b2e01f0639465efb8fbf9b709a394e212531`
- rockwrestler: `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a`

Fetch commands (run from this directory):

```
cp /Users/shahmir/Documents/GitHub/quickmelonDS/.tmp-shrek.nds shrek.nds
curl -sL -o armwrestler.nds  https://raw.githubusercontent.com/Atem2069/armwrestler-fixed/main/armwrestler.nds
curl -sL -o rockwrestler.nds https://raw.githubusercontent.com/RockPolish/rockwrestler/master/rockwrestler.nds
```

## CPU-correctness oracle

For each test ROM, run to test-completion twice and compare the final screen:
reference = `--mode interp` (exact-timing build, e.g. `build-host`); candidate =
`--mode jit` with the **full flag stack** (`build-mem-es`:
`LITEV_JIT_DISPATCH + LINK_* + EVENT_SLICES + MEM_DTCM_BLOCK + MEM_MAINRAM_LOAD`).

```
# reference (interpreter)
build-host/liteDS-headless   --rom testroms/armwrestler.nds --frames 600 --mode interp \
    --input-script baselines/armwrestler-arm-600.script --fb-dump-ppm 599:/tmp/aw-interp.ppm
# candidate (JIT, full stack)
build-mem-es/liteDS-headless --rom testroms/armwrestler.nds --frames 600 --mode jit \
    --input-script baselines/armwrestler-arm-600.script --fb-dump-ppm 599:/tmp/aw-jit.ppm
cmp /tmp/aw-interp.ppm /tmp/aw-jit.ppm   # byte-identical
```

Results (frame 600, this branch):

| ROM | interp = reference passes? | jit-full-stack final screen | final_top / final_bot hash |
|---|---|---|---|
| armwrestler | yes — ARM ALU/LDR-STR/LDM-STM all **OK** | **byte-identical** to interp | `aff3a5a037969248` / `bcf2cf1d8d3b4974` |
| rockwrestler | yes — driven ARMv4 sub-tests **OK** | **byte-identical** to interp | `6086c6af0e759b1d` / `bcf2cf1d8d3b4974` |

No test flagged a failure under either mode for the driven subsets. (Upstream
melonDS is known to fail some ARMWrestler edge cases even on interp; none were
observed in the ARM ALU/LDR-STR/LDM-STM pages driven here.)

## Golden traces

Per-frame state traces (exact-timing config, `--record-trace`, double-recorded
byte-identical) are committed at `../baselines/<rom>.trace` + `.trace.json`.
Verify with the same script:

```
build-host/liteDS-headless --rom testroms/armwrestler.nds \
    --input-script baselines/armwrestler-arm-600.script \
    --verify-trace baselines/armwrestler-arm-600.trace
```

## Coverage gaps / TODO

- **ARMWrestler THUMB + ARM vsTE** suites (menu items 4–7) are not yet scripted;
  only the ARM instruction suite is driven.
- **Rockwrestler** ARMv5, IPC, DS MATH, MEMORY, INITIAL STATE, and the **ARM7**
  tab are reachable via its hierarchical menu but not yet scripted.
- **Arisotura/arm7wrestler** (melonDS author's dedicated ARM7 tester) is
  source-only — https://github.com/Arisotura/arm7wrestler — no prebuilt `.nds`;
  build with devkitARM if ARM7-specific coverage is needed.
- **B.4 commercial ROMs** still needed for the full compat oracle: Pokémon,
  NSMB, Mario Kart (the U5 GXFIFO-interleave watch item), WarioWare, and an SMC
  stressor. Only Shrek is locally available.
