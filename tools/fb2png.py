"""Converts a frame dump (.fb) to PNG.

    fb2png.py IN.fb OUT.png [--scale 2]

An .fb file is the 12000-byte display buffer written by src/pd/scenario_io.c: 240 rows
of 50 bytes, 400 bits per row, MSB-first, 1 = white. Exit 0 on success, 2 on bad input.
Needs Pillow."""
import argparse
import sys

W, H, ROW = 400, 240, 50
FRAME_BYTES = ROW * H


def read_fb(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != FRAME_BYTES:
        raise ValueError(f"{path}: expected {FRAME_BYTES} bytes, got {len(data)}")
    return data


def main():
    p = argparse.ArgumentParser(description="Convert a .fb frame dump to PNG.")
    p.add_argument("inp", help="input .fb file")
    p.add_argument("out", help="output .png file")
    p.add_argument("--scale", type=int, default=2, help="integer pixel scale (default 2)")
    a = p.parse_args()
    if a.scale < 1:
        print("fb2png: --scale must be at least 1", file=sys.stderr)
        return 2
    try:
        data = read_fb(a.inp)
    except (OSError, ValueError) as e:
        print(f"fb2png: {e}", file=sys.stderr)
        return 2

    from PIL import Image  # lazy: only needed when producing PNG

    # Mode "1" is packed MSB-first with 1 = white, which is exactly the .fb layout.
    img = Image.frombytes("1", (W, H), data).convert("L")
    img = img.resize((W * a.scale, H * a.scale), Image.NEAREST)
    img.save(a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
