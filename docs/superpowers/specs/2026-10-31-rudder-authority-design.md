# Rudder authority: consistent steering across boat sizes

## Intent

The player reports that the rudder "doesn't have the authority I'd expect", and specifically
that the TP52 in CHOPPY weather (10 m/s wind, 2 m waves) would not turn into the wind with
auto-trim on, both when heading up from a reach and when trying to tack from close-hauled.

The goal is that on every boat, from the sunfish to the TP52 and the longship, putting the
helm over turns the boat the way a sailor expects: it heads up, goes through the wind in a
tack, and recovers from irons, in flat water and in chop. The way to get there is to fix the
causes below in the model, then prove it with one test that runs all boats.

## What was measured

Everything below was measured by driving the real `boat_step`, `sea_hull_forces` and
`autotrim_update` in a throwaway harness (not in the repo). Wind 10 m/s, wave 2 m (CHOPPY).

1. **Steady course-holding is fine** on every boat. This is not a problem to fix.
2. **The wheel's top half is past stall.** The rudder stalls at 14-16 deg but travels 30-35 deg.
   `foil_cl` (`src/sim/boat.c`) falls to 70 % of peak by twice the stall angle, so the last
   half of the wheel gives *less* force than the middle.
3. **Lee helm when close-hauled.** The sail yaw moment pushes the bow away from the wind. The
   area-weighted sail centre of effort (main and jib, not the spinnaker) against the board:

   | boat | length m | sail CE x m | board x m | rudder area m^2 |
   |------|---------:|------------:|----------:|----------------:|
   | sunfish | 4.2 | -0.10 | 0.2 | 0.06 |
   | dinghy | 4.0 | 0.12 | 0.0 | 0.06 |
   | j24 | 7.3 | 0.60 | 0.5 | 0.15 |
   | tp52 | 15.8 | 1.17 | 0.5 | 0.50 |
   | longship | 12.0 | 0.20 | 0.0 | 0.30 |

   A sail centre of effort ahead of the board means lee helm. The TP52 is worst, the J24 is
   about level, the sunfish already has weather helm.
4. **TP52 heading up from a reach stalls at ~42 deg TWA.** With full lock held, the turn rate
   falls to ~0 there. Sail yaw is about -15,000 N m against a rudder moment of +6,000 to
   +10,000 N m, heel ~18 deg. This is the same with auto-trim off and with trim set by hand,
   so auto-trim is not the cause. Doubling `rudder_area` barely changes it: the rudder
   already sits on the stalled plateau and the boat skids sideways to cancel the rest.
5. **TP52 trying to tack from close-hauled** does turn (10-13 deg/s), reaches TWA ~0 at
   ~1 m/s, then sits in irons for 10-15 s drifting astern at ~1 m/s, turn rate near zero,
   before falling off. The rudder has almost no authority at that speed because lift goes as
   speed squared.
6. Steering force is `~ u^2`, so every boat is weak at low speed; the big boats are the
   weakest in absolute terms. The longship also skids badly because the hull has no yaw
   moment and almost no lateral resistance (`fy_hull` is tiny). That is left alone, see
   Non-goals.

## Goals

- Rudder force never falls as the wheel goes further over: more wheel always means at least
  as much turning force on every boat.
- A stalled or very slow boat can still steer: it can get out of irons and complete a tack.
- Every shipped boat has slight weather helm when close-hauled at moderate heel, so holding
  the helm to windward heads the boat up instead of balancing it at ~42 deg.
- One test, run for every boat in `boats/*.json`, passes for all of them, and fails if a
  boat's balance or steering regresses. At least one case runs with CHOPPY sea forces.
- Behaviour is described in `docs/boat-schema.md` for any new field.

## Non-goals

- A hull yaw moment or a bigger lateral-resistance model. The longship's skid is a separate
  project.
- Modelling reversed rudder action when the boat moves astern. Today steering "works" astern
  as if going ahead, which helps the player leave irons; keep that.
- Changing the crank-to-wheel mapping, wheel speed, autopilot logic or auto-trim logic.
  (They must keep working, see Constraints.)
- Rendering, camera, sound, wave generation.
- Switching to the original game's speed-saturating turn model (Approach C below).

## Anti-goals

- Boats that turn like cars: a stopped boat must not spin on the spot. The minimum rudder
  flow must be small and must not make the turn rate at zero speed look like cruising speed.
- Boats that round up uncontrollably. Weather helm must be slight; the autopilot and a
  player holding a steady wheel must still hold a close-hauled course without broaching.
- Noticeably changed feel at normal speeds where nothing was wrong (reaching, running,
  steady course keeping).
- Per-boat special cases in code. Sizes are handled by data and by scaling from the boat's
  own length and hull speed.

## Constraints

- C99, runs on Playdate and on the host; floats, no heap allocation in `boat_step`.
- `make -C tests test` (ASan + UBSan) stays green. Existing tests in `tests/test_boat.c`,
  `test_autopilot.c`, `test_autotrim.c` and `test_boatcfg.c` may be updated only where the
  intended behaviour changed, with a note saying why.
- Boat files are read into a 16 KB buffer (`tools/boat_check.c` enforces this); new fields
  need defaults in `src/sim/boatcfg.c` so existing and community boat files still load.
  See `docs/boat-schema.md` and `docs/superpowers/specs/2026-10-30-boat-contribution-design.md`
  so the validation stays consistent with how contributed boats are checked.
- Fixed sim step (`SIM_DT`, 60 Hz in the harness); no change to the substep structure in
  `game_update`.

## Approaches considered

- **A. Retune boat files only.** Move sail and board positions, cap `rudder_max_deg` near
  stall, enlarge rudders. No code change, but irons and low-speed steering are not fixed, and
  each boat is hand-tuned with nothing stopping the next one from being wrong.
- **B. Fix the model in `boat.c`, plus a retune and an all-boats test.** Chosen by the
  project owner. Removes the stall dead zone, adds a small flow floor for the rudder, and
  verifies balance and steering for all boats with a test.
- **C. Replace steering with the original game's model** (wheel commands a turn rate that
  scales with speed and length). Same feel on every size by construction, but drops the
  rudder-versus-sail physics and hides the lee-helm problem. Rejected.

## Design (B)

1. **Rudder lift is monotone in wheel angle.** `foil_cl` (or a rudder-specific variant) holds
   lift at the stall peak, or very close to it, past stall instead of falling to 70 %. The
   board may keep its existing curve; whether it shares the change is the builder's call.
2. **Minimum rudder flow.** The rudder's dynamic pressure uses an effective speed that does
   not drop below a floor `rudder_min_flow` (m/s). *My call:* new `foils.rudder_min_flow`,
   defaulting to 15 % of `hull.hull_speed` so it scales with boat size (about 1.35 m/s on
   the TP52, about 0.6 m/s on a small dinghy). The builder sizes the default from the
   tests, as long as the anti-goal about spinning on the spot holds.
3. **Weather helm.** Each boat is tuned in its JSON (sail `ce_x`, `board_x`, rudder size)
   so that, close-hauled at moderate heel in flat water, the wheel angle needed to hold the
   course is slightly to windward. *My call:* target roughly 1 to 8 degrees of rudder.
   Because `ce_x` may also be used by the drawing code, check the render before moving it.
   `tools/boat_check.c` gains a warning when the balance check fails for a boat file, so
   contributed boats get the same guard.
4. **The test.** A new test (for example `tests/test_rudder.c`) loads each `boats/*.json`
   and checks, with thresholds scaled by length (`sqrt(L)`) rather than written per boat:
   1. rudder force is non-decreasing from zero to full wheel at a fixed speed;
   2. close-hauled, weather helm exists and is within the target band;
   3. from a close-hauled start, full lock reaches TWA within 10 deg of head-to-wind and
      the boat completes a tack to the other side within a time limit;
   4. from a beam reach, full lock heads up to a close-hauled angle or better;
   5. the same for the TP52 with CHOPPY wind and `sea_hull_forces`.

## Calls made after the builder check

These were settled without the project owner; flagged so they can be checked.

1. **Longship (square rig) is judged against its own sail limit, not head-to-wind.** Its only
   sail has `awa_min_deg` 55, and a measured run showed it cannot get closer than ~66 deg
   TWA (turn rate ~0 there) and cannot tack. Test cases 3 and 4 therefore use each boat's
   close-hauled limit derived from its sails' `awa_min_deg` (the largest value among
   non-spinnaker sails; 0 for fore-and-aft rigs, so "head-to-wind" there). For a boat whose
   limit is above ~30 deg, case 3 becomes "reaches that limit" and the tack is skipped. No new
   JSON field is needed, so this is not a per-boat special case in code. The longship's skid
   stays out of scope.
2. **Weather-helm operating point.** Flat water, true wind 10 m/s, auto-trim on, boat settled
   close-hauled for 30 s (heel and speed steady), wheel held by a simple heading controller;
   measure the settled wheel angle, converted to rudder degrees as `wheel * rudder_max_deg`.
   Band: 1 to 8 degrees of rudder to windward. The same function is used by the test and by
   `boat_check`, for fore-and-aft rigs only. Reaching feel is checked by running the existing
   boat tests, not by a new threshold.
3. **Which lever fixes the lee helm is the builder's.** Moving sail `ce_x`, `board_x`, or
   both are all acceptable; pick the one that disturbs the render and reach handling least.
   If the retune plus the monotone-lift change does not make the TP52 head up from a reach to
   its close-hauled angle, the builder stops and reports instead of adding a new model lever.
4. **Thresholds** come from each boat's own data: the close-hauled limit above and a time limit
   of `K * sqrt(length)` seconds with one global `K` tuned against the five boats.

## Left to the builder

- Exact numbers: stall plateau level, `rudder_min_flow` default, the weather-helm band, the
  time limits, and the per-boat retune values. Start from the measurements above.
- Whether the board foil gets the same post-stall treatment.
- Whether autopilot gains need a touch-up once boats are more responsive; the existing
  autopilot tests are the check.
- The `docs/boat-schema.md` wording and a `boat_check` message.
