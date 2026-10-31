"""Tests for fb2png.py and fbdiff.py. Run from the repo root:

    python3 -I -m unittest discover -s tools -p test_fbtools.py

Frames are built in a temp dir; the tools are run as subprocesses with the same
interpreter and -I, so they are tested the way CI runs them."""
import os
import subprocess
import sys
import tempfile
import unittest

TOOLS = os.path.dirname(os.path.abspath(__file__))
FB2PNG = os.path.join(TOOLS, "fb2png.py")
FBDIFF = os.path.join(TOOLS, "fbdiff.py")

W, H, ROW = 400, 240, 50          # 400 bits per row = 50 bytes, MSB-first
FRAME_BYTES = ROW * H             # 12000
PIXELS = W * H                    # 96000

try:
    import PIL  # noqa: F401
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


def run(script, *args):
    return subprocess.run([sys.executable, "-I", script, *args],
                          capture_output=True, text=True)


def white_frame():
    return bytearray(b"\xff" * FRAME_BYTES)


def flip(frame, x, y):
    """Toggle pixel (x, y): bit 7 of each byte is the leftmost pixel."""
    frame[y * ROW + x // 8] ^= 1 << (7 - x % 8)


class FbToolTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name, data):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(bytes(data))
        return path

    def path(self, name):
        return os.path.join(self.dir, name)


class FbDiffTest(FbToolTest):
    def test_identical_frames_pass(self):
        a = self.write("a.fb", white_frame())
        b = self.write("b.fb", white_frame())
        r = run(FBDIFF, a, b)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("mismatch=0 ", r.stdout)

    def test_flip_inside_mask_passes(self):
        fa = white_frame()
        fb = white_frame()
        flip(fb, 10, 20)
        a = self.write("a.fb", fa)
        b = self.write("b.fb", fb)
        r = run(FBDIFF, a, b, "--mask", "8,18,5,5", "--tolerance", "0")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("mismatch=0 ", r.stdout)
        self.assertIn("masked=25", r.stdout)

    def test_flip_outside_mask_fails_at_zero_tolerance(self):
        fa = white_frame()
        fb = white_frame()
        flip(fb, 10, 20)
        a = self.write("a.fb", fa)
        b = self.write("b.fb", fb)
        r = run(FBDIFF, a, b, "--mask", "100,100,5,5", "--tolerance", "0")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("mismatch=1 ", r.stdout)

    def test_tolerance_allows_96_flips_not_97(self):
        # 0.001 of 96000 pixels is exactly 96 mismatches.
        a = self.write("a.fb", white_frame())
        fb96 = white_frame()
        for i in range(96):
            flip(fb96, i, 0)
        r = run(FBDIFF, a, self.write("b96.fb", fb96), "--tolerance", "0.001")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("mismatch=96 ", r.stdout)
        fb97 = white_frame()
        for i in range(97):
            flip(fb97, i, 0)
        r = run(FBDIFF, a, self.write("b97.fb", fb97), "--tolerance", "0.001")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)

    def test_masked_pixels_leave_the_denominator(self):
        fb = white_frame()
        flip(fb, 0, 0)
        a = self.write("a.fb", white_frame())
        b = self.write("b.fb", fb)
        # Whole screen masked: nothing left to compare, so fraction is 0 and it passes.
        r = run(FBDIFF, a, b, "--mask", "0,0,400,240", "--tolerance", "0")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("masked=96000", r.stdout)
        self.assertIn("fraction=0.0", r.stdout)

    def test_wrong_size_exits_2(self):
        a = self.write("a.fb", white_frame())
        short = self.write("short.fb", b"\xff" * 11999)
        r = run(FBDIFF, a, short)
        self.assertEqual(r.returncode, 2)
        self.assertIn("12000", r.stderr)

    def test_bad_mask_exits_2(self):
        a = self.write("a.fb", white_frame())
        r = run(FBDIFF, a, a, "--mask", "1,2,3")
        self.assertEqual(r.returncode, 2)

    @unittest.skipUnless(HAVE_PIL, "Pillow not installed")
    def test_png_is_side_by_side(self):
        from PIL import Image
        fb = white_frame()
        flip(fb, 3, 3)
        a = self.write("a.fb", white_frame())
        b = self.write("b.fb", fb)
        out = self.path("diff.png")
        r = run(FBDIFF, a, b, "--tolerance", "1", "--png", out)
        self.assertEqual(r.returncode, 0, r.stderr)
        with Image.open(out) as img:
            self.assertEqual(img.size, (3 * 800, 480))


@unittest.skipUnless(HAVE_PIL, "Pillow not installed")
class Fb2PngTest(FbToolTest):
    def test_output_is_800x480(self):
        a = self.write("a.fb", white_frame())
        out = self.path("out.png")
        r = run(FB2PNG, a, out)
        self.assertEqual(r.returncode, 0, r.stderr)
        from PIL import Image
        with Image.open(out) as img:
            self.assertEqual(img.size, (800, 480))

    def test_black_and_white_pixels(self):
        fb = bytearray(FRAME_BYTES)          # all black
        a = self.write("a.fb", fb)
        out = self.path("out.png")
        r = run(FB2PNG, a, out)
        self.assertEqual(r.returncode, 0, r.stderr)
        from PIL import Image
        with Image.open(out) as img:
            self.assertEqual(img.convert("L").getpixel((0, 0)), 0)

    def test_wrong_size_exits_2(self):
        a = self.write("a.fb", b"\x00" * 100)
        r = run(FB2PNG, a, self.path("out.png"))
        self.assertEqual(r.returncode, 2)
        self.assertIn("12000", r.stderr)


if __name__ == "__main__":
    unittest.main()
