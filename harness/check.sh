#!/usr/bin/env bash
# check.sh regression|fidelity|all
# Regression: build/out equals the host goldens byte for byte (.fb and .state).
# Fidelity:   build/out frames vs the real-device goldens, within masks and tolerance.
# Paths come from the environment so tests can point it at a fabricated tree.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE/.."
MODE="${1:-all}"
OUT_DIR="${OUT_DIR:-$HERE/build/out}"
HOST_GOLDEN="${HOST_GOLDEN:-$ROOT/tests/golden/host}"
REAL_GOLDEN="${REAL_GOLDEN:-$ROOT/tests/golden/real}"
SCEN_DIR="${SCEN_DIR:-$ROOT/tests/scenarios}"
DIFF_DIR="${DIFF_DIR:-$HERE/build/diff}"
FBDIFF="${FBDIFF:-$ROOT/tools/fbdiff.py}"

fails=0
checked=0

fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# subdirectory names of $1, one per line (empty if $1 is missing)
scenarios_in() { [ -d "$1" ] && find "$1" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort; }
# snapshot names (basename without extension $2) in directory $1
snaps_in() { [ -d "$1" ] && find "$1" -maxdepth 1 -type f -name "*.$2" -printf '%f\n' | sed "s/\.$2\$//" | sort; }

diff_png() { # diff_png scenario snap expected actual
  mkdir -p "$DIFF_DIR/$1"
  python3 -I "$FBDIFF" "$3" "$4" --png "$DIFF_DIR/$1/$2.png" >/dev/null 2>&1
}

regression() {
  local s snap ext start=$checked
  local all
  all="$( { scenarios_in "$OUT_DIR"; scenarios_in "$HOST_GOLDEN"; } | sort -u)"
  for s in $all; do
    if [ ! -d "$OUT_DIR/$s" ]; then fail "regression $s: scenario has a golden but no output"; continue; fi
    if [ ! -d "$HOST_GOLDEN/$s" ]; then fail "regression $s: scenario has output but no golden"; continue; fi
    for ext in fb state; do
      for snap in $( { snaps_in "$OUT_DIR/$s" "$ext"; snaps_in "$HOST_GOLDEN/$s" "$ext"; } | sort -u); do
        local a="$OUT_DIR/$s/$snap.$ext" g="$HOST_GOLDEN/$s/$snap.$ext"
        checked=$((checked + 1))
        if [ ! -f "$a" ]; then fail "regression $s/$snap.$ext: missing from output"
        elif [ ! -f "$g" ]; then fail "regression $s/$snap.$ext: missing from golden"
        elif cmp -s "$g" "$a"; then echo "PASS regression $s/$snap.$ext"
        else
          fail "regression $s/$snap.$ext: differs from golden"
          [ "$ext" = fb ] && diff_png "$s" "$snap" "$g" "$a"
        fi
      done
    done
  done
  [ "$checked" -gt "$start" ] || fail "regression checked 0 snapshots (empty or missing $OUT_DIR / $HOST_GOLDEN)"
}

fidelity() {
  local s snap tol masks line sn x y w h res rc
  local all
  tol="$(tr -d '[:space:]' < "$SCEN_DIR/default.tolerance" 2>/dev/null)"
  [ -n "$tol" ] || tol=0
  all="$( { scenarios_in "$OUT_DIR"; scenarios_in "$REAL_GOLDEN"; } | sort -u)"
  for s in $all; do
    if [ ! -d "$OUT_DIR/$s" ]; then fail "fidelity $s: real golden scenario has no output"; continue; fi
    # real goldens with no matching output snapshot
    for snap in $(snaps_in "$REAL_GOLDEN/$s" fb); do
      [ -f "$OUT_DIR/$s/$snap.fb" ] || fail "fidelity $s/$snap: real golden has no output snapshot"
    done
    for snap in $(snaps_in "$OUT_DIR/$s" fb); do
      if [ ! -f "$REAL_GOLDEN/$s/$snap.fb" ]; then
        echo "WARN no real golden for $s/$snap"
        continue
      fi
      checked=$((checked + 1))
      masks=()
      if [ -f "$SCEN_DIR/$s.masks" ]; then
        while read -r sn x y w h _; do
          case "$sn" in ''|'#'*) continue ;; esac
          [ "$sn" = "$snap" ] && masks+=("--mask=$x,$y,$w,$h")
        done < "$SCEN_DIR/$s.masks"
      fi
      res="$(python3 -I "$FBDIFF" "$REAL_GOLDEN/$s/$snap.fb" "$OUT_DIR/$s/$snap.fb" \
             "${masks[@]+"${masks[@]}"}" --tolerance "$tol" 2>&1)"
      rc=$?
      case $rc in
        0) echo "PASS fidelity $s/$snap: $res" ;;
        1) fail "fidelity $s/$snap: $res (tolerance $tol)"
           diff_png "$s" "$snap.real" "$REAL_GOLDEN/$s/$snap.fb" "$OUT_DIR/$s/$snap.fb" ;;
        *) fail "fidelity $s/$snap: bad input (exit $rc): $res" ;;
      esac
    done
  done
}

case "$MODE" in
  regression) regression ;;
  fidelity) fidelity ;;
  all) regression; fidelity ;;
  *) echo "usage: check.sh regression|fidelity|all" >&2; exit 2 ;;
esac

if [ "$fails" -gt 0 ]; then echo "FAILED: $fails failure(s) in $checked check(s)"; exit 1; fi
echo "OK: $checked check(s) passed"
