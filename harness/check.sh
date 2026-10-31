#!/usr/bin/env bash
# check.sh regression|fidelity|all
# Regression: build/out vs the host goldens. Identical bytes always pass; otherwise .fb frames
#             may differ by up to the fraction in tests/scenarios/regression.tolerance (default
#             0.0005, no masks) and .state files must match key by key within a numeric tolerance
#             (second value in that file, default 0.001). The goldens were generated on a different
#             libc/libm than CI, so a handful of pixels or last-digit differences are expected;
#             anything bigger fails. A malformed tolerance fails closed (the tools exit 2).
# Fidelity:   build/out frames vs the real-device goldens, within masks and default.tolerance;
#             every real .state is compared against the host output (tools/statediff.py, 1e-2).
#             Fails if real goldens exist but nothing was compared.
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
STATEDIFF="${STATEDIFF:-$ROOT/tools/statediff.py}"
FID_STATE_TOL=0.01

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
  local s snap ext start=$checked rtol stol res rc
  local all
  # first non-comment line: "<fb fraction> [<state tolerance>]"
  read -r rtol stol < <(grep -v '^[[:space:]]*#' "$SCEN_DIR/regression.tolerance" 2>/dev/null | head -n 1)
  [ -n "${rtol:-}" ] || rtol=0.0005
  [ -n "${stol:-}" ] || stol=0.001
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
        elif [ "$ext" = fb ]; then
          res="$(python3 -I "$FBDIFF" "$g" "$a" --tolerance "$rtol" 2>&1)"; rc=$?
          case $rc in
            0) echo "PASS regression $s/$snap.fb (within tolerance $rtol: $res)" ;;
            1) fail "regression $s/$snap.fb: differs from golden: $res (tolerance $rtol)"
               diff_png "$s" "$snap" "$g" "$a" ;;
            *) fail "regression $s/$snap.fb: bad input (exit $rc): $res" ;;
          esac
        else
          res="$(python3 -I "$STATEDIFF" "$g" "$a" --tolerance "$stol" 2>&1)"; rc=$?
          case $rc in
            0) echo "PASS regression $s/$snap.state (within tolerance $stol)" ;;
            1) fail "regression $s/$snap.state: differs from golden (tolerance $stol): $(echo "$res" | tr '\n' ';')" ;;
            *) fail "regression $s/$snap.state: bad input (exit $rc): $res" ;;
          esac
        fi
      done
    done
  done
  [ "$checked" -gt "$start" ] || fail "regression checked 0 snapshots (empty or missing $OUT_DIR / $HOST_GOLDEN)"
}

fidelity() {
  local s snap tol masks line sn x y w h res rc start=$checked
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
    # every real .state against the host output
    for snap in $(snaps_in "$REAL_GOLDEN/$s" state); do
      if [ ! -f "$OUT_DIR/$s/$snap.state" ]; then fail "fidelity $s/$snap.state: real golden has no output state"; continue; fi
      checked=$((checked + 1))
      res="$(python3 -I "$STATEDIFF" "$REAL_GOLDEN/$s/$snap.state" "$OUT_DIR/$s/$snap.state" --tolerance "$FID_STATE_TOL" 2>&1)"; rc=$?
      case $rc in
        0) echo "PASS fidelity $s/$snap.state: $res" ;;
        1) fail "fidelity $s/$snap.state: $(echo "$res" | tr '\n' ';') (tolerance $FID_STATE_TOL)" ;;
        *) fail "fidelity $s/$snap.state: bad input (exit $rc): $res" ;;
      esac
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
  # real goldens exist but nothing was compared: a silent pass would hide a broken run
  if [ "$checked" -eq "$start" ] && [ -n "$(find "$REAL_GOLDEN" -type f \( -name '*.fb' -o -name '*.state' \) -print -quit 2>/dev/null)" ]; then
    fail "fidelity compared 0 snapshots although real goldens exist in $REAL_GOLDEN"
  fi
}

case "$MODE" in
  regression) regression ;;
  fidelity) fidelity ;;
  all) regression; fidelity ;;
  *) echo "usage: check.sh regression|fidelity|all" >&2; exit 2 ;;
esac

if [ "$fails" -gt 0 ]; then echo "FAILED: $fails failure(s) in $checked check(s)"; exit 1; fi
echo "OK: $checked check(s) passed"
