#!/usr/bin/env python3
"""Rasterise the menu fonts in assets/fonts/ to Playdate 1-bit bitmap fonts.

Writes Source/fonts/<name>.fnt and <name>-table-W-H.png for each entry in
FONTS. Glyphs are rendered with antialiasing and thresholded; nothing
is hand-tuned, so re-running this reproduces the checked-in fonts exactly.
Small serif sizes use a lower threshold so hairlines survive. Cells are
trimmed to the inked rows; the printed baseline is the distance from the
cell top to the baseline, which the menu layout uses.

    python3 tools/make_fonts.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "fonts")
OUT = os.path.join(ROOT, "Source", "fonts")

# name, ttf, pixel size, tracking baked into the .fnt, ink threshold (0-255)
FONTS = [
    ("CormorantSC-Bold-28", "CormorantSC-Bold.ttf", 28, 0, 100),
    ("CormorantSC-Bold-22", "CormorantSC-Bold.ttf", 22, 0, 72),
    ("CormorantSC-SemiBold-22", "CormorantSC-SemiBold.ttf", 22, 0, 72),
    # labels too wide for the menu item at 22 px shrink to these
    ("CormorantSC-Bold-18", "CormorantSC-Bold.ttf", 18, 0, 64),
    ("CormorantSC-SemiBold-18", "CormorantSC-SemiBold.ttf", 18, 0, 64),
    # eyebrow and subtitle
    ("CormorantSC-Bold-16", "CormorantSC-Bold.ttf", 16, 0, 64),
    # hint bar, at twice Silkscreen's 8 px design size so its pixels stay square
    ("Silkscreen-16", "Silkscreen-Regular.ttf", 16, 0, 128),
]

CHARS = [chr(c) for c in range(32, 127)] + ["·", "—", "›"]
COLS = 16


def build(name, ttf, size, tracking, threshold):
    font = ImageFont.truetype(os.path.join(SRC, ttf), size)
    ascent, descent = font.getmetrics()
    boxes = [font.getbbox(c, anchor="ls") for c in CHARS if c != " "]
    top = max(0, ascent + min(b[1] for b in boxes))
    bottom = min(ascent + descent, ascent + max(b[3] for b in boxes))
    h = bottom - top
    adv = {c: max(1, round(font.getlength(c))) for c in CHARS}
    # the cell must hold the inked extent as well as the advance
    ink_r = {c: font.getbbox(c, anchor="ls")[2] for c in CHARS}
    w = max(max(adv.values()), max(ink_r.values()))

    rows = math.ceil(len(CHARS) / COLS)
    table = Image.new("RGBA", (COLS * w, rows * h), (0, 0, 0, 0))
    for i, c in enumerate(CHARS):
        cell = Image.new("L", (w, h), 0)
        ImageDraw.Draw(cell).text((0, ascent - top), c, font=font, fill=255, anchor="ls")
        ink = cell.point(lambda v: 255 if v >= threshold else 0)
        black = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        table.paste(black, ((i % COLS) * w, (i // COLS) * h), ink)

    lines = [f"tracking={tracking}"]
    for c in CHARS:
        lines.append(f"space\t{adv[c]}" if c == " " else f"{c}\t\t{adv[c]}")
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.startswith(name + "-table-"):
            os.remove(os.path.join(OUT, f))
    table.save(os.path.join(OUT, f"{name}-table-{w}-{h}.png"))
    with open(os.path.join(OUT, name + ".fnt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{name}: cell {w}x{h}, baseline {ascent - top}")


def main():
    for spec in FONTS:
        build(*spec)
    return 0


if __name__ == "__main__":
    sys.exit(main())
