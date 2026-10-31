"""Compares two frame dumps (.fb, format described in fb2png.py).

    fbdiff.py EXPECTED.fb ACTUAL.fb [--mask x,y,w,h]... [--tolerance FRACTION] [--png OUT.png]

A pixel mismatches when expected and actual differ and it is not inside any mask. Masked
pixels are left out of both the count and the denominator:

    fraction = mismatch / (96000 - masked)

where masked is the number of screen pixels covered by at least one mask (masks are clipped
to the 400x240 screen, so overlaps count once). If every pixel is masked, fraction is 0.0.
The check passes when fraction <= tolerance (default 0).

Prints "mismatch=<count> fraction=<f> masked=<count>". Exit 0 pass, 1 fail, 2 bad input.
--png writes expected | actual | diff side by side at 2x, written whether or not the check
passes: mismatches are red, masked pixels grey, other pixels show the expected colour.
Pillow is imported only when --png is given."""
import argparse
import sys

W, H, ROW = 400, 240, 50
FRAME_BYTES = ROW * H
PIXELS = W * H
PNG_SCALE = 2


def read_fb(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != FRAME_BYTES:
        raise ValueError(f"{path}: expected {FRAME_BYTES} bytes, got {len(data)}")
    return data


def parse_mask(text):
    try:
        x, y, w, h = (int(v) for v in text.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError(f"mask must be x,y,w,h, got {text!r}")
    if w < 0 or h < 0:
        raise argparse.ArgumentTypeError(f"mask width and height must be >= 0: {text!r}")
    return x, y, w, h


def unpack(data):
    """One byte per pixel, row-major, 1 = white."""
    return bytearray((data[y * ROW + x // 8] >> (7 - x % 8)) & 1
                     for y in range(H) for x in range(W))


def mask_grid(masks):
    grid = bytearray(PIXELS)
    for x, y, w, h in masks:
        for yy in range(max(0, y), min(H, y + h)):
            for xx in range(max(0, x), min(W, x + w)):
                grid[yy * W + xx] = 1
    return grid


def write_png(path, exp_bytes, act_bytes, exp, masked, bad):
    from PIL import Image  # lazy: only the --png path needs Pillow

    rgb = bytearray(PIXELS * 3)
    for i in range(PIXELS):
        if bad[i]:
            c = b"\xff\x00\x00"
        elif masked[i]:
            c = b"\xbe\xbe\xbe"
        else:
            c = b"\xff\xff\xff" if exp[i] else b"\x00\x00\x00"
        rgb[3 * i:3 * i + 3] = c

    exp_img = Image.frombytes("1", (W, H), exp_bytes).convert("RGB")
    act_img = Image.frombytes("1", (W, H), act_bytes).convert("RGB")
    diff_img = Image.frombytes("RGB", (W, H), bytes(rgb))
    canvas = Image.new("RGB", (3 * W, H))
    canvas.paste(exp_img, (0, 0))
    canvas.paste(act_img, (W, 0))
    canvas.paste(diff_img, (2 * W, 0))
    canvas.resize((3 * W * PNG_SCALE, H * PNG_SCALE), Image.NEAREST).save(path)


def main():
    p = argparse.ArgumentParser(description="Compare two .fb frame dumps.")
    p.add_argument("expected")
    p.add_argument("actual")
    p.add_argument("--mask", action="append", default=[], type=parse_mask,
                   metavar="x,y,w,h", help="ignore a rectangle (repeatable)")
    p.add_argument("--tolerance", type=float, default=0.0,
                   help="allowed mismatch fraction of unmasked pixels (default 0)")
    p.add_argument("--png", metavar="OUT.png", help="write expected | actual | diff")
    a = p.parse_args()
    if a.tolerance < 0:
        print("fbdiff: --tolerance must be >= 0", file=sys.stderr)
        return 2
    try:
        exp_bytes = read_fb(a.expected)
        act_bytes = read_fb(a.actual)
    except (OSError, ValueError) as e:
        print(f"fbdiff: {e}", file=sys.stderr)
        return 2

    exp = unpack(exp_bytes)
    act = unpack(act_bytes)
    masked = mask_grid(a.mask)
    bad = bytearray(PIXELS)
    mismatch = 0
    for i in range(PIXELS):
        if not masked[i] and exp[i] != act[i]:
            bad[i] = 1
            mismatch += 1
    masked_count = sum(masked)
    denom = PIXELS - masked_count
    fraction = mismatch / denom if denom else 0.0

    if a.png:
        try:
            write_png(a.png, exp_bytes, act_bytes, exp, masked, bad)
        except ImportError:
            print("fbdiff: --png needs Pillow (pip install pillow)", file=sys.stderr)
            return 2

    print(f"mismatch={mismatch} fraction={fraction} masked={masked_count}")
    return 0 if fraction <= a.tolerance else 1


if __name__ == "__main__":
    sys.exit(main())
