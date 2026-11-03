# Trailing ghost Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In journey and free range, a faint ghost boat replays the player's own track from 30 m of path behind the boat.

**Architecture:** A new `Follower` (ring of distance-spaced pose samples) lives in `Game`, is recorded at the end of `game_update` after the floating-origin `recentre`, and is shifted by `recentre` like `Journey`. `render_frame` reads it with `follower_pose` and draws it through the race ghost's existing hull/rig routines.

**Tech Stack:** C99, host tests in `tests/` (`make -C tests test`, ASan/UBSan, `-Werror`, `T_ASSERT`/`T_NEAR`/`T_RUN` from `tests/t.h`), scenario harness (`make -C harness run-all`).

**Spec:** `docs/superpowers/specs/2026-11-02-trailing-ghost-design.md`

## Global Constraints

- Modes: `GAME_FREE` and `GAME_JOURNEY` only; `GAME_RACE` records and draws nothing here.
- `FOLLOW_DISTANCE_M` = **30.0f** m of *path length* behind the boat (not a time delay, not straight-line distance).
- `FOLLOW_STEP_M` = **0.25** m between samples; `FOLLOW_CAP` = **256** samples (covers >= 64 m).
- The newest sample is always the boat's current pose (it is overwritten, not appended, while the boat has moved < `FOLLOW_STEP_M` from the one before).
- `follower_pose` returns false until the trail is at least `behind_m` long.
- Race ghost behaviour, files and tests unchanged.
- Style: match `journey.c`/`ghost.c` (terse comments, `static` helpers, `wrap_pi` from `mathx.h`).

## Review Focus

- Origin shift mid-trail: the ghost must stay ~30 m behind, not jump thousands of metres (Task 2 test).
- Boat at rest for a long time: no sample growth, no NaN, ring not churned (Task 1 test).
- Trail longer than the ring: wraparound still answers 30 m and 60 m (Task 1 test).
- Heading across the +-pi seam: ghost must not spin through 0 (Task 1 test).
- Restart / mode switch: no stale trail from the previous run (Task 2 tests).

## File Structure

- Create `src/sim/follower.h`, `src/sim/follower.c`: the trail and its query. Picked up automatically by `src/sim/*.c` wildcards (root `Makefile`, `tests/Makefile`).
- Create `tests/test_follower.c`: unit tests (auto-discovered by `tests/Makefile`).
- Modify `src/sim/game.h`, `src/sim/game.c`: own, reset, shift and record the trail.
- Modify `src/pd/render.c`: split `draw_ghost`, draw the follower outside race mode.
- Create `tests/scenarios/trailing_ghost.txt`; refresh host goldens.

---

### Task 1: Follower trail

**Files:**
- Create: `src/sim/follower.h`, `src/sim/follower.c`
- Test: `tests/test_follower.c`

**Interfaces:**
- Produces (`follower.h`, includes `boat.h`):
```c
#define FOLLOW_CAP 256
#define FOLLOW_STEP_M 0.25
#define FOLLOW_DISTANCE_M 30.0f
typedef struct {
    double x, y;                       /* m, local (game) frame */
    float heading, heel, wheel, u;
    float trims[BOAT_MAX_SAILS], hoists[BOAT_MAX_SAILS], swing[BOAT_MAX_SAILS];
} FollowPose;
typedef struct { FollowPose p; double s; } FollowSample;   /* s: path length from the first sample, m */
typedef struct { FollowSample smp[FOLLOW_CAP]; int head, n; } Follower;   /* head: index of the newest; n: samples held */
void follower_clear(Follower *f);
void follower_record(Follower *f, const BoatState *b);
bool follower_pose(const Follower *f, float behind_m, FollowPose *out);
void follower_shift(Follower *f, double dx, double dy);
```

- [ ] **Step 1: Write the failing tests** in `tests/test_follower.c` (`#include "t.h"`, `"follower.h"`; a helper `rec(Follower*, double x, double y, float heading)` that fills a zeroed `BoatState` and calls `follower_record`). Tests, each `T_RUN` from `main`:
  - `test_empty_has_no_pose`: after `follower_clear`, `follower_pose(&f, 30, &p)` is false.
  - `test_pose_is_30m_of_path_behind`: record x = 0.1 .. 50.0 in 0.1 steps, y = 0; `follower_pose(30)` true, `T_NEAR(p.x, 20.0, 0.01)`, `T_NEAR(p.y, 0, 1e-9)`.
  - `test_needs_a_full_trail`: after 20 m false; after 30.1 m true.
  - `test_fields_interpolate`: x 0..40 step 0.1 with `heading = 0.01 * x`, `heel = 0.02 * x`, `hoists[0] = 0.5`; pose(30) has `heading` within 1e-3 of 0.1 and `heel` within 1e-3 of 0.2 and `hoists[0]` 0.5.
  - `test_heading_across_the_seam`: record (0,0) heading 3.1, then x = 0.25, 0.5 .. 30.0 heading -3.1, then x = 30.125; `follower_pose(30)` has `fabsf(p.heading) > 3.0`.
  - `test_dense_records_overwrite_the_head`: 1000 records at 0.01 m steps (10 m); `f.n <= 45`; `T_NEAR` the newest sample's `p.x` to 10.0 within 1e-9.
  - `test_ring_wraps`: 2000 records at 0.5 m steps (1000 m); pose(30) true with `p.x` within 0.01 of `1000 - 30 - 0.5` adjusted for the last record's x (assert against `lastx - 30`); pose(60) true likewise.
  - `test_at_rest_stays_put`: 5000 records at the same point; `f.n <= 2`, pose(30) false, no NaN in `f.smp[f.head]`.
  - `test_shift_moves_the_trail`: after the 50 m run, `follower_shift(&f, 100, -40)`; pose(30) x within 0.01 of -80, y within 1e-9 of 40; newest sample moved the same.

- [ ] **Step 2: Run to verify failure:** `make -C tests build/test_follower` — Expected: compile error, `follower.h` not found.

- [ ] **Step 3: Implement** `follower.c`. `follower_record`: empty -> store at index 0, `s = 0`, `n = 1`. Otherwise measure `hypot` from the newest sample; if >= `FOLLOW_STEP_M` advance `head` (mod cap, `n` capped at `FOLLOW_CAP`) with `s = prev.s + d`; else overwrite the newest in place with `s = prev.s + d` where prev is the sample before it (or `s = d` if it is the only one... keep the first sample fixed: if `n == 1` and the boat moved < step, overwrite pose only, `s` stays 0). Non-finite x/y: ignore the record. `follower_pose`: false if `n < 2`, non-finite `behind_m`, or `newest.s - oldest.s < behind_m`; otherwise walk back from the newest to the bracketing pair of the target `s`, and lerp x, y, heel, wheel, u, trims, hoists, swing; heading via `wrap_pi` of the difference. `follower_shift` subtracts (dx, dy) from every held sample.

- [ ] **Step 4: Run:** `make -C tests build/test_follower && tests/build/test_follower` — Expected: `9 tests passed`.

- [ ] **Step 5: Commit**
```bash
git add src/sim/follower.h src/sim/follower.c tests/test_follower.c
git commit -m "feat: follower trail for the trailing ghost"
```

---

### Task 2: Game owns and records the trail

**Files:**
- Modify: `src/sim/game.h` (include `follower.h`; add `Follower follow;` after `journey`), `src/sim/game.c` (`reset`, `recentre`, `game_update`)
- Test: `tests/test_follower.c` (add below the Task 1 tests)

**Interfaces:**
- Consumes: `follower_clear`, `follower_record`, `follower_pose`, `follower_shift`, `FOLLOW_DISTANCE_M`.
- Produces: `Game.follow`, recorded every `game_update` when `mode != GAME_RACE`.

- [ ] **Step 1: Write the failing tests** (a `fresh_journey()` helper: `boatcfg_default`, `weather_default`, `world_default`, `game_init(..., WEATHER_SUNNY, 1)`, `game_set_journey`). Each frame is `game_update(&g, 0, 0, 0, 1.0f/30)`; where a test needs the boat to move for certain, set `g.b.u = 8` and `g.b.heading = 0` before each update.
  - `test_journey_records_and_ghost_appears`: step until `follower_pose(&g.follow, FOLLOW_DISTANCE_M, &p)` is true (cap 20000 frames, assert reached); then `hypot(p.x - g.b.x, p.y - g.b.y) <= 30.001`.
  - `test_free_range_records`: same with `game_set_track(&g, NULL)`.
  - `test_race_records_nothing`: `track_default`, `game_set_track`, 300 frames; `g.follow.n == 0`.
  - `test_restart_clears_the_trail`: after the journey run, `game_restart(&g, 2)`; `g.follow.n` is 0 or 1 and `follower_pose(..., 30, ...)` false. Same after `game_set_journey`/`game_set_track` (they go through `reset`).
  - `test_origin_shift_keeps_the_ghost_close`: drive with `g.b.u = 8` until `g.b.x` decreases between two frames (a recentre happened; cap 60000 frames, assert it did); then the ghost pose exists and `hypot` to the boat is between 15 and 30.001.

- [ ] **Step 2: Run to verify failure:** `make -C tests build/test_follower` — Expected: compile error, no member `follow` in `Game`.

- [ ] **Step 3: Implement.** `reset(g)` calls `follower_clear(&g->follow)`. In `recentre`, after the existing shifts, `follower_shift(&g->follow, dx, dy)`. At the very end of `game_update`, after `recentre(g)`: `if (g->mode != GAME_RACE) follower_record(&g->follow, &g->b);`.

- [ ] **Step 4: Run the whole suite:** `make -C tests test` — Expected: every binary prints `N tests passed`, exit 0 (existing tests, including the race ghost ones, unchanged).

- [ ] **Step 5: Commit**
```bash
git add src/sim/game.h src/sim/game.c tests/test_follower.c
git commit -m "feat: game records the player's trail outside races"
```

---

### Task 3: Draw the trailing ghost

**Files:**
- Modify: `src/pd/render.c:1495-1528` (`draw_ghost`) and `src/pd/render.c:1542` (`render_frame`)
- Create: `tests/scenarios/trailing_ghost.txt`
- Refresh: `tests/golden/host/` for scenarios that change

**Interfaces:**
- Consumes: `follower_pose(&g->follow, FOLLOW_DISTANCE_M, &fp)`, `FollowPose`.
- Produces: `static void draw_ghost_pose(PlaydateAPI *pd, const Game *g, const CameraPose *c, const FollowPose *p)` — draws the ghost at a **local-frame** pose.

- [ ] **Step 1: Split `draw_ghost`.** Move everything from `BoatState gb = g->b;` onward into `draw_ghost_pose`, reading `p->x`, `p->y` (already local), `p->heading`, `heel`, `wheel`, `u` (-> `gb.u`), `trims`, `hoists`, `swing`. `draw_ghost` keeps `ghost_pose_at`, then builds a `FollowPose` (`x = p.x - g->race.ox`, `y = p.y - g->race.oy`, the scalar fields, sails `i < GHOST_SAILS` copied, the rest `trims = 0`, `hoists = 0`, `swing = -1`) and calls `draw_ghost_pose`. Race ghost output must not change.

- [ ] **Step 2: Draw the follower.** In `render_frame`, next to the existing `draw_ghost` line: when `g->mode != GAME_RACE` and `follower_pose(&g->follow, FOLLOW_DISTANCE_M, &fp)`, call `draw_ghost_pose(pd, g, c, &fp)`. Keep the existing `visibility` cull inside `draw_ghost_pose`.

- [ ] **Step 3: Verify race rendering is unchanged:** `make -C harness run-all` — Expected: `race_start` and `smoke` pass regression with no diff.

- [ ] **Step 4: Add the scenario** `tests/scenarios/trailing_ghost.txt` in the style of `free_sail.txt`: start free range, `hold A 20` for the autopilot, `wait` long enough to sail well over 30 m, then `crank` the wheel hard so the boat turns and the ghost swings into view, then `snap trailing_ghost`, `state`, `end`. Tune the waits/crank until the ghost is visible in the snapped frame; open the PNG from `harness/build/` and look at it.

- [ ] **Step 5: Refresh goldens deliberately.** `make -C harness run-all`; for each scenario whose regression fails (expected: any that sail >30 m in free range or journey), view the diff PNG in `harness/build/diff/`, confirm the only change is the ghost boat, then `make -C harness update-golden`. Note any scenario with a real-simulator golden (`tests/golden/real/`) that now differs, since fidelity only warns without one and blocks with one; tell James rather than refreshing real goldens.

- [ ] **Step 6: Run everything:** `make -C tests test && make -C harness run-all` — Expected: all pass.

- [ ] **Step 7: Commit**
```bash
git add src/pd/render.c tests/scenarios/trailing_ghost.txt tests/golden/host
git commit -m "feat: draw the trailing ghost in journey and free range"
```

---

## Self-review

- Spec coverage: distance-based lag (T1, `test_pose_is_30m_of_path_behind`), modes (T2 race/free/journey tests), origin shift (T1 shift + T2), appears after 30 m (T1 `test_needs_a_full_trail`), drawing style via shared routines (T3), memory bounded by `FOLLOW_CAP` (T1 ring test).
- Types: `FollowPose`/`Follower` fields and function names are the same in all three tasks.
- Not done on purpose (spec non-goals): toggle, HUD indicator, on-device check.
