#!/usr/bin/env bash
# Tests for check.sh against fabricated trees (no game or SDK needed).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="$HERE/check.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fails=0

fb() { # fb PATH BYTE  (12000-byte frame filled with BYTE, as octal-free hex via python)
  mkdir -p "$(dirname "$1")"
  python3 -I -c 'import sys; open(sys.argv[1],"wb").write(bytes([int(sys.argv[2],16)])*12000)' "$1" "$2"
}

# fresh tree: scenario "s" with snapshot "a"; out == host golden; no real golden
mk() {
  T="$TMP/$1"; rm -rf "$T"
  fb "$T/out/s/a.fb" 00;  echo state1 > "$T/out/s/a.state"
  fb "$T/host/s/a.fb" 00; echo state1 > "$T/host/s/a.state"
  mkdir -p "$T/real" "$T/scen"
  echo 0.02 > "$T/scen/default.tolerance"
}
run() { # run MODE -> sets RC and OUTPUT
  OUTPUT="$(OUT_DIR="$T/out" HOST_GOLDEN="$T/host" REAL_GOLDEN="$T/real" SCEN_DIR="$T/scen" \
    DIFF_DIR="$T/diff" bash "$CHECK" "$1" 2>&1)"; RC=$?
}
expect() { # expect NAME CONDITION-RESULT
  if [ "$2" = 0 ]; then echo "ok: $1"; else echo "FAIL: $1"; echo "$OUTPUT" | sed 's/^/    /'; fails=$((fails+1)); fi
}

mk t1; run regression
[ $RC = 0 ]; expect "identical regression passes" $?

mk t2; fb "$T/host/s/a.fb" ff; run regression
[ $RC != 0 ] && [ -s "$T/diff/s/a.png" ]; expect "changed host golden fails and leaves PNG" $?

mk t3; echo other > "$T/host/s/a.state"; run regression
[ $RC != 0 ]; expect "changed state fails" $?

mk t4; fb "$T/out/s/b.fb" 00; run regression
[ $RC != 0 ]; expect "snapshot only in out fails" $?

mk t5; fb "$T/host/s/b.fb" 00; run regression
[ $RC != 0 ]; expect "snapshot only in golden fails" $?

mk t6; fb "$T/host/z/a.fb" 00; run regression
[ $RC != 0 ]; expect "scenario only in golden fails" $?

mk t7; run fidelity
[ $RC = 0 ] && echo "$OUTPUT" | grep -q "WARN no real golden"; expect "missing real golden passes with WARN" $?

mk t8; fb "$T/real/s/a.fb" ff; run fidelity
[ $RC != 0 ] && echo "$OUTPUT" | grep -q FAIL; expect "real golden beyond tolerance fails" $?

# small difference: first row (50 bytes = 400 px) flipped -> 400/96000 = 0.4%, within 0.02
mk t9; fb "$T/real/s/a.fb" 00
python3 -I -c 'import sys; d=bytearray(open(sys.argv[1],"rb").read()); d[:50]=b"\xff"*50; open(sys.argv[1],"wb").write(d)' "$T/real/s/a.fb"
run fidelity
[ $RC = 0 ] && echo "$OUTPUT" | grep -q PASS; expect "real golden within tolerance passes" $?

mk t10; fb "$T/real/s/a.fb" ff; echo "a 0 0 400 240" > "$T/scen/s.masks"; run fidelity
[ $RC = 0 ]; expect "mask covering difference passes" $?

mk t11; fb "$T/real/s/a.fb" ff; printf '# c\n\na 0 0 400 100\n' > "$T/scen/s.masks"; run fidelity
[ $RC != 0 ]; expect "partial mask leaves failure" $?

mk t12; mkdir -p "$T/real/s"; head -c 100 /dev/zero > "$T/real/s/a.fb"; run fidelity
[ $RC != 0 ]; expect "bad-size real golden fails" $?

mk t13; fb "$T/real/s/a.fb" 00; fb "$T/real/s/extra.fb" 00; run fidelity
[ $RC != 0 ]; expect "real golden without a snapshot fails" $?

mk t14; fb "$T/real/s/a.fb" ff; run all
[ $RC != 0 ]; expect "all runs fidelity too" $?

[ $fails = 0 ] || { echo "$fails test(s) failed"; exit 1; }
echo "all check.sh tests passed"
