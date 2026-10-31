# Sailing Sim

A sailing simulator for the [Playdate](https://play.date). Sail a dinghy, a Sunfish, a J/24, a TP52 or a Viking longship (free range only) around a wind-shifting sea, or race a set of marked courses against the clock. The d-pad picks what the crank controls: helm, sail trim, hoist or camera.

## Features

- Boats configured as JSON (`boats/`), with a physical model of hull, sails, keel and rudder; see [docs/boat-schema.md](docs/boat-schema.md) and [docs/boat-model-analysis.md](docs/boat-model-analysis.md)
- Weather and world settings in `config/`
- Racing courses in `tracks/`: triangles, windward-leeward, trapezoids and a two-buoy sprint, with an animated course preview
- Autopilot, auto trim, anchor and paddling
- In-game controls screen, written in `config/help.md`

## Controls

| Input | Action |
| --- | --- |
| Up | Helm: crank turns the wheel |
| Right | Trim: crank sheets the sail (press again for the next sail) |
| Left | Hoist: crank raises or lowers the sail |
| Down | Look: crank swings the camera |
| Hold A | Toggle autopilot (tap A to step the heading, crank to nudge it) |
| Hold B | Toggle auto trim (tap B for the anchor, mash B to paddle) |

The full list is on the Controls screen in the main menu. The Playdate system menu has boat, weather, restart and main menu.

## Building

You need the [Playdate SDK](https://play.date/dev/) and, for hardware builds, `arm-none-eabi-gcc`. Set `PLAYDATE_SDK_PATH` to the SDK (the scripts default to `~/Developer/PlaydateSDK-3.1.2`).

```bash
tools/sim.sh            # build SailingSim.pdx and open it in the simulator
tools/deploy.sh         # build for hardware and copy it to a connected Playdate over USB
make                    # build only
```

## Tests

The simulation core in `src/sim/` has no Playdate dependencies and is tested on the host:

```bash
make -C tests
```

## Layout

| Path | Contents |
| --- | --- |
| `src/sim/` | Platform-independent simulation: boat, sail, sea, weather, race, menu, controls |
| `src/pd/` | Playdate glue: main loop, rendering, HUD, menu drawing |
| `boats/`, `tracks/`, `config/` | JSON and text data copied into the `.pdx` at build time |
| `tests/` | Host unit tests |
| `tools/` | Simulator, deploy and frame-dump scripts |

## License

[GNU AGPL v3](LICENSE).
