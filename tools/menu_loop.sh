#!/usr/bin/env bash
# Render the looping menu animation: a fixed camera, a boat sailing past in 3 s, and the game's
# title over the top.
#
#   tools/menu_loop.sh [out.gif]            default build/menu_loop.gif
#
# Env: BOAT (default boats/sunfish.json), PRESET (weather index, default 3 = choppy), FPS (20),
# DIST / EYE / PITCH_DEG / LOOP_S / REL / WIND (see tools/gif_clip.c). Needs the Playdate SDK C_API
# headers (PLAYDATE_SDK_PATH) and Pillow.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/menu_loop.gif}"
SDK="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
INC="${PD_API_INCLUDE:-$SDK/C_API}"
[ -f "$INC/pd_api.h" ] || { echo "menu_loop: pd_api.h not found in $INC" >&2; exit 1; }
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$(dirname "$OUT")"
cc -std=c99 -DTARGET_EXTENSION=1 -DTARGET_SIMULATOR=1 -O1 -w -I src/sim -I src/pd -I "$INC" \
   tools/gif_clip.c src/sim/*.c src/pd/render.c src/pd/hud.c -lm -o "$WORK/gif_clip"
FPS="${FPS:-20}"
PASSBY=1 BOAT="${BOAT:-boats/sunfish.json}" FRAME_DIR="$WORK" FPS="$FPS" "$WORK/gif_clip" | tail -n 3
python3 -I - "$WORK" "$OUT" "$FPS" <<'PY'
import sys, glob
from PIL import Image, ImageDraw, ImageFont, ImageFilter
work, out, fps = sys.argv[1], sys.argv[2], int(sys.argv[3])
# the title in the menu's own face: Jersey 10 at 3x its 3 px design grid (tools/make_fonts.py)
font = ImageFont.truetype("assets/fonts/Jersey10-Regular.ttf", 56)
mask = Image.new("L", (400, 80), 0)
ImageDraw.Draw(mask).text((0, 0), "Crank'n Sail", font=font, fill=255)
mask = mask.crop(mask.getbbox()).point(lambda v: 255 if v > 127 else 0)
frames = []
for p in sorted(glob.glob(work + "/*.pgm")):
    im = Image.open(p).convert("L")
    x, y = (400 - mask.width) // 2, 28
    pad = Image.new("L", im.size, 0)
    pad.paste(mask, (x, y))
    halo = pad.filter(ImageFilter.MaxFilter(5))              # 2 px black outline so it reads on any sea
    im.paste(0, mask=halo.point(lambda v: 255 if v else 0))
    im.paste(255, mask=pad)
    frames.append(im.convert("1").convert("P"))
frames[0].save(out, save_all=True, append_images=frames[1:], duration=1000 // fps, loop=0, optimize=True)
print(out, len(frames), "frames")
PY
