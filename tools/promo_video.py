#!/usr/bin/env python3
"""Shoot promo footage: play scripted shots in the Playdate simulator and encode each one as a video.

    tools/promo_video.py series weather-orbit [options]   the boat in every weather, camera circling it
    tools/promo_video.py trailer [options]                 every shot, cut into OUT/crank_sail_trailer.mp4
    tools/promo_video.py run SCENARIO.txt [options]       any scenario; every `record NAME` becomes NAME.mp4
    tools/promo_video.py encode FRAMES_DIR OUT.mp4        re-encode frames kept with --keep-frames

A shot is an ordinary scenario (tests/scenarios format, see README) using the recording commands:
`record NAME` / `record off` (write every frame), `hud on|off`, `wind DEG` (wind from DEG off the bow,
+ = starboard) and `orbit DEG_PER_S` (the camera circles the boat; 0 stops).

The scenarios run in the real simulator (text and all) on a scratch Xvfb, like tools/golden_refresh.sh.
--host runs them on the host harness instead: much faster, but it draws no text, so use it only to
preview shots with the HUD off. Needs PLAYDATE_SDK_PATH (default ~/Developer/PlaydateSDK-3.1.2),
Xvfb and ffmpeg.

The simulator's Data dir for the game ($SDK/Disk/Data/<bundleID>) is wiped before every shot, which
would disturb a simulator session of this game you have open.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, FB_BYTES = 400, 240, 50 * 240
FPS = 30
WEATHERS = ["mist", "sunny", "cloudy", "choppy", "rollers", "storm", "hurricane"]   # weather.h order
PALETTES = {
    "bw": ((0, 0, 0), (255, 255, 255)),
    # the Playdate's unlit memory-LCD: dark grey-brown on warm grey
    "playdate": ((0x31, 0x2F, 0x28), (0xB1, 0xAF, 0xA8)),
}


def die(msg, code=1):
    print(f"promo_video: {msg}", file=sys.stderr)
    sys.exit(code)


def sdk_path():
    return os.environ.get("PLAYDATE_SDK_PATH") or os.path.expanduser("~/Developer/PlaydateSDK-3.1.2")


def run(cmd, **kw):
    print("   $ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=kw.pop("cwd", ROOT), **kw)


# ---------------------------------------------------------------- series

# Weathers the boat cannot sail in at full hoist: hoist fraction to reef every sail to. Auto trim stays off
# there, since it hoists back up as soon as the heel eases (and never reefs below autotrim_hoist_min), and
# the dinghy is then flattened in under a second. 0.16 keeps it sailing at ~2.3 m/s, heeled ~25 degrees.
DEFAULT_REEF = {"hurricane": 0.16}


def parse_reef(spec):
    """'hurricane=0.16,storm=0.5' -> dict; 'none' -> {}."""
    if spec is None:
        return dict(DEFAULT_REEF)
    out = {}
    if spec == "none":
        return out
    for part in spec.split(","):
        w, _, v = part.partition("=")
        try:
            h = float(v)
        except ValueError:
            die(f"bad --reef entry '{part}' (want WEATHER=HOIST)", 2)
        if w not in WEATHERS or not 0 <= h <= 1:
            die(f"bad --reef entry '{part}' (weather from {', '.join(WEATHERS)}, hoist 0..1)", 2)
        out[w] = h
    return out


def boat_controls(boat):
    with open(os.path.join(ROOT, "boats", f"{boat}.json")) as f:
        cfg = json.load(f)
    k = cfg.get("controls", {})
    # defaults as in src/sim/boatcfg.c
    return k.get("crank_gain_hoist", 0.003), k.get("hoist_rate", 0.25), len(cfg.get("sails", []))


def weather_orbit(a):
    """One shot per weather: same boat, same heading, the same wind off the bow, autopilot and auto trim
    on, and the camera circling the boat once in orbit_s seconds while it records."""
    names = a.weathers.split(",") if a.weathers else WEATHERS
    for n in names:
        if n not in WEATHERS:
            die(f"unknown weather '{n}' (have {', '.join(WEATHERS)})", 2)
    reef = parse_reef(a.reef)
    gain, rate, nsails = boat_controls(a.boat)
    frames = round(a.orbit_s * FPS)
    settle = max(1, round(a.settle_s * FPS))
    shots = []
    for n in names:
        name = f"{a.boat.replace('-', '_')}_{n}{'_hud' if a.hud else ''}"   # record names are [A-Za-z0-9_]
        lines = [f"# {a.boat} in {n}: wind from {a.wind:g} deg off the bow, one camera orbit in {a.orbit_s:g} s",
                 "wait 2",
                 "press A                 # main menu -> free range",
                 "wait 5",
                 f"sysmenu boat {a.boat}",
                 "wait 2"]
        if n in reef:
            # hoist mode, then each sail in turn (LEFT again selects the next one); lowered in calm air
            crank = -(1.0 - reef[n]) / gain
            lines.append(f"# reefed to {reef[n]:g} of full hoist")
            for _ in range(nsails):
                lines += ["press left", f"crank {crank / 10:.6g} for 10"]
            lines += ["press up                # back to the helm",
                      f"wait {round((1.0 - reef[n]) / rate * FPS) + 15}"]
        lines += [f"sysmenu weather {WEATHERS.index(n)}",
                  f"wind {a.wind:g}",
                  f"hud {'on' if a.hud else 'off'}",
                  "wait 1",
                  "press A                 # autopilot holds the starting heading"]
        if n not in reef:
            lines += ["wait 1", "press B                 # auto trim"]
        lines += [f"wait {settle}           # get up to speed",
                  f"orbit {360.0 / a.orbit_s:.6g}",
                  f"record {name}",
                  f"wait {frames}",
                  "record off",
                  "state",
                  "orbit 0",
                  "end"]
        shots.append((name, "\n".join(lines) + "\n"))
    return shots


def buoy_pass(a):
    """A caption over a buoy filling most of the screen; the boat sails by behind it, left to right, close
    enough that the hull fills the frame and the masthead is cut off. The camera is fixed low beside the
    buoy, on the far side from the boat's track, looking back across it."""
    names = a.weathers.split(",") if a.weathers else ["sunny"]
    for n in names:
        if n not in WEATHERS:
            die(f"unknown weather '{n}' (have {', '.join(WEATHERS)})", 2)
    if len(a.caption) > 47 or any(not 32 <= ord(ch) <= 126 or ch == "#" for ch in a.caption):
        die("--caption: at most 47 printable ASCII characters, no '#'", 2)
    reef = parse_reef(a.reef)
    gain, rate, nsails = boat_controls(a.boat)
    settle = max(1, round(a.settle_s * FPS))
    shots = []
    for n in names:
        name = f"{a.boat.replace('-', '_')}_buoy_pass_{n}{'_hud' if a.hud else ''}"
        lines = [f"# {a.boat} sails past a buoy in {n}, caption '{a.caption}'",
                 "wait 2",
                 "press A                 # main menu -> free range",
                 "wait 5",
                 f"sysmenu boat {a.boat}",
                 "wait 2"]
        if n in reef:
            crank = -(1.0 - reef[n]) / gain
            for _ in range(nsails):
                lines += ["press left", f"crank {crank / 10:.6g} for 10"]
            lines += ["press up", f"wait {round((1.0 - reef[n]) / rate * FPS) + 15}"]
        lines += [f"sysmenu weather {WEATHERS.index(n)}",
                  f"wind {a.wind:g}",
                  f"hud {'on' if a.hud else 'off'}",
                  "ghost off               # no trailing ghost boat in the shot",
                  "wait 1",
                  "press A                 # autopilot holds the starting heading"]
        if n not in reef:
            lines += ["wait 1", "press B                 # auto trim"]
        lines += [f"wait {settle}           # get up to speed",
                  # laid close ahead and just to starboard: the boat makes leeway to port, away from the camera
                  f"buoy {a.buoy_ahead:g} {a.buoy_side:g}",
                  f"fixcam {a.cam_dist:g} {a.cam_height:g} {a.cam_aim:g} {a.cam_pitch:g}"]
        if a.caption:
            lines.append(f"caption {a.caption}")
        lines += [f"record {name}",
                  f"wait {round(a.length_s * FPS)}",
                  "record off",
                  "state",
                  "end"]
        shots.append((name, "\n".join(lines) + "\n"))
    return shots


SERIES = {"weather-orbit": weather_orbit, "buoy-pass": buoy_pass}


# ---------------------------------------------------------------- players

class SimPlayer:
    """Plays scenarios in the real Playdate simulator on a scratch Xvfb (or the current display)."""

    def __init__(self, on_current_display):
        self.sdk = sdk_path()
        self.sim = os.path.join(self.sdk, "bin", "PlaydateSimulator")
        if not os.access(self.sim, os.X_OK):
            die(f"simulator not found at {self.sim} (set PLAYDATE_SDK_PATH)")
        self.pdx = os.path.join(ROOT, "SailingSimScenario.pdx")
        self.xvfb = None
        self.data = self._data_dir()
        if on_current_display:
            if not os.environ.get("DISPLAY"):
                die("--on-current-display needs DISPLAY set", 2)
            self.display = os.environ["DISPLAY"]
        else:
            self.display = self._start_xvfb()

    def _data_dir(self):
        with open(os.path.join(ROOT, "Source", "pdxinfo")) as f:
            m = re.search(r"^bundleID=(.+)$", f.read(), re.M)
        bundle = m.group(1).strip() if m else ""
        # wiped before every shot, so be strict about what it can be
        if not re.fullmatch(r"[A-Za-z0-9._-]+", bundle) or bundle in (".", ".."):
            die(f"refusing odd bundleID '{bundle}' in Source/pdxinfo")
        root = os.path.realpath(os.path.join(self.sdk, "Disk", "Data"))
        data = os.path.realpath(os.path.join(root, bundle))
        if os.path.dirname(data) != root:
            die(f"refusing Data path {data}")
        return data

    def _start_xvfb(self):
        xvfb = os.environ.get("XVFB_BIN") or shutil.which("xvfb") or shutil.which("Xvfb")
        if not xvfb:
            die("Xvfb not found (set XVFB_BIN)")
        n = 99
        while os.path.exists(f"/tmp/.X{n}-lock") or os.path.exists(f"/tmp/.X11-unix/X{n}"):
            n += 1
            if n >= 140:
                die("no free scratch display")
        self.xvfb = subprocess.Popen([xvfb, f":{n}", "-screen", "0", "800x600x24", "-nolisten", "tcp"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            if os.path.exists(f"/tmp/.X11-unix/X{n}"):
                return f":{n}"
            if self.xvfb.poll() is not None:
                die("Xvfb died")
            time.sleep(0.1)
        die("Xvfb did not come up")

    def build(self):
        print("== building SailingSimScenario.pdx", flush=True)
        # the simulator target only: `all` also builds for the device, which needs arm-none-eabi-gcc
        run(["make", "SCENARIO=1", "simulator"], stdout=subprocess.DEVNULL)
        if not os.path.isdir(self.pdx):
            die(f"{self.pdx} was not produced")

    def play(self, name, text, out_dir):
        """Runs one scenario; copies its recorded frame folders into out_dir. Returns their names."""
        shutil.rmtree(self.data, ignore_errors=True)
        os.makedirs(self.data)
        with open(os.path.join(self.pdx, "scenario.txt"), "w") as f:
            f.write(text)
        timeout = 60 + 3 * scenario_frames(text) / FPS
        env = dict(os.environ, DISPLAY=self.display, GDK_BACKEND="x11", LIBGL_ALWAYS_SOFTWARE="1",
                   SDL_AUDIODRIVER="dummy")
        env.pop("WAYLAND_DISPLAY", None)   # never open on the desktop
        log = tempfile.TemporaryFile()
        proc = subprocess.Popen([self.sim, self.pdx], env=env, stdout=log, stderr=subprocess.STDOUT)
        marker = os.path.join(self.data, "done.marker")
        start = time.time()
        try:
            while not os.path.exists(marker):
                if proc.poll() is not None:
                    die(f"simulator exited early on {name}:\n{read_log(log)}")
                if time.time() - start > timeout:
                    die(f"timeout after {timeout:.0f}s on {name}:\n{read_log(log)}")
                time.sleep(0.2)
            time.sleep(0.3)   # let the last frame file close
        finally:
            stop(proc)
        with open(marker) as f:
            status = f.read()
        if status.startswith("error"):
            die(f"{name} failed in the simulator: {status.strip()}\n{read_log(log)}")
        return collect(self.data, out_dir), read_log(log)

    def close(self):
        try:
            os.remove(os.path.join(self.pdx, "scenario.txt"))
        except OSError:
            pass
        if self.xvfb:
            self.xvfb.terminate()


class HostPlayer:
    """Plays scenarios on the host harness (stub Playdate API, draws no text)."""

    def __init__(self):
        self.harness = os.path.join(ROOT, "harness", "build", "harness")
        self.assets = os.path.join(ROOT, "harness", "build", "assets")

    def build(self):
        print("== building the host harness", flush=True)
        run(["make", "-C", "harness", "build/harness", "assets"], stdout=subprocess.DEVNULL)

    def play(self, name, text, out_dir):
        with tempfile.TemporaryDirectory() as work:
            scen = os.path.join(work, "scenario.txt")
            with open(scen, "w") as f:
                f.write(text)
            data = os.path.join(work, "out")
            r = subprocess.run([self.harness, scen, data, self.assets], cwd=os.path.join(ROOT, "harness"),
                               capture_output=True, text=True)
            if r.returncode != 0:
                die(f"{name} failed on the host harness (exit {r.returncode}):\n{r.stderr}")
            return collect(data, out_dir), r.stdout + r.stderr

    def close(self):
        pass


def read_log(f):
    f.seek(0)
    return f.read().decode(errors="replace")[-20000:]


def stop(proc):
    # the simulator may ignore SIGTERM, so escalate; never wait on it unbounded
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def scenario_frames(text):
    n = 0
    for line in text.splitlines():
        t = line.split("#", 1)[0].split()
        if not t:
            continue
        if t[0] == "wait" and len(t) == 2:
            n += int(t[1])
        elif t[0] == "hold" and len(t) == 3:
            n += int(t[2])
        elif t[0] == "crank" and len(t) == 4:
            n += int(t[3])
        elif t[0] == "press":
            n += 1
    return n


def collect(data, out_dir):
    """Moves every recorded folder (NAME/NNNNN.fb) from data to out_dir/NAME."""
    names = []
    for d in sorted(os.listdir(data)):
        src = os.path.join(data, d)
        if not os.path.isdir(src) or not any(f.endswith(".fb") for f in os.listdir(src)):
            continue
        dst = os.path.join(out_dir, d)
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst)
        names.append(d)
    return names


# ---------------------------------------------------------------- encoding

def encode(frames_dir, out, scale, palette, crf):
    files = sorted(f for f in os.listdir(frames_dir) if re.fullmatch(r"\d+\.fb", f))
    if not files:
        die(f"no frames in {frames_dir}")
    dark, light = PALETTES[palette]
    # monob: 1 bit per pixel, MSB first, 1 = white -- exactly the .fb layout
    lut = ":".join(f"{c}='{lo}+val*{hi - lo}/255'" for c, lo, hi in zip("rgb", dark, light))
    vf = f"scale={W * scale}:{H * scale}:flags=neighbor,format=rgb24,lutrgb={lut},scale=out_color_matrix=bt709:out_range=tv,format=yuv420p"
    cmd = ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "monob", "-s", f"{W}x{H}",
           "-r", str(FPS), "-i", "-"]
    # the game's sea sound, recorded in step with the frames (44.1 kHz stereo s16le, 1470 samples a frame),
    # brought up to the usual web loudness (-16 LUFS); the game plays it much quieter
    audio = os.path.join(frames_dir, "audio.pcm")
    if os.path.isfile(audio) and os.path.getsize(audio) > 0:
        cmd += ["-f", "s16le", "-ar", "44100", "-ac", "2", "-i", audio, "-af", "loudnorm=I=-16:TP=-1.5:LRA=11", "-ar", "48000", "-c:a", "aac", "-b:a", "192k", "-shortest"]
    cmd += ["-vf", vf, "-c:v", "libx264", "-preset", "slow", "-crf", str(crf),
           "-tune", "animation", "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
           "-movflags", "+faststart", out]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for f in files:
        with open(os.path.join(frames_dir, f), "rb") as fh:
            data = fh.read()
        if len(data) != FB_BYTES:
            p.kill()
            die(f"{frames_dir}/{f}: expected {FB_BYTES} bytes, got {len(data)}")
        p.stdin.write(data)
    p.stdin.close()
    if p.wait() != 0:
        die(f"ffmpeg failed on {out}")
    print(f"   {out}: {len(files)} frames, {len(files) / FPS:.2f} s, {W * scale}x{H * scale}"
          f"{', with sound' if '-c:a' in cmd else ''}", flush=True)


# ---------------------------------------------------------------- main

def cut(parts, out, crf):
    """Joins (clip, start_s, length_s) parts with hard cuts; the sound gets a short fade at each cut so
    nothing clicks. Clips without sound get silence."""
    args, f, cat = [], "", ""
    for i, (clip, start, length) in enumerate(parts):
        args += ["-i", clip]
        has_audio = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a", "-show_entries",
                                    "stream=index", "-of", "csv=p=0", clip],
                                   capture_output=True, text=True).stdout.strip() != ""
        f += f"[{i}:v]trim=start={start}:duration={length},setpts=PTS-STARTPTS[v{i}];"
        if has_audio:
            f += (f"[{i}:a]atrim=start={start}:duration={length},asetpts=PTS-STARTPTS,aresample=48000,"
                  f"afade=t=in:d=0.04,afade=t=out:st={max(0.0, length - 0.08):.3f}:d=0.08[a{i}];")
        else:
            f += f"anullsrc=r=48000:cl=stereo,atrim=duration={length}[a{i}];"
        cat += f"[v{i}][a{i}]"
    f += f"{cat}concat=n={len(parts)}:v=1:a=1[v][a]"
    run(["ffmpeg", "-loglevel", "error", "-y", *args, "-filter_complex", f, "-map", "[v]", "-map", "[a]",
         "-c:v", "libx264", "-preset", "slow", "-crf", str(crf), "-tune", "animation", "-pix_fmt", "yuv420p",
         "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
         "-c:a", "aac", "-b:a", "192k", "-ar", "48000", "-movflags", "+faststart", out])
    total = sum(p[2] for p in parts)
    print(f"   {out}: {len(parts)} shots, {total:g} s", flush=True)


def trailer(a):
    """The opening buoy-pass with its caption, then clip_s of each weather orbit, calmest first."""
    weathers = a.weathers.split(",") if a.weathers else WEATHERS
    clips = os.path.join(a.out, "clips")
    boat = a.boat.replace("-", "_")
    opening = os.path.join(clips, f"{boat}_buoy_pass_{a.opening}{'_hud' if a.hud else ''}.mp4")
    orbits = [os.path.join(clips, f"{boat}_{w}{'_hud' if a.hud else ''}.mp4") for w in weathers]
    if not a.recut:
        out = a.out
        a.out = clips
        a.weathers = a.opening
        shots = buoy_pass(a)
        a.weathers = ",".join(weathers)
        shots += weather_orbit(a)
        shoot(a, shots)
        a.out = out
    for c in [opening] + orbits:
        if not os.path.isfile(c):
            die(f"{c} is missing (run without --recut to render it)")
    if not 0 <= a.clip_start < a.orbit_s or a.clip_s <= 0 or a.clip_start + a.clip_s > a.orbit_s:
        die("--clip-start / --clip-s must fit inside one orbit", 2)
    print("== cutting the trailer", flush=True)
    parts = [(opening, 0, a.length_s)] + [(c, a.clip_start, a.clip_s) for c in orbits]
    cut(parts, os.path.join(a.out, a.name), a.crf)


def shoot(a, shots):
    if not shutil.which("ffmpeg"):
        die("ffmpeg not found")
    os.makedirs(a.out, exist_ok=True)
    player = HostPlayer() if a.host else SimPlayer(a.on_current_display)
    frames_root = os.path.join(a.out, "frames")
    try:
        player.build()
        for name, text in shots:
            print(f"== {name}", flush=True)
            if a.keep_scenarios:
                with open(os.path.join(a.out, f"{name}.txt"), "w") as f:
                    f.write(text)
            recorded, log = player.play(name, text, frames_root)
            if not recorded:
                die(f"{name} recorded nothing (no `record NAME` line?)")
            # a `state` line logs run=0 sailing, 1 capsized
            if re.search(r"^run=[12]", log, re.M):
                print(f"   WARNING: {name}: the boat capsized (see `state` in the log)", flush=True)
            for r in recorded:
                encode(os.path.join(frames_root, r), os.path.join(a.out, f"{r}.mp4"), a.scale, a.palette, a.crf)
    finally:
        player.close()
        if not a.keep_frames:
            shutil.rmtree(frames_root, ignore_errors=True)
    print(f"== done: {a.out}")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=__doc__.split("\n\n", 1)[1])
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp):
        sp.add_argument("--out", default=os.path.join(ROOT, "build", "promo"), help="output dir (default build/promo)")
        sp.add_argument("--scale", type=int, default=4, help="integer pixel scale (default 4: 1600x960)")
        sp.add_argument("--palette", choices=sorted(PALETTES), default="bw",
                        help="bw (default) or playdate (the real screen's grey tones)")
        sp.add_argument("--crf", type=int, default=12, help="x264 quality, lower is better (default 12)")
        sp.add_argument("--host", action="store_true", help="host harness instead of the simulator (no text)")
        sp.add_argument("--on-current-display", action="store_true", help="show the simulator on $DISPLAY")
        sp.add_argument("--keep-frames", action="store_true", help="keep the raw .fb frames in OUT/frames/")
        sp.add_argument("--keep-scenarios", action="store_true", help="write each shot's scenario to OUT/")

    s = sub.add_parser("series", help="a ready-made series of shots")
    s.add_argument("series", choices=sorted(SERIES))

    def shot_args(s):
        s.add_argument("--boat", default="dinghy", help="boat file name without .json (default dinghy)")
        s.add_argument("--weathers", help=f"comma-separated subset of {','.join(WEATHERS)} "
                                          "(default: all for weather-orbit, sunny for buoy-pass)")
        s.add_argument("--caption", default="Are you ready to sail?",
                       help="buoy-pass: text in the title font across the top ('' for none)")
        s.add_argument("--length-s", type=float, default=9, help="buoy-pass: shot length (default 9)")
        s.add_argument("--buoy-ahead", type=float, default=12, help="buoy-pass: buoy laid this far ahead, m (default 12)")
        s.add_argument("--buoy-side", type=float, default=0.3, help="buoy-pass: ...and this far to starboard, m (default 0.3)")
        s.add_argument("--cam-dist", type=float, default=3, help="buoy-pass: camera to buoy, m (default 3)")
        s.add_argument("--cam-height", type=float, default=0.9, help="buoy-pass: camera height, m (default 0.9)")
        s.add_argument("--cam-aim", type=float, default=15,
                       help="buoy-pass: view turned this far right of the buoy, deg (default 15)")
        s.add_argument("--cam-pitch", type=float, default=-5, help="buoy-pass: camera tilt, deg, - = down (default -5)")
        s.add_argument("--wind", type=float, default=60,
                       help="wind from this many degrees off the bow, + = starboard (default 60)")
        s.add_argument("--orbit-s", type=float, default=8, help="seconds for one camera orbit; the shot length (default 8)")
        s.add_argument("--settle-s", type=float, default=12, help="seconds of sailing before recording (default 12)")
        s.add_argument("--hud", action="store_true", help="show the HUD (default off)")
        s.add_argument("--reef", help="WEATHER=HOIST,... sails held at that hoist with auto trim off, or 'none' "
                                      "(default hurricane=0.16: the dinghy capsizes there at full sail)")
    shot_args(s)
    common(s)

    t = sub.add_parser("trailer", help="render the shots and cut them into one trailer")
    shot_args(t)
    t.add_argument("--opening", default="rollers", help="weather of the opening buoy-pass (default rollers)")
    t.add_argument("--clip-s", type=float, default=3, help="seconds of each weather orbit (default 3)")
    t.add_argument("--clip-start", type=float, default=1.5, help="where in each orbit the cut starts, s (default 1.5)")
    t.add_argument("--name", default="crank_sail_trailer.mp4", help="output file in --out (default crank_sail_trailer.mp4)")
    t.add_argument("--recut", action="store_true", help="re-cut from the clips already in OUT/clips/")
    common(t)

    r = sub.add_parser("run", help="play a scenario file and encode its recordings")
    r.add_argument("scenario")
    common(r)

    e = sub.add_parser("encode", help="encode a folder of .fb frames")
    e.add_argument("frames_dir")
    e.add_argument("output")
    e.add_argument("--scale", type=int, default=4)
    e.add_argument("--palette", choices=sorted(PALETTES), default="bw")
    e.add_argument("--crf", type=int, default=12)

    a = p.parse_args()
    if a.scale < 1:
        die("--scale must be at least 1", 2)
    if a.cmd == "encode":
        encode(a.frames_dir, a.output, a.scale, a.palette, a.crf)
    elif a.cmd in ("series", "trailer"):
        if not re.fullmatch(r"[a-z0-9_-]{1,23}", a.boat):
            die(f"bad boat name '{a.boat}'", 2)
        if not os.path.isfile(os.path.join(ROOT, "boats", f"{a.boat}.json")):
            die(f"no boats/{a.boat}.json", 2)
        if a.orbit_s <= 0:
            die("--orbit-s must be positive", 2)
        a.out = os.path.abspath(a.out)
        if a.cmd == "trailer":
            if a.opening not in WEATHERS:
                die(f"unknown weather '{a.opening}'", 2)
            trailer(a)
        else:
            shoot(a, SERIES[a.series](a))
    else:
        with open(a.scenario) as f:
            text = f.read()
        a.out = os.path.abspath(a.out)
        shoot(a, [(os.path.splitext(os.path.basename(a.scenario))[0], text)])


if __name__ == "__main__":
    main()
