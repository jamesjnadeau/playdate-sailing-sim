"""Compares two scenario .state files (one key=value per line).

    statediff.py REAL.state HOST.state [--tolerance 1e-2]

Keys whose values both parse as numbers match when |real - host| <= tolerance; every other
key (app, mode label, ...) must match exactly. A key present in only one file is a mismatch.

Prints one line per differing key ("key: real=<v> host=<v>"). Exit 0 pass, 1 mismatch,
2 bad input (unreadable file or a line that is not key=value)."""
import argparse
import sys


def read_state(path):
    state = {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            key, sep, value = line.partition("=")
            if not sep or not key:
                raise ValueError(f"{path}:{n}: not key=value: {line!r}")
            state[key] = value
    return state


def as_number(text):
    try:
        return float(text)
    except ValueError:
        return None


def diff(real, host, tolerance):
    """Return a list of (key, real_value, host_value, detail) mismatches."""
    bad = []
    for key in sorted(set(real) | set(host)):
        r, h = real.get(key), host.get(key)
        if r is None or h is None:
            bad.append((key, r, h, "missing"))
            continue
        rn, hn = as_number(r), as_number(h)
        if rn is not None and hn is not None:
            if abs(rn - hn) > tolerance:
                bad.append((key, r, h, f"delta={abs(rn - hn):.6g}"))
        elif r != h:
            bad.append((key, r, h, "differs"))
    return bad


def max_delta(real, host):
    deltas = [abs(float(real[k]) - float(host[k])) for k in real.keys() & host.keys()
              if as_number(real[k]) is not None and as_number(host[k]) is not None]
    return max(deltas, default=0.0)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("real")
    ap.add_argument("host")
    ap.add_argument("--tolerance", type=float, default=1e-2)
    args = ap.parse_args(argv)
    try:
        real, host = read_state(args.real), read_state(args.host)
    except (OSError, ValueError) as e:
        print(f"statediff: {e}", file=sys.stderr)
        return 2
    bad = diff(real, host, args.tolerance)
    for key, r, h, detail in bad:
        print(f"{key}: real={r} host={h} ({detail})")
    print(f"mismatch={len(bad)} max_delta={max_delta(real, host):.6g}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
