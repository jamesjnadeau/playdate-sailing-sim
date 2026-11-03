# Baseline ghosts — design

## Intent
Every player should have something to race against from their first attempt. A host-side tool sails each
course with autopilot and autotrim and saves the result as a ghost; the game ships those ghosts and loads one
when the player has no better run of their own.

## Goals
- A generator tool that produces one baseline ghost per (course, boat) for **sunfish, dinghy, j24** in the
  **default weather (choppy)**: 9 courses x 3 boats = 27 files (~2 MB at ~5 min a race; to be measured).
- Reproducible: re-running the tool regenerates identical files (fixed seed per course, already `ghost_seed`).
- The game loads a baseline automatically; no player action.

## Non-goals
- Other boats (longship, tp52) and other weather presets: no baseline; they behave as today.
- A second ghost on screen. The renderer still shows one ghost.
- A fast or "expert" line. The baseline is a deliberately steady, safe run.

## Anti-goals
- A baseline that clips or touches a buoy, capsizes, or fails to finish. The tool must fail loudly (non-zero
  exit, names the course/boat) rather than write a bad ghost.
- Extra ghost memory on device (it has crashed on this before): no third ghost buffer.

## Approaches considered
1. **Host C tool linking `src/sim`** (chosen): runs `game_update` headless, like `tools/boat_check.c`.
2. Scripted scenarios through `harness/`: scripts cannot steer toward waypoints. Rejected.

Loading behaviour (chosen by James): **baseline is the floor.** Load the player's saved ghost if any,
otherwise the shipped baseline. A new ghost is saved only when it beats whatever was loaded. Rejected:
loading both and showing the faster, which needs a third ghost buffer.

## Design
**Generator** (`tools/ghost_gen.c`, `make ghosts`): for each course and boat in the list, `game_init` with the
choppy preset and `ghost_seed(course)`, `game_set_track`, `ghost_begin`, then loop `game_update` at a fixed dt,
`ghost_record` each step, until the race finishes or a time cap (GHOST_MAX_SECONDS) is hit. Autopilot and
autotrim are switched on through the normal button path (A, quick B). `ghost_finish`, `ghost_encode`, write
`ghosts/ghost_<course>_<boat>_<weather>.bin` using one shared name function (the weather is in the name
so more presets can be added later; the game's saved ghosts use it too, giving a personal best per weather). Reports each finish time.

**Navigator**: a small layer that each step picks the autopilot target heading. Each mark's aim point is
20 m out along its gate ray, the side the game requires the boat to cross, so it passes 20 m wide and still
counts as rounded. Heading is the bearing to the next waypoint; if that is inside the no-go zone it picks the
better tack and tacks/gybes when the leg's cross-track error or lay-line says so. Next waypoint advances when
the game's own `race.next` does. The gate-crossing test is still the game's, so the 20 m offset must still
cross each gate ray; the tool verifies the run finished with every mark rounded in order.

**Shipping**: `ghosts/` is staged into the .pdx by the Makefile like `boats/`, `config/`, `tracks/`.

**Loading** (`src/pd/main.c`): `ghost_load` reads the player's file from the data folder (`kFileReadData`), else
the bundled `ghosts/<same name>` from the pdx (`kFileRead`); two explicit opens, so nothing relies on how the
SDK orders a combined flag. `ghost_opponent` and the save rule are unchanged: `ghost_best` is whatever was loaded,
and a run replaces it only if faster. `boat_best_refresh` reads the same way so the boat screen shows baseline
times.

## Constraints
- Baseline only matches if course JSON, boat JSON, seed, preset and ghost format are unchanged; changing any
  makes the shipped ghost silently stop showing (`ghost_matches`). Mitigation: `make ghosts` is cheap, and a
  test (below) fails if a shipped file no longer matches.
- Uncommitted ghost work is in the tree (`ghost.c/.h`, `main.c`, `menu_draw.c`, `render.h`); the tool builds on it.
- Determinism: compile the tool with `-ffp-contract=off` like the harness.

## Testing
- Host test: generate one course/boat in a temp dir and assert it decodes, `ghost_matches` the live game, and
  finishes with all marks rounded.
- Test that the minimum distance to each buoy is >= ~15 m.
- Regenerate all 27 and check every one finishes (the tool's exit code).

## Left to the builder
- Navigator tuning (tack timing, lay-lines), the dt used for the headless run, and the choice of course
  time cap if a boat cannot finish in light air.
