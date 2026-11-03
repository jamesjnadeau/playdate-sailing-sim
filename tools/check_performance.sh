#!/usr/bin/env bash
# Run the game in the Playdate performance simulator (pdsim), which charges work to a virtual
# 168 MHz Cortex-M7 clock, and report how long the frames would take on the device.
#
#   tools/check_performance.sh [--frames N] [--input script.json] [--profile rev-a|default]
#                              [--out DIR] [--max-over-budget N] [--max-avg-ms X] [--no-build]
#
# Builds the device pdex.elf (arm-none-eabi-gcc), then runs it with an input script that starts
# free range and sails (default tools/perf_free_sail.json). Writes report.json, frames.jsonl and
# screenshot.png to --out (default build/perf); look at the screenshot to confirm it reached sailing.
# Exits 1 if the game errors or a --max-* limit is exceeded.
#
# Needs the Playdate SDK (PLAYDATE_SDK_PATH, default ~/Developer/PlaydateSDK-3.1.2) and a pdsim
# checkout (PDSIM_DIR, default ~/Code/playdate-performance-simulator).
set -euo pipefail

cd "$(dirname "$0")/.."
FRAMES=600 INPUT=tools/perf_free_sail.json PROFILE=rev-a OUT=build/perf BUILD=1 LIMITS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --frames) FRAMES="$2"; shift 2 ;;
        --input) INPUT="$2"; shift 2 ;;
        --profile) PROFILE="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --max-over-budget|--max-avg-ms) LIMITS+=("$1" "$2"); shift 2 ;;
        --no-build) BUILD=0; shift ;;
        -h|--help) sed -n 2,13p "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
PDSIM_DIR="${PDSIM_DIR:-$HOME/Code/playdate-performance-simulator}"
PDSIM="$PDSIM_DIR/target/release/pdsim"
CAL="$PDSIM_DIR/calibration/$PROFILE.json"

[ -d "$PDSIM_DIR" ] || { echo "check_performance: pdsim not found at $PDSIM_DIR (set PDSIM_DIR)" >&2; exit 1; }
[ -f "$CAL" ] || { echo "check_performance: no calibration profile $CAL" >&2; exit 1; }
[ -f "$INPUT" ] || { echo "check_performance: input script $INPUT not found" >&2; exit 1; }
command -v arm-none-eabi-gcc >/dev/null ||
    { echo "check_performance: arm-none-eabi-gcc not found (Debian/Ubuntu: sudo apt install gcc-arm-none-eabi)" >&2; exit 1; }

if [ ! -x "$PDSIM" ]; then
    echo "== building pdsim"
    (cd "$PDSIM_DIR" && cargo build --release -p pdsim-cli)
fi

# The device build; the elf lands in Source/ next to the boats/config/tracks/ghosts it reads.
if [ "$BUILD" -eq 1 ]; then
    echo "== building device pdex.elf"
    log=$(mktemp)
    make device_bin >"$log" 2>&1 || { cat "$log" >&2; exit 1; }
    rm -f "$log"
fi
[ -f Source/pdex.elf ] || { echo "check_performance: Source/pdex.elf missing (run without --no-build)" >&2; exit 1; }

mkdir -p "$OUT"
echo "== running $FRAMES frames, profile $PROFILE"
status=0
"$PDSIM" run Source --frames "$FRAMES" --input "$INPUT" -q \
    --calibration "$CAL" \
    --report "$OUT/report.json" --frames-out "$OUT/frames.jsonl" --screenshot "$OUT/screenshot.png" \
    --data-dir "$(mktemp -d)" ${LIMITS[@]+"${LIMITS[@]}"} || status=$?

echo "== results in $OUT (report.json, frames.jsonl, screenshot.png)"
exit "$status"
