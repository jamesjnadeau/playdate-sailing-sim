#!/usr/bin/env bash
# Build SailingSim.pdx for the desktop simulator and open it.
#
#   tools/sim.sh [--no-run]
#
# Needs the Playdate SDK (PLAYDATE_SDK_PATH, default ~/Developer/PlaydateSDK-3.1.2).
set -euo pipefail

cd "$(dirname "$0")/.."
RUN=1
while [ $# -gt 0 ]; do
    case "$1" in
        --no-run) RUN=0; shift ;;
        -h|--help) sed -n 2,6p "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
SIM="$PLAYDATE_SDK_PATH/bin/PlaydateSimulator"
PDX=SailingSim.pdx

[ -x "$SIM" ] || { echo "sim: simulator not found at $SIM" >&2; exit 1; }

echo "== building simulator .pdx"
make
[ -d "$PDX" ] || { echo "sim: $PDX was not produced" >&2; exit 1; }

if [ "$RUN" -eq 1 ]; then
    echo "== launching simulator"
    # an already-running simulator reloads the game when it is given the same path
    exec "$SIM" "$PDX"
fi
echo "== done"
