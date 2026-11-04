#!/usr/bin/env bash
# Build the Playdate launcher art (Source/launcher/) from the game's own renderer:
#   card.png                350x155 still (boat mid-pass)
#   card-highlighted/N.png  350x155, the looping pass-by: 3 s boat, 1 s empty sea (20 fps)
#   launchImages/N.png      400x240 RGBA, 20 fps: starts as the card drawn at (25,43), the frame grows
#                           to full screen while the camera tips up to blank sky
#   launchImage.png         400x240 all white (shown while loading; the white start menu follows)
#
#   tools/launcher_art.sh [out_dir]         default Source/launcher
# Env: BOAT, FPS (20), FREEZE (clip frame the launch animation starts from, default 70 = empty sea),
# LOOKUP_FRAMES (20), LOOKUP_DEG (50), CARD_Y0 (first screen row kept in the card, default 35).
# Needs the Playdate SDK C_API headers (PLAYDATE_SDK_PATH) and Pillow. Set imagePath=launcher in pdxinfo.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-Source/launcher}"
SDK="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
INC="${PD_API_INCLUDE:-$SDK/C_API}"
[ -f "$INC/pd_api.h" ] || { echo "launcher_art: pd_api.h not found in $INC" >&2; exit 1; }
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pass" "$WORK/look"
cc -std=c99 -DTARGET_EXTENSION=1 -DTARGET_SIMULATOR=1 -O1 -w -I src/sim -I src/pd -I "$INC" \
   tools/gif_clip.c src/sim/*.c src/pd/render.c src/pd/hud.c -lm -o "$WORK/gif_clip"
export PASSBY=1 BOAT="${BOAT:-boats/sunfish.json}" FPS="${FPS:-20}"
FRAME_DIR="$WORK/pass" "$WORK/gif_clip" | tail -n 1
FREEZE="${FREEZE:-70}" FRAME_DIR="$WORK/look" "$WORK/gif_clip" | tail -n 1
rm -rf "$OUT"; mkdir -p "$OUT/card-highlighted" "$OUT/launchImages"
python3 -I - "$WORK" "$OUT" "${CARD_Y0:-35}" "${LOOKUP_DEG:-50}" "${PITCH_DEG:-5}" <<'PY'
import sys, glob, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
work, out, y0, deg, p0 = sys.argv[1], sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
CW, CH, CX, CY = 350, 155, 25, 43             # card size and where the launcher draws it
font = ImageFont.truetype("assets/fonts/Jersey10-Regular.ttf", 56)
mask = Image.new("L", (400, 80), 0)
ImageDraw.Draw(mask).text((0, 0), "Crank'n Sail", font=font, fill=255)
mask = mask.crop(mask.getbbox()).point(lambda v: 255 if v > 127 else 0)
TITLE_Y = y0 + 8                                # a little below the top edge of the card crop

def titled(im, shift=0):
    """scene (L, 400x240) with the title stamped on it; `shift` rows moves the title with the scene"""
    im = im.convert("L").copy()
    pad = Image.new("L", im.size, 0)
    pad.paste(mask, ((400 - mask.width) // 2, TITLE_Y + shift))
    halo = pad.filter(ImageFilter.MaxFilter(5))   # 2 px black outline so it reads on any sea
    im.paste(0, mask=halo.point(lambda v: 255 if v else 0))
    im.paste(255, mask=pad)
    return im

def one_bit(im): return im.point(lambda v: 255 if v > 127 else 0).convert("1")

def card_of(im): return one_bit(im.crop((CX, y0, CX + CW, y0 + CH)))

passes = sorted(glob.glob(work + "/pass/*.pgm"))
cards = [card_of(titled(Image.open(p))) for p in passes]
for i, c in enumerate(cards, 1): c.save(f"{out}/card-highlighted/{i}.png")
cards[len(cards) // 4].save(f"{out}/card.png")   # boat mid-pass

# launch animation: same pixels as the card inside the card rect, then the rect grows to the full
# screen while the camera pitches up (the horizon and the title slide down with it)
looks = sorted(glob.glob(work + "/look/*.pgm"))
n = len(looks)
t0 = math.tan(math.radians(p0))
for i, p in enumerate(looks):
    u = i / (n - 1) if n > 1 else 1
    e = u * u * (3 - 2 * u)
    shift = round(220 * (math.tan(math.radians(p0 + e * deg)) - t0))   # title rows moved by the pitch
    scene = titled(Image.open(p), shift)
    # place the render so that rows y0.. land at CY (frame 0 == card in its rect)
    canvas = Image.new("L", (400, 240), 255)
    canvas.paste(scene, (0, CY - y0))
    k = min(1.0, i / max(1, n - 4))              # rect fully open a few frames before the end
    k = k * k * (3 - 2 * k)
    l, t = round(CX * (1 - k)), round(CY * (1 - k))
    r, b = round(CX + CW + (400 - CX - CW) * k), round(CY + CH + (240 - CY - CH) * k)
    alpha = Image.new("L", (400, 240), 0)
    ImageDraw.Draw(alpha).rectangle((l, t, r - 1, b - 1), fill=255)
    rgba = one_bit(canvas).convert("L").convert("RGBA")
    rgba.putalpha(alpha)
    rgba.save(f"{out}/launchImages/{i + 1}.png")
Image.new("L", (400, 240), 255).convert("1").save(f"{out}/launchImage.png")
last = Image.open(f"{out}/launchImages/{n}.png").convert("L")
print("launch frames", n, "| last frame all white:", last.getextrema() == (255, 255), "| cards", len(cards))
PY
echo "wrote $OUT"
