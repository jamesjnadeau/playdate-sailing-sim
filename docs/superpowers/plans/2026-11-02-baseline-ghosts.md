# Baseline Ghosts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a deterministic, autopilot-sailed baseline ghost for each course (sunfish, dinghy, j24; choppy weather) and have the game load it when the player has no ghost of their own.

**Architecture:** A pure-sim navigator (`navigator.c`) steers the autopilot target through aim points 20 m out along each mark's gate ray. `baseline.c` runs a whole race headless with it and records a `Ghost`. A thin host tool writes the 27 files to `ghosts/`; the Makefile stages them into the .pdx and `main.c` falls back to them when no saved ghost exists.

**Tech Stack:** C (Playdate SDK build, host `cc` for tools/tests), existing `tests/t.h` harness.

**Spec:** `docs/superpowers/specs/2026-11-02-baseline-ghosts-design.md`

## Global Constraints

- Boats: `sunfish`, `dinghy`, `j24` (file stems in `boats/`). Weather: `WEATHER_CHOPPY`. Courses: every `tracks/*.json` (9). 27 files.
- Aim point: 20 m from the mark along its gate ray (`cos(gate), sin(gate)`, the convention in `gate_crossed`).
- Seed per course is `ghost_seed(track.name)`; run at dt = 1/30 s; cap at `GHOST_MAX_SECONDS`.
- Tool must exit non-zero, naming course and boat, when a run does not finish or exceeds the cap; never write a bad ghost.
- No third ghost buffer on device. Baseline is a floor: the player's saved ghost wins; a new ghost saves only if faster than what was loaded.
- Tests compile with `-Wall -Wextra -Werror` and ASan/UBSan (`make -C tests test`); tool compiles with `-ffp-contract=off`.
- The tree already has uncommitted ghost work (`src/sim/ghost.c`, `ghost.h`, `src/pd/main.c`, `menu_draw.c`, `render.h`). Build on it; when committing, `git add` only the files each task names and leave other pre-existing hunks of those files for the user (use `git add -p` for `main.c`, `ghost.c`, `ghost.h`).

## Review Focus

- A course whose first leg is dead upwind: navigator must pick a tack, not oscillate at the no-go edge (test in Task 2).
- Aim point reached but game has not advanced `race.next` (crossing missed): navigator must not stall; it keys off `race.next` only (Task 2).
- Boat blown off course by shifts: tacks when outside the corridor, not every step (Task 2).
- A boat/course that cannot finish: tool fails loudly (Task 3).
- Course JSON, boat JSON or format edited after generation: shipped ghost silently stops matching; test fails (Task 4).

---

### Task 1: Shared ghost file name

**Files:**
- Modify: `src/sim/ghost.h`, `src/sim/ghost.c`, `src/pd/main.c` (the `ghost_path` function around line 80)
- Test: `tests/test_ghost.c`

**Interfaces:**
- Produces: `void ghost_file_name(char *out, size_t n, const char *course, const char *boat, const char *weather);` writes `ghost_<course>_<boat>_<weather>.bin`, each name reduced to letters and digits, always NUL-terminated, truncating rather than overflowing. Callers pass `WEATHER_NAMES[preset]` (declared in `weather.h`); the game's own saved ghosts use the same name, so each weather keeps its own personal best.

- [ ] **Step 1: Write failing test** `test_file_name_keeps_letters_and_digits`: `ghost_file_name(b, sizeof b, "Windward-Leeward", "j24", "choppy")` gives `"ghost_WindwardLeeward_j24_choppy.bin"`; and `test_file_name_truncates` with `n = 8` is NUL-terminated and `strlen < 8`. Add both to `main`'s T_RUN list.
- [ ] **Step 2: Run** `make -C tests build/test_ghost && tests/build/test_ghost` — expect compile failure (undefined).
- [ ] **Step 3: Implement** `ghost_file_name` in `ghost.c` by moving the loop body of `ghost_path` in `main.c` unchanged; declare it in `ghost.h` after `ghost_seed`. Make `main.c`'s `ghost_path` a one-line call to it, passing `WEATHER_NAMES[preset_index]`; `ghost_load`, `ghost_save` and `boat_best_refresh` all use the current weather preset, and the boat screen lists times for the weather selected. Old `ghost_<course>_<boat>.bin` files are simply no longer read.
- [ ] **Step 4: Run** `make -C tests test` — all PASS.
- [ ] **Step 5: Commit** `git add -p src/sim/ghost.h src/sim/ghost.c src/pd/main.c tests/test_ghost.c && git commit -m "refactor: share ghost file naming"`

### Task 2: Navigator

**Files:**
- Create: `src/sim/navigator.h`, `src/sim/navigator.c`, `tests/test_navigator.c`

**Interfaces:**
- Produces:
  - `#define NAV_AIM_OFFSET_M 20.0f`
  - `typedef struct { int side; } NavState;` (`side` = -1/+1 current tack, 0 unset)
  - `void nav_init(NavState *s);`
  - `void nav_aim_point(const Mark *m, float offset, float *x, float *y);` point `offset` m from the mark along its gate ray.
  - `float nav_heading(NavState *s, const Track *t, int next, double px, double py, double ox, double oy, float wind_from, float no_go_rad);` target heading (rad, wrapped) for a boat at track-frame position (px+ox, py+oy) heading for mark `next`'s aim point. `(ox, oy)` is `race.ox/oy`, so `px,py` are the boat's local coordinates. If the bearing is outside `no_go_rad + NAV_UPWIND_MARGIN_RAD` of `wind_from`, return the bearing. Otherwise return `wind_from ± (no_go_rad + NAV_UPWIND_MARGIN_RAD)` on the side `s->side`; choose the side nearer the bearing on first use and flip it only when the boat's cross-track distance from the leg (previous aim point, or start, to this aim point) exceeds `NAV_CORRIDOR_FRAC * leg length` (min `NAV_CORRIDOR_MIN_M`) on the side that the current tack heads away from.
  - Constants `NAV_UPWIND_MARGIN_RAD` (10 deg), `NAV_CORRIDOR_FRAC` (0.15), `NAV_CORRIDOR_MIN_M` (30); builder tunes.

- [ ] **Step 1: Write failing tests** in `test_navigator.c`:
  - `test_aim_point_is_20m_along_gate`: mark at (10, 5), gate = 0 gives (30, 5); gate = pi/2 gives (10, 25) (T_NEAR 1e-3).
  - `test_reach_leg_heads_straight`: wind from +y (`pi/2`), no-go 40 deg, boat at (0,0), aim point at (100,0): heading ~0.
  - `test_upwind_leg_holds_close_hauled`: aim point directly upwind; heading differs from `wind_from` by exactly `no_go + margin` (abs), same sign on repeated calls with small lateral drift (no flip).
  - `test_tacks_only_beyond_corridor`: sailing the upwind leg, move the boat sideways in 1 m steps away from the leg; side stays until cross-track exceeds `max(NAV_CORRIDOR_MIN_M, 0.15*leg)`, then flips once.
  - `test_advances_with_next`: calling with `next = 1` aims at mark 1's aim point (use a 2-mark track).
- [ ] **Step 2: Run** `make -C tests build/test_navigator && tests/build/test_navigator` — compile failure.
- [ ] **Step 3: Implement** as specified; pure functions of their arguments plus `NavState`; use `wrap_pi`, `clampf` from `mathx.h`.
- [ ] **Step 4: Run** `make -C tests test` — PASS.
- [ ] **Step 5: Commit** `git add src/sim/navigator.* tests/test_navigator.c && git commit -m "feat: waypoint navigator for the autopilot"`

### Task 3: Headless baseline run and generator tool

**Files:**
- Create: `src/sim/baseline.h`, `src/sim/baseline.c`, `tools/ghost_gen.c`, `tests/test_baseline.c`
- Modify: `Makefile` (add `ghosts` target after the `include`)

**Interfaces:**
- Consumes: `nav_*` (Task 2), `ghost_*`, `ghost_file_name` (Task 1), `game_*`.
- Produces:
  ```c
  typedef struct {
      bool finished;       /* every mark rounded, race.finished */
      double time;         /* race.t at the finish, else the time reached */
      float min_mark_m;    /* closest the boat came to any mark centre */
  } BaselineResult;
  /* Sails one race in `g` (already game_init'd with the boat, weather, world, choppy preset and
     set to `track`) with autopilot and autotrim on; fills `gh` (caller-owned storage, ghost_begin'd here).
     Restarts the game with ghost_seed(track.name). boat_hash is ghost_hash_str(boat name). */
  BaselineResult baseline_sail(Game *g, Ghost *gh, const char *boat_name);
  ```
  Tool CLI: `ghost_gen <repo_root> <out_dir>`; writes `<out_dir>/ghost_<course>_<boat>_choppy.bin` (name from `ghost_file_name` with `WEATHER_NAMES[WEATHER_CHOPPY]`; the weather list in the tool is one array, so adding presets later is one line); prints `course boat time min_mark`; exit 1 if any run is not `finished` or `min_mark_m < 15`.

- [ ] **Step 1: Write failing tests** in `test_baseline.c` (load real files from `../boats`, `../config`, `../tracks` relative to `tests/`; helper reads a file into a static buffer):
  - `test_sunfish_finishes_windward_leeward`: result `finished`, `time` in (30, 1200), `ghost_usable(&gh)`, `ghost_matches(&gh, ghost_seed(track.name), ghost_track_hash(&track), ghost_hash_str("sunfish"), WEATHER_CHOPPY)`.
  - `test_passes_marks_wide`: same run, `min_mark_m >= 15.0f`.
  - `test_deterministic`: two runs give bit-identical `ghost_encode` output.
  - `test_all_courses_all_boats_finish`: loop over the 9 track files and 3 boats, assert `finished` (this is the navigator's real acceptance test; slow is fine, keep under ~60 s with ASan, otherwise note and loop only j24 + dinghy here).
- [ ] **Step 2: Run** `make -C tests build/test_baseline` — compile failure.
- [ ] **Step 3: Implement `baseline_sail`**: `game_restart(g, ghost_seed(g->track.name))`; `ghost_begin`; press A once and a quick B (`pressed = BTN_B` then release next step; see `engine_buttons` for the hold threshold) to enable autopilot and autotrim; each step compute `g->ap.target = nav_heading(...)` using `g->race.next`, `g->b.x/y`, `g->race.ox/oy`, wind from `wrap_pi(g->wind.dir + pi)`, no-go from `g->boat.controls.autopilot_no_go_deg`; `game_update(g, 0, 0, 0, 1/30)`; `ghost_record`; track min mark distance; stop at `race.finished` or `race.t >= GHOST_MAX_SECONDS`; `ghost_finish`. If the boat capsizes (`g->run != RUN_SAILING`) stop with `finished = false`. Tune navigator constants until Step 4 passes.
- [ ] **Step 4: Run** `make -C tests test` — PASS.
- [ ] **Step 5: Implement `tools/ghost_gen.c`** (plain host C: list `tracks/*.json` with `opendir`, load boat via `boatcfg_load`, weather/world via `weather_load`/`world_load`, track via `track_load`, as `src/pd/assets.c` does; static buffers for `GhostSample` storage and the file image; sort tracks by name for stable output). Add a `ghosts` target to `Makefile` after `include`: compiles `tools/ghost_gen.c src/sim/*.c` with `cc -std=gnu99 -O1 -ffp-contract=off -Isrc/sim -lm` into `build/ghost_gen` and runs it with `. ghosts`.
- [ ] **Step 6: Run** `make ghosts` — expect 27 lines, exit 0, 27 files in `ghosts/`; run it twice and `diff -r` the two outputs (identical). Note the file sizes and total.
- [ ] **Step 7: Commit** `git add src/sim/baseline.* tools/ghost_gen.c tests/test_baseline.c Makefile && git commit -m "feat: headless baseline run and ghost generator"`

### Task 4: Ship and load the baselines

**Files:**
- Modify: `Makefile` (STAGE copies `ghosts` too), `.gitignore` (add `Source/ghosts/`), `src/pd/main.c` (`ghost_load`, `boat_best_refresh`)
- Create: `ghosts/*.bin` (generated, committed), `tests/test_shipped_ghosts.c`

**Interfaces:**
- Consumes: `ghost_file_name`, generated files from Task 3.

- [ ] **Step 1: Write failing test** `test_shipped_ghosts.c`: for each of the 9 tracks x {sunfish, dinghy, j24} read `../ghosts/<ghost_file_name(..., "choppy")>`, `ghost_decode`, and assert `ghost_matches(&gh, ghost_seed(track.name), ghost_track_hash(&track), ghost_hash_str(boat), WEATHER_CHOPPY)`. Failure message names the file and says to run `make ghosts`. Also `test_other_boats_have_no_baseline`: no file for longship or tp52.
- [ ] **Step 2: Run** `make -C tests build/test_shipped_ghosts && tests/build/test_shipped_ghosts` — passes only if Task 3's `make ghosts` output is present; delete one file temporarily to confirm it fails, then restore.
- [ ] **Step 3: Implement loading** in `main.c`: add a static helper that opens the player's file with `kFileReadData`, and if that fails opens `ghosts/<name>` with `kFileRead`; use it in `ghost_load` and in `boat_best_refresh` (header read). Saving is unchanged. Update the Makefile `STAGE` line to copy `ghosts` (create the folder if missing so a fresh checkout still builds) and `.gitignore`.
- [ ] **Step 4: Verify in the host harness**: `make -C harness` builds with the new loading code; add a scenario step or inspect: starting a sunfish race on a clean data dir loads the baseline (`ghost_opponent()` non-NULL) — if the scenario language cannot show it, state that and verify by the Task 4 test plus a simulator run (`make` then Playdate Simulator): ghost boat visible on first race, boat screen shows baseline times.
- [ ] **Step 5: Run everything**: `make -C tests test`, `make -C harness check-determinism`, `make` (device/simulator build) — all pass.
- [ ] **Step 6: Commit** `git add ghosts Makefile .gitignore tests/test_shipped_ghosts.c && git add -p src/pd/main.c && git commit -m "feat: ship baseline ghosts and load them when the player has none"`

---

**Self-review notes:** spec coverage — generator (T3), 27 files and exclusions (T3, T4), 20 m aim point (T2), floor loading and save rule (T4), mismatch test (T4), min buoy distance and finish check (T3). Design deviation recorded in the spec: the aim point lies on the gate ray, and the fallback load uses two explicit opens.
