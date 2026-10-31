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
[ $RC != 0 ] && echo "$OUTPUT" | grep -q "bad input"; expect "bad-size real golden fails as bad input" $?

mk t13; fb "$T/real/s/a.fb" 00; fb "$T/real/s/extra.fb" 00; run fidelity
[ $RC != 0 ]; expect "real golden without a snapshot fails" $?

mk t15; rm -rf "$T/out" "$T/host"; run regression
[ $RC != 0 ] && echo "$OUTPUT" | grep -q "checked 0 snapshots"; expect "empty out and golden fails regression" $?

mk t14; fb "$T/real/s/a.fb" ff; run all
[ $RC != 0 ]; expect "all runs fidelity too" $?

# regression tolerance: default 0.0005 of 96000 px = 48 px. 5 bytes flipped = 40 px passes, 50 bytes = 400 px fails.
flipbytes() { python3 -I -c 'import sys; d=bytearray(open(sys.argv[1],"rb").read()); n=int(sys.argv[2]); d[:n]=b"\xff"*n; open(sys.argv[1],"wb").write(d)' "$1" "$2"; }
mk t16; flipbytes "$T/out/s/a.fb" 5; run regression
[ $RC = 0 ] && echo "$OUTPUT" | grep -q "within tolerance"; expect "small regression frame diff passes" $?

mk t17; flipbytes "$T/out/s/a.fb" 50; run regression
[ $RC != 0 ] && [ -s "$T/diff/s/a.png" ]; expect "large regression frame diff fails" $?

mk t18; flipbytes "$T/out/s/a.fb" 50; echo "0.01" > "$T/scen/regression.tolerance"; run regression
[ $RC = 0 ]; expect "regression.tolerance file widens the limit" $?

mk t19; flipbytes "$T/out/s/a.fb" 5; echo "nan" > "$T/scen/regression.tolerance"; run regression
[ $RC != 0 ]; expect "malformed regression tolerance fails closed" $?

mk t20; flipbytes "$T/out/s/a.fb" 5; echo "0" > "$T/scen/regression.tolerance"; run regression
[ $RC != 0 ]; expect "regression tolerance 0 demands exact frames" $?

STATE='app=play\nx=1.500\nheading=-0.0231\n'
mk t21; printf "$STATE" > "$T/host/s/a.state"; printf 'app=play\nx=1.5004\nheading=-0.0231\n' > "$T/out/s/a.state"; run regression
[ $RC = 0 ]; expect "small regression state diff passes" $?

mk t22; printf "$STATE" > "$T/host/s/a.state"; printf 'app=play\nx=1.600\nheading=-0.0231\n' > "$T/out/s/a.state"; run regression
[ $RC != 0 ] && echo "$OUTPUT" | grep -q "a.state"; expect "large regression state diff fails" $?

mk t23; printf "$STATE" > "$T/host/s/a.state"; printf 'app=play\nx=nan\nheading=-0.0231\n' > "$T/out/s/a.state"; run regression
[ $RC != 0 ]; expect "nan regression state fails" $?

# fidelity: real .state compared against the host output (tolerance 0.01)
mk t24; fb "$T/real/s/a.fb" 00; printf "$STATE" > "$T/real/s/a.state"; printf 'app=play\nx=1.505\nheading=-0.0231\n' > "$T/out/s/a.state"; run fidelity
[ $RC = 0 ] && echo "$OUTPUT" | grep -q "PASS fidelity s/a.state"; expect "real state within tolerance passes" $?

mk t25; fb "$T/real/s/a.fb" 00; printf "$STATE" > "$T/real/s/a.state"; printf 'app=play\nx=1.600\nheading=-0.0231\n' > "$T/out/s/a.state"; run fidelity
[ $RC != 0 ] && echo "$OUTPUT" | grep -q "FAIL fidelity s/a.state"; expect "real state beyond tolerance fails" $?

mk t26; fb "$T/real/s/a.fb" 00; printf "$STATE" > "$T/real/s/a.state"; rm "$T/out/s/a.state"; run fidelity
[ $RC != 0 ]; expect "real state with no output state fails" $?

[ $fails = 0 ] || { echo "$fails test(s) failed"; exit 1; }
echo "all check.sh tests passed"
