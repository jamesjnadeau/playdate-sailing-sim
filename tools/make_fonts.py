#!/usr/bin/env python3
"""Rasterise the menu fonts in assets/fonts/ to Playdate 1-bit bitmap fonts.

Writes Source/fonts/<name>.fnt and <name>-table-W-H.png for each entry in
FONTS. Both faces are pixel fonts: each is rendered at a size where its
design grid falls on whole pixels, cut to black and white, reduced so one
grid square is one pixel, then scaled up by a whole number so the pixels
stay square. Nothing is hand-tuned, so re-running this reproduces the
checked-in fonts exactly. Cells are trimmed to the inked rows; the printed
baseline is the distance from the cell top to the baseline, which the menu
layout uses.

    python3 tools/make_fonts.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "fonts")
OUT = os.path.join(ROOT, "Source", "fonts")

# name, ttf, render size, design grid in px at that size, scale, tracking baked into the .fnt.
# Jersey 10's grid is 3/56 em, so at 56 px each grid square is 3 px; its capitals are 10 squares tall.
FONTS = [
    ("Jersey10-30", "Jersey10-Regular.ttf", 56, 3, 3, 0),   # one-line titles
    ("Jersey10-20", "Jersey10-Regular.ttf", 56, 3, 2, 0),   # titles and list items
    ("Jersey10-10", "Jersey10-Regular.ttf", 56, 3, 1, 0),   # eyebrow, subtitle, labels too wide at 2x
    # hint bar, at twice Silkscreen's 8 px design size so its pixels stay square
    ("Silkscreen-16", "Silkscreen-Regular.ttf", 16, 1, 1, 0),
]

CHARS = [chr(c) for c in range(32, 127)] + ["·", "—", "›"]
COLS = 16


def build(name, ttf, size, grid, scale, tracking):
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

    # grid squares to output pixels; every edge and advance is a whole number of squares
    def px(v):
        return math.ceil(v / grid) * scale
    base = px(ascent - top)
    w, h = px(w), px(h)

    rows = math.ceil(len(CHARS) / COLS)
    table = Image.new("RGBA", (COLS * w, rows * h), (0, 0, 0, 0))
    for i, c in enumerate(CHARS):
        cell = Image.new("L", (w // scale * grid, h // scale * grid), 0)
        ImageDraw.Draw(cell).text((0, ascent - top), c, font=font, fill=255, anchor="ls")
        # each grid square is all ink or none, so its mean says which
        ink = cell.resize((w // scale, h // scale), Image.BOX).point(lambda v: 255 if v >= 128 else 0)
        ink = ink.resize((w, h), Image.NEAREST)
        black = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        table.paste(black, ((i % COLS) * w, (i // COLS) * h), ink)

    lines = [f"tracking={tracking}"]
    for c in CHARS:
        a = px(adv[c])
        lines.append(f"space\t{a}" if c == " " else f"{c}\t\t{a}")
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.startswith(name + "-table-"):
            os.remove(os.path.join(OUT, f))
    table.save(os.path.join(OUT, f"{name}-table-{w}-{h}.png"))
    with open(os.path.join(OUT, name + ".fnt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{name}: cell {w}x{h}, baseline {base}")


def main():
    for spec in FONTS:
        build(*spec)
    return 0


if __name__ == "__main__":
    sys.exit(main())
