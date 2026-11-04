#!/usr/bin/env bash
# Build the full game and the demo for the Playdate hardware each for sideloading.
#
#   tools/package_release.sh v0.2.0 [build-number] [out-dir]
#
# Stamps version/buildNumber into Source/pdxinfo for the builds (the working tree is restored
# afterwards). Needs arm-none-eabi-gcc, python3 and the Playdate SDK (PLAYDATE_SDK_PATH or
# ~/.Playdate/config). Writes <out-dir>/CranknSail-<tag>.pdx.zip and CranknSail-Demo-<tag>.pdx.zip.
set -euo pipefail

cd "$(dirname "$0")/.."
TAG="${1:?usage: package_release.sh <tag> [build-number] [out-dir]}"
BUILD="${2:-1}"
OUT="${3:-dist}"
VERSION="${TAG#v}"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+)*([-+][0-9A-Za-z.-]+)?$ ]] ||
    { echo "package_release: '$TAG' is not a version tag like v1.2.3" >&2; exit 2; }
[[ "$BUILD" =~ ^[0-9]+$ ]] || { echo "package_release: build number must be an integer" >&2; exit 2; }
command -v arm-none-eabi-gcc >/dev/null || { echo "package_release: arm-none-eabi-gcc not found" >&2; exit 1; }

cp Source/pdxinfo build/.pdxinfo.orig 2>/dev/null || { mkdir -p build; cp Source/pdxinfo build/.pdxinfo.orig; }
trap 'cp build/.pdxinfo.orig Source/pdxinfo' EXIT
sed -i -e "s/^version=.*/version=$VERSION/" -e "s/^buildNumber=.*/buildNumber=$BUILD/" Source/pdxinfo

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
rm -f "$OUT"/CranknSail-*.pdx.zip

package() {  # <pdx> <zip name> <make args...>
    local pdx="$1" zipname="$2"; shift 2
    echo "== building $pdx"
    rm -rf "$pdx"
    make device "$@"
    [ -f "$pdx/pdex.bin" ] || { echo "package_release: $pdx has no pdex.bin, not a device build" >&2; exit 1; }
    python3 -I -m zipfile -c "$OUT/$zipname" "$pdx"
}

package SailingSim.pdx "CranknSail-$TAG.pdx.zip"
package SailingSimDemo.pdx "CranknSail-Demo-$TAG.pdx.zip" DEMO=1

ls -l "$OUT"
