#!/usr/bin/env bash
# Validate and render the boats a pull request touches.
#   tools/pr_check.sh <base-ref> <outdir>
# Writes <outdir>/<boat>.gif for each rendered boat and <outdir>/summary.md.
# Changed boats are those added or modified under boats/ since <base-ref>; if none changed
# but src/, tools/ or config/ did, boats/j24.json is rendered as a smoke check. Exits 1 if a
# changed boat fails validation or a render fails. The unit tests are run by the workflow.
set -euo pipefail

[ $# -eq 2 ] || { echo "usage: $0 <base-ref> <outdir>" >&2; exit 2; }
base=$1
out=$2
cd "$(dirname "$0")/.."
mkdir -p "$out"
summary=$out/summary.md

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Added, modified or renamed (new path); -z keeps odd file names intact.
mapfile -d '' -t boats < <(git diff -z --name-only --diff-filter=AMR "$base"...HEAD -- 'boats/*.json')
smoke=0
if [ ${#boats[@]} -eq 0 ] && [ -n "$(git diff --name-only "$base"...HEAD -- src tools config)" ]; then
    boats=(boats/j24.json)
    smoke=1
fi

if [ ${#boats[@]} -eq 0 ]; then
    echo "No boat or code changes to render." > "$summary"
    cat "$summary"
    exit 0
fi

cc -std=c99 -O1 -Wall -Wextra -I src/sim tools/boat_check.c src/sim/*.c -lm -o "$work/boat_check"

cell() { tr '\n' ' ' | sed 's/|/\\|/g; s/ *$//'; }

status=0
declare -A verdict
# The game lists only boats/<name>.json with this name; anything else would pass here and never show up.
namerule='^boats/[a-z0-9_-]{1,23}\.json$'
for f in "${boats[@]}"; do
    if ! [[ $f =~ $namerule ]]; then
        verdict[$f]="name not accepted by the game (need boats/<a-z0-9_->.json, at most 23 characters)"
        status=1
    elif msg=$("$work/boat_check" "$f" 2>&1); then
        verdict[$f]=ok
    else
        verdict[$f]=$(printf '%s' "${msg#"$f": }" | cell)
        status=1
    fi
done

{
    [ $smoke -eq 1 ] && echo "No boat changed; rendering the default test boat to check the code change."
    [ $smoke -eq 1 ] && echo
    echo "| Boat | Validation | Clip |"
    echo "|---|---|---|"
} > "$summary"

for f in "${boats[@]}"; do
    name=$(basename "$f" .json)
    if [ "${verdict[$f]}" = ok ]; then
        if clip=$(tools/boat_clip.sh "$f" "$out/$name.gif" 2>"$work/err"); then
            clip=$(printf '%s' "$clip" | tail -n 1 | cell)
            clip="$name.gif: $clip"
        else
            clip="render failed: $(tail -n 3 "$work/err" | cell)"
            status=1
        fi
        echo "| $f | ok | $clip |" >> "$summary"
    else
        echo "| $(printf '%s' "$f" | cell) | ${verdict[$f]} | not rendered |" >> "$summary"
    fi
done

cat "$summary"
exit $status
