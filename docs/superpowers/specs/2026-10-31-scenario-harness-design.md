# Scenario harness: scripted interaction tests with real-simulator goldens

## Intent

Today `tools/frame_dump.c` renders frames against a hand-written stub of the Playdate
graphics API and calls `game_update` directly from C. The PNGs therefore show the draw
calls but not what the Playdate simulator actually draws, and the real `main.c` loop
(menu, input handling, camera snap, system menu) is never exercised.

We want a programmable way to drive the game the way a player does (buttons, crank,
menus), capture the screen, and run that in CI, while knowing how closely the captured
frames match the real simulator.

## Goals

- Scenario scripts (plain text) that press buttons, turn the crank and wait, then
  capture frames and state. The same script runs in two places:
  1. **Host build** (CI, every push): the real `src/pd/main.c` compiled for the host
     against a stub Playdate API. Fast, deterministic, no display, no SDK binary.
  2. **Real simulator** (on demand): the same game built as a `.pdx` with the scenario
     player compiled in, run in `PlaydateSimulator`, writing real framebuffers.
- Goldens captured from the real simulator are checked into the repo. CI compares the
  host build's frames against them with a tolerance, so drift in the stub is a CI
  failure instead of a silent fidelity loss.
- A script to refresh the real goldens (`tools/golden_refresh.sh`), runnable locally
  or in a manual / scheduled CI job.
- Frames are 400x240 1-bit, compared as bits (not PNGs). A tool converts them to PNG
  for humans and for PR artifacts.

## Non-goals

- Pixel-identical host output. The host stub approximates the simulator; the tolerance
  against the real goldens is what bounds the approximation.
- Running the simulator on every push. It needs Xvfb, software GL and the SDK's GTK /
  WebKit dependencies; it was seen to start and draw the menu under Xvfb here, but is
  not a dependable per-push dependency.
- Reproducing the system font on the host (the font lives inside the simulator
  binary). Text is handled by masking, see Comparison.
- Replacing `make -C tests` unit tests. Those stay as they are.

## Anti-goals

- No scenario or harness code on the device path: the shipped `.pdx` and the device
  build must be byte-for-byte unaffected unless the scenario flag is set.
- No second copy of the game loop. The harness must drive the real `update()` in
  `main.c`, not a reimplementation.

## Approaches considered

- **A. Real simulator under Xvfb in CI.** Exact pixels, but slow and flaky (software
  GL, a GUI app the game cannot exit, SDK download in the job). Rejected as the
  per-push check; kept as the golden source.
- **B. Host build with a calibrated stub only.** Reliable, but nothing says the stub
  matches the simulator.
- **C. B on every push, plus real-simulator goldens as the fidelity reference.**
  Chosen by the project owner.

## Design

### Seam in `main.c`

`update()` reads input from `pd->system->getButtonState` / `getCrankChange` and time
from `getElapsedTime`. A small module `src/pd/scenario.c/.h` provides, when
`-DSCENARIO` is defined:

- `scenario_poll(&cur, &pushed, &released, &crank, &dt)`: replaces the real input
  reads in `update()`; `dt` is fixed at 1/30 s.
- `scenario_frame_done(pd, const Game *)`: called at the end of `update()` after
  rendering; handles `snap` / `state` commands.

Without `-DSCENARIO` both compile to the current direct calls (a macro or inline
wrapper in `scenario.h`), so the normal and device builds are unchanged. The fresh
seed in `main.c` (`fresh_seed`) must also be fixed under `-DSCENARIO` so scenarios are
reproducible; this uses the frame counter instead of
`getCurrentTimeMilliseconds`.

### Scenario format

One command per line, `#` comments. Times are frames at 30 fps.

```
# menu -> free range -> sail on autopilot
wait 2
snap menu_home
press A                  # one-frame press
hold A 20                # held for 20 frames (autopilot toggle)
crank 15.0 for 30        # crank degrees per frame for 30 frames
wait 90
snap sailing_a
state                    # appends key=value lines (app, mode, heading, speed, ...) to the log
```

The player is a small state machine over the parsed lines. The file is read through
`pd->file` so the same code works in the stub and in the simulator. The scenario
path comes from a file `scenario.txt` at the `.pdx` root (simulator) or the stub's
command line (host). Exact command set beyond the above is the builder's call, as long
as it stays text and one-command-per-line.

### Output

- `snap NAME` writes `NAME.fb`: the 12000-byte (`LCD_ROWSIZE` 52 x 240, trimmed to
  400 bits per row) 1-bit buffer returned by `pd->graphics->getDisplayFrame()`.
  `NAME.state` holds the text from `state`.
- Host: written to the output dir given on the command line.
- Simulator: written via `pd->file` into the game's Data directory. `golden_refresh.sh`
  collects them, then writes `done.marker`, after which the script kills the simulator
  (the game cannot quit it from C).
- `tools/fb2png.py` (Pillow, already a CI dependency) converts `.fb` to PNG at 2x.

### Host build

New directory `harness/` (host only, not shipped):

- `pdstub.c`: implements the `PlaydateAPI` pieces the game uses today: `system`
  (buttons/crank/clock/menu items/update callback/menu image/logging), `file`
  (open/read/close/listfiles against a directory copy of `boats/ config/ tracks/`),
  `display`, and `graphics` (drawLine, drawRect, fillRect, fillPolygon, fillTriangle,
  drawEllipse, fillEllipse, drawText, setDrawMode, clear, bitmaps/contexts,
  getDisplayFrame). The rasterizers are lifted from `tools/frame_dump.c` and then
  tuned against the real goldens.
- `host_main.c`: loads the scenario, calls `eventHandler(kEventInit)`, then calls the
  registered update callback until the scenario ends.
- Built by `harness/Makefile` (or a `make harness` target) with
  `-DSCENARIO -DTARGET_SIMULATOR=1`, compiling `src/pd/*.c` and `src/sim/*.c`.
  Needs the SDK headers only (already fetched in `pr-check.yml`).

`tools/frame_dump.c` and `tools/frames.sh` are superseded for scenario work; they are
left in place until the scenarios cover what they show, then removed.

### Real-simulator build

`make SCENARIO=1` produces a `.pdx` (separate output name so it never overwrites the
normal one) with `-DSCENARIO` and the scenario file. `tools/golden_refresh.sh`:
builds it, starts `PlaydateSimulator` (under `xvfb-run`/Xvfb with
`LIBGL_ALWAYS_SOFTWARE=1`, `GDK_BACKEND=x11` and `WAYLAND_DISPLAY` unset when no
display is wanted, otherwise on the current display), waits for `done.marker`, kills
the simulator, and copies the `.fb` / `.state` files into `tests/golden/real/`.

### Comparison

`tools/fbdiff.py` (and a host test target) compares two frames:

- **Regions**: each scenario/snapshot may carry a list of rectangles to ignore (text
  that the host cannot reproduce); the default is to mask nothing and the builder
  decides, per snapshot, once real goldens exist.
- **Tolerance**: a fraction of mismatching pixels outside the ignored regions, per
  snapshot, with a default threshold; the builder calibrates the default after the
  first real goldens exist and records it.
- Output on failure: a PNG with expected / actual / diff side by side, uploaded as a
  CI artifact.

Two checks run in CI on every push:

1. **Fidelity**: host frames vs `tests/golden/real/` with the tolerance above.
2. **Regression**: host frames vs `tests/golden/host/`, exact match, so any change in
   rendering is a visible diff in the PR (the owner approves it by refreshing those
   with `harness/Makefile` target `update-golden`).

State dumps are compared exactly (host) against `tests/golden/host/*.state`; the real
simulator's state dumps are compared against the host's within a small numeric
tolerance, since both run the same C simulation.

### CI

- `pr-check.yml` gets a step after unit tests: build the host harness, run all
  scenarios in `tests/scenarios/`, run the two checks, upload diff PNGs on failure.
- A separate workflow `golden-refresh.yml` (manual `workflow_dispatch`) installs
  Xvfb and the simulator's GTK/WebKit/GL libraries, downloads the SDK, runs
  `golden_refresh.sh` and opens the result as an artifact (not a commit). Whether the
  simulator starts in that environment is unverified; the first job is expected to need
  iteration. Local refresh is the supported path until it works.

## Decisions made by the owner

- Approach C (host stub in CI, real-simulator goldens as the fidelity reference).
- CI must run it without the owner's involvement.

## Calls made without the owner (change freely)

- Frames compared as 1-bit buffers, not PNGs.
- Scenario text format and the `-DSCENARIO` seam in `main.c` rather than xdotool or
  Playdate input recording.
- Two golden sets: real (tolerance) and host (exact).
- `tests/scenarios/`, `tests/golden/real/`, `tests/golden/host/`, `harness/`.
- Fixed `fresh_seed` and fixed `dt` under `-DSCENARIO`.
- Real-simulator golden refresh is manual (local script and `workflow_dispatch`), not
  per push.

## Answers to the builder check

- **Producing the first real goldens is part of the build.** The builder can run the
  simulator: it was started under a locally unpacked Xvfb (`apt-get download xvfb`,
  `dpkg -x`, no root) with `LIBGL_ALWAYS_SOFTWARE=1 GDK_BACKEND=x11` and `WAYLAND_DISPLAY`
  unset, and drew the main menu. So the plan includes a tuning phase after the harness
  exists: capture goldens, tune the stub, calibrate tolerance and masks. Never launch the
  simulator without unsetting `WAYLAND_DISPLAY`, or it opens on the developer's desktop.
- **Empty real goldens in CI (my call):** a snapshot with no file in `tests/golden/real/`
  is skipped with a warning in the fidelity check, and fails once its golden exists. The
  first PR can go green; the regression check against host goldens is always blocking.
- **Many scenarios, one simulator (my call):** one simulator launch per scenario.
  `golden_refresh.sh` loops over `tests/scenarios/*.txt`, copies one into the `.pdx` as
  `scenario.txt`, launches, waits for `done.marker`, kills, collects into
  `tests/golden/real/<scenario>/`. A fresh process per scenario keeps `main.c` statics
  from leaking between scenarios; refresh is manual so speed matters less. Host output
  uses the same `<scenario>/<snapshot>` naming; the host runs a scenario per process
  too. The scenario ends with an `end` command (or end of file).
- **Cross-machine "exact" (my call):** host builds use `-ffp-contract=off` and fixed
  `-O1`. `.state` values are printed at `%.3f`. Host goldens are generated and checked
  with the same compiler family (`gcc`). If CI still flips pixels, the regression check
  falls back to a tiny mismatch threshold rather than pinning libm.
- **Persistent state:** none. The game never writes files (`pd->file` is read-only in
  `src/`), so there are no saves to leak. The only file writes are the harness's own
  snapshots, into an output directory (host) or the Data directory (simulator, wiped by
  `golden_refresh.sh` before each launch).

## Left to the builder

- Exact command set, parser and error reporting of the scenario language.
- Whether the stub's drawText draws anything (blank box vs the SDK's fonts) and which
  regions get masked.
- Calibrating the tolerance, and tuning the stub rasterizers (line caps, polygon fill
  rule, dither patterns) against the first real goldens.
- The initial scenario set (menu navigation, free range sail, racing start,
  capsize/restart, boat and weather change via the system menu).
- How the system menu items (boat, weather, restart, main menu) are triggered from a
  script; the stub must expose them.
- Build wiring (`Makefile` target names, flags) and the Xvfb/library setup for the
  refresh workflow.

## Risks

- The simulator may not start in a clean container; checked once on a developer
  machine only (Xvfb, software GL, X11 forced), never in a container.
- Stub drift is only caught when goldens are refreshed; staleness is bounded by how
  often someone runs `golden_refresh.sh`.
- `getDisplayFrame()` returns the frame the system last displayed, which may lag one
  frame behind in the simulator; the first scenario run must settle which of
  `getFrame()` / `getDisplayFrame()` matches what the host captures.
