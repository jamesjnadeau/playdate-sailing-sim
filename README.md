# CrankSail

A sailing simulator for the [Playdate](https://play.date). Sail a dinghy, a Sunfish, a J/24, or a Reynolds 33 catamaran around a wind-shifting sea, or race a set of marked courses against the clock. The d-pad picks what the crank controls: helm, sail trim, hoist or camera.

## Features

- Boats configured as JSON (`boats/`), with a physical model of hull, sails, keel and rudder; see [docs/boat-schema.md](docs/boat-schema.md) and [docs/boat-model-analysis.md](docs/boat-model-analysis.md)
- Weather and world settings in `config/`
- Racing courses in `tracks/`: triangles, windward-leeward, trapezoids and a two-buoy sprint, with an animated course preview
- Journey mode: an endless run of buoys 60 to 150 m apart (`journey_min_m` and `journey_max_m` in `config/world.json`), each leg turning a little but always heading away from the buoys already passed; the pause menu counts buoys passed
- Autopilot, auto trim and an auxiliary engine
- In-game controls screen, written in `config/help.md`

## Controls

| Input | Action |
| --- | --- |
| Up | Helm: crank turns the wheel; press again to recentre the camera and stop following the buoy |
| Right | Trim: crank sheets the sail (press again for the next sail) |
| Left | Hoist: crank raises or lowers the sail |
| Down | Look: crank swings the camera round the boat; tap again to raise/lower it; hold to follow the next buoy (hold again to go back behind); turning the crank stops following |
| A | Toggle autopilot (crank to nudge the heading) |
| B | Toggle auto trim; stops the engine instead while it runs |
| Hold B | Start the engine (1 knot, pushes along the rudder) |

The full list is on the Controls screen in the main menu. The Playdate system menu has boat, weather and go back. Go back asks for confirmation, then returns to the home screen.

## Building

You need the [Playdate SDK](https://play.date/dev/) and, for hardware builds, `arm-none-eabi-gcc`. Set `PLAYDATE_SDK_PATH` to the SDK (the scripts default to `~/Developer/PlaydateSDK-3.1.2`).

```bash
tools/sim.sh            # build SailingSim.pdx and open it in the simulator
tools/deploy.sh         # build for hardware and copy it to a connected Playdate over USB
make                    # build only
```

## Web version

`web/` builds the same game for the browser. The simulation (`src/sim/`) and the app flow (`src/app/`: menus, free range, journey, races, ghosts and best times) are the Playdate build's own C, compiled to WebAssembly. Only the drawing differs: a full-colour Three.js view instead of the 1-bit renderer.

| Key | Playdate |
| --- | --- |
| W A S D (or the arrow keys) | d-pad |
| V | A |
| C | B |
| Move the mouse (up or left, down or right) | Crank: left turns it the same way as up, right as down (click the view first to lock the pointer; the wheel works too) |
| Backspace | System menu: boat, weather, go back, mouse sensitivity and direction |

In the main menu the mouse works too: point at a row to select it, click to choose it, and click the button hints along the bottom. **About** in that bar opens a page rendered from [`web/www/about.md`](web/www/about.md); edit the Markdown to change it.

Best times and ghosts are saved in the browser (IndexedDB). On the web, rocks are drawn; the Playdate renderer leaves them out.

You need [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (`source emsdk_env.sh` so `emcc` is on `PATH`):

```bash
make -C web          # build into web/dist/
```

```bash
make -C web serve    # build and serve on http://localhost:8000
```

```bash
make -C web test     # native vs wasm parity, input unit tests, headless browser smoke test (needs Playwright)
```

The `Web build` workflow publishes `web/dist/` to GitHub Pages on every push to `main`. In the repository settings, set Pages → Build and deployment → Source to GitHub Actions.

## Tests

The simulation core in `src/sim/` has no Playdate dependencies and is tested on the host:

```bash
make -C tests
```

## Scenario harness

Scenarios are scripted playthroughs that run the real `src/pd` main loop twice: on the host against a stub Playdate API (fast, deterministic, runs in CI) and in the real Playdate simulator (slow, used to capture reference goldens).

### Scenario format

A scenario is `tests/scenarios/NAME.txt`: one command per line, `#` starts a comment, times are in frames at 30 fps.

| Command | Effect |
| --- | --- |
| `wait N` | Advance N frames with nothing pressed |
| `press X` | Press button X for one frame (`left`, `right`, `up`, `down`, `A`, `B`) |
| `hold X N` | Hold button X for N frames |
| `crank DEG for N` | Turn the crank DEG degrees on each of N frames |
| `snap NAME` | Save the frame buffer as `NAME.fb` (`NAME` is `[A-Za-z0-9_]+`; needs at least one frame before it) |
| `state` | Save the game state as text |
| `sysmenu back\|boat N\|weather N` | Pick that system-menu item on the next frame |
| `record NAME` / `record off` | Save every frame from the next one on as `NAME/00000.fb`, `NAME/00001.fb`, ... (for videos) |
| `hud on\|off` | Draw or leave out the HUD (bar, markers, banners, prompts) from the next frame |
| `wind DEG` | Turn the wind to blow from DEG degrees off the bow (+ = starboard) on the next frame; put it after any `sysmenu weather` |
| `orbit DEG` | Swing the camera DEG degrees per second round the boat, every frame from the next one; `orbit 0` stops |
| `end` | Stop the scenario |

Two `press A` lines in a row land on consecutive frames, so put a `wait 1` between them if the game should see two separate taps. A `sysmenu` with no frame after it is a parse error. The grammar is in `src/pd/scenario.c`.

### Running a scenario

```bash
make -C harness run SCEN=smoke   # one scenario, output in harness/build/out/smoke/
make -C harness run-all          # every scenario
build/harness SCENARIO.txt OUTDIR ASSETS_ROOT   # the host binary itself (run from harness/)
```

The host binary exits 0 on success, 1 on timeout and 2 on a parse error. Frames are `.fb` dumps; `tools/fb2png.py IN.fb OUT.png` turns one into a PNG and `tools/fbdiff.py A.fb B.fb` compares two. `PLAYDATE_SDK_PATH` must point at the SDK for the harness make targets (they need its headers).

### The two checks

`make -C harness check` runs both (`check-fidelity` and `check-regression` run one each):

- Fidelity compares the host frames with the real-simulator goldens in `tests/golden/real/`, ignoring the masks in `NAME.masks` (text regions the host stub does not draw) and allowing the mismatch in `tests/scenarios/default.tolerance` (0.0065, a fraction of the unmasked pixels). A scenario with no real golden only warns.
- Regression compares the host frames and states with `tests/golden/host/`: identical bytes pass, otherwise frames may differ by `tests/scenarios/regression.tolerance` (default 0.0005 of pixels, for libc differences between machines) and states within 0.001. It always blocks and fails if it checked no snapshots. Fidelity also compares every real `.state` with the host output (tolerance 0.01). After an intended change, review the frames and run `make -C harness update-golden`.

CI runs both and uploads PNGs of any failing frames as the `scenario-diffs` artifact (locally they are in `harness/build/diff/`).

### Refreshing the real goldens

```bash
tools/golden_refresh.sh [--on-current-display] [scenario ...]
```

This builds `SailingSimScenario.pdx` (`make SCENARIO=1`) and plays each scenario in the real simulator on its own Xvfb, capturing frames with `getFrame` (`getDisplayFrame` lags a frame behind). Needs `PLAYDATE_SDK_PATH` and Xvfb (`XVFB_BIN`, or `xvfb`/`Xvfb` on `PATH`). The GitHub workflow `golden-refresh` does the same and uploads `tests/golden/real/` as an artifact; it commits nothing. To compare a real `.state` with the host's: `python3 -I tools/statediff.py REAL.state HOST.state`.

Warning: never start the simulator with `WAYLAND_DISPLAY` set, it opens on your desktop. The script unsets it and forces `DISPLAY` to its own Xvfb (`GDK_BACKEND=x11`, software GL). It also wipes `$SDK/Disk/Data/com.jamesnadeau.sailingsim` before each scenario, which would disturb a simulator session of this game you have open. The simulator ignores SIGTERM, so the script sends TERM and then KILL to its own process only.

### What the fidelity check can and cannot see

- One global tolerance of 0.0065 catches roughly a 19x19 block of solid pixels. Smaller changes pass fidelity, and only the regression check sees them.
- Text is masked because the host stub draws none, so the check cannot see text bugs.
- The remaining 230-390 px per frame are 1px edge differences on the sail, hull and horizon. Two stub rasterizer variants (a line offset and polygon edge inclusion) were worse. Line caps, ellipse extent and dither patterns were not tried.

### Frame-dump tools that remain

`tools/frames.sh` (with `tools/frame_dump.c`) renders fixed gallery views to PNG: wake trails in three weathers, side views, forced heel angles and the longship scenes. Scenarios do not replace these, since they drive the game rather than posing the renderer. `tools/gif_clip.c` and `tools/boat_clip.sh` render the boat clips for the PR check. All three stay.

## Promo videos

`tools/promo_video.py` plays scripted shots in the real simulator (headless, on its own Xvfb) and encodes each recording as an MP4 (H.264, 30 fps, 1600x960 by default: 4x nearest-neighbour). It needs `PLAYDATE_SDK_PATH`, Xvfb and ffmpeg.

```bash
tools/promo_video.py series weather-orbit          # the dinghy in every weather, HUD off -> build/promo/dinghy_<weather>.mp4
```

```bash
tools/promo_video.py series weather-orbit --hud --boat j24 --weathers storm,hurricane
```

Each `weather-orbit` shot starts a free-range run, picks the boat and weather, turns the wind to come from `--wind` degrees off the bow (default 60, the same for every shot), engages the autopilot on the starting heading and the auto trim, sails for `--settle-s` seconds (default 12) and then records one full camera orbit of `--orbit-s` seconds (default 8). The camera ends at the angle it started from (the sea and boat keep moving, so it is not a seamless loop). In the hurricane the dinghy capsizes in under a second at full sail, and the auto trim hoists a reefed sail back up, so there the sails are reefed to 16% and the auto trim stays off (`--reef WEATHER=HOIST,...` changes this, `--reef none` turns it off). The tool warns when a shot ends capsized.

Your own shots are ordinary scenarios using `record`, `hud`, `wind` and `orbit`; `tools/promo_video.py run my_shot.txt` writes one MP4 per `record NAME`. Common options: `--out DIR` (default `build/promo`), `--scale N`, `--palette playdate` (the real screen's grey tones instead of black and white), `--crf N` (quality, default 12), `--keep-frames` (keep the `.fb` frames; `tools/promo_video.py encode DIR OUT.mp4` re-encodes them) and `--keep-scenarios`. `--host` plays the shots on the host harness instead: about 5x faster, but it draws no text, so use it only to preview shots with the HUD off.

Like `golden_refresh.sh`, it wipes `$SDK/Disk/Data/com.jamesnadeau.sailingsim` before every shot.

## Contributing a boat

A boat is one JSON file in `boats/`; see [docs/boat-schema.md](docs/boat-schema.md) for the fields and naming rules. Open the pull request with the [boat template](https://github.com/jamesjnadeau/playdate-sailing-sim/compare?template=boat.md). The PR check runs the tests, validates the file and renders a sail-and-pan clip you can download as the `boat-clips` artifact. Code changes get the same check, rendering the J/24.

## Layout

| Path | Contents |
| --- | --- |
| `src/sim/` | Platform-independent simulation: boat, sail, sea, weather, race, menu, controls |
| `src/app/` | Platform-independent app flow shared by the Playdate and web builds: menus, races, ghosts, best times, config loading |
| `src/pd/` | Playdate glue: main loop, rendering, HUD, menu drawing |
| `web/` | Browser build: Emscripten glue (`web/src/`), the Three.js page (`web/www/`) and its tests |
| `boats/`, `tracks/`, `config/` | JSON and text data copied into the `.pdx` at build time |
| `tests/` | Host unit tests, scenarios (`tests/scenarios/`) and goldens (`tests/golden/`) |
| `harness/` | Host scenario harness and golden checks |
| `tools/` | Simulator, deploy, golden-refresh, frame and clip tools |

## License

[GNU AGPL v3](LICENSE).
