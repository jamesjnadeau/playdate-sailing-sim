# Square-Rig Longship Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first square-rigged boat, a Viking longship (one big square sail, steering oar), chosen as the easiest square rig to sail, as a fifth selectable boat.

**Architecture:** The sail force model already handles a square sail: `trim` is the sail's chord angle off the centreline (90 deg = yard square across), `awa_min_deg` gives the no-go window, and the drag-dominated `cl`/`cd` tables make a run work. So the boat is mostly data (`boats/longship.json`) plus one new sail `shape` value (3 = square) that the renderer draws as a yard-hung rectangle centred on the mast instead of a boom-and-luff triangle.

**Tech Stack:** C99, Playdate C API; headless host tests in `tests/` (`make -C tests`, ASan/UBSan, `t.h` macros); JSON boat config.

**Spec:** none beyond the user's request, verbatim: "I want to add a square rig sailing boat, please add the easiest to sail first".

## Decisions made in this plan (not in the request)

- **The boat is a Viking longship**, ~12 m, one square sail. Easiest because: one sail, nothing to gybe (a dead run is the stable point of sail), very heavy and stable (no practical capsize), forgiving steering oar. Later square riggers (brig, ship-rigged clipper) are out of scope.
- **Name/file:** `longship` / `boats/longship.json`, appended as boat index 4 so Sunfish stays the default (index 0).
- **Square sail = `shape` 3**, drawn only; physics is unchanged. `luff` = sail height, `foot` = yard width (constant top to bottom), `tack_x` = `mast_x`, `tack_z` = height of the foot above the deck.
- **No upwind ability by design:** `awa_min_deg` ~55 so the sail goes dead (flogs) closer than that. The autopilot's `autopilot_no_go_deg` is set wide (65) so tapping A never steps the heading into the no-go zone.
- **Open hull:** the cockpit well is the whole interior (long, `cockpit_depth` > 0) so the existing "every boat has a cockpit" test keeps its meaning.
- **Boat class, requested in review:** a new top-level boat-JSON key `"boat_class"` (int, default 0): `0` racer, `1` cruiser. A cruiser can be sailed in free range but is not offered for racing. Constants `BOAT_CLASS_RACER`/`BOAT_CLASS_CRUISER` live in `src/sim/boatcfg.h`. The longship is a cruiser (it cannot sail the upwind legs, so it could never finish a course). Existing boats omit the key and stay racers.
- **Race gating lives in the menu:** the boat screen, which only appears in the racing flow, shows cruisers as `<name> (free range only)`, the cursor skips them, and it opens on the first racer when the current boat is a cruiser. `Menu` gets `menu_set_boat_racing(Menu*, const bool *raceable)`. The app fills the array once at startup by reading each boat's `boat_class` (`assets_boat_raceable`). Free range and the system-menu boat option are unchanged (all boats). The system menu already hides the boat option during a race.

## Global Constraints

- A missing `boat_class` means racer (0); only 0..1 are valid (out of range clamps with a warning, like every other field).
- Boat JSON must be under 16000 bytes and load with no warnings (`assets.c` reads boats into a 16 KB buffer; tests assert both).
- `crew_mass` is capped at 500; `shape` range becomes 0..3; `awa_min_deg <= awa_max_deg`.
- `src/sim` must not include Playdate headers.
- Tests build with `-Wall -Wextra -Werror` and ASan/UBSan.
- Numeric values in `boats/longship.json` are starting points; the tests in Task 3 are the spec. Tune the JSON until they pass, do not loosen the tests.
- Baseline: `make -C tests` already fails on `test_weather` (`memcmp(&d, &l, sizeof d)`) before any change here; it runs last. Treat it as pre-existing and unrelated. `src/pd/hud.c` also has uncommitted edits; do not stage it.

## Review Focus

- Wind inside the no-go window (twa 20): sail makes no forward thrust, the boat does not accelerate astern, state stays finite. (Task 3)
- Wind flips side on a dead run (awa +175 to -175): sail force is the mirror image, no jump in forward force, so a gybe is a non-event. (Task 3)
- 35 kn on a beam reach and on a run with the sail sheeted hard: no capsize, no NaN. (Task 3)
- Sail fully lowered (`hoist_target` 0): nothing drawn, boat drifts, finite state. (Task 3 sim, Task 4 draw guard)
- Boat screen with every boat a cruiser (or `raceable` NULL): no hang or empty screen; falls back to all selectable. (Task 5)
- Entering the race flow while the current boat is a cruiser (the player was just sailing the longship): cursor starts on a racer, and A can never return a cruiser as `menu.boat`. (Task 5)
- JSON with `shape` 4 or -1: clamped to the 0..3 range with a warning naming the field. (Task 1)

---

### Task 1: Accept `shape` 3 in boat config

**Files:**
- Modify: `src/sim/boatcfg.c:87` (`FI(SailCfg, shape, 0, 0, 2)`), `src/sim/boatcfg.h` (comment on `shape`)
- Modify: `docs/boat-schema.md` (the `shape` row of the final table: `0, 1, 2, 3` and `3 square sail (drawing only)`)
- Test: `tests/test_boatcfg.c`

**Interfaces:**
- Produces: `SailCfg.shape` may be 3. No other API changes.

- [ ] **Step 1: Write the failing test `test_square_shape_accepted`** in `tests/test_boatcfg.c` (register with `T_RUN`): load `{"sails":[{"name":"sq","shape":3}]}` with the file's `load(&c, s, warn)` helper; assert it returns true, `warn[0] == 0`, `c.sails[0].shape == 3`. Second case `"shape":4` : returns false, `c.sails[0].shape == 3`, `strstr(warn, "shape") != NULL`. Third case `"shape":-1`: clamps to 0 with a warning.
- [ ] **Step 2: Run** `make -C tests build/test_boatcfg && tests/build/test_boatcfg`. Expected: FAIL on the first case (`shape` 3 clamped to 2).
- [ ] **Step 3: Change the `shape` field's max from 2 to 3** in `src/sim/boatcfg.c:87`; update the `shape` comment in `boatcfg.h` and the schema table in `docs/boat-schema.md`.
- [ ] **Step 4: Run the same command.** Expected: PASS (all boatcfg tests, including `test_dinghy_json_reproduces_default`).
- [ ] **Step 5: Commit**
  ```bash
  git add src/sim/boatcfg.c src/sim/boatcfg.h docs/boat-schema.md tests/test_boatcfg.c
  git commit -m "feat: accept sail shape 3 (square) in boat config"
  ```

---

### Task 2: Boat class field

**Files:**
- Modify: `src/sim/boatcfg.h` (`BoatCfg` gains `int boat_class;`; add `enum { BOAT_CLASS_RACER = 0, BOAT_CLASS_CRUISER = 1 };`), `src/sim/boatcfg.c` (field table next to `world_units_per_metre`: `FI(BoatCfg, boat_class, 0, 0, 1)`)
- Modify: `docs/boat-schema.md` (top-level row: `boat_class`, 0 racer / 1 cruiser, cruisers are free range only)
- Test: `tests/test_boatcfg.c`

**Interfaces:**
- Produces: `BoatCfg.boat_class`; `BOAT_CLASS_RACER`, `BOAT_CLASS_CRUISER`. `boatcfg_default` gives `BOAT_CLASS_RACER`.

- [ ] **Step 1: Write the failing tests** (register with `T_RUN`): `test_boat_class_defaults_to_racer` (`boatcfg_default` and `{}` both give `BOAT_CLASS_RACER`, and every existing boat file `sunfish`, `dinghy`, `j24`, `tp52` loads as racer); `test_boat_class_cruiser_accepted` (`{"boat_class":1}` gives `BOAT_CLASS_CRUISER`, no warning); `test_boat_class_out_of_range_clamps` (`{"boat_class":2}` returns false, value 1, warning contains `boat_class`; `-1` gives 0 with a warning); `test_boat_class_round_trips` (write a cruiser cfg with the file's existing dump helper and reload; class is kept).
- [ ] **Step 2: Run** `make -C tests build/test_boatcfg && tests/build/test_boatcfg`. Expected: FAIL (unknown field / no member `boat_class`).
- [ ] **Step 3: Add the member, enum and table row** above; update the schema doc.
- [ ] **Step 4: Run the same command.** Expected: PASS.
- [ ] **Step 5: Commit**
  ```bash
  git add src/sim/boatcfg.h src/sim/boatcfg.c docs/boat-schema.md tests/test_boatcfg.c
  git commit -m "feat: add boat_class (racer or cruiser) to boat config"
  ```

---

### Task 3: `boats/longship.json` and its physics tests

**Files:**
- Create: `boats/longship.json`
- Modify: `tests/test_boatcfg.c`, `tests/test_boat.c`, `tests/test_sail.c`, `tests/test_autotrim.c`

**Interfaces:**
- Consumes: `shape` 3 from Task 1, `boat_class` from Task 2; `boatcfg_load`, `boat_init`, `boat_inputs_init`, `boat_step`, `sail_force`, `autotrim_best_trim(const SailCfg*, float awa, float aws, float heel)`.
- Produces: `boats/longship.json`, one sail named `"square"`, `shape` 3.

Starting values (tune to the tests): top level `"boat_class": 1` and `"world_units_per_metre": 10`; `hull` length 12, beam 2.6, mass 7000, crew_mass 500, hull_speed 5, `righting` strong and monotone with `capsize_deg` 120; `foils` shallow keel (`board_area` ~0.25, `board_stall_deg` 14), steering oar `rudder_x` -5.8, `rudder_area` 0.3, `rudder_max_deg` 35; sail `area` 44, `luff` 5.5, `foot` 8, `ce_x` 0.2, `ce_z` 4, `trim_min_deg` 35, `trim_max_deg` 90, `awa_min_deg` 55, `awa_max_deg` 180, `shape` 3, `tack_x` = `mast_x`, `tack_z` 0.8, `camber` 0.15; `look` `mast_x` 0.3, `mast_height` 8.5, `forestay_x` 5.8, `hull_width` symmetric and pointed at both ends (`[[0,0],[0.12,0.5],[0.5,1],[0.88,0.5],[1,0]]`), `freeboard` 0.9, cockpit spanning about x -4.5..4.5 with half-width ~0.9, `cockpit_depth` 0.6; `controls.autopilot_no_go_deg` 65; `camera` `height` 2.4, `x_offset` -7, `far_height` 5. Sail `cl`/`cd` tables: lift peaks near 30 deg AoA then falls toward a drag-dominated plateau, so a run (AoA ~90) drives mostly by drag (copy the J/24 main's tables as a base).

- [ ] **Step 1: Write the failing tests**
  - `tests/test_boatcfg.c`
    - `test_longship_json`: `load_boat("../boats/longship.json", &c)` (loader already asserts size < 16000 and no warnings); `c.hull.length` in 11..13; `c.sail_count == 1`; `c.sails[0].shape == 3`; `c.sails[0].trim_max_deg == 90`; `c.sails[0].awa_min_deg` in 40..70 and `awa_max_deg == 180`; `c.look.mast_x` within the hull (`fabsf(mast_x) < 0.5f * length`); `c.controls.autopilot_no_go_deg >= 60`; `c.boat_class == BOAT_CLASS_CRUISER`.
    - extend `test_every_boat_has_a_cockpit_inside_the_hull`: add `"../boats/longship.json"` to `files`, loop to 5.
  - `tests/test_boat.c` (new helper `load_longship(Sim *s, float twa_deg, float wind_ms, float u0)` modelled on `test_sunfish_sails_without_capsizing`: load the file into `s->cfg`, then `boat_init`, set heading `PI - D(twa)`, `u`, `boat_inputs_init`; set `autopilot = 0`, `autotrim = 0`):
    - `test_longship_runs_downwind`: twa 180, 12 kn, trims/targets 90 deg, wheel 0, 60 s: `s.b.u` in 2.0..4.5 m/s; max |heel| < 10 deg; |wrap_pi(heading - start)| < 25 deg; finite state.
    - `test_longship_beam_reach`: twa 90, 12 kn, trim target 60 deg, 60 s: `u` in 2.5..5.0; max |heel| < 15 deg.
    - `test_longship_cannot_point_close`: twa 30, 12 kn, `u0` 2.0, trim target 40 deg, 60 s: `u < 1.0` and finite state (it stalls; it must not reverse violently: `u > -0.5`).
    - `test_longship_no_thrust_in_the_no_go_zone`: twa 20, 12 kn, `u0` 0, 30 s: `u > -0.5` and `u < 0.8`; finite.
    - `test_longship_survives_a_gale`: twa 90 and twa 180 at 35 kn, full hoist, trim 45 and 90: max |heel| < `D(capsize_deg)` in all four; finite.
    - `test_longship_drops_its_sail`: `hoist_target[0] = 0`, twa 90, 12 kn, 30 s: `b.hoists[0] < 0.02f`; finite; `u < 1.0f` after the sail is down.
  - `tests/test_sail.c`: `test_longship_gybe_is_symmetric`: load longship's sail 0; `sail_force(s, D(90), 1, D(175), 6, 0, 1)` vs the same at `-D(175)`: `fx` equal within 1%, `fy` equal and opposite within 1%, `fx > 0`.
  - `tests/test_autotrim.c`: `test_longship_squares_up_on_a_run`: `autotrim_best_trim(&sail, PI, 6, 0)` within 1 deg of `D(90)`; on a reach (`D(100)`) the result is >= 0 and <= `D(90)`.
- [ ] **Step 2: Run** `make -C tests build/test_boatcfg build/test_boat build/test_sail build/test_autotrim` then each binary. Expected: FAIL (cannot open `../boats/longship.json`).
- [ ] **Step 3: Create `boats/longship.json`** in the exact key layout of `boats/j24.json` (same group names; `sails` array of one entry), starting from the values above. Keep it compact (< 16000 bytes).
- [ ] **Step 4: Tune and re-run** the four test binaries until every assertion passes. Change the JSON only; if an assertion is physically unreachable, stop and report which one rather than editing the test.
- [ ] **Step 5: Run** `make -C tests`. Expected: everything passes except the pre-existing `test_weather` memcmp failure.
- [ ] **Step 6: Commit**
  ```bash
  git add boats/longship.json tests/test_boatcfg.c tests/test_boat.c tests/test_sail.c tests/test_autotrim.c
  git commit -m "feat: add longship boat data and physics tests"
  ```

---

### Task 4: Draw the square sail

**Files:**
- Modify: `src/pd/render.c` (`draw_sail`, ~line 697-770; the mesh loop and head edge)
- Modify: `tools/frame_dump.c` (new scenario `longship`)

**Interfaces:**
- Consumes: `SailCfg.shape == 3`; `BoatState.swing[i]`, `trims[i]`, `luff[i]`, `hoists[i]`; existing `heel_xf`, `rel_pt`, `render_pattern`.
- Produces: nothing new for other tasks; `draw_sail` handles shape 3.

Geometry for a shape-3 mesh point (row `u` 0 foot..1 head, column `v` 0 windward end..1 leeward end), with `side = b->swing[i]` and `tr = b->trims[i]`; this replaces the `x`, `y`, `z` computation in the existing loop for that shape only:

```
z = tz + (hz - tz) * u                       /* hz as now: tz + luff*hoist */
x = mast_x + (v - 0.5) * foot * cos(tr) * side + belly
y = (v - 0.5) * foot * sin(tr) + flutter
belly = camber * foot * 4v(1-v) * sin(pi*u) * (1 - luff_state)   /* bellies forward */
```

For shape 3 the width is `s->foot` unscaled (hoisting raises the head, it does not narrow the yard). The yard squares up as `side` passes through 0 on a flip and braces the other way, rather than collapsing. Existing luff/flutter terms, outline clipping, shading and seams are reused unchanged. Draw the head row's edge (`a == mh`) with line width 3 so it reads as the yard.

- [ ] **Step 1: Add a `longship` scenario to `tools/frame_dump.c`**, modelled on the existing scenarios (lines ~133-160) but loading `boats/longship.json` into the `BoatCfg` the way `tools/gif_clip.c:116` does; render twa 90 (trim 60 deg) and twa 180 (trim 90 deg) to `longship_reach` / `longship_run`.
- [ ] **Step 2: Run** `tools/frames.sh build/frames` before changing `render.c`. Expected: the longship frames show the sail as a triangle on the centreline (wrong). Read the PNG to confirm.
- [ ] **Step 3: Implement the shape-3 branch in `draw_sail`** per the geometry above, with the existing `hoist < 0.02f` early return untouched (a lowered sail draws nothing). Keep the head-row line width 3 for shape 3 only.
- [ ] **Step 4: Run** `tools/frames.sh build/frames` and read `longship_run.png` and `longship_reach.png`. Expected: a rectangle centred on the mast, square across on the run, braced (windward end aft, leeward end forward, leeward side matches the wind) on the reach, thick yard along the top, no triangle. Also confirm the existing scenario PNGs (J/24 jib and main) are unchanged.
- [ ] **Step 5: Run** `make -C tests` (sim untouched; sanity) and `make` if the Playdate SDK is available. Expected: builds clean with `-Wall -Wextra`.
- [ ] **Step 6: Commit**
  ```bash
  git add src/pd/render.c tools/frame_dump.c
  git commit -m "feat: draw square sails as a yard-hung rectangle centred on the mast"
  ```

---

### Task 5: Make the longship selectable, free range only

**Files:**
- Modify: `src/sim/menu.h`, `src/sim/menu.c`, `src/pd/menu_draw.c` (boat rows), `tests/test_menu.c`
- Modify: `src/pd/assets.h` (`#define ASSETS_BOATS 5`; new `void assets_boat_raceable(PlaydateAPI *pd, bool out[ASSETS_BOATS]);`), `src/pd/assets.c` (`ASSET_BOAT_NAMES` gains `"longship"` last; implement `assets_boat_raceable`), `src/pd/main.c` (call it once at startup and pass the array to the menu; replace the duplicate `boat_names` array at line 93 with `ASSET_BOAT_NAMES`)
- Modify: `README.md` (boat list; note longship is free range only), `docs/boat-schema.md` (mention `boats/longship.json`)

**Interfaces:**
- Consumes: `boats/longship.json` (Task 3), shape-3 drawing (Task 4), `BOAT_CLASS_*` (Task 2).
- Produces: `void menu_set_boat_racing(Menu *m, const bool *raceable);` (in `menu.h`; `raceable` holds `m->nboats` entries, is not copied, NULL means all raceable, and must outlive the menu). `assets_boat_raceable` fills `out[i]` true when boat i loads with `boat_class == BOAT_CLASS_RACER`; an unreadable or invalid file counts as a racer.

Behaviour of the boat screen with `raceable` set: UP/DOWN skip rows where `raceable[i]` is false; entering the screen puts `sel` on `m->boat` if that row is raceable, else on the first raceable row; A can therefore only return a raceable `menu.boat`. If no row is raceable, behave as if `raceable` were NULL. Skipped rows are drawn as `<name> (free range only)`.

- [ ] **Step 1: Write the failing tests in `tests/test_menu.c`** (helper: 5 boats, `raceable = {1,1,1,1,0}`, walk to the boat screen via Racing, a track and the preview):
  - `test_boat_screen_skips_cruisers`: with `m.boat = 3`, press DOWN: `sel == 0` (wraps over row 4); press UP from row 0: `sel == 3`.
  - `test_boat_screen_opens_on_a_racer_when_current_boat_is_a_cruiser`: `m.boat = 4`: on entering the boat screen `sel == 0`; A then weather (`menu_set_weather`) then A gives `MENU_START_RACE` with `m.boat == 0`.
  - `test_boat_screen_b_returns_to_a_racer_row`: from the weather screen, B returns to the boat screen with `sel` on a raceable row.
  - `test_all_cruisers_falls_back_to_all_selectable`: `raceable = {0,0,0,0,0}`: DOWN steps through every row 0..4.
  - `test_null_raceable_selects_every_boat`: no call to `menu_set_boat_racing`: row 4 is reachable and returned in `m.boat`.
- [ ] **Step 2: Run** `make -C tests build/test_menu && tests/build/test_menu`. Expected: FAIL (`menu_set_boat_racing` undeclared).
- [ ] **Step 3: Implement `menu_set_boat_racing`** (store the pointer in `Menu`; `menu_init` clears it to NULL), a static `row_ok(const Menu*, int row)` used by the UP/DOWN stepping and by the three places in `menu.c` that set `sel` on entering the boat screen. Keep the wrap behaviour of the other screens.
- [ ] **Step 4: Run the same command.** Expected: PASS (existing menu tests still pass).
- [ ] **Step 5: Draw cruiser rows** in the `MENU_BOATS` branch of `src/pd/menu_draw.c`, using a small stack buffer and `snprintf` for `"%s (free range only)"`. Not host-tested (Playdate-only file); check in Step 7.
- [ ] **Step 6: Register the boat and read the classes:** make the `assets`/`main.c` edits above, keeping `"sunfish"` at index 0 and the other four names in their present order. In `main.c`, `show_menu` calls `menu_set_boat_racing(&menu, boat_raceable)` with a static `bool boat_raceable[ASSETS_BOATS]` filled once in init.
- [ ] **Step 7: Verify.** `grep -n '"longship"' src/pd/assets.c` and `grep -n "ASSETS_BOATS 5" src/pd/assets.h` both match; `grep -n boat_names src/pd/main.c` shows no leftover duplicate; `make -C tests` passes except the known `test_weather` failure; if the SDK is installed run `tools/sim.sh`: Racing -> a track -> boat screen shows `longship (free range only)` that the cursor skips; Free Range -> system menu boat option lists `longship` and it sails, with a square sail and no warning strip.
- [ ] **Step 8: Update `README.md` and `docs/boat-schema.md`**, then commit
  ```bash
  git add src/sim/menu.h src/sim/menu.c src/pd/menu_draw.c src/pd/assets.h src/pd/assets.c src/pd/main.c tests/test_menu.c README.md docs/boat-schema.md
  git commit -m "feat: add the longship as a free-range-only cruiser"
  ```
