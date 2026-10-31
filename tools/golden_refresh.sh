#!/usr/bin/env bash
# Rebuild the scenario .pdx and (re)capture the real-simulator goldens.
#
#   tools/golden_refresh.sh [--on-current-display] [scenario ...]
#
# With no scenario names, runs every tests/scenarios/*.txt that contains a `snap`
# line. `typo` (a deliberate parse failure) is always skipped. Outputs land in
# tests/golden/real/<scenario>/ (NAME.fb, NAME.state; done.marker is dropped).
#
# The simulator runs headless on a scratch Xvfb it starts itself (XVFB_BIN, else
# xvfb / Xvfb on PATH). WAYLAND_DISPLAY is always unset and DISPLAY is always
# overridden, so nothing can open on your desktop. --on-current-display skips Xvfb
# and uses the inherited DISPLAY instead (you will see the window).
#
# Needs the Playdate SDK (PLAYDATE_SDK_PATH, default ~/Developer/PlaydateSDK-3.1.2).
# The simulator's Data dir for the game is $SDK/Disk/Data/<bundleID> (printed below);
# it is wiped before every scenario.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$PWD
TIMEOUT=120
ON_CURRENT=0
NAMES=()
while [ $# -gt 0 ]; do
    case "$1" in
        --on-current-display) ON_CURRENT=1; shift ;;
        -h|--help) sed -n 2,16p "$0"; exit 0 ;;
        -*) echo "golden_refresh: unknown option: $1" >&2; exit 2 ;;
        *) NAMES+=("$1"); shift ;;
    esac
done

export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
SIM="$PLAYDATE_SDK_PATH/bin/PlaydateSimulator"
PDX="$ROOT/SailingSimScenario.pdx"
[ -x "$SIM" ] || { echo "golden_refresh: simulator not found at $SIM" >&2; exit 1; }

BUNDLE=$(sed -n 's/^bundleID=//p' Source/pdxinfo | head -n 1)
[ -n "$BUNDLE" ] || { echo "golden_refresh: no bundleID in Source/pdxinfo" >&2; exit 1; }
# $DATA is rm -rf'd before every scenario, so be strict about what it can be.
if ! printf '%s\n' "$BUNDLE" | grep -Eq '^[A-Za-z0-9._-]+$' || [ "$BUNDLE" = . ] || [ "$BUNDLE" = .. ]; then
    echo "golden_refresh: refusing odd bundleID '$BUNDLE' in Source/pdxinfo" >&2; exit 1
fi
DATA_ROOT=$(realpath -m "$PLAYDATE_SDK_PATH/Disk/Data")
DATA=$(realpath -m "$DATA_ROOT/$BUNDLE")
case "$DATA" in
    "$DATA_ROOT"/?*) ;;
    *) echo "golden_refresh: refusing Data path '$DATA' (not under $DATA_ROOT)" >&2; exit 1 ;;
esac
[ "$(dirname "$DATA")" = "$DATA_ROOT" ] || { echo "golden_refresh: refusing Data path '$DATA'" >&2; exit 1; }
echo "== simulator Data dir: $DATA"

# Scenario list
if [ ${#NAMES[@]} -eq 0 ]; then
    for f in tests/scenarios/*.txt; do
        n=$(basename "$f" .txt)
        [ "$n" = typo ] && continue
        grep -Eq '^[[:space:]]*snap([[:space:]]|$)' "$f" || continue
        NAMES+=("$n")
    done
else
    for n in "${NAMES[@]}"; do
        [ "$n" = typo ] && { echo "golden_refresh: 'typo' is a deliberate parse failure; not capturing it" >&2; exit 2; }
        [ -f "tests/scenarios/$n.txt" ] || { echo "golden_refresh: no tests/scenarios/$n.txt" >&2; exit 2; }
    done
fi
[ ${#NAMES[@]} -gt 0 ] || { echo "golden_refresh: no scenarios with a snap" >&2; exit 1; }

# Display: our own scratch Xvfb, or the current one on request.
XVFB_PID=""
SIM_PID=""
LOG=$(mktemp)
# The simulator may ignore SIGTERM, so escalate; never wait on it unbounded.
stop_sim() {
    [ -n "$SIM_PID" ] || return 0
    kill "$SIM_PID" 2>/dev/null || true
    for _ in $(seq 1 25); do
        kill -0 "$SIM_PID" 2>/dev/null || break
        sleep 0.2
    done
    kill -9 "$SIM_PID" 2>/dev/null || true
    wait "$SIM_PID" 2>/dev/null || true
    SIM_PID=""
}
cleanup() {
    local rc=$?
    stop_sim
    if [ -n "$XVFB_PID" ]; then kill "$XVFB_PID" 2>/dev/null || true; fi
    rm -f "$LOG"
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

if [ "$ON_CURRENT" -eq 1 ]; then
    [ -n "${DISPLAY:-}" ] || { echo "golden_refresh: --on-current-display needs DISPLAY set" >&2; exit 2; }
    SIM_DISPLAY=$DISPLAY
    echo "== using current display $SIM_DISPLAY (the simulator window will be visible)"
else
    XVFB="${XVFB_BIN:-}"
    [ -n "$XVFB" ] || XVFB=$(command -v xvfb 2>/dev/null || command -v Xvfb 2>/dev/null || true)
    [ -n "$XVFB" ] && [ -x "$XVFB" ] || { echo "golden_refresh: Xvfb not found (set XVFB_BIN)" >&2; exit 1; }
    n=99
    while [ -e "/tmp/.X$n-lock" ] || [ -e "/tmp/.X11-unix/X$n" ]; do
        n=$((n + 1))
        [ "$n" -lt 120 ] || { echo "golden_refresh: no free scratch display" >&2; exit 1; }
    done
    SIM_DISPLAY=":$n"
    "$XVFB" "$SIM_DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$LOG" 2>&1 &
    XVFB_PID=$!
    for _ in $(seq 1 50); do
        [ -e "/tmp/.X11-unix/X$n" ] && break
        kill -0 "$XVFB_PID" 2>/dev/null || { echo "golden_refresh: Xvfb died:" >&2; cat "$LOG" >&2; XVFB_PID=""; exit 1; }
        sleep 0.1
    done
    [ -e "/tmp/.X11-unix/X$n" ] || { echo "golden_refresh: Xvfb did not come up" >&2; exit 1; }
    echo "== Xvfb on $SIM_DISPLAY (pid $XVFB_PID)"
fi

echo "== building $(basename "$PDX")"
# Simulator target only: the SDK's default `all` also builds device_bin, which needs
# arm-none-eabi-gcc (absent on CI and not needed here). `simulator` builds pdex.so and runs pdc.
# The dylib rule tracks source timestamps only; the Makefile's variant stamp covers
# switching between the normal and scenario builds.
make SCENARIO=1 simulator
[ -d "$PDX" ] || { echo "golden_refresh: $PDX was not produced" >&2; exit 1; }

for name in "${NAMES[@]}"; do
    echo "== $name"
    rm -rf "$DATA"
    mkdir -p "$DATA"
    cp "tests/scenarios/$name.txt" "$PDX/scenario.txt"
    : >"$LOG"
    env -u WAYLAND_DISPLAY DISPLAY="$SIM_DISPLAY" GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy "$SIM" "$PDX" >"$LOG" 2>&1 &
    SIM_PID=$!
    waited=0
    while [ ! -e "$DATA/done.marker" ]; do
        if ! kill -0 "$SIM_PID" 2>/dev/null; then
            echo "golden_refresh: simulator exited early on $name:" >&2; cat "$LOG" >&2; SIM_PID=""; exit 1
        fi
        if [ "$waited" -ge $((TIMEOUT * 5)) ]; then
            echo "golden_refresh: timeout after ${TIMEOUT}s on $name (no done.marker in $DATA)" >&2
            cat "$LOG" >&2
            exit 1
        fi
        sleep 0.2
        waited=$((waited + 1))
    done
    stop_sim
    if grep -q '^error' "$DATA/done.marker" 2>/dev/null; then
        echo "golden_refresh: $name failed on the device side: $(cat "$DATA/done.marker")" >&2
        cat "$LOG" >&2
        exit 1
    fi

    out="tests/golden/real/$name"
    rm -rf "$out"
    mkdir -p "$out"
    (cd "$DATA" && find . -type f \( -name '*.fb' -o -name '*.state' \) -print0) |
        while IFS= read -r -d '' f; do
            mkdir -p "$out/$(dirname "$f")"
            cp "$DATA/$f" "$out/$f"
        done
    count=$(find "$out" -type f | wc -l)
    echo "   $count files -> $out"
    [ "$count" -gt 0 ] || { echo "golden_refresh: $name produced no snapshots" >&2; cat "$LOG" >&2; exit 1; }
done
rm -f "$PDX/scenario.txt"
echo "== done"
