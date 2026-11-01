# Camera Look Axis and Hold-to-Recentre Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In look mode, the camera can swing a full 360° around the boat (no sideways limit); a DOWN tap switches the crank between swinging it sideways and raising/lowering it; holding DOWN for `controls.camera_hold_s` (default 0.75 s) recentres both.

**Architecture:** `controls.c` already latches `MODE_CAMERA` and eases `camera_yaw` back at `camera.return_rate`. It gains a second look axis (`camera_pitch`), an axis flag, and a DOWN hold timer modelled on `autopilot_buttons` (needs the `held` mask, so `controls_update` grows a parameter). `camera.c` turns `camera_pitch` into an orbit elevation at fixed radius in the trailing view and a plain look-up/down in the near view.

**Tech Stack:** C (sim in `src/sim/`, tests in `tests/`, `make -C tests`), boat/config schema in `src/sim/boatcfg.c`.

**Spec:** none; the request is the spec: "Currently when pressing down a 2nd time on the dpad, it re-centers the camera, I want this to happen when the user holds down on the dpad for a configurable amount of time, start with 0.75 seconds for now. Add a new feature that allows the user to hit the dpad to switch between horizontal and vertical camera changes. the distance should stay fixed."

## Global Constraints

- Hold time is a config field `controls.camera_hold_s`, default `0.75`, range `0.1`..`3` (same style as `autopilot_hold_s`).
- Trailing-view distance stays fixed: the eye stays `hypot(trail distance, far_height)` from the boat centre at sea level at every elevation, and the boat keeps its screen position.
- Sideways look has no limit: the camera can circle the boat all the way round, in both directions. `camera.yaw_range_deg` is removed (field, table entry, every boat JSON, docs).
- The switch uses DOWN (the look button); no other button is added.
- `camera_yaw` and its target stay wrapped to (-π, π] so the value never grows; recentre eases along the short way round.
- Leaving look mode keeps the look (existing behaviour, `test_camera_yaw_clamped_and_returns`); it does not recentre.
- Boat JSON that omits the new fields must load with defaults. Boat files must load with no warnings after `yaw_range_deg` is removed from them.

## Review Focus

- A tap that enters look mode from another mode must not flip the axis.
- A hold that fires must not also flip the axis when DOWN is released.
- Cranking past straight ahead (±180°, the bow) keeps going with no jump, stop or snap; many turns of the crank never leaves `camera_yaw` outside (-π, π].
- Recentre from behind the boat (near ±180°) takes the short way and ends at exactly 0.
- Holding DOWN while the crank is turning: the crank takes over (cancels the recentre), as today.
- Eye must never go below the water at the lowest elevation, including with wave heave.
- A boat with no pitch input (`camera_pitch == 0`) frames exactly as before (existing camera tests untouched).

---

### Task 1: Controls: axis toggle, vertical look, hold-to-recentre

**Files:**
- Modify: `src/sim/boatcfg.h` (`ControlsCfg`, `CameraCfg`), `src/sim/boatcfg.c` (`CONTROLS_F`, `CAMERA_F`)
- Modify: `boats/dinghy.json`, `boats/j24.json`, `boats/longship.json`, `boats/sunfish.json`, `boats/tp52.json` (delete `"yaw_range_deg":120` from `camera`)
- Modify: `src/sim/controls.h`, `src/sim/controls.c`
- Modify: `src/sim/game.c:126` (pass `held`)
- Test: `tests/test_controls.c`

**Interfaces:**
- Consumes: existing `ControlsCfg`, `CameraCfg`, `FF(...)` field macro.
- Produces:
  - `CameraCfg.yaw_range_deg` is gone; `CameraCfg.pitch_range_deg` replaces it as the only look limit
  - `ControlsCfg.camera_hold_s` (`float`, seconds, default 0.75, 0.1..3)
  - `CameraCfg.pitch_range_deg` (`float`, default 45, 5..80): vertical look is clamped to ±this
  - `Controls.camera_pitch` (`float`, rad, + = look from higher / look up), `Controls.camera_target_pitch` (`float`, rad), `Controls.camera_vertical` (`bool`, crank axis; false = swing sideways)
  - `void controls_update(Controls *c, const BoatCfg *cfg, uint32_t pressed, uint32_t held, float crank_delta_deg, float dt, BoatInputs *out)`

- [ ] **Step 1: Write the failing tests.** In `tests/test_controls.c` change the `tick` helper to `tick_h(Rig*, pressed, held, crank)` plus `tick(r, pressed, crank)` calling it with `held = pressed`-free semantics (`held = 0` is wrong for a press frame, so `tick` passes `held = pressed`). Update the two direct `controls_update` calls at lines 116-117 to the new signature (`held = 0`). Rewrite `test_camera_yaw_clamped_and_returns` as `test_camera_yaw_unlimited_and_hold_returns`: crank `+90` for 200 frames and assert `camera_yaw` is in (-π, π] every frame and has passed ±π at least once (full circle, no clamp); crank the other way likewise; then with the look at some nonzero `y`, `tick_h(BTN_DOWN, BTN_DOWN, 0)` once (enters, no recentre), then `tick_h(0, BTN_DOWN, 0)` frames until `camera_hold_s` of dt has elapsed, then assert `camera_yaw` moves by `deg2rad(return_rate)/30` per frame toward 0 and ends at exactly `0.0f`. Replace the `lim` uses accordingly. Add, registered in `main`:
  - `test_down_tap_in_camera_mode_toggles_axis`: DOWN press+release enters (`camera_vertical == false`); second DOWN press then release (`held = 0` next frame) sets `camera_vertical == true`; a third tap sets it false. After the second tap crank `+90` for 200 frames: `camera_pitch == deg2rad(pitch_range_deg)` (clamped) and `camera_yaw` unchanged.
  - `test_entering_tap_does_not_toggle_axis`: from `MODE_WHEEL`, DOWN press then release: `mode == MODE_CAMERA`, `camera_vertical == false`.
  - `test_hold_recentres_both_axes_and_does_not_toggle`: set yaw to a nonzero value, toggle to vertical and set pitch nonzero, hold DOWN for `camera_hold_s + 0.1` s, release; after enough idle frames `camera_yaw == 0 && camera_pitch == 0` and `camera_vertical` is unchanged from before the hold.
  - `test_hold_shorter_than_threshold_toggles_not_recentres`: hold for `camera_hold_s - 0.1` s then release: axis toggled, yaw unchanged.
  - `test_crank_cancels_hold_recentre`: after the hold fired and recentre started, a nonzero crank frame stops easing (yaw/pitch stay where the crank puts them).
  - `test_yaw_crosses_behind_without_clamp_or_jump`: start at yaw 0, crank in steps of `+10` until past π: each frame's change (wrapped) is the same step, and no frame holds the value still.
  - `test_recentre_from_near_pi_goes_short_way`: set `camera_yaw = camera_target_yaw = 3.0f`, hold DOWN to fire the recentre: yaw only ever decreases from 3.0 toward 0 (never passes through ±π) and ends at exactly `0.0f`.
  - `test_camera_hold_default_is_0_75`: `boatcfg_default` gives `controls.camera_hold_s == 0.75f`.

- [ ] **Step 2: Run to verify failure.** Run: `make -C tests test 2>&1 | head -30`. Expected: compile errors (`camera_hold_s`, `camera_vertical`, `held` argument missing).

- [ ] **Step 3: Implement config fields.** In `boatcfg.h` add `float camera_hold_s;` (comment `/* hold DOWN this long in look mode to recentre */`) to `ControlsCfg` next to `autopilot_hold_s`, and `float pitch_range_deg;` to `CameraCfg` next to `yaw_range_deg`. Register `FF(ControlsCfg, camera_hold_s, 0.75f, 0.1f, 3)` and `FF(CameraCfg, pitch_range_deg, 45, 5, 80)` in `boatcfg.c`.

- [ ] **Step 4: Implement `controls_update` changes** in `controls.c`/`controls.h`:
  - Add the `held` parameter and the `Controls` fields above (zeroed in `controls_init`; add `float down_time; bool down_tracking, down_fired, down_entered;`).
  - DOWN branch of the mode priority chain: set `down_tracking = true; down_time = 0; down_fired = false; down_entered = (c->mode != MODE_CAMERA)`, then `mode = MODE_CAMERA`. Remove the old `camera_recentre = true` on a second press.
  - After the mode switch, while `down_tracking`: if `held & BTN_DOWN`, add `dt` to `down_time` and, once `down_time >= camera_hold_s` and not yet fired, set `down_fired = true; camera_recentre = true` (only if `mode == MODE_CAMERA`; if another button took the mode off camera, stop tracking). If DOWN is not held: if `!down_fired && !down_entered && mode == MODE_CAMERA` toggle `camera_vertical`; clear tracking.
  - `MODE_CAMERA` crank case: the crank drives `camera_target_yaw` when `!camera_vertical`, with no clamp: `target = wrap_pi(target + deg2rad(delta * crank_gain_camera))` (`wrap_pi` from `mathx.h`), else `camera_target_pitch` clamped to ±`deg2rad(pitch_range_deg)` with the same `crank_gain_camera`; any nonzero crank clears `camera_recentre`; the live value follows the target when not recentring.
  - Recentre easing: both axes ease to 0 at `deg2rad(return_rate) * dt`, `camera_recentre` clears when both are 0; targets are zeroed while recentring (as `camera_target_yaw` is today).
  - Delete `yaw_range_deg` from `boatcfg.h` and `CAMERA_F`, and from the five boat JSON files. Camera yaw recentre keeps its current rule (step toward 0 by `return_rate`), which is the short way because yaw is wrapped.
  - Update the header comment: DOWN tap in look mode switches sideways/vertical, hold recentres; delete the stale "returns to 0 ... once left" claim (leaving keeps the look).

- [ ] **Step 5: Pass `held` in `game.c:126`** (`controls_update(&g->ctl, &g->boat, pressed, held, crank_delta, dt, &g->in)`).

- [ ] **Step 6: Run to verify pass.** Run: `make -C tests test 2>&1 | tail -15`. Expected: all tests pass (including the all-boats load loop in `test_boatcfg.c` with no warnings), including the unchanged `test_game.c` and `test_scenario.c` suites.

- [ ] **Step 7: Commit.**

```bash
git add src/sim/boatcfg.h src/sim/boatcfg.c src/sim/controls.h src/sim/controls.c src/sim/game.c tests/test_controls.c
git commit -m "feat: DOWN tap switches look axis, hold recentres (camera_hold_s)"
```

### Task 2: Camera: vertical look at fixed distance

**Files:**
- Modify: `src/sim/camera.c:45-75` (`camera_update`)
- Test: `tests/test_camera.c`

**Interfaces:**
- Consumes: `Controls.camera_pitch` (rad, + = from higher / look up) from Task 1; existing `trail_framing`, `approach`, `CameraPose`.
- Produces: no new API; `camera_update` honours `g->ctl.camera_pitch`.

- [ ] **Step 1: Write the failing tests** in `tests/test_camera.c`, registered in `main`:
  - `test_trailing_pitch_keeps_distance_and_boat_screen_position`: `fresh(); cp.trail = true;` `settle()`, record `r0 = hypot(hypot(cp.x - g.b.x, cp.y - g.b.y), cp.height)` and the screen `sy` of `camera_project(&cp, g.b.x, g.b.y, 0, ...)`. Set `g.ctl.camera_pitch = 0.5f`, `settle()`: assert radius `T_NEAR(r, r0, 0.02)`, `cp.height > ` the old height, `cp.pitch <` the old pitch, and the boat's `sy` `T_NEAR` the old one within 0.5 px.
  - `test_trailing_pitch_down_keeps_eye_above_water`: `g.ctl.camera_pitch = -deg2rad(bc.camera.pitch_range_deg)`, `settle()`: `cp.height >= 0.5f`.
  - `test_near_pitch_looks_up_and_down`: `cp.trail = false`, `camera_pitch = 0.3f` gives `cp.pitch` greater than with `0` by ≈ 0.3 and `cp.height` unchanged; `-0.3f` the reverse.
  - Existing tests are the "zero pitch frames as before" check and must pass unmodified.

- [ ] **Step 2: Run to verify failure.** Run: `make -C tests test 2>&1 | grep -i "camera" | head`. Expected: the new assertions fail (pitch ignored).

- [ ] **Step 3: Implement in `camera_update`.** Trailing view: after `trail_framing`, with `R = hypot(xo, far_height)`, `phi0 = atan2(far_height, xo)`, `phi = clamp(phi0 + camera_pitch, asin(MIN_EYE / R), 85°)` (define `#define MIN_EYE 0.5f` near the other defines), set `xo = R*cos(phi)`, eye height `R*sin(phi)` (replacing `far_height` in the `c->height` line, heave still added), and subtract `(phi - phi0)` from `trail_pitch`. Near view: add `camera_pitch` to the pitch target instead. Comment the geometry in one line: it pivots about the boat centre at sea level, so the boat holds its screen position.

- [ ] **Step 4: Run to verify pass.** Run: `make -C tests test 2>&1 | tail -15`. Expected: all pass.

- [ ] **Step 5: Commit.**

```bash
git add src/sim/camera.c tests/test_camera.c
git commit -m "feat: vertical look orbits the trailing camera at fixed distance"
```

### Task 3: Docs and on-device check

**Files:**
- Modify: `config/help.md:6`, `docs/boat-schema.md` (controls row line 19, camera row line 21, field table near line 41), `README.md` if it describes the look button
- Test: `tests/test_helptext.c` (run only; edit if it pins the DOWN line)

- [ ] **Step 1: Update `config/help.md`** DOWN line to: `DOWN: Look. Crank swings the camera all the way round the boat. Tap again to switch to raising and lowering it. Hold to bring it back to centre.` Check the line fits whatever width `test_helptext.c` enforces.

- [ ] **Step 2: Document the fields** in `docs/boat-schema.md`: add `camera_hold_s` to the `controls` row, replace `yaw_range_deg` with `pitch_range_deg` in the `camera` row, and one table row each (`controls.camera_hold_s` 0.1-3, default 0.75, hold DOWN this long in look mode to recentre; `camera.pitch_range_deg` 5-80, default 45, vertical look limit each way; the eye also stays above the water).

- [ ] **Step 3: Run the full tests.** Run: `make -C tests test 2>&1 | tail -15`. Expected: all pass.

- [ ] **Step 4: Check in the simulator** (`make` then the Playdate Simulator; see `README.md`). Pick the TP52 trailing view: DOWN, crank sideways; tap DOWN, crank to raise and lower (boat stays put, size unchanged); hold DOWN about 0.75 s and confirm it eases back; confirm a quick double-tap does not recentre.

- [ ] **Step 5: Commit.**

```bash
git add config/help.md docs/boat-schema.md README.md
git commit -m "docs: look axis switch and hold-to-recentre"
```

## Self-Review Notes

- Coverage: hold-to-recentre (Task 1), configurable 0.75 s (Task 1 field), axis switch via DOWN (Task 1), fixed distance (Task 2), docs (Task 3).
- Open interpretation, flagged for review: the tap that toggles is DOWN while already in look mode; vertical in the near view looks up/down rather than orbiting (there is no distance to hold).
