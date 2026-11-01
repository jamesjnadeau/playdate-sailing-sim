#!/usr/bin/env bash
# Shows the praise banner as the real simulator draws it. Builds the scenario .pdx, runs
# tools/praise_banner.txt in the simulator and converts each snapshot to PNG.
#
#   tools/praise_preview.sh [outdir]
#
# Output lands in outdir (default build/praise): praise_short.png and praise_long.png, 2x scale.
# The snapshots are the device framebuffer, so the font and shadow are the real ones (unlike the
# host frame harness, whose drawText is a stub). The simulator opens on the current DISPLAY for
# the few seconds the scenario takes, then closes itself. Needs the Playdate SDK
# (PLAYDATE_SDK_PATH, default ~/Developer/PlaydateSDK-3.1.2) and Pillow for the PNG step.
set -euo pipefail

cd "$(dirname "$0")/.."
export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
SIM="$PLAYDATE_SDK_PATH/bin/PlaydateSimulator"
OUT="${1:-build/praise}"
PDX=SailingSimScenario.pdx
TIMEOUT=120

[ -x "$SIM" ] || { echo "praise_preview: simulator not found at $SIM" >&2; exit 1; }
[ -n "${DISPLAY:-}" ] || { echo "praise_preview: needs DISPLAY set (the simulator window opens there)" >&2; exit 1; }

echo "== building $PDX"
make SCENARIO=1 simulator
[ -d "$PDX" ] || { echo "praise_preview: $PDX was not produced" >&2; exit 1; }

BUNDLE=$(sed -n 's/^bundleID=//p' Source/pdxinfo | head -n 1)
case "$BUNDLE" in
    ""|.|..|*[!A-Za-z0-9._-]*) echo "praise_preview: odd bundleID '$BUNDLE' in Source/pdxinfo" >&2; exit 1 ;;
esac
DATA="$PLAYDATE_SDK_PATH/Disk/Data/$BUNDLE"
rm -rf "$DATA"
mkdir -p "$DATA"
cp tools/praise_banner.txt "$PDX/scenario.txt"

echo "== running the scenario in the simulator"
env -u WAYLAND_DISPLAY GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy "$SIM" "$PDX" >/dev/null 2>&1 &
SIM_PID=$!
cleanup() {
    kill "$SIM_PID" 2>/dev/null || true
    rm -f "$PDX/scenario.txt"
}
trap cleanup EXIT
waited=0
while [ ! -e "$DATA/done.marker" ]; do
    kill -0 "$SIM_PID" 2>/dev/null || { echo "praise_preview: simulator exited before the scenario ended" >&2; exit 1; }
    [ "$waited" -lt $((TIMEOUT * 5)) ] || { echo "praise_preview: timed out after ${TIMEOUT}s" >&2; exit 1; }
    sleep 0.2
    waited=$((waited + 1))
done
if grep -q '^error' "$DATA/done.marker"; then
    echo "praise_preview: scenario failed: $(cat "$DATA/done.marker")" >&2
    exit 1
fi

echo "== converting to PNG in $OUT"
mkdir -p "$OUT"
for name in praise_short praise_long; do
    [ -f "$DATA/$name.fb" ] || { echo "praise_preview: no $name.fb in $DATA" >&2; exit 1; }
    cp "$DATA/$name.fb" "$OUT/$name.fb"
    python3 -I tools/fb2png.py "$OUT/$name.fb" "$OUT/$name.png" --scale 2
    echo "   $OUT/$name.png"
done
echo "== done"
