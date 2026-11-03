"""compare.py A B [tol]: the parity dumps must have the same lines, with every number within tol (relative to
max(1, |value|)) and everything else identical. Exits 1 on the first mismatch."""
import re
import sys

NUM = re.compile(r"-?\d+(?:\.\d+)?")


def main(a_path, b_path, tol):
    with open(a_path) as f:
        a = f.read().splitlines()
    with open(b_path) as f:
        b = f.read().splitlines()
    if len(a) != len(b):
        print(f"parity: {len(a)} vs {len(b)} lines")
        return 1
    worst = 0.0
    for n, (la, lb) in enumerate(zip(a, b), 1):
        if NUM.sub("#", la) != NUM.sub("#", lb):
            print(f"parity: line {n} differs:\n  {la}\n  {lb}")
            return 1
        for x, y in zip(NUM.findall(la), NUM.findall(lb)):
            d = abs(float(x) - float(y)) / max(1.0, abs(float(x)))
            worst = max(worst, d)
            if d > tol:
                print(f"parity: line {n}: {x} vs {y}\n  {la}\n  {lb}")
                return 1
    print(f"parity: native and wasm agree over {len(a)} checkpoints (worst relative difference {worst:.2g})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 1e-3))
