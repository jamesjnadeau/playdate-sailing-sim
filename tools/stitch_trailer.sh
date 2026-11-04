#!/usr/bin/env bash
# Stitch promo clips into a trailer from a storyboard file.
#
#   tools/stitch_trailer.sh [options] STORYBOARD OUT.mp4
#
# Storyboard: one shot per line, '#' starts a comment, blank lines are ignored.
#   CLIP  START  DURATION  [CAPTION]     a slice of CLIP (path relative to --clips), START seconds in
#   card  -      DURATION  CAPTION       a title card on a plain background
# CAPTION is the rest of the line; write '|' for a line break. Leave it out for no caption.
#
# Every shot is centred on a 1920x1080 canvas WITHOUT scaling when the source is smaller (the promo clips
# are 1600x960, an exact 4x of the 400x240 screen, so the 1-bit pixels stay crisp) and downscaled only
# if it is larger. Captions use the game's own Silkscreen font and fade in and out. Shots are joined
# with hard cuts. Output is H.264 1080p 30 fps; with --music, a track is faded in and out under it.
#
# Options:
#   --clips DIR      where the clips live (default build/promo)
#   --music FILE     background track (looped/trimmed to the trailer length, 1 s fade in, 2 s fade out)
#   --pad COLOR      canvas colour around the clips (default black; the Playdate palette grey is 0xB1AFA8)
#   --crf N          x264 quality, lower is better (default 14)
#   --vertical       also write OUT_vertical.mp4 (1080x1920): the full frame, scaled to 1080 wide (not an
#                    integer scale, so slightly softer pixels), centred on a tall canvas with the caption below
#   --keep           keep the work directory and print its path
# Needs ffmpeg (sudo apt install ffmpeg). See docs/marketing-plan.md.
set -euo pipefail
cd "$(dirname "$0")/.."

CLIPS=build/promo MUSIC="" PAD=black CRF=14 VERTICAL=0 KEEP=0
FONT="assets/fonts/Silkscreen-Regular.ttf"
usage() { sed -n '2,/^# Needs/p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-2}"; }
die() { echo "stitch_trailer: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --clips) CLIPS="$2"; shift 2;;
    --music) MUSIC="$2"; shift 2;;
    --pad) PAD="$2"; shift 2;;
    --crf) CRF="$2"; shift 2;;
    --vertical) VERTICAL=1; shift;;
    --keep) KEEP=1; shift;;
    -h|--help) usage 0;;
    --) shift; break;;
    -*) echo "unknown option $1" >&2; usage;;
    *) break;;
  esac
done
[ $# -eq 2 ] || usage
BOARD="$1" OUT="$2"
[ -f "$BOARD" ] || die "storyboard $BOARD not found"
[ -z "$MUSIC" ] || [ -f "$MUSIC" ] || die "music file $MUSIC not found"
[ -f "$FONT" ] || die "font $FONT not found"
command -v ffmpeg >/dev/null || die "ffmpeg not installed (sudo apt install ffmpeg)"
[ -x "$(command -v ffprobe)" ] || die "ffprobe not installed (comes with ffmpeg)"

WORK="$(mktemp -d)"
if [ "$KEEP" = 1 ]; then echo "work dir: $WORK"; else trap 'rm -rf "$WORK"' EXIT; fi

# build_shot CW CH SRC_ARGS... : writes $WORK/NAME.mp4
# Arguments (all set by the caller as globals): $INPUT (ffmpeg input args), $DUR, $CAP, $NAME, $W, $H
shot() {
  local vf cap_file="$WORK/$NAME.txt" fade
  # Fit inside WxH (never upscale), centre on the canvas, constant frame rate.
  vf="scale=w='min(iw,$W)':h='min(ih,$H)':force_original_aspect_ratio=decrease:flags=neighbor"
  vf="$vf,pad=$W:$H:(ow-iw)/2:(oh-ih)/2:color=$PAD,setsar=1,fps=30"
  if [ -n "$CAP" ]; then
    # Silkscreen is monospaced, so centre each line by padding it with spaces (drawtext only left-aligns).
    local -a lines; local l max=0
    IFS='|' read -ra lines <<<"$CAP"
    for l in "${lines[@]}"; do [ "${#l}" -gt "$max" ] && max=${#l}; done
    : > "$cap_file"
    for l in "${lines[@]}"; do
      printf '%*s%s\n' $(((max - ${#l}) / 2)) '' "$l" >> "$cap_file"
    done
    fade="if(lt(t,0.3),t/0.3,if(gt(t,$DUR-0.3),($DUR-t)/0.3,1))"   # inside the single quotes below
    local size=$((W / 32)) y="h-th-$((H / 10))"
    [ "$KIND" = card ] && { size=$((W / 20)); y="(h-th)/2"; }
    vf="$vf,drawtext=fontfile=$FONT:textfile=$cap_file:fontsize=$size:fontcolor=white:line_spacing=12"
    vf="$vf:box=1:boxcolor=black@0.6:boxborderw=18:x=(w-tw)/2:y=$y:alpha='$fade'"
  fi
  # shellcheck disable=SC2086
  ffmpeg -nostdin -v error -y $INPUT -t "$DUR" -vf "$vf" -an -c:v libx264 -crf "$CRF" -pix_fmt yuv420p \
    -preset medium "$WORK/$NAME.mp4"
}

# render W H OUTFILE: build every shot at that size and join them
render() {
  local W="$1" H="$2" dest="$3" n=0 total=0 line
  : > "$WORK/list.txt"
  while IFS= read -r line || [ -n "$line" ]; do
    line="${line%%#*}"
    [[ "$line" =~ ^[[:space:]]*$ ]] && continue
    local clip start dur cap
    read -r clip start dur cap <<<"$line"
    [ -n "${dur:-}" ] || die "bad storyboard line (want CLIP START DURATION [CAPTION]): $line"
    n=$((n + 1)); NAME="s$(printf %03d "$n")_${W}"; CAP="$cap"; DUR="$dur"
    if [ "$clip" = card ]; then
      KIND=card
      INPUT="-f lavfi -i color=c=$PAD:s=${W}x${H}:r=30:d=$dur"
    else
      KIND=clip
      [ -f "$CLIPS/$clip" ] || die "clip $CLIPS/$clip not found (line: $line)"
      local len; len=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$CLIPS/$clip" </dev/null)
      awk -v s="$start" -v d="$dur" -v l="$len" 'BEGIN{exit !(s + d <= l + 0.05)}' \
        || die "$clip is ${len}s long; $start + $dur runs past the end"
      INPUT="-ss $start -i $CLIPS/$clip"
    fi
    echo "  shot $n: $clip ${start}s +${dur}s ${cap:+\"$cap\"}"
    shot
    echo "file '$WORK/$NAME.mp4'" >> "$WORK/list.txt"
    total=$(awk -v t="$total" -v d="$dur" 'BEGIN{print t + d}')
  done < "$BOARD"
  [ "$n" -gt 0 ] || die "storyboard has no shots"

  if [ -n "$MUSIC" ]; then
    local fo; fo=$(awk -v t="$total" 'BEGIN{print (t > 2 ? t - 2 : 0)}')
    ffmpeg -v error -y -f concat -safe 0 -i "$WORK/list.txt" -stream_loop -1 -i "$MUSIC" -t "$total" \
      -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k \
      -af "afade=t=in:d=1,afade=t=out:st=$fo:d=2" -movflags +faststart "$dest"
  else
    ffmpeg -v error -y -f concat -safe 0 -i "$WORK/list.txt" -c copy -movflags +faststart "$dest"
  fi
  echo "wrote $dest (${total}s, ${W}x${H})"
}

mkdir -p "$(dirname "$OUT")"
render 1920 1080 "$OUT"
if [ "$VERTICAL" = 1 ]; then
  render 1080 1920 "${OUT%.mp4}_vertical.mp4"
fi
