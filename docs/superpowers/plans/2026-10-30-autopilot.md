# Autopilot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A heading-hold autopilot: hold **A** to toggle it on/off; while on, a tap of **A** steps the target heading to the next one that is not into the wind.

**Architecture:** A new pure module `autopilot.[ch]` holds the state, the A-button hold/tap detector, the "next heading skipping the no-go zone" step, and a PD steering law that returns a `wheel_target`. `game_update` owns an `Autopilot`, feeds it the held-button mask, heading, yaw rate and wind, and overwrites `in.wheel_target` while it is on. Anchor moves from A to B.

**Tech Stack:** C99, Playdate C API; headless tests in `tests/` (`make -C tests test`, ASan/UBSan, `t.h` macros).

**Spec:** none beyond the user's request (no design doc). Request, verbatim: "turn on autopilot for the boat, so it will head in a direction, you press the a button to change this mode, holding down to turn it on or off. Pressing it once changes the direction to try and go, it will skip over trying to go into the wind". Related: `docs/superpowers/specs/2026-10-30-sailing-sim-design.md` (Controls section, A = anchor).

## Decisions made in this plan (not in the request)

- **A was the anchor toggle, so anchor moves to B** (B is unused per the design spec).
- A **tap while off does nothing**; only a hold engages. On engage, target = current heading (unsnapped).
- Taps step the target **clockwise (+heading) in `autopilot_step_deg` increments** (default 45°), skipping any candidate within `autopilot_no_go_deg` (default 45°) of the wind's source direction.
- The no-go check happens **at tap time only**; a later wind shift does not move the target.
- While on, the autopilot owns the wheel (crank in WHEEL mode is overwritten); TRIM / HOIST / CAMERA modes still work.
- A hold only counts if its **press** was seen while sailing, so the A press that restarts after a capsize never engages it.

## Global Constraints

- Playdate button bits: `BTN_A` is `1u << 5`, `BTN_B` is `1u << 4` (`src/sim/controls.h`); `held` uses the same bits as `pressed`.
- Heading `h` points the bow along `(cos h, sin h)`; positive wheel gives positive yaw rate (heading increases). Wind `dir` is where the wind blows **toward**, so it comes **from** `wrap_pi(dir + PI)`.
- Use `wrap_pi` and `clampf` from `mathx.h`; non-finite inputs must never reach `wheel_target` (`boat.c` falls back, but don't rely on it).
- Tests build with `-Wall -Wextra -Werror` and sanitizers; no `-Wunused` leftovers.
- Config fields are added in `src/sim/boatcfg.c` `CONTROLS_F` with `FF(struct, field, default, min, max)`; new `ControlsCfg` fields need defaults only, existing boat JSON files stay valid.

## Review Focus

- A held while the post-capsize restart A-press happens: autopilot must not engage 0.6 s later.
- A held for a very long time: toggles exactly once, not every 0.6 s; releasing afterwards is not a tap.
- Hold-off then immediate release at the threshold boundary: a tap that was shorter than the threshold is a tap, not a toggle.
- Engage while the heading is exactly at ±π, or the wind source is exactly at the candidate: no NaN, wrap handled.
- Capsized / wrecked while engaged: autopilot stops driving the wheel; restart clears it.
- Non-finite `dt`, `heading`, `yaw rate`: wheel target stays finite.

---

### Task 1: Autopilot module

**Files:**
- Create: `src/sim/autopilot.h`, `src/sim/autopilot.c`
- Modify: `src/sim/boatcfg.h` (`ControlsCfg`), `src/sim/boatcfg.c` (`CONTROLS_F`)
- Test: `tests/test_autopilot.c` (picked up automatically by `tests/Makefile`'s wildcard)

**Interfaces:**
- Consumes: `BTN_A` from `controls.h`; `ControlsCfg`; `wrap_pi`, `clampf` from `mathx.h`.
- Produces (in `autopilot.h`):
  ```c
  typedef struct {
      bool on;
      float target;          /* rad, world heading to hold */
      float a_time;          /* s A has been down since its press, 0 if not tracking */
      bool a_tracking;       /* a press was seen and A is still down */
      bool a_fired;          /* this hold already toggled */
  } Autopilot;

  void autopilot_init(Autopilot *ap);
  /* Call once per frame while sailing. Handles hold (toggle) and tap (next heading).
   * heading and wind_from are world radians; wind_from is where the wind comes FROM. */
  void autopilot_buttons(Autopilot *ap, const ControlsCfg *k, uint32_t pressed, uint32_t held,
                         float dt, float heading, float wind_from);
  /* Next target after `from`: +step increments, skipping those within no_go_rad of wind_from.
   * Returns `from` unchanged if every increment is blocked. Result wrapped to (-pi, pi]. */
  float autopilot_next_heading(float from, float wind_from, float step_rad, float no_go_rad);
  /* PD law -> wheel_target in -1..1. Non-finite inputs give 0. */
  float autopilot_wheel(const Autopilot *ap, const ControlsCfg *k, float heading, float yaw_rate);
  ```
- New `ControlsCfg` fields (all `float`): `autopilot_hold_s` (0.6, range 0.1–3), `autopilot_step_deg` (45, 5–180), `autopilot_no_go_deg` (45, 0–90), `autopilot_kp` (2.0, 0–20, wheel per rad of error), `autopilot_kd` (1.0, 0–20, wheel per rad/s of yaw rate).

- [ ] **Step 1: Write failing tests in `tests/test_autopilot.c`** (include `t.h`, `autopilot.h`; build a `ControlsCfg` via `boatcfg_default(&cfg); k = &cfg.controls`). Tests, each with the stated assertions:
  - `test_next_heading_steps_clockwise`: `autopilot_next_heading(0, PI, deg2rad(45), deg2rad(45))` is within 1e-5 of `deg2rad(45)`.
  - `test_next_heading_skips_no_go`: wind from 45° (`deg2rad(45)`), from 0 → result is 90° (45° is inside the zone, 90° is exactly 45° away and allowed: boundary is `<` no_go).
  - `test_next_heading_wraps`: from `deg2rad(170)`, wind from `PI/2 + PI` → result in `(-PI, PI]` and equals `deg2rad(-145)` ±1e-5.
  - `test_next_heading_all_blocked_returns_from`: `no_go_rad = PI` → returns `from`.
  - `test_hold_toggles_on_and_sets_target_to_heading`: frame 1 `pressed=BTN_A, held=BTN_A, dt=0.1`, heading 1.0; then 6 more frames `held=BTN_A`; `ap.on` true, `ap.target` ≈ 1.0.
  - `test_hold_toggles_once_then_off_on_second_hold`: keep holding 3 s → `on` stays true (single toggle). Release a frame, hold again past threshold → `on` false.
  - `test_tap_when_on_steps_target`: engage (as above), release, then tap (`pressed=BTN_A, held=BTN_A`, next frame `held=0`) → `target` ≈ `autopilot_next_heading(old, wind_from, ...)`. Release after a hold-toggle does **not** step the target.
  - `test_tap_when_off_does_nothing`: tap with `on` false → `on` false.
  - `test_held_without_press_is_ignored`: 3 s of `held=BTN_A` with `pressed=0` → `on` false (restart-press case).
  - `test_wheel_sign_and_clamp`: `target=0.5, heading=0, yaw_rate=0` → `autopilot_wheel > 0`; `target=-0.5` → `< 0`; huge error → exactly ±1; error wrapped (target `PI-0.1`, heading `-PI+0.1` → negative wheel, short way round).
  - `test_wheel_damping`: error 0, `yaw_rate = 0.5` → wheel `< 0`.
  - `test_wheel_nonfinite_is_zero`: NaN heading, NaN yaw_rate, and `ap.target = NAN` each → 0.
  - `test_buttons_nonfinite_dt_and_heading_safe`: NaN `dt` / `heading` / `wind_from` for several frames → `isfinite(ap.target)` and `isfinite(ap.a_time)`.

- [ ] **Step 2: Run `make -C tests test`** — expect FAIL (compile error: `autopilot.h` missing).

- [ ] **Step 3: Add the five `ControlsCfg` fields** in `boatcfg.h` and the matching `FF(ControlsCfg, ...)` rows in `CONTROLS_F` (`boatcfg.c:~96`) with the defaults/ranges above.

- [ ] **Step 4: Implement `autopilot.[ch]`.** Hold logic per frame: if `pressed & BTN_A`, start tracking (`a_tracking=true, a_time=0, a_fired=false`). If tracking and `held & BTN_A`: `a_time += dt` (dt sanitised: non-finite or negative → 0); when `a_time >= autopilot_hold_s` and not fired → toggle `on`, set `a_fired`; on turning on set `target = heading` (wrapped, 0 if non-finite). If tracking and A not held: if not fired and `on` → `target = autopilot_next_heading(target, wind_from, deg2rad(step), deg2rad(no_go))`; stop tracking. `autopilot_wheel`: `clampf(kp * wrap_pi(target - heading) - kd * yaw_rate, -1, 1)`, 0 if any input is non-finite. Skip-check is `fabsf(wrap_pi(cand - wind_from)) < no_go_rad`.

- [ ] **Step 5: Run `make -C tests test`** — expect all PASS (existing tests unaffected so far).

- [ ] **Step 6: Commit**
  ```bash
  git add src/sim/autopilot.[ch] src/sim/boatcfg.[ch] tests/test_autopilot.c
  git commit -m "feat: autopilot module (hold toggle, tap next heading, PD wheel law)"
  ```

---

### Task 2: Wire into the game; anchor moves to B

**Files:**
- Modify: `src/sim/controls.c:25` (anchor on `BTN_B`), `src/sim/game.h`, `src/sim/game.c`, `src/pd/main.c`
- Modify (signature churn): `tests/test_game.c`, `tools/frame_dump.c` (every `game_update` call)
- Test: `tests/test_controls.c` (rename anchor test), `tests/test_game.c` (new closed-loop tests)

**Interfaces:**
- Consumes: Task 1's `Autopilot`, `autopilot_init`, `autopilot_buttons`, `autopilot_wheel`.
- Produces: `Game` gains `Autopilot ap;`. New signature `void game_update(Game *g, uint32_t pressed, uint32_t held, float crank_delta, float frame_dt);` (`held` is the current-state button mask, same bits as `pressed`).

- [ ] **Step 1: Write failing tests.**
  - `tests/test_controls.c`: rename `test_a_toggles_anchor` → `test_b_toggles_anchor`, replacing `BTN_A` with `BTN_B`; add assertion that pressing `BTN_A` alone leaves `r.in.anchor` false.
  - `tests/test_game.c` (helper `static void step(uint32_t pressed, uint32_t held, int frames)` calling `game_update(&g, pressed, held, 0, 1.0f/30)`):
    - `test_autopilot_holds_heading`: `fresh(1)`, one frame `pressed=BTN_A, held=BTN_A`, then `held=BTN_A` for 25 frames (≥0.6 s), release; set `g.in.trim_target[0]` to a sensible mid trim if needed so the boat moves; run 600 frames with `g.ap.target` forced to `g.b.heading + deg2rad(60)` (assert `g.ap.on` before forcing) → `fabsf(wrap_pi(g.b.heading - g.ap.target)) < deg2rad(5)` and `g.run == RUN_SAILING`. If the stock gains don't settle within 5° in the default sunny preset, tune `autopilot_kp`/`kd` defaults in `boatcfg.c` — that is the point of this test.
    - `test_autopilot_overrides_crank_wheel`: engaged, then `game_update(&g, BTN_UP, 0, 90.0f, ...)` → `g.in.wheel_target == autopilot_wheel(...)` for the current state (i.e. not driven toward +1 by the crank).
    - `test_restart_press_does_not_engage`: capsize is hard to stage, so instead: `game_update` with `held=BTN_A, pressed=0` for 90 frames → `!g.ap.on`.
    - `test_autopilot_cleared_on_restart`: engage, `game_restart(&g, 2)` → `!g.ap.on`.
    - `test_autopilot_frozen_when_capsized`: engage, set `g.run = RUN_CAPSIZED`, record `g.in.wheel_target`, run 30 frames with `held=BTN_A` → `g.in.wheel_target` unchanged and `ap.on` unchanged.
  - Update all existing `game_update(&g, X, Y, Z)` calls in `tests/test_game.c` and `tools/frame_dump.c` to `game_update(&g, X, 0, Y, Z)`.

- [ ] **Step 2: Run `make -C tests test`** — expect FAIL (compile errors / anchor test).

- [ ] **Step 3: Implement.** `controls.c:25`: `if (pressed & BTN_B) out->anchor = !out->anchor;` and update the doc comment in `controls.h` if it mentions A. `game.c`: add `autopilot_init(&g->ap)` in `reset`; in `game_update`, inside the `RUN_SAILING` branch right after `controls_update`, compute `wind_from = wrap_pi(g->wind.dir + PI)`, call `autopilot_buttons(&g->ap, &g->boat.controls, pressed, held, dt, g->b.heading, wind_from)`, then `if (g->ap.on) g->in.wheel_target = autopilot_wheel(&g->ap, &g->boat.controls, g->b.heading, g->b.r);`. `main.c`: pass `(uint32_t)cur` as `held`; update the comment at line 62 ("while sailing A is the autopilot button, B the anchor").

- [ ] **Step 4: Run `make -C tests test`** — expect all PASS. Also `make` (device/simulator build) compiles clean if the Playdate SDK is configured.

- [ ] **Step 5: Commit**
  ```bash
  git add src tests tools
  git commit -m "feat: wire autopilot into game loop; anchor moves to B"
  ```

---

### Task 3: HUD indicator and docs

**Files:**
- Modify: `src/pd/hud.c` (`hud_draw`), `docs/boat-schema.md:19`, `docs/superpowers/specs/2026-10-30-sailing-sim-design.md:49`, `boats/dinghy.json` + `boats/spinnaker_example.json` (add the five new `controls` keys, same values as defaults)
- Test: `tools/frames.sh` visual check (render code has no unit tests)

**Interfaces:**
- Consumes: `Game.ap.on`, `Game.ap.target`.

- [ ] **Step 1: In `hud_draw`, when `g->ap.on`, draw `AP %03.0f` (target heading in degrees, same formula as `hdg`) with the existing `text()` helper at x=250, y=`top + 2`** (clear of the sail bar at x 150–232 and the wind speed text at x≈320).

- [ ] **Step 2: Docs.** Add the five field names to the `controls` row in `docs/boat-schema.md`; change the design-spec line 49 to "**A** = autopilot (hold toggles; tap steps target heading, skipping into-wind). **B** = anchor toggle." Add the five keys to both boat JSONs; confirm `make -C tests test` still passes (`test_boatcfg` round-trips configs).

- [ ] **Step 3: Verify visually.** Temporarily set `g.ap.on = true; g.ap.target = 1.0f;` in a `tools/frame_dump.c` scenario (do not commit that edit), run `tools/frames.sh`, open a PNG, and confirm `AP 057` is legible and does not overlap other HUD text. Revert the edit.

- [ ] **Step 4: Commit**
  ```bash
  git add src/pd/hud.c docs boats
  git commit -m "feat: autopilot HUD indicator and docs; add autopilot config keys to boats"
  ```

---

## Self-Review

- **Coverage:** hold-toggle (T1/T2), tap-next-heading (T1), skip-into-wind (T1 `skips_no_go`), steering (T1 sign tests + T2 closed loop), A→B anchor (T2), HUD (T3). Every Review Focus line has a test: restart press → `held_without_press_is_ignored` + `restart_press_does_not_engage`; long hold once → `toggles_once_then_off_on_second_hold`; short tap vs hold → `tap_when_on_steps_target`; ±π wrap → `next_heading_wraps` and `wheel_sign_and_clamp`; capsize → `frozen_when_capsized`; non-finite → `wheel_nonfinite_is_zero`, `buttons_nonfinite_dt_and_heading_safe`.
- **Type consistency:** `autopilot_buttons`, `autopilot_next_heading`, `autopilot_wheel` and `Autopilot` fields are named identically in all three tasks; `game_update` gains `held` as the 3rd parameter everywhere.
- **Known gap by decision:** a wind shift after selection can leave the target in the no-go zone (boat will stall head to wind); revisit if it feels bad in play.
