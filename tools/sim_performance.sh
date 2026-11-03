#!/usr/bin/env bash
# Open the game in the performance simulator's desktop app (Tauri GUI): the game screen at device
# speed with the live frame-time panel, hotspots and flame view. Keys: arrows = d-pad, X = A,
# Z = B, space = play/pause, . = step one frame; the mouse wheel turns the crank.
#
#   tools/sim_performance.sh [--no-build]
#
# Builds the device pdex.elf (arm-none-eabi-gcc), then runs `tauri dev` on Source/. For a headless
# report use tools/check_performance.sh. The app uses its built-in (estimated) calibration.
#
# Needs the Playdate SDK (PLAYDATE_SDK_PATH, default ~/Developer/PlaydateSDK-3.1.2), node/npm, and a
# pdsim checkout (PDSIM_DIR, default ~/Code/playdate-performance-simulator).
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD=1
while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) BUILD=0; shift ;;
        -h|--help) sed -n 2,11p "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
PDSIM_DIR="${PDSIM_DIR:-$HOME/Code/playdate-performance-simulator}"
GAME="$PWD/Source"

[ -d "$PDSIM_DIR/app" ] || { echo "sim_performance: pdsim app not found at $PDSIM_DIR/app (set PDSIM_DIR)" >&2; exit 1; }
command -v npm >/dev/null || { echo "sim_performance: npm not found" >&2; exit 1; }
command -v arm-none-eabi-gcc >/dev/null ||
    { echo "sim_performance: arm-none-eabi-gcc not found (Debian/Ubuntu: sudo apt install gcc-arm-none-eabi)" >&2; exit 1; }

if [ "$BUILD" -eq 1 ]; then
    echo "== building device pdex.elf"
    log=$(mktemp)
    make device_bin >"$log" 2>&1 || { cat "$log" >&2; exit 1; }
    rm -f "$log"
fi
[ -f Source/pdex.elf ] || { echo "sim_performance: Source/pdex.elf missing (run without --no-build)" >&2; exit 1; }

cd "$PDSIM_DIR/app"
[ -d node_modules ] || npm install
echo "== launching the performance simulator"
exec npx tauri dev -- -- "$GAME"
