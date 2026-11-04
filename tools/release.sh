#!/usr/bin/env bash
# Tag the current commit and push the tag; the Release workflow builds and publishes.
#
#   tools/release.sh v0.2.0
set -euo pipefail

cd "$(dirname "$0")/.."
TAG="${1:?usage: release.sh vX.Y.Z}"
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]] || { echo "release: '$TAG' should look like v1.2.3" >&2; exit 2; }
[ "$(git branch --show-current)" = main ] || { echo "release: not on main" >&2; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "release: working tree is dirty" >&2; exit 1; }
git fetch -q origin main
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || { echo "release: HEAD differs from origin/main; push or pull first" >&2; exit 1; }
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && { echo "release: tag $TAG already exists" >&2; exit 1; }

git tag -a "$TAG" -m "Crank'n Sail $TAG"
git push origin "$TAG"
echo "Pushed $TAG; watch the Release workflow in GitHub Actions."
