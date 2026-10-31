#!/usr/bin/env bash
# Render a looping sail-and-pan GIF of one boat file headlessly.
#
#   tools/boat_clip.sh <boat.json> <out.gif>
#
# Needs pd_api.h from the Playdate SDK: PD_API_INCLUDE names the directory holding it
# (default $PLAYDATE_SDK_PATH/C_API). Also needs Pillow. Last stdout line is the clip tool's summary.
set -euo pipefail
cd "$(dirname "$0")/.."
[ $# -eq 2 ] || { sed -n 2,8p "$0" >&2; exit 2; }
BOAT_FILE="$1"; OUT_GIF="$2"
[ -f "$BOAT_FILE" ] || { echo "boat_clip: $BOAT_FILE: no such file" >&2; exit 1; }
SDK="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
INC="${PD_API_INCLUDE:-$SDK/C_API}"
[ -f "$INC/pd_api.h" ] || { echo "boat_clip: pd_api.h not found in $INC (set PD_API_INCLUDE)" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$(dirname "$OUT_GIF")"

cc -std=c99 -DTARGET_EXTENSION=1 -DTARGET_SIMULATOR=1 -O1 -w -I src/sim -I src/pd -I "$INC" \
   tools/gif_clip.c src/sim/*.c src/pd/render.c src/pd/hud.c -lm -o "$WORK/gif_clip"

FPS="${FPS:-20}"
MAX_BYTES=$((5 * 1024 * 1024))
while :; do
    rm -f "$WORK"/*.pgm
    SUMMARY="$(BOAT="$BOAT_FILE" FRAME_DIR="$WORK" FPS="$FPS" "$WORK/gif_clip" | tail -n 1)"
    python3 -I - "$WORK" "$OUT_GIF" "$FPS" <<'PY'
import sys, glob
from PIL import Image
work, out, fps = sys.argv[1], sys.argv[2], int(sys.argv[3])
frames = [Image.open(p).convert("P") for p in sorted(glob.glob(work + "/*.pgm"))]
frames[0].save(out, save_all=True, append_images=frames[1:], duration=1000 // fps, loop=0, optimize=True)
PY
    SIZE=$(stat -c %s "$OUT_GIF")
    [ "$SIZE" -le "$MAX_BYTES" ] && break
    [ "$FPS" -gt 5 ] || { echo "boat_clip: $OUT_GIF is still over 5 MB at 5 fps" >&2; exit 1; }
    FPS=$((FPS / 2 > 5 ? FPS / 2 : 5))
done
echo "$SUMMARY"
