# Buoy-follow camera Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Holding DOWN toggles a look mode where the camera yaw tracks the next buoy (pitch untouched), swinging to each new target over a configurable `buoy_swing_s` (default 2 s).

**Architecture:** `Controls` owns the follow state and drives `camera_yaw` (relative to the boat's heading, as today). The game sets two fields on `Controls` each frame (relative bearing to the next buoy, and an id that changes when the target buoy changes). `camera.c` is untouched. Same "set by the caller, survives updates" pattern as `CameraPose.trail`.

**Tech Stack:** C99, host tests in `tests/` (`make -C tests test`, ASan/UBSan, `-Werror`), `T_ASSERT` / `T_NEAR` / `T_RUN` from `tests/t.h`.

**Spec:** `docs/superpowers/specs/2026-11-02-buoy-follow-camera-design.md`

## Global Constraints

- Hold DOWN for `controls.camera_hold_s` (0.75 s) toggles follow; this **replaces** hold-DOWN-recentre. Recentre stays on the second UP press.
- `camera.buoy_swing_s` default **2.0** s, range 0..10, 0 = immediate. Existing boat JSON files need no edit.
- Swing = smoothstep blend from the yaw at blend start to the *live* target, restarted on: entering follow, target buoy change, leaving follow (target = 0, behind). Takes the short way round (`wrap_pi`).
- Pitch/elevation is never touched by follow.
- Any crank turn while `mode == MODE_CAMERA` exits follow (and any return-to-behind swing) in place: no yaw jump, crank continues from the live yaw.
- Follow persists when switching to wheel/trim/hoist (only a crank turn *in look mode* exits it).
- No next buoy (NaN bearing): yaw holds, follow stays on.
- Second UP press (the existing recentre) also ends follow (my call, see spec).
- Style: match `controls.c` (terse comments, `static` helpers, `wrap_pi`/`clampf` from `mathx.h`).

## Review Focus

- Buoy almost directly behind (relative bearing near ±π) while the current yaw is on the other side: the swing goes the short way, never spins through the long way (Task 2).
- Last buoy passed / free-ride mode (no target) while follow is on: yaw holds, no NaN, and the camera swings to the next buoy when one appears (Task 2, Task 3).
- A buoy passed mid-swing: the blend restarts from the *current* yaw, no jump (Task 2).
- `dt` of 0 or NaN, `buoy_swing_s` of 0, `buoy_rel` of ±inf: stays finite, no divide by zero (Task 2).
- Starting a new race or journey with follow left on from the last one: first buoy is picked up with a swing, not a snap (Task 3).

---

### Task 1: `buoy_swing_s` config

**Files:**
- Modify: `src/sim/boatcfg.h:91` (CameraCfg: add `float buoy_swing_s;` after `return_rate`, with comment `/* s the camera takes to swing to a new buoy-follow target; 0 = immediate */`)
- Modify: `src/sim/boatcfg.c` (CAMERA_F: add `FF(CameraCfg, buoy_swing_s, 2.0f, 0, 10),` after the `return_rate` line)
- Test: `tests/test_boatcfg.c`

**Interfaces:**
- Produces: `cfg.camera.buoy_swing_s` (float, seconds), default 2.0, clamped 0..10, round-trips through `boatcfg_to_json`/`boatcfg_load`.

- [ ] **Step 1: Write failing tests** in `tests/test_boatcfg.c`, following the file's existing pattern for a defaulted/clamped field (look at how `camera_hold_s` or `return_rate` is tested there and mirror it):
  - `test_buoy_swing_default_is_2`: `boatcfg_default(&c); T_NEAR(c.camera.buoy_swing_s, 2.0, 1e-6);`
  - `test_buoy_swing_loads_and_clamps`: loading `{"camera":{"buoy_swing_s":3}}` gives 3; loading `{"camera":{"buoy_swing_s":99}}` gives 10 and `boatcfg_load` returns false with a warning naming `buoy_swing_s`; `0` is accepted as 0 with no warning.
  - Register both with `T_RUN` in `main`.
- [ ] **Step 2: Run** `make -C tests build/test_boatcfg && tests/build/test_boatcfg` — Expected: compile FAIL (`no member named buoy_swing_s`).
- [ ] **Step 3: Add the field and the `FF(...)` row** as listed under Files.
- [ ] **Step 4: Run** `make -C tests build/test_boatcfg && tests/build/test_boatcfg` — Expected: all pass.
- [ ] **Step 5: Commit**

```bash
git add src/sim/boatcfg.h src/sim/boatcfg.c tests/test_boatcfg.c
git commit -m "feat: camera.buoy_swing_s config (default 2 s)"
```

---

### Task 2: Follow state in `Controls`

**Files:**
- Modify: `src/sim/controls.h`, `src/sim/controls.c`
- Test: `tests/test_controls.c`

**Interfaces:**
- Consumes: `cfg->camera.buoy_swing_s` (Task 1); `cfg->controls.camera_hold_s`; `wrap_pi`, `clampf` (`mathx.h`).
- Produces, on `Controls` (all new, none change `controls_update`'s signature):
  - `bool camera_follow;` buoy-follow is on.
  - `float buoy_rel;` set by the caller each frame: rad, bearing to the next buoy minus boat heading; `NAN` = no target. `controls_init` sets `NAN`.
  - `int buoy_id;` set by the caller: changes when the target buoy changes (e.g. a count of buoys passed). `controls_init` sets 0.
  - private blend state: `bool swing_active; float swing_from, swing_t; int swing_id; bool swing_seen;`
- Behaviour contract for `controls_update`:
  - The `down_fired` branch toggles `camera_follow` and starts a blend (`swing_from = camera_yaw`, `swing_t = 0`, `swing_active = true`); on turning follow off the goal is 0. It no longer sets `camera_recentre`. Toggling also clears `camera_recentre`.
  - Each update, after the mode `switch`, one `static void follow_update(Controls *c, const CameraCfg *k, float dt)` drives `camera_yaw`: goal = `buoy_rel` while following (skip and hold if not finite), 0 while returning behind. A new `buoy_id` (or the first valid target after entering follow, tracked by `swing_seen`) restarts the blend from the current `camera_yaw`. Yaw = `swing_from + wrap_pi(goal - swing_from) * s`, `s = smoothstep(swing_t / swing_s)` (a local `static` smoothstep; `swing_s <= 0` gives `s = 1`; `swing_t` only advances by `dt` when the goal is valid). The blend ends (`swing_active = false`) when `s` reaches 1; while following it then keeps `camera_yaw = buoy_rel` exactly. Finally `camera_target_yaw = camera_yaw` so the crank and leaving the mode continue from the live view.
  - In the `MODE_CAMERA` case: a nonzero crank sets `camera_follow = false; swing_active = false;`, and `camera_target_yaw = camera_yaw` (like the recentre cancel). The `camera_yaw = camera_target_yaw` / `camera_pitch = camera_target_pitch` assignment is skipped while `camera_follow || swing_active || camera_recentre`... pitch assignment keeps working during follow (only yaw is driven), so guard the two assignments separately.
  - `up_again` also clears `camera_follow` and `swing_active` (then recentre runs as today).
  - Non-finite `dt` is already treated as 0 at the top of `controls_update`; `follow_update` must also tolerate a non-finite `buoy_rel` (treated as "no target").

- [ ] **Step 1: Write the failing tests** in `tests/test_controls.c`. Add helpers next to `hold_down`:

```c
static void aim(Rig *r, float rel, int id) { r->c.buoy_rel = rel; r->c.buoy_id = id; }
static void toggle_follow(Rig *r) { hold_down(r, r->cfg.controls.camera_hold_s + 0.1f); }
```

  New tests (register all in `main`). `idle(&r, n)` ticks at 1/30 s, so the default 2 s swing is 60 ticks; allow ±0.02 rad on time-based assertions:
  - `test_hold_down_toggles_follow`: `T_ASSERT(!r.c.camera_follow)`; `toggle_follow` → `camera_follow`, mode is `MODE_CAMERA`; `toggle_follow` again → `!camera_follow`.
  - `test_follow_swings_to_buoy_over_swing_time`: `aim(&r, 1.0f, 1)`; `toggle_follow`; then `T_ASSERT(r.c.camera_yaw > 0 && r.c.camera_yaw < 0.9f)` right after; yaw non-decreasing across 80 `idle` ticks; at the end `T_NEAR(r.c.camera_yaw, 1.0, 1e-3)`. Also `r.c.camera_pitch == 0`.
  - `test_follow_tracks_live_bearing_after_swing`: after the swing completes, `aim(&r, 0.3f, 1)` (same id) then one `idle` → `T_NEAR(camera_yaw, 0.3, 1e-4)` (no new blend for the same id).
  - `test_new_buoy_restarts_swing_without_jump`: follow at 1.0 settled; `aim(&r, -0.5f, 2)`; one tick → `|camera_yaw - 1.0| < 0.05`; after 80 ticks `T_NEAR(camera_yaw, -0.5, 1e-3)`. Also passing a new id *mid*-swing: `aim(1.0,1)`, toggle, idle 15, record `y`, `aim(-0.5,2)`, one tick → `|camera_yaw - y| < 0.05`.
  - `test_swing_takes_the_short_way_round`: start with yaw 3.0 (set `r.c.camera_yaw = r.c.camera_target_yaw = 3.0f` after entering look mode), `aim(&r, -3.0f, 1)`, toggle; over the swing `camera_yaw` never drops below 2.9 (wraps through ±π instead of through 0).
  - `test_follow_holds_yaw_with_no_target_and_keeps_following`: `aim(&r, NAN, 0)`; set a yaw of 0.7 by cranking, toggle follow → after 100 idle ticks `T_NEAR(camera_yaw, 0.7, 1e-6)` and `camera_follow`; then `aim(&r, 0.0f, 1)` → ends at 0 after 80 ticks (swing resumes). Also `aim(&r, INFINITY, 1)` stays finite and holds.
  - `test_toggle_off_swings_behind`: follow at 1.0 settled; `toggle_follow` → `!camera_follow`; after 80 ticks `T_NEAR(camera_yaw, 0, 1e-3)`; `camera_pitch` still 0.
  - `test_crank_in_look_mode_exits_follow_in_place`: follow at 1.0 settled; `tick(&r, 0, 10)`: `!camera_follow`, `T_NEAR(wrap_pi(camera_yaw - 1.0f), deg2rad(10 * crank_gain_camera), 1e-4)`; after 30 idle ticks yaw unchanged. Same exit mid-swing (`idle 15` then crank): no jump larger than one crank step from the live yaw.
  - `test_follow_survives_leaving_look_mode`: follow on, settled; `tick(&r, BTN_RIGHT, 0)` (trim mode); `aim(&r, 0.4f, 1)`; idle → `camera_follow` still true, yaw ends at 0.4; a crank turn in trim mode does not clear it.
  - `test_second_up_press_ends_follow_and_recentres`: follow on, `tick(&r, BTN_UP, 0)` (wheel; still following), `tick(&r, BTN_UP, 0)` → `!camera_follow`, `camera_recentre`; after 600 idle ticks yaw is 0.
  - `test_swing_zero_is_immediate`: `r.cfg.camera.buoy_swing_s = 0`; `aim(&r, 1.0f, 1)`; `toggle_follow` → `T_NEAR(camera_yaw, 1.0, 1e-5)` once the toggle has fired.
  - `test_follow_bad_dt_stays_finite`: follow on, `controls_update(..., 0, NAN, ...)` and dt 0 several times → `isfinite(camera_yaw)` and unchanged by the dt-0 calls.
  - `test_init_has_no_target`: after `controls_init`, `!isfinite(c.buoy_rel)` and `!c.camera_follow`.

- [ ] **Step 2: Update the existing hold-to-recentre tests** (the gesture is gone). Recentre is now started with two UP presses:
  - `test_camera_yaw_unlimited_and_hold_returns` (rename `..._and_second_up_returns`): replace the final hold section (from the `tick_h(&r, BTN_DOWN, BTN_DOWN, 0); /* entering does not recentre */` line through the end) with: enter look mode via `tick_h(DOWN)` and assert yaw unchanged; `tick(&r, BTN_UP, 0)` ×2 (second UP recentres); check one step is `deg2rad(return_rate)/30` (as today), then yaw is exactly 0 after 200 idle ticks.
  - `test_hold_recentres_both_axes_and_does_not_toggle` (rename `test_second_up_recentres_both_axes`): same setup, then `tick(&r, BTN_UP, 0); tick(&r, BTN_UP, 0);` instead of `hold_down`; same final assertions (yaw 0, pitch 0, `camera_vertical` still true).
  - `test_hold_shorter_than_threshold_toggles_not_recentres`: keep; add `T_ASSERT(!r.c.camera_follow)` after the short hold.
  - `test_crank_cancels_hold_recentre` (rename `test_crank_cancels_recentre`) and `test_crank_after_cancel_moves_one_step_from_the_live_view`: start the recentre with `tick(&r, BTN_UP, 0)` ×2 instead of the `hold fires` loop, then `tap_down(&r)` to re-enter look mode (recentre keeps running), then assert/crank as before.
- [ ] **Step 3: Run** `make -C tests build/test_controls && tests/build/test_controls` — Expected: compile FAIL (`no member named camera_follow` / `buoy_rel`).
- [ ] **Step 4: Add the fields to `Controls` (`controls.h`) and initialise them in `controls_init`** (`buoy_rel = NAN`, `buoy_id = 0`, rest zero/false).
- [ ] **Step 5: Implement the contract above in `controls.c`.** Use `#include <math.h>` (already present for `isfinite`). `follow_update` runs once per `controls_update` after the `switch`, before the `camera_recentre` block.
- [ ] **Step 6: Run** `make -C tests build/test_controls && tests/build/test_controls` — Expected: all pass, no sanitizer output.
- [ ] **Step 7: Update the header comment in `controls.h`** (the "DOWN -> CAMERA look" paragraph): holding DOWN toggles buoy-follow / behind-follow instead of recentring; second UP recentres and ends follow; a crank turn in CAMERA exits follow.
- [ ] **Step 8: Commit**

```bash
git add src/sim/controls.h src/sim/controls.c tests/test_controls.c
git commit -m "feat: hold DOWN toggles a buoy-follow camera look"
```

---

### Task 3: Feed the next buoy from the game, document

**Files:**
- Modify: `src/sim/game.c` (new `static void aim_camera(Game *g)`, called in `game_update` just before the `controls_update(...)` call at the top of the `RUN_SAILING` block)
- Modify: `README.md:18,21`
- Test: `tests/test_game.c`

**Interfaces:**
- Consumes: `Controls.buoy_rel` / `Controls.buoy_id` (Task 2); `race_mark_local(const Race*, const Track*, int, double*, double*)` (`race.h:54`, local frame); `journey_next(const Journey*)` and `Journey.passed` (`journey.h`); `g->b.x/y/heading`; `wrap_pi`.
- Produces: `aim_camera` sets, per `g->mode`:
  - `GAME_RACE`, not finished and `race.next < track.nmarks`: mark via `race_mark_local`, `buoy_rel = wrap_pi(atan2(my - b.y, mx - b.x) - b.heading)`, `buoy_id = race.next`.
  - `GAME_JOURNEY` with `journey_next() != NULL`: same maths with the buoy's `x, y`, `buoy_id = journey.passed`.
  - Otherwise (free ride, race finished, no buoy ahead): `buoy_rel = NAN` (leave `buoy_id` alone).

- [ ] **Step 1: Write failing tests** in `tests/test_game.c`. Mirror the setup of `test_recentre_moves_marks_with_boat` (`tests/test_game.c:381`) for a race with a track, and use whichever existing journey test builds a `GAME_JOURNEY` game for the journey case:
  - `test_follow_camera_faces_next_mark_in_race`: start a race, hold DOWN via `game_update` (pressed on the first frame, held for `camera_hold_s + 0.1` s), then run 3 s of `game_update` with no input; assert `T_NEAR(wrap_pi(g.ctl.camera_yaw - g.ctl.buoy_rel), 0, 1e-3)` and that `buoy_rel` equals the hand-computed bearing to the first mark minus `g.b.heading` (use `race_mark_local`).
  - `test_follow_camera_swings_to_next_mark_when_one_is_rounded`: teleport/step the race so `race.next` increments (use the existing helper the race tests use to pass a mark); one frame later `|camera_yaw - old yaw| < 0.1` (no snap); after 3 s it equals the new bearing within 1e-3.
  - `test_follow_camera_in_journey_uses_next_buoy`: same assertion as the race test against `journey_next()`.
  - `test_follow_camera_free_mode_holds_yaw`: `GAME_FREE`, follow on → `!isfinite(g.ctl.buoy_rel)` and `camera_yaw` unchanged over 60 frames.
  - `test_follow_left_on_into_a_new_race_swings_to_first_mark`: follow on in one race, start a second race (same call the other tests use), run frames → yaw reaches the first mark's bearing without a one-frame jump bigger than the swing's first step (≤ 0.1 rad).
  - Register all with `T_RUN`.
- [ ] **Step 2: Run** `make -C tests build/test_game && tests/build/test_game` — Expected: the new tests FAIL (yaw never moves; `buoy_rel` stays NaN).
- [ ] **Step 3: Implement `aim_camera` in `game.c`** per the Interfaces block and call it before `controls_update`. If `game_start`/`game_init` do not already `controls_init` on a new race, leave that alone; the `buoy_id` change is what triggers the swing.
- [ ] **Step 4: Run** `make -C tests build/test_game && tests/build/test_game` — Expected: all pass.
- [ ] **Step 5: Update `README.md`**: the Up row keeps "press again to recentre the camera" (also ends buoy-follow); the Down row becomes: "Look: crank swings the camera round the boat; tap again to raise/lower it; hold to follow the next buoy (hold again to go back behind); turning the crank stops following".
- [ ] **Step 6: Full verification:** `make -C tests test` — Expected: every suite passes. If `harness/Makefile` goldens depend on controls, also run `make -C harness check-regression` and report any golden diff rather than updating goldens.
- [ ] **Step 7: Commit**

```bash
git add src/sim/game.c tests/test_game.c README.md
git commit -m "feat: aim the buoy-follow camera at the next race mark or journey buoy"
```

---

## Self-review

- **Spec coverage:** toggle by hold (T2), recentre kept on UP (T2 tests), swing config (T1), smoothstep restart on entry / buoy change / leave (T2), short way round (T2), crank exit in place (T2), persistence across modes (T2), no-target hold (T2/T3), pitch untouched (T2), game wiring for race, journey, free (T3), docs (T2 step 7, T3 step 5).
- **Types:** `Controls.buoy_rel` / `buoy_id` / `camera_follow` and `CameraCfg.buoy_swing_s` named identically in all tasks; `follow_update` is private to `controls.c`.
- **New calls vs the spec (flagged for review):** follow state is two caller-set fields on `Controls` rather than a new parameter (avoids touching the 4 call sites and matches `CameraPose.trail`); second UP press also ends follow.
