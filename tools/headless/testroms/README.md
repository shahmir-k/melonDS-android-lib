# Test ROMs (not committed)

This directory holds third-party test ROMs used by the liteDS-v2 headless
oracle. ROM binaries are **git-ignored** (see `.gitignore`) — do not commit them
unless their license clearly permits redistribution.

## ARMWrestler (DS CPU tester, by Mic / "mic_")

Status for Unit 1: **NOT fetched / NOT committed.**

- Upstream source repo: https://github.com/mic-/armwrestler
- That repo contains only assembly source (`armwrestler-ds.asm`,
  `thumbwrestler-ds.asm`, `armwrestler-arm7.asm`) plus a build batch file and a
  linker script. There is **no prebuilt `.nds`** and **no GitHub release**.
- The repo has **no LICENSE file** (`license: null` via the GitHub API), i.e. it
  is all-rights-reserved by default. Redistribution is therefore not clearly
  permitted, so per the Unit 1 brief we did not download or commit a binary.
- ARM7 counterpart (separate project): https://github.com/Arisotura/arm7wrestler

To use it locally: build from the source repo with a devkitARM/GNU-ARM toolchain
(the repo's `makeawds.bat` assembles the `.asm` sources), drop the resulting
`armwrestler.nds` in this directory, then:

```
liteDS-headless --rom tools/headless/testroms/armwrestler.nds \
    --frames 600 --record-trace tools/headless/baselines/armwrestler-600.trace
```
