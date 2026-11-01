# Rudder Authority Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every shipped boat steers consistently: more wheel never means less rudder force, a slow or stalled boat can still turn, and every fore-and-aft boat has slight weather helm close-hauled, proven by one test that runs all of `boats/*.json`.

**Architecture:** Fix the rudder model in `src/sim/boat.c` (monotone lift past stall, a flow floor `foils.rudder_min_flow`), add a small shared `src/sim/balance.c/.h` that measures weather helm and the close-hauled limit (used by the test and by `tools/boat_check.c`), retune the boat JSON files until the new test passes. The test is written first and drives the tuning.

**Tech Stack:** C99, the `tests/t.h` harness (ASan/UBSan, `-Wall -Wextra -Werror`), boat JSON files, `tools/boat_check.c`.

**Spec:** `docs/superpowers/specs/2026-10-31-rudder-authority-design.md`

## Global Constraints

- C99; floats; no heap allocation in `boat_step`.
- `make -C tests test` (ASan + UBSan) stays green. Existing tests in `test_boat.c`, `test_autopilot.c`, `test_autotrim.c`, `test_boatcfg.c` may change only where intended behaviour changed, with a comment saying why.
- Boat files are read into a 16 KB buffer; new fields need defaults in `src/sim/boatcfg.c` so existing and community boat files still load.
- Fixed sim step `SIM_DT` (60 Hz); no change to the substep structure in `game_update`.
- No per-boat special cases in code; sizes are handled by data and by scaling from the boat's own length and hull speed.
- Crank-to-wheel mapping, wheel speed, autopilot logic and auto-trim logic are unchanged.
- Steering astern keeps working as if going ahead (`atan2f(vr, speed_u)` with `speed_u = |u|`).
- Update `docs/boat-schema.md` for every new field.
- Stop and report (do not add new model levers) if, after Tasks 2-4, the TP52 cannot head up from a beam reach to its close-hauled angle.

## Review Focus

- A stopped boat (u = 0, v = 0, full lock) must not spin like a car: yaw rate at rest stays well below the yaw rate at cruising speed on the same boat. Pinned in Task 3.
- A close-hauled boat holding a steady wheel (and the autopilot) must hold course without rounding up or broaching. Pinned in Task 4.
- The longship (square rig, `awa_min_deg` 55) is judged against its own close-hauled limit and its tack is skipped; the check must not fail or loop on it. Pinned in Task 1.
- Boat files with no sails, only a spinnaker, or odd rigs must not crash or hang the balance function. Pinned in Task 1.
- The assist warning must not appear for autopilot or autotrim use, at cruising speed, or with a centred wheel. Pinned in Task 3b.
- Gusts and chop must not stop a tack from completing (TP52 in CHOPPY with `sea_hull_forces`). Pinned in Task 1.

---

## File Structure

- Create `src/sim/balance.h`, `src/sim/balance.c`: pure measurement helpers on top of `boat_step`. Picked up automatically by `tests/Makefile`, the top-level device Makefile and `tools/pr_check.sh` (all glob `src/sim/*.c`).
- Create `tests/test_rudder.c`: the all-boats test.
- Modify `src/sim/boat.c`: rudder lift and flow floor.
- Modify `src/sim/boatcfg.h`, `src/sim/boatcfg.c`: `rudder_min_flow`.
- Modify `boats/{sunfish,dinghy,j24,tp52,longship}.json`: retune.
- Modify `tools/boat_check.c`: balance warning.
- Modify `src/sim/boat.h`, `src/sim/hudinfo.h` and `src/pd/hud.c`: the assist warning (Task 3b).
- Modify `docs/boat-schema.md`.

---

### Task 1: Balance helpers and the all-boats test (written first, failing)

**Files:**
- Create: `src/sim/balance.h`, `src/sim/balance.c`, `tests/test_rudder.c`

**Interfaces:**
- Produces (in `balance.h`, includes `boatcfg.h`):
  - `float balance_closehauled_limit_deg(const BoatCfg *c)`: the largest `awa_min_deg` among sails with `shape != 2`; `0` when there are none.
  - `bool balance_is_fore_and_aft(const BoatCfg *c)`: `balance_closehauled_limit_deg(c) < 30.0f` and at least one non-spinnaker sail.
  - `bool balance_weather_helm_deg(const BoatCfg *c, float *rudder_deg)`: flat water, true wind 10 m/s, `Autotrim` on, start at TWA 45° and heading controller (P on heading error to the settled heading) for 30 s, then average `wheel * rudder_max_deg` over the last 5 s. Returns false if the boat is not fore-and-aft or `|r|` has not settled below 0.02 rad/s. Positive result means rudder to windward.
- Consumes: `boat_init`, `boat_inputs_init`, `boat_step`, `wind_step`, `autotrim_init/update` (see `src/sim/autotrim.h`).
- Tests (`tests/test_rudder.c`) load each of `sunfish dinghy j24 tp52 longship` via `fopen("../boats/<name>.json")` and `boatcfg_load`; helper `static void run_all(void (*fn)(const char *name, const BoatCfg *c))`. Use `sqrt(length)` scaling: `#define TACK_K 3.0f` (tuned in Task 4) so the time limit is `TACK_K * sqrtf(c->hull.length)` seconds.

- [ ] **Step 1: Write `tests/test_rudder.c` with five tests, each looping over all boats**
  - `test_rudder_force_monotone_in_wheel`: at fixed `u = 0.5 * hull_speed`, `v = r = 0`, step `boat_step` with `wheel_target` stepping 0 → 1 in 20 increments (wheel snapped, use `b.wheel` set directly), and assert `b.helm_load` magnitude never decreases by more than 1 %.
  - `test_weather_helm_in_band`: for fore-and-aft boats, `balance_weather_helm_deg` returns true and `1.0f <= deg && deg <= 8.0f`.
  - `test_full_lock_reaches_closehauled_limit`: start close-hauled at TWA 45° (or limit + 5° if the limit is larger), speed settled; hold full lock toward the wind for up to `TACK_K * sqrtf(length)` s; assert min |TWA| reaches `limit + 10°`. Fore-and-aft boats must also finish the tack (TWA changes sign and then |TWA| > 40°) within that time; skip the tack part when the limit is ≥ 30° (longship).
  - `test_full_lock_heads_up_from_beam_reach`: start at TWA 90°, trim eased, autotrim on, full lock for the same time limit; assert |TWA| ends ≤ limit + 10°... use 50° for fore-and-aft boats (spec: "close-hauled angle or better").
  - `test_tp52_choppy_tack`: TP52 only, `WEATHER_CHOPPY` wind (use `wc.preset[WEATHER_CHOPPY]`), `sea_init`, `sea_hull_forces(&sea,&b,&c,t)` each step; same assertions as the two cases above.
  - Also `test_balance_helpers_survive_odd_boats`: `boatcfg_default` with `sail_count = 0`, and a config with only a spinnaker: `balance_weather_helm_deg` returns false, no crash, limit 0.
- [ ] **Step 2: Create `balance.h`/`balance.c` with the signatures above** (bodies minimal; the weather-helm function does the simulation described).
- [ ] **Step 3: Run `make -C tests test`**
  Expected: builds clean; `test_rudder` FAILS on TP52 (lift monotone, weather helm band, head up) and probably others; all older tests still pass. Record the measured numbers for each boat in the commit message.
- [ ] **Step 4: Commit**
  ```bash
  git add src/sim/balance.h src/sim/balance.c tests/test_rudder.c
  git commit -m "test: all-boats rudder authority test (failing for tp52)"
  ```

### Task 2: Rudder lift is monotone in wheel angle

**Files:**
- Modify: `src/sim/boat.c:31-39` (`foil_cl`) and the rudder block at `:140-148`

**Interfaces:**
- Produces: `static float foil_cl_hold(float slope, float stall, float a)` (name is free) used for the rudder only: linear to stall, then holds the stall peak (allow at most a 5 % droop by 2× stall). The board keeps `foil_cl`.

- [ ] **Step 1: Run `test_rudder_force_monotone_in_wheel`**, confirm it fails on boats whose `rudder_max_deg` is above `rudder_stall_deg`.
- [ ] **Step 2: Implement `foil_cl_hold` and call it for `cl_r`.**
- [ ] **Step 3: Run `make -C tests test`**. Expected: the monotone test passes; any existing test that now fails is investigated (update only if the change is intended, with a comment).
- [ ] **Step 4: Commit** `feat: rudder lift holds its stall peak past stall`

### Task 3: Minimum rudder flow

**Files:**
- Modify: `src/sim/boatcfg.h` (add `rudder_min_flow` to `FoilCfg`), `src/sim/boatcfg.c` (field entry in the `FF(...)` table near line 60-65 plus default), `src/sim/boat.c`, `docs/boat-schema.md`, `tests/test_boatcfg.c`

**Interfaces:**
- Produces: `FoilCfg.rudder_min_flow` (m/s, range 0-5). Default: `0.15f * hull.hull_speed`, applied in `boatcfg_default` after the hull defaults and also when a loaded file omits the field (the loader must not leave it 0 for old files; `boatcfg_to_json` round-trip must keep passing).
- In `boat_step`: `qa_r` uses `fmaxf(sqrtf(u*u+vr*vr), rudder_min_flow)` squared instead of `u*u+vr*vr`. `alpha_r` still uses `speed_u`.

- [ ] **Step 1: Write failing tests**
  - `tests/test_boatcfg.c`: `test_rudder_min_flow_default_and_clamp`: default equals `0.15f * hull_speed`; a JSON that sets it to 99 clamps to 5 with a warning; a file without the field gets the default.
  - `tests/test_rudder.c`: `test_stopped_boat_does_not_spin`: for each boat, at u = v = r = 0 with full lock held for 3 s the peak |r| is below `0.5 *` the |r| after 3 s of full lock at `0.5 * hull_speed` (steady speed assumed by resetting `u` each step).
  - `tests/test_rudder.c`: `test_steering_astern_unchanged`: at `u = -1`, full lock gives the same sign of `r` as at `u = +1`.
- [ ] **Step 2: Run, confirm they fail** (compile failure on the new field counts).
- [ ] **Step 3: Implement the field, default, clamp, flow floor.**
- [ ] **Step 4: Document `foils.rudder_min_flow` in `docs/boat-schema.md`** (the `foils` row at line 17 plus a sentence on the default).
- [ ] **Step 5: Run `make -C tests test`**. Expected: new tests pass; the tack and head-up tests move toward passing. If the spin test fails, lower the 15 % default until it holds.
- [ ] **Step 6: Commit** `feat: rudder_min_flow keeps a slow boat steerable`

### Task 3b: On-screen warning while the rudder flow floor is helping

Requested by the user after the plan was approved: show a warning on screen when the player is being helped, saying how. Only the new rudder flow floor counts; autopilot and autotrim already show their icons and are **not** part of this warning.

**Files:**
- Modify: `src/sim/boat.h` (`BoatState`), `src/sim/boat.c`, `src/sim/hudinfo.h`, `src/sim/hudinfo.c` (create if the HUD helpers live elsewhere; follow where `hud_shows_nav` is defined), `src/pd/hud.c`, `tests/test_hudinfo.c`, `tests/test_rudder.c`, `docs/boat-schema.md`

**Interfaces:**
- Consumes: `FoilCfg.rudder_min_flow` from Task 3.
- Produces:
  - `BoatState.rudder_assist` (`float`, 0..1, derived each step like `helm_load`): `0` when the rudder's real flow `sqrtf(u*u + vr*vr)` is at or above `rudder_min_flow`, else `1 - flow / rudder_min_flow`.
  - `const char *hud_assist_text(float rudder_assist, float wheel)` in `hudinfo.h`: returns `"RUDDER ASSIST: LOW SPEED"` when `rudder_assist > 0.1f` **and** `fabsf(wheel) > 0.05f` (the wheel is actually being turned, so a drifting boat with a centred wheel does not nag); otherwise `NULL`. Non-finite inputs give `NULL`.
- In `hud_draw` (`src/pd/hud.c`): when the text is non-NULL, draw it centred in a white-on-black inverted bar directly above the status bar (`top - 16`, height 14), inside the camera area, so it does not cover the one-row HUD.

- [ ] **Step 1: Write failing tests**
  - `tests/test_hudinfo.c`: `test_assist_text_only_when_flow_floor_helps_and_wheel_turned`: `hud_assist_text(0.5f, 0.5f)` equals `"RUDDER ASSIST: LOW SPEED"`; `(0.0f, 0.5f)`, `(0.5f, 0.0f)`, `(0.05f, 0.5f)`, `(NAN, 0.5f)`, `(0.5f, NAN)` all return `NULL`.
  - `tests/test_rudder.c`: `test_rudder_assist_reports_flow_floor`: for each boat, at `u = 0.5 * hull_speed` after one `boat_step` `rudder_assist == 0`; at `u = v = r = 0` it is `> 0.9`.
- [ ] **Step 2: Run, confirm they fail.**
- [ ] **Step 3: Implement the field in `boat_step`, `hud_assist_text`, and the `hud_draw` banner.**
- [ ] **Step 4: Document `rudder_assist` under the flow-floor note in `docs/boat-schema.md`** (one sentence: the HUD warns while the floor is raising the rudder's flow).
- [ ] **Step 5: Run `make -C tests test`**, then check the banner by hand in the simulator (use the `run` skill): a stopped boat with the wheel turned shows it, a boat at cruising speed does not, and turning autopilot or autotrim on does not show it.
- [ ] **Step 6: Commit** `feat: warn on screen while the rudder flow floor is helping`

### Task 4: Weather-helm retune of the boat files

**Files:**
- Modify: `boats/tp52.json` first, then `j24.json`, `sunfish.json`, `dinghy.json`; `longship.json` only if a test demands it
- Tune: `TACK_K` in `tests/test_rudder.c`

**Interfaces:**
- Consumes: `balance_weather_helm_deg`; the `test_rudder` tests.
- Levers (builder's choice, spec call 3): sail `ce_x`, `foils.board_x`, rudder size. Check `src/pd/render.c:887` (`ce_x` is used for draw order only) before moving `ce_x`.

- [ ] **Step 1: Print current weather helm for each boat** (add a temporary `printf` in `test_weather_helm_in_band`, remove it after).
- [ ] **Step 2: TP52 first.** Move the lever so the settled rudder angle is within 1-8° to windward. Re-run `test_full_lock_reaches_closehauled_limit`, `test_full_lock_heads_up_from_beam_reach`, `test_tp52_choppy_tack`. If the TP52 still cannot head up from a reach after retune plus Tasks 2-3, stop and report to the user.
- [ ] **Step 3: Repeat for the other boats**, smallest change that gets into the band. Reaching feel is checked by the existing `tests/test_boat.c` and `test_autopilot.c` staying green.
- [ ] **Step 4: Set `TACK_K`** to the smallest round number that passes all five boats with ~30 % margin.
- [ ] **Step 5: Add Review Focus test** `test_closehauled_holds_without_broaching` in `tests/test_rudder.c`: with the weather-helm wheel angle held fixed for 60 s, heading drifts by less than 25° and the boat does not go through the wind.
- [ ] **Step 6: Run `make -C tests test` and `tools/pr_check.sh`** (boat files under 16 KB). Expected: all green.
- [ ] **Step 7: Commit** `feat: retune boat balance for slight weather helm`

### Task 5: `boat_check` balance warning

**Files:**
- Modify: `tools/boat_check.c`, `docs/boat-schema.md`

**Interfaces:**
- Consumes: `balance_is_fore_and_aft`, `balance_weather_helm_deg`.
- Behaviour: for a fore-and-aft boat, print a warning `weather helm %.1f deg is outside 1-8 (sail ce_x, board_x or rudder size)` when the value is out of band, or `could not settle close-hauled` when the function returns false. Square-rigged boats are skipped silently. Warnings do not change the exit code unless the existing tool already treats warnings as failures (follow what it does).

- [ ] **Step 1: Run `tools/boat_check.c` on a deliberately lee-helm copy of `tp52.json`** (scratch copy with `board_x` moved aft of the sail CE) and confirm it currently prints "ok".
- [ ] **Step 2: Add the warning.**
- [ ] **Step 3: Re-run on the scratch copy (warns) and on all `boats/*.json` (ok).**
- [ ] **Step 4: Document the check in `docs/boat-schema.md`** (one short paragraph in the validation section).
- [ ] **Step 5: Commit** `feat: boat_check warns on lee helm`

### Task 6: Regression sweep

**Files:** none expected; fix forward in whichever file breaks.

- [ ] **Step 1: Run `make -C tests test` and the device build (`make` at top level or `tools/pr_check.sh`).** Expected: green, no new warnings.
- [ ] **Step 2: Autopilot and autotrim on the TP52 in CHOPPY.** Confirm via `tests/test_autopilot.c` and `tests/test_autotrim.c` that they pass; if autopilot gains oscillate with the more responsive rudder, adjust gains only as far as the existing tests need and say so in the commit.
- [ ] **Step 3: Run the simulator** (use the `run` skill) on the TP52 in CHOPPY and confirm by hand that heading up from a reach and tacking work, and that a stopped boat does not spin.
- [ ] **Step 4: Commit any fixes** `fix: <what>`; otherwise nothing to commit.
