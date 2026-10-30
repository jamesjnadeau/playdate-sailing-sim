#!/usr/bin/env bash
# Render scenario frames headlessly to PNG (2x). Usage: tools/frames.sh [outdir]
set -euo pipefail
cd "$(dirname "$0")/.."
SDK="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
OUT="${1:-build/frames}"; mkdir -p "$OUT"
cc -std=c99 -DTARGET_EXTENSION=1 -DTARGET_SIMULATOR=1 -O1 -Wall -Wextra -I src/sim -I src/pd -I "$SDK/C_API" \
   tools/frame_dump.c src/sim/*.c src/pd/render.c src/pd/hud.c -lm -o "$OUT/frame_dump"
FRAME_DIR="$OUT" "$OUT/frame_dump"
python3 -I - "$OUT" <<'PY'
import sys, glob
from PIL import Image
for p in glob.glob(sys.argv[1] + "/*.pgm"):
    im = Image.open(p); im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")
PY
