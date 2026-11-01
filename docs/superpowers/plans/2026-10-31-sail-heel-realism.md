# Sail / Heel Realism Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four realism gaps in the sail aerodynamics and heel physics: wrong off-wind sail coefficients, no heel-induced weather helm, no depowering in strong wind, and a fixed hydro-force depth for every boat.

**Architecture:** All changes are small edits to the existing sim: `sail_force()` in `src/sim/sail.c` gains heel-dependent yaw and lift terms, the shared Cl table changes in the config defaults and boat JSONs, and the hard-coded `HYDRO_ARM` in `src/sim/boat.c` becomes a per-boat `hull.clr_depth` config field.

**Tech Stack:** C99, `-Wall -Wextra -Werror` with ASan/UBSan, test runner `make -C tests test` using `tests/t.h` (`T_ASSERT`, `T_NEAR`, `T_RUN`).

**Spec:** No spec doc. The requirements are the four findings from the sail/heel review in this session, restated in each task's Goal line. Reference for conventions: `src/sim/boat.h` (boat frame x forward, y starboard, heel > 0 leans to starboard, positive yaw turns the bow to starboard) and `docs/boat-schema.md`.

## Global Constraints

- C99, must build clean under `-Wall -Wextra -Werror -fsanitize=address,undefined` (see `tests/Makefile`).
- `sail_force()` signature in `src/sim/sail.h` does not change.
- `Table` has at most `TABLE_MAX` = 16 points, x strictly ascending.
- Boat JSON that omits a new field must load with the registered default (`boatcfg_load` already does this for registered fields).
- The working tree has uncommitted changes by the user in `config/weather.json`, `src/pd/render.c`, `src/sim/hudinfo.*`, `src/sim/weather.*`, `tests/scenarios/boat_weather.txt`, `tests/scenarios/capsize_restart.txt`, `tests/test_hudinfo.c`. Never edit, stage, revert or re-baseline these. Stage only the files named in a task, by path (never `git add -A` or `git commit -a`).
- Do not re-baseline golden or scenario files. If a golden/scenario test fails because of a physics change, stop and report the test name and diff to the reviewer.
- Every commit message ends with the line `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Tasks run strictly in order 1 → 4 (they share `boatcfg.c`, `sail.c` and the boat JSONs).

## Review Focus

- Port tack mirror: heel < 0 / wind from port must give the mirrored heel-helm sign (bow toward the wind on both tacks). Pinned in Task 2.
- Square-rigged longship still runs downwind and reaches after the Cl table change (its sail is driven at AoA 60–90°). Pinned in Task 1 by running the existing `test_longship_*` tests.
- Depowering must not stop the boat capsizing in a real gust (`test_capsize_in_gust`) or make it uncapsizable. Pinned in Task 3.
- Custom boat JSON without `clr_depth` loads with 0.4 m and behaves exactly as before. Pinned in Task 4.
- Extreme heel (±105°, the `MAX_HEEL` clamp) and NaN heel stay finite through the new terms. Pinned in Task 3.

---

### Task 1: Off-wind sail coefficients

**Goal:** A sail sheeted at 90° angle of attack (dead run) should produce almost no lift, only drag, instead of the current Cl 1.1 that creates a large sideways force.

**Files:**
- Modify: `src/sim/boatcfg.c:30-33` (`SAIL_CL` default table)
- Modify: `boats/dinghy.json`, `boats/sunfish.json`, `boats/j24.json`, `boats/tp52.json`, `boats/longship.json` (every `cl` array that begins `[[0,0],[5,0.5]` — main, jib, square; NOT the spinnaker's)
- Test: `tests/test_sail.c`

**Interfaces:**
- Consumes: `sail_force(const SailCfg*, float trim_rad, float hoist, float awa_rad, float aws, float heel_rad, float slot_cl_mult) -> SailForce` (unchanged).
- Produces: nothing new. Data change only.

New `cl` table (12 points, replaces the old one; the `cd` table is unchanged):
`[[0,0],[5,0.5],[10,1],[15,1.35],[18,1.4],[25,1.1],[40,0.9],[60,0.7],[75,0.35],[90,0.1],[120,0],[180,0]]`

- [ ] **Step 1: Baseline.** Run `make -C tests test` and note which tests already fail (the tree is dirty). Report them; do not fix them.
- [ ] **Step 2: Write the failing test** `test_dead_run_has_little_side_force` in `tests/test_sail.c` and register it in `main` with `T_RUN`. It calls `sail_force(main_sail(), D(90), 1, D(180), 8, 0, 1)` (AoA 90°) and asserts `f.fx > 50` and `fabsf(f.fy) < 0.1f * f.fx`.
- [ ] **Step 3: Run** `make -C tests build/test_sail && ./tests/build/test_sail`. Expected: FAIL on the new test (current fy/fx ≈ 0.85).
- [ ] **Step 4: Replace the `SAIL_CL` table** in `src/sim/boatcfg.c` and the matching `cl` arrays in the five boat JSONs with the table above.
- [ ] **Step 5: Run** `make -C tests test`. Expected: the new test passes and nothing that passed at baseline fails. In particular `test_longship_runs_downwind`, `test_longship_beam_reach`, `test_top_speed_near_hull_speed` and `test_upwind_angle` in `tests/test_boat.c` must pass. If any fails, report which and by how much; do not loosen the assertion.
- [ ] **Step 6: Commit** the files above plus `tests/test_sail.c` with message `fix: sail lift fades to zero by 90 deg AoA so a dead run has no side force`.

---

### Task 2: Heel-induced weather helm

**Goal:** As the boat heels, the sail's centre of effort moves to leeward, so the forward drive force turns the bow toward the wind (weather helm), on both tacks.

**Files:**
- Modify: `src/sim/sail.c:46-47` (yaw moment in `sail_force`)
- Test: `tests/test_sail.c`

**Interfaces:**
- Consumes: `sail_force` as above.
- Produces: `SailForce.yaw_moment` now equals `fy * ce_x - HEEL_HELM_K * fx * ce_z * sin(heel_rad)`, with `#define HEEL_HELM_K 0.5f` at the top of `sail.c` (comment: CLR also shifts to leeward, so only half the CE shift is felt as helm). Derivation: heel > 0 puts the CE at lateral +y by `ce_z * sin(heel)`; a forward force `fx` there has yaw moment `-y * fx`, negative = bow to port = into the wind that blows from port when heel > 0.

- [ ] **Step 1: Write the failing tests** in `tests/test_sail.c`, registered in `main`:
  - `test_heel_adds_weather_helm`: `SailForce f = sail_force(s, D(60), 1, D(-90), 8, D(20), 1)` with `s = main_sail()` (wind from port, heeled to starboard). Assert `f.yaw_moment` is within 1e-3 relative of `f.fy * s->ce_x - 0.5f * f.fx * s->ce_z * sinf(D(20))` (use `T_NEAR` with eps `0.001f * fabsf(expected) + 1e-3f`), and `f.yaw_moment < f.fy * s->ce_x`.
  - `test_heel_helm_mirrors_on_port_tack`: compute the same with `awa = D(+90)`, `heel = D(-20)`; assert `yaw_moment` equals the negative of the first test's value within 1e-3 relative.
  - `test_upright_has_no_heel_helm`: with heel 0, `f.yaw_moment` equals `f.fy * s->ce_x` (`T_NEAR`, eps 1e-3).
- [ ] **Step 2: Run** `make -C tests build/test_sail && ./tests/build/test_sail`. Expected: first two FAIL, third passes.
- [ ] **Step 3: Implement** the yaw formula above in `sail_force` (`src/sim/sail.c`), leaving the early-return NaN guard as is.
- [ ] **Step 4: Run** `make -C tests test`. Expected: no new failures vs. baseline. If `test_upwind_angle`, `test_tack_takes_4_to_8s_and_loses_speed` or `test_autopilot` fail because the helm is too strong, report the numbers; the reviewer will decide whether to lower `HEEL_HELM_K`. Do not change it yourself.
- [ ] **Step 5: Commit** `src/sim/sail.c tests/test_sail.c` with message `feat: heel moves the sail's centre of effort to leeward, giving weather helm`.

---

### Task 3: Depower when heeled

**Goal:** Strong wind at 25 kn upwind currently heels the boats 36–49°; sails should flatten (lose lift, keep drag) as the boat heels past ~20° so heel self-limits.

**Files:**
- Modify: `src/sim/sail.c:33` (Cl multiplier)
- Test: `tests/test_sail.c`, `tests/test_boat.c`

**Interfaces:**
- Consumes: `sail_force` as above.
- Produces: in `sail.c`, `#define DEPOWER_START_DEG 20.0f`, `#define DEPOWER_RANGE_DEG 25.0f`, `#define DEPOWER_MAX 0.5f`. The Cl used in `sail_force` is multiplied by `1 - DEPOWER_MAX * clampf((|heel_deg| - DEPOWER_START_DEG) / DEPOWER_RANGE_DEG, 0, 1)`. `cd` is not scaled.

- [ ] **Step 1: Write the failing tests.**
  - In `tests/test_sail.c`, `test_no_depower_below_20_deg`: with `s = main_sail()`, trim `D(0)`, awa `D(15)` (AoA 15°), aws 8, compare `sail_force(..., heel D(15), ...)` to heel 0: `f15.fy` equals `f0.fy * cosf(D(15))^2` within 1e-3 relative.
  - `test_depower_flattens_sail_at_40_deg`: same wind, heel `D(40)`: assert `f40.fy < 0.75f * f0.fy * cosf(D(40)) * cosf(D(40))`.
  - `test_depower_survives_extreme_heel`: heel `D(105)` and `D(-105)` and `NAN` give finite `fx, fy, heel_moment, yaw_moment`.
  - In `tests/test_boat.c`, `test_strong_wind_heel_is_limited`: using the file's `sim_init`/step helpers (read them first), default dinghy, 25 kn wind (`25 * KN`), TWA 45, autopilot and autotrim on as `test_upwind_angle` does, run 60 s; assert final `fabsf(heel) < D(32)` and that the boat has not capsized. Register in `main`.
- [ ] **Step 2: Run** the two test binaries. Expected: the 40° and boat-level tests FAIL (dinghy baseline heel ≈ 36°), the others pass.
- [ ] **Step 3: Implement** the Cl multiplier in `sail_force`, converting `heel_rad` to degrees with the file's existing `PI_F`.
- [ ] **Step 4: Run** `make -C tests test`. `test_capsize_in_gust` must still pass and `test_moderate_wind_does_not_capsize` must still pass. If `test_strong_wind_heel_is_limited` still fails, report the measured heel and stop; the reviewer will tune `DEPOWER_MAX`/`DEPOWER_START_DEG`.
- [ ] **Step 5: Commit** `src/sim/sail.c tests/test_sail.c tests/test_boat.c` with message `feat: sails depower as the boat heels past 20 degrees`.

---

### Task 4: Per-boat centre of lateral resistance depth

**Goal:** Replace the fixed 0.4 m `HYDRO_ARM` with a per-boat `hull.clr_depth`, so deep keelboats get the longer heel lever arm they really have.

**Files:**
- Modify: `src/sim/boatcfg.h` (`HullCfg`), `src/sim/boatcfg.c` (`HULL_F`), `src/sim/boat.c:7` and `:155`
- Modify: `boats/j24.json` (`"clr_depth":0.6`), `boats/tp52.json` (`"clr_depth":1.8`) inside the `hull` object; dinghy, sunfish and longship keep the 0.4 default
- Modify: `docs/boat-schema.md:16` (add `clr_depth` to the hull row, with unit and meaning)
- Test: `tests/test_boatcfg.c`, `tests/test_boat.c`

**Interfaces:**
- Consumes: `boatcfg_load`, `boatcfg_to_json`, `boat_step`.
- Produces: `float clr_depth;` in `HullCfg` (metres below the waterline of the combined hull/board/rudder lateral force), registered as `FF(HullCfg, clr_depth, 0.4f, 0, 6)` right after `heel_drag`. `boat_step` uses `h->clr_depth` where it used `HYDRO_ARM`; the `HYDRO_ARM` macro is deleted.

- [ ] **Step 1: Write the failing tests.**
  - `tests/test_boatcfg.c`: `test_clr_depth_defaults_loads_and_clamps`: `boatcfg_default` gives `hull.clr_depth` within 1e-6 of 0.4; loading JSON `{"hull":{"clr_depth":1.8}}` gives 1.8; loading `{"hull":{"clr_depth":99}}` gives 6 and `boatcfg_load` returns false (warning). Follow how neighbouring tests in that file call `boatcfg_load`. Also assert a `boatcfg_to_json` → `boatcfg_load` round trip keeps 1.8 if the file already has a round-trip test pattern to copy.
  - `tests/test_boat.c`: `test_deeper_clr_heels_more`: two `Sim`s from `sim_init` with identical setup, 10 kn wind, TWA 60, autopilot and autotrim on, differing only in `cfg.hull.clr_depth` (0.1 vs 2.0); after 30 s assert `fabsf(deep.b.heel) > fabsf(shallow.b.heel) + D(0.5)`.
- [ ] **Step 2: Run** both test binaries. Expected: FAIL (field missing; compile error counts as the failure).
- [ ] **Step 3: Implement** the field, registration, `boat.c` change, both JSON edits and the doc row.
- [ ] **Step 4: Run** `make -C tests test`. Expected: no new failures vs. baseline. The dinghy, sunfish and longship behave exactly as before; j24 and tp52 heel slightly more (about 4% and 15% more moment). Report any failure with numbers.
- [ ] **Step 5: Commit** the listed files with message `feat: per-boat clr_depth replaces the fixed hydro arm`.

---

## Final reviewer checks (done by the session owner, not an implementer)

- [ ] Re-run the steady-state probe at `/tmp/claude-1000/-home-jamesn-Code-playdate-sailing-game/977b335f-ee6b-4080-b02c-b13b3b1266d1/scratchpad/probe` and compare against the pre-change numbers: 12 kn heel unchanged, 25 kn upwind heel down, dead-run heel down, no new NaN.
- [ ] Run `make -C tests test` once more and `git diff --stat` to confirm no file outside the plan (and none of the user's uncommitted files) changed.
