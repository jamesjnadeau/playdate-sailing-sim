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

DISK=$(readlink -f "$DEV")
# give the desktop automounter a moment so we don't race it to the mount
MNT=""
for _ in $(seq 1 6); do
    MNT=$(findmnt -n -o TARGET "$DISK" 2>/dev/null | head -n 1 || true)
    [ -n "$MNT" ] && break
    sleep 0.5
done
if [ -z "$MNT" ]; then
    command -v udisksctl >/dev/null || die "$LABEL not mounted and udisksctl missing; mount $DEV yourself and rerun"
    # may still lose the race (AlreadyMounted); findmnt below is the real check
    out=$(udisksctl mount -b "$DISK" 2>&1) || true
    MNT=$(findmnt -n -o TARGET "$DISK" | head -n 1)
    [ -n "$MNT" ] || die "mount failed: $out"
fi

echo "== copying to $MNT/Games"
mkdir -p "$MNT/Games"
rm -rf "$MNT/Games/$PDX"
cp -r "$PDX" "$MNT/Games/$PDX"
sync

if [ "$KEEP" -eq 0 ]; then
    echo "== unmounting $MNT"
    sync
    for _ in 1 2 3 4 5; do
        findmnt -rn "$MNT" >/dev/null 2>&1 || break
        udisksctl unmount -b "$DISK" ||
            umount "$MNT" 2>/dev/null || sleep 1
    done
    if findmnt -rn "$MNT" >/dev/null 2>&1; then
        echo "deploy: could not unmount $MNT; eject $LABEL manually" >&2
    else
        # unmounting alone leaves the Playdate in disk mode; it only returns
        # once the host ejects the drive. Use udisks' Drive.Eject (allowed for
        # desktop users, unlike raw eject on /dev/sdX), not udisksctl power-off,
        # which also drops the device off the USB bus so it never comes back
        echo "== ejecting $LABEL"
        UD="--system --dest org.freedesktop.UDisks2"
        DRIVE=$(gdbus call $UD --object-path "/org/freedesktop/UDisks2/block_devices/$(basename "$DISK")" \
                    --method org.freedesktop.DBus.Properties.Get org.freedesktop.UDisks2.Block Drive 2>/dev/null |
                sed -n "s/.*objectpath '\([^']*\)'.*/\1/p")
        { [ -n "$DRIVE" ] &&
            gdbus call $UD --object-path "$DRIVE" --method org.freedesktop.UDisks2.Drive.Eject '{}' >/dev/null; } ||
            eject "$DISK" 2>/dev/null ||
            echo "deploy: could not eject $LABEL; eject it manually" >&2
    fi
fi

if [ "$RUN" -eq 1 ]; then
    echo "== launching"
    # leaving data-disk mode re-enumerates the device, so the port may change
    PORT=""
    for _ in $(seq 1 40); do
        PORT=$(ls /dev/serial/by-id/*Playdate* /dev/ttyACM* 2>/dev/null | head -n 1 || true)
        [ -n "$PORT" ] && [ -e "$PORT" ] && break
        sleep 0.5
    done
    [ -n "$PORT" ] || die "Playdate serial port did not come back; start SailingSim from its menu"
    sleep 2
    "$PDUTIL" "$PORT" run "/Games/$PDX" ||
        echo "deploy: launch failed; start SailingSim from the Playdate menu" >&2
fi
echo "== done"
