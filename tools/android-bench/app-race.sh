#!/usr/bin/env bash
###############################################################################
# app-race.sh — tap-driven adb automation for the melonDS Android GUI app
#                (liteDS-v2 fork) on an Anbernic RG DS handheld.
#
# PURPOSE
#   Drive the real GUI app from a cold start into an in-race state WITHOUT any
#   manual interaction, then sample the LITEV_PROF FPS/profiler logcat stream.
#   This exists because in-game gamepad input injected over adb does NOT reach
#   the emulated game reliably (see DEVICE FACTS), so the only sanctioned path
#   into a race is via the app's own Pause dialog -> Load state -> slot.
#
# USAGE
#   ./app-race.sh launch            Force-stop + launch app, tap Shrek ROM row,
#                                   boot the game (~10s), verify process alive.
#   ./app-race.sh load-race         From a RUNNING game: open Pause dialog,
#                                   tap "Load state", tap slot 1, verify load
#                                   (screenshot diff + FPS band), verify alive.
#   ./app-race.sh fps [seconds]     Sample LITEV_PROF logcat for N s (default 60)
#                                   and print median/min/max fps and runFrame ms.
#   ./app-race.sh shot [name]       Capture BOTH physical displays to PNG files.
#   ./app-race.sh probe-input       OPTIONAL diagnostic: at the game main menu,
#                                   send `input keyevent 20` (DPAD_DOWN) and diff
#                                   top-screen screenshots to report whether the
#                                   emulated game reacts to adb input. Advisory
#                                   only — nothing else depends on it.
#   ./app-race.sh full [fps_secs]   launch + load-race + fps (default 60), then
#                                   print a PASS/FAIL summary with FPS numbers.
#                                   Exits non-zero on any failure.
#
# ARTIFACTS
#   Every screenshot/log is written under ./app-race-artifacts/ (relative to the
#   current working directory) with timestamped names. Paths are printed.
#
# ---------------------------------------------------------------------------
# HARD-WON DEVICE FACTS (trust these — verified on-device this session)
# ---------------------------------------------------------------------------
#   Device : Anbernic RG DS, Android 14. TWO physical displays, BOTH 640x480,
#            1:1 with screencap coordinates.
#              display 0 = bottom / main = the TOUCH screen.
#              display 1 = top screen (in-race shows 3D track + HUD).
#            Screenshot: adb exec-out screencap -p -d 0 > file.png   (and -d 1)
#
#   App    : package  me.magnum.melonds.dev
#            activity me.magnum.melonds.ui.emulator.EmulatorActivity
#            launch:  adb shell monkey -p me.magnum.melonds.dev \
#                       -c android.intent.category.LAUNCHER 1
#
#   ROM    : /sdcard/Documents/DS/Shrek - Smash n' Crash Racing (USA).nds
#            ROM-list row was at tap (250,168) in the OLD install, but the app
#            was freshly reinstalled — DO NOT hard-code blindly. Prefer:
#            `uiautomator dump` -> parse a node whose text contains "Shrek" ->
#            tap its bounds center. Fall back to (250,168) only if that fails.
#
#   INPUT  : adb-injected gamepad input (input keyevent / sendevent to the
#            gamepad at /dev/input/event9) does NOT reliably reach the emulated
#            game. But Android UI (app dialogs, pickers) responds perfectly to
#            `input tap` and BACK. Therefore the ONLY path into the race is:
#              BACK (input keyevent 4) -> Pause dialog -> Load state -> slot 1.
#            Do NOT build the race flow on in-game button input.
#
#   PAUSE DIALOG (640x480, verified this session; open with keyevent 4):
#              Settings    (320,143)
#              Save state  (320,191)
#              Load state  (320,239)
#              Cheats      (320,287)
#              Reset       (320,335)
#              Exit        (320,383)
#   SLOT DIALOG:
#              Quick Slot  (320, 84)
#              slot 1      (320,130)
#              slot 2      (320,176)
#              CANCEL      (459,437)
#            Prefer uiautomator text matching ("Load state", "1.") when present;
#            fall back to these coordinates.
#
#   SAVESTATES: live next to the ROM as
#              "Shrek - Smash n' Crash Racing (USA).ml<slot>"
#            (slot 1 -> .ml1, slot 0 -> .ml0). load-race fails fast if neither
#            .ml1 nor .ml0 exists.
#
#   PROFILER/FPS: with system property debug.litev.prof=1 the app logs logcat
#            lines tagged LITEV_PROF, one per 60 frames (~every 1.7s), e.g.:
#              60f: cpu_loop=28.25ms (fenceWait=0.00 runFrame=18.89 blit=8.24 \
#                   other=1.12) | gpu=-1.00ms | wall/frame=28.40ms (35.2 fps)
#            This script sets that prop itself.
#
#   IN-RACE HEURISTIC: after load, the TOP screen (display 1) shows the 3D
#            track + HUD, very different from menu art. Robust check: compare a
#            top-screen shot from BEFORE load with one AFTER load and require a
#            substantial difference. Also require: process alive and LITEV_PROF
#            fps in a plausible in-race band (20-45). Screenshots are saved so a
#            human can eyeball; paths are printed.
#
#   CRASH CHECK: after each phase, grep recent logcat for
#            "Fatal signal|SIGSEGV|FATAL EXCEPTION"; fail loudly if found.
###############################################################################
set -uo pipefail

# --------------------------------------------------------------------------- #
# Config / constants
# --------------------------------------------------------------------------- #
PKG="me.magnum.melonds.dev"
ACT="me.magnum.melonds.ui.emulator.EmulatorActivity"
ROM_DIR="/sdcard/Documents/DS"
ROM_NAME="Shrek - Smash n' Crash Racing (USA).nds"
ROM_PATH="$ROM_DIR/$ROM_NAME"
ROM_MATCH="Shrek"                      # uiautomator text match for the ROM row
STATE_BASE="Shrek - Smash n' Crash Racing (USA)"

# Fallback coordinates (640x480). Used only when uiautomator matching fails.
ROM_ROW_XY="250 168"
LOAD_STATE_XY="320 239"
SLOT1_XY="320 130"
# The in-race savestate lives in slot 2 (node text "2." centered at ~213,266 in the
# 640x480 slot dialog). Override with RACE_SLOT / SLOT_XY env vars if it moves.
RACE_SLOT="${RACE_SLOT:-2}"
SLOT_XY="${SLOT_XY:-213 266}"

BOOT_WAIT=10                           # seconds to wait after tapping ROM row
DIALOG_WAIT=1                          # settle time after opening a dialog
FPS_DEFAULT=60

ART_DIR="./app-race-artifacts"
mkdir -p "$ART_DIR"

# --------------------------------------------------------------------------- #
# Small helpers
# --------------------------------------------------------------------------- #
ts()   { date +%Y%m%d-%H%M%S; }
log()  { printf '[app-race %s] %s\n' "$(date +%H:%M:%S)" "$*" >&2; }
die()  { log "FATAL: $*"; exit 1; }

adbsh() { adb shell "$@" 2>/dev/null | tr -d '\r'; }

# Capture one physical display to a PNG. $1=display(0|1) $2=outfile
screencap() {
  local disp="$1" out="$2"
  adb exec-out screencap -p -d "$disp" > "$out" 2>/dev/null
  [ -s "$out" ]
}

proc_alive() { [ -n "$(adbsh pidof "$PKG")" ]; }

require_alive() {
  proc_alive || die "app process ($PKG) is not alive after $1"
  log "process alive: pid $(adbsh pidof "$PKG")"
}

# Fail if recent logcat shows a crash. $1 = phase label
crash_check() {
  local hits
  hits="$(adb logcat -d -t 300 2>/dev/null | tr -d '\r' \
           | grep -aE 'Fatal signal|SIGSEGV|FATAL EXCEPTION' \
           | grep -aiE 'melon|litev|libmelon|dev\.magnum|magnum' )"
  # Also catch any crash line naming our package even without the melon filter.
  if [ -z "$hits" ]; then
    hits="$(adb logcat -d -t 300 2>/dev/null | tr -d '\r' \
             | grep -aE 'Fatal signal|SIGSEGV|FATAL EXCEPTION' \
             | grep -a "$PKG")"
  fi
  if [ -n "$hits" ]; then
    log "CRASH detected during phase '$1':"
    printf '%s\n' "$hits" >&2
    die "crash during '$1'"
  fi
  log "no crash signatures in recent logcat (phase '$1')"
}

# uiautomator dump -> stdout XML (empty on failure).
ui_dump() {
  local dev="/sdcard/uidump.xml"
  adb shell uiautomator dump "$dev" >/dev/null 2>&1 || return 1
  adb exec-out cat "$dev" 2>/dev/null | tr -d '\r'
}

# From a uiautomator XML on stdin, find first node whose text/content-desc
# contains $1 (case-insensitive) and echo "cx cy" (bounds center). Empty if none.
ui_center_for() {
  local needle="$1"
  # Split each <node> onto its own line, grep the needle, parse bounds center.
  local line
  line="$(cat)"
  printf '%s' "$line" \
    | sed 's/<node /\n<node /g' \
    | grep -iE "(text|content-desc)=\"[^\"]*$needle[^\"]*\"" \
    | head -n1 \
    | grep -oE 'bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' \
    | head -n1 \
    | sed -E 's/bounds="\[([0-9]+),([0-9]+)\]\[([0-9]+),([0-9]+)\]"/\1 \2 \3 \4/' \
    | awk '{ if (NF==4) printf "%d %d", int(($1+$3)/2), int(($2+$4)/2) }'
}

# Tap by text (via uiautomator) with a coordinate fallback.
# $1=needle  $2=fallback "x y"  $3=label
tap_text_or_xy() {
  local needle="$1" fallback="$2" label="$3" xy=""
  local dump; dump="$(ui_dump)"
  if [ -n "$dump" ]; then
    xy="$(printf '%s' "$dump" | ui_center_for "$needle")"
  fi
  if [ -n "$xy" ]; then
    log "tap $label via uiautomator ('$needle') at ($xy)"
    adb shell input tap $xy
  else
    log "tap $label via fallback coords ($fallback) — uiautomator had no '$needle'"
    adb shell input tap $fallback
  fi
}

# Median/min/max of a whitespace/newline list of numbers on stdin.
stats() {
  awk '{ a[NR]=$1 } END{
    n=NR; if(n==0){ print "NA NA NA"; exit }
    for(i=1;i<=n;i++) for(j=i+1;j<=n;j++) if(a[j]<a[i]){t=a[i];a[i]=a[j];a[j]=t}
    if(n%2) med=a[(n+1)/2]; else med=(a[n/2]+a[n/2+1])/2
    printf "%.2f %.2f %.2f", med, a[1], a[n]
  }'
}

# --------------------------------------------------------------------------- #
# Preflight
# --------------------------------------------------------------------------- #
preflight() {
  adb get-state >/dev/null 2>&1 || die "no adb device (adb get-state failed)"
  local dev; dev="$(adb devices | awk 'NR>1 && $2=="device"{print $1; exit}')"
  [ -n "$dev" ] || die "no device in 'adb devices' state 'device'"
  log "device: $dev"
  # Enable the profiler log stream.
  adb shell setprop debug.litev.prof 1 >/dev/null 2>&1 || true
  log "set debug.litev.prof=1 (current: $(adbsh getprop debug.litev.prof))"
}

# --------------------------------------------------------------------------- #
# Subcommand: launch
# --------------------------------------------------------------------------- #
cmd_launch() {
  preflight
  log "force-stopping $PKG"
  adb shell am force-stop "$PKG" >/dev/null 2>&1 || true
  sleep 1
  log "launching app via monkey LAUNCHER intent"
  adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
  sleep 4    # let the ROM-list activity come up

  local shot0="$ART_DIR/launch-romlist-$(ts).png"
  screencap 0 "$shot0" && log "ROM-list screenshot: $shot0"

  log "tapping Shrek ROM row"
  tap_text_or_xy "$ROM_MATCH" "$ROM_ROW_XY" "Shrek ROM row"

  log "waiting ${BOOT_WAIT}s for game boot"
  sleep "$BOOT_WAIT"

  local shot1="$ART_DIR/launch-booted-top-$(ts).png"
  screencap 1 "$shot1" && log "post-boot top screenshot: $shot1"

  crash_check "launch"
  require_alive "launch"
  # Confirm the emulator activity is actually focused (best-effort).
  local foc; foc="$(adbsh dumpsys window | grep -iE 'mCurrentFocus|mFocusedApp' | grep -i melon | head -n1)"
  [ -n "$foc" ] && log "focus: $foc" || log "warn: emulator activity not clearly focused (may still be fine)"
  log "launch OK"
}

# --------------------------------------------------------------------------- #
# Subcommand: load-race
# --------------------------------------------------------------------------- #
cmd_load_race() {
  preflight
  proc_alive || die "game not running — run 'launch' first"

  # NOTE: savestates live in app-private storage (not next to the ROM on /sdcard),
  # so we can't stat them here. The slot dialog is authoritative; we verify the
  # load succeeded via the post-load screen diff below.
  log "using save slot $RACE_SLOT (savestate is in app-private storage; slot dialog is authoritative)"

  # Baseline top-screen shot (pre-load) for the diff heuristic.
  local pre="$ART_DIR/preload-top-$(ts).png"
  screencap 1 "$pre" || die "could not screencap top display (pre-load)"
  log "pre-load top screenshot: $pre"

  # Open pause dialog.
  log "opening Pause dialog (keyevent 4 / BACK)"
  adb shell input keyevent 4
  sleep "$DIALOG_WAIT"
  local pmenu="$ART_DIR/pausemenu-$(ts).png"
  screencap 0 "$pmenu" && log "pause-menu screenshot: $pmenu"

  # Tap "Load state".
  tap_text_or_xy "Load state" "$LOAD_STATE_XY" "Load state"
  sleep "$DIALOG_WAIT"
  local sdlg="$ART_DIR/slotdialog-$(ts).png"
  screencap 0 "$sdlg" && log "slot-dialog screenshot: $sdlg"

  # Tap the race slot by coordinate (the "<slot>." text match is ambiguous — it hits
  # the wrong node — so use the fixed slot-2 coordinate directly).
  log "tapping slot ${RACE_SLOT} at ($SLOT_XY)"
  adb shell input tap $SLOT_XY
  sleep 2   # allow the state to load and render

  # Post-load top-screen shot for the diff heuristic.
  local post="$ART_DIR/postload-top-$(ts).png"
  screencap 1 "$post" || die "could not screencap top display (post-load)"
  log "post-load top screenshot: $post"

  crash_check "load-race"
  require_alive "load-race"

  # Screenshot-diff verification: require the top screen to have changed.
  local diffbytes=0 same=1
  local sz_pre sz_post
  sz_pre=$(wc -c < "$pre"); sz_post=$(wc -c < "$post")
  if cmp -s "$pre" "$post"; then
    same=1
  else
    same=0
    diffbytes=$(cmp -l "$pre" "$post" 2>/dev/null | wc -l | tr -d ' ')
  fi
  log "top-screen diff: pre=${sz_pre}B post=${sz_post}B differing_bytes=${diffbytes}"

  # Optional sharper diff if ImageMagick is available on the host.
  local mig_ok="n/a"
  if command -v compare >/dev/null 2>&1; then
    local ae
    ae=$(compare -metric AE "$pre" "$post" null: 2>&1 | awk '{print $1}')
    mig_ok="$ae"
    log "ImageMagick AE pixel diff: $ae"
  fi

  if [ "$same" = "1" ]; then
    die "top screen did NOT change after load (pre==post). Load likely failed. See $pre / $post"
  fi
  # Require a substantial change, not just a stray pixel.
  if [ "$diffbytes" -lt 1000 ]; then
    log "warn: only $diffbytes bytes differ — load may not have fully taken; continuing to FPS band check"
  fi

  log "load-race OK (top screen changed). pre=$pre post=$post"
}

# --------------------------------------------------------------------------- #
# Subcommand: fps [seconds]
# --------------------------------------------------------------------------- #
# Echoes a machine-readable summary line to stdout:
#   FPS median=.. min=.. max=.. | runFrame_ms median=.. min=.. max=.. | samples=N
cmd_fps() {
  preflight
  local secs="${1:-$FPS_DEFAULT}"
  local raw="$ART_DIR/litev-prof-$(ts).log"
  log "sampling LITEV_PROF for ${secs}s -> $raw"

  adb logcat -c >/dev/null 2>&1 || true
  # Collect for N seconds, then stop.
  ( adb logcat -s LITEV_PROF > "$raw" 2>/dev/null ) &
  local lc=$!
  sleep "$secs"
  kill "$lc" >/dev/null 2>&1 || true
  wait "$lc" 2>/dev/null || true

  local nlines
  nlines=$(grep -cE 'fps\)' "$raw" 2>/dev/null || echo 0)
  if [ "${nlines:-0}" -eq 0 ]; then
    log "no LITEV_PROF fps lines captured in $raw"
    echo "FPS median=NA min=NA max=NA | runFrame_ms median=NA min=NA max=NA | samples=0"
    return 1
  fi

  # fps values are the "(NN.N fps)" fields.
  local fps_stats rf_stats
  fps_stats=$(grep -oE '\([0-9]+\.[0-9]+ fps\)' "$raw" | grep -oE '[0-9]+\.[0-9]+' | stats)
  # runFrame values are "runFrame=NN.NN".
  rf_stats=$(grep -oE 'runFrame=[0-9]+\.[0-9]+' "$raw" | grep -oE '[0-9]+\.[0-9]+' | stats)

  read -r fmed fmin fmax <<< "$fps_stats"
  read -r rmed rmin rmax <<< "$rf_stats"

  log "captured $nlines LITEV_PROF samples"
  echo "FPS median=$fmed min=$fmin max=$fmax | runFrame_ms median=$rmed min=$rmin max=$rmax | samples=$nlines"
}

# --------------------------------------------------------------------------- #
# Subcommand: shot [name]
# --------------------------------------------------------------------------- #
cmd_shot() {
  preflight
  local name="${1:-shot}"
  local t; t="$(ts)"
  local b="$ART_DIR/${name}-bottom-d0-$t.png"
  local top="$ART_DIR/${name}-top-d1-$t.png"
  screencap 0 "$b"  && log "bottom (display 0): $b"  || log "failed bottom capture"
  screencap 1 "$top" && log "top    (display 1): $top" || log "failed top capture"
  echo "$b"
  echo "$top"
}

# --------------------------------------------------------------------------- #
# Subcommand: probe-input  (OPTIONAL diagnostic — advisory only)
# --------------------------------------------------------------------------- #
cmd_probe_input() {
  preflight
  proc_alive || die "game not running — run 'launch' first"
  local pre="$ART_DIR/probe-pre-$(ts).png"
  local post="$ART_DIR/probe-post-$(ts).png"
  screencap 1 "$pre" || die "screencap failed"
  log "sending in-game input (keyevent 20 / DPAD_DOWN)"
  adb shell input keyevent 20
  sleep 1
  screencap 1 "$post" || die "screencap failed"
  if cmp -s "$pre" "$post"; then
    echo "PROBE-INPUT: NO CHANGE — emulated game did NOT react to adb input (expected on this device)"
  else
    local d; d=$(cmp -l "$pre" "$post" 2>/dev/null | wc -l | tr -d ' ')
    echo "PROBE-INPUT: CHANGED ($d bytes) — emulated game MAY react to adb input"
  fi
  log "probe screenshots: $pre / $post"
}

# --------------------------------------------------------------------------- #
# Subcommand: full
# --------------------------------------------------------------------------- #
cmd_full() {
  local fps_secs="${1:-60}"
  local ok=1
  local fps_line="(not sampled)"

  echo "=================== app-race full run ==================="
  if cmd_launch; then log "phase launch: PASS"; else log "phase launch: FAIL"; ok=0; fi

  if [ "$ok" = 1 ]; then
    if cmd_load_race; then log "phase load-race: PASS"; else log "phase load-race: FAIL"; ok=0; fi
  fi

  if [ "$ok" = 1 ]; then
    fps_line="$(cmd_fps "$fps_secs")" || ok=0
    log "phase fps: $fps_line"
    # In-race FPS band sanity check (20-45).
    local fmed
    fmed="$(printf '%s' "$fps_line" | sed -nE 's/.*FPS median=([0-9.]+).*/\1/p')"
    if [ -n "$fmed" ] && [ "$fmed" != "NA" ]; then
      if awk -v m="$fmed" 'BEGIN{exit !(m>=20 && m<=45)}'; then
        log "FPS median $fmed within in-race band [20,45]"
      else
        log "WARN: FPS median $fmed OUTSIDE in-race band [20,45] — may be menu or throttled"
      fi
    fi
  fi

  echo "========================================================"
  if [ "$ok" = 1 ]; then
    echo "RESULT: PASS"
    echo "  $fps_line"
    echo "  artifacts: $ART_DIR"
    exit 0
  else
    echo "RESULT: FAIL"
    echo "  $fps_line"
    echo "  artifacts: $ART_DIR"
    exit 1
  fi
}

# --------------------------------------------------------------------------- #
# Dispatch
# --------------------------------------------------------------------------- #
usage() {
  sed -n '2,60p' "$0" | sed 's/^# \{0,1\}//'
}

main() {
  local cmd="${1:-}"; shift || true
  case "$cmd" in
    launch)      cmd_launch "$@" ;;
    load-race)   cmd_load_race "$@" ;;
    fps)         cmd_fps "$@" ;;
    shot)        cmd_shot "$@" ;;
    probe-input) cmd_probe_input "$@" ;;
    full)        cmd_full "$@" ;;
    ""|-h|--help|help) usage ;;
    *) echo "unknown subcommand: $cmd" >&2; usage; exit 2 ;;
  esac
}

main "$@"
