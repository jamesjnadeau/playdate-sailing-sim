#!/usr/bin/env bash
# Build SailingSim.pdx for the Playdate hardware and install it over USB.
#
#   tools/deploy.sh [--port /dev/ttyACM0] [--no-run] [--keep-mounted]
#
# Needs: arm-none-eabi-gcc (device build), the Playdate SDK (PLAYDATE_SDK_PATH
# or ~/.Playdate/config), and udisksctl or an already-mounted PLAYDATE volume.
set -euo pipefail

cd "$(dirname "$0")/.."
PORT="" RUN=1 KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --no-run) RUN=0; shift ;;
        --keep-mounted) KEEP=1; shift ;;
        -h|--help) sed -n 2,8p "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

export PLAYDATE_SDK_PATH="${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}"
PDUTIL="$PLAYDATE_SDK_PATH/bin/pdutil"
PDX=SailingSim.pdx
LABEL=PLAYDATE

die() { echo "deploy: $*" >&2; exit 1; }

command -v arm-none-eabi-gcc >/dev/null ||
    die "arm-none-eabi-gcc not found (Debian/Ubuntu: sudo apt install gcc-arm-none-eabi)"
[ -x "$PDUTIL" ] || die "pdutil not found at $PDUTIL"

if [ -z "$PORT" ]; then
    PORT=$(ls /dev/serial/by-id/*Playdate* /dev/ttyACM* 2>/dev/null | head -n 1 || true)
fi
[ -n "$PORT" ] && [ -e "$PORT" ] ||
    die "no Playdate serial port found; plug it in, unlock it, or pass --port"

echo "== building device .pdx"
make device
[ -d "$PDX" ] || die "$PDX was not produced"

echo "== mounting data disk via $PORT"
"$PDUTIL" "$PORT" datadisk
DEV=/dev/disk/by-label/$LABEL
for _ in $(seq 1 30); do [ -e "$DEV" ] && break; sleep 0.5; done
[ -e "$DEV" ] || die "no $LABEL volume appeared after 15 s"

MNT=$(findmnt -n -o TARGET "$DEV" 2>/dev/null || true)
if [ -z "$MNT" ]; then
    command -v udisksctl >/dev/null || die "$LABEL not mounted and udisksctl missing; mount $DEV yourself and rerun"
    udisksctl mount -b "$(readlink -f "$DEV")" >/dev/null
    MNT=$(findmnt -n -o TARGET "$DEV") || die "mount failed"
fi

echo "== copying to $MNT/Games"
mkdir -p "$MNT/Games"
rm -rf "$MNT/Games/$PDX"
cp -r "$PDX" "$MNT/Games/$PDX"
sync

if [ "$KEEP" -eq 0 ]; then
    echo "== unmounting"
    udisksctl unmount -b "$(readlink -f "$DEV")" >/dev/null 2>&1 ||
        umount "$MNT" || echo "deploy: could not unmount; eject $LABEL manually" >&2
fi

if [ "$RUN" -eq 1 ]; then
    echo "== launching"
    for _ in $(seq 1 20); do [ -e "$PORT" ] && break; sleep 0.5; done
    sleep 2
    "$PDUTIL" "$PORT" run "/Games/$PDX" ||
        echo "deploy: launch failed; start SailingSim from the Playdate menu" >&2
fi
echo "== done"
