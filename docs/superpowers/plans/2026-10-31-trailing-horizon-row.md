# Trailing Camera Horizon Row Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a boat pin the trailing camera's horizon to a chosen screen row, so the TP52's horizon is not pushed down toward the boat by its tall mast.

**Architecture:** `trail_framing()` in `src/sim/camera.c` today picks the pitch that puts the masthead `frame_margin` px from the top, then solves the back-off distance. A new optional `camera.horizon_y` (0 = off, today's behaviour) instead fixes the pitch from the horizon row and solves the back-off distance so the stern still sits `frame_margin` px above the status panel. The masthead lands wherever it falls (it may crop). Only `tp52.json` opts in.

**Tech Stack:** C99, `-Wall -Wextra -Werror` with ASan/UBSan, test runner `make -C tests test` using `tests/t.h` (`T_ASSERT`, `T_NEAR`, `T_RUN`).

**Spec:** No spec doc. Requirements come from this session: TP52 horizon sits at screen row ~130 vs ~112 on the J24 (screen is `CAM_H` 240 rows, horizon at zero pitch is `CAM_HORIZON_Y` 110). The user chose horizon row 124 for the TP52 and accepted that the masthead sits about 5 px from the top (it may crop slightly).

## Global Constraints

- C99, must build clean under `-Wall -Wextra -Werror -fsanitize=address,undefined` (see `tests/Makefile`).
- Boat JSON that omits `horizon_y` must load with the default `0` and frame exactly as before (j24, dinghy, sunfish, longship unchanged).
- Do not re-baseline golden or scenario files. If one fails because of this change, stop and report the test name and diff.
- Boat files are read into a 16 KB buffer, so keep `tp52.json` compact (one added key).
- Stage only the files named in the task, by path (never `git add -A`).

## Review Focus

- `horizon_y` below the stern limit (>= `HUD_TOP_PX - frame_margin` = 200 with defaults): back-off distance must stay finite and positive, not divide by zero or go negative.
- `horizon_y` set while `frame_margin` is 80: still finite.
- Default boats (`horizon_y` 0): trailing framing identical to before (existing `test_trailing_view_frames_mast_and_hull_with_margin` must still pass untouched).
- Pitch smoothing/roll: `horizon_y` only replaces the base `trail_pitch`; `pitch_gain * b->pitch` still adds on top.

---

### Task 1: `camera.horizon_y` knob, framing, TP52 value, docs

**Files:**
- Modify: `src/sim/boatcfg.h:84` (add field to `CameraCfg`)
- Modify: `src/sim/boatcfg.c:140` (register field in `CAMERA_F`)
- Modify: `src/sim/camera.c:12-28` (`trail_framing`)
- Modify: `boats/tp52.json` (`"horizon_y":124` inside `camera`)
- Modify: `docs/boat-schema.md:21` and the camera parameter table near line 41
- Test: `tests/test_camera.c`, `tests/test_boatcfg.c`

**Interfaces:**
- Produces: `CameraCfg.horizon_y` (`float`, px screen row; 0 = off; range 0..200).
- Consumes: existing `trail_framing(const BoatCfg *cfg, float *dist, float *pitch)`; signature unchanged.

- [ ] **Step 1: Write the failing tests**

In `tests/test_camera.c`, add and register in `main`:

```c
static void test_horizon_y_pins_horizon_and_stern(void) {
    fresh();
    bc.camera.horizon_y = 124.0f;
    cp.trail = true;
    g.b.heading = 0;
    settle();
    float sx, sy;
    T_ASSERT(camera_project(&cp, cp.x + 5000, cp.y, cp.height, &sx, &sy, NULL));
    T_NEAR(sy, 124.0, 1.0);
    T_ASSERT(camera_project(&cp, g.b.x - 0.5 * bc.hull.length, g.b.y, 0.0f, &sx, &sy, NULL));
    T_NEAR(sy, (CAM_H - HUD_H) - bc.camera.frame_margin, 3.0);
}

static void test_horizon_y_too_low_stays_finite(void) {
    fresh();
    bc.camera.horizon_y = 200.0f;   /* at/below the stern limit */
    cp.trail = true;
    settle();
    T_ASSERT(isfinite(cp.x) && isfinite(cp.pitch));
    T_ASSERT(cp.x < g.b.x);         /* still aft of the boat */
}
```

In `tests/test_boatcfg.c` `test_tp52_json`, add `T_NEAR(c.camera.horizon_y, 124.0, 1e-3);`. In the all-boats loop test near line 243 nothing changes; add a check in `test_sunfish_json` or the default-boat test that `c.camera.horizon_y == 0` for a boat without the key (use `boatcfg_default` and assert `horizon_y == 0`).

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -C tests test`
Expected: compile error, `CameraCfg` has no member `horizon_y`.

- [ ] **Step 3: Add and register the field**

Add `float horizon_y;` to `CameraCfg` in `src/sim/boatcfg.h` with the comment `/* trailing view: screen row to pin the horizon to; 0 = frame by masthead instead */`. Register `FF(CameraCfg, horizon_y, 0.0f, 0, 200),` in `CAMERA_F` after `frame_margin`.

- [ ] **Step 4: Implement in `trail_framing`**

In `src/sim/camera.c`, when `k->horizon_y > 0`: `pitch = atanf((horizon_y - CAM_HORIZON_Y) / CAM_FOCAL)`; stern depression below the view axis `a = -bot - pitch` (using the existing `bot`); if `a < 0.02f` clamp it to `0.02f`; `dist = half + h / tanf(a)`, clamped to the existing `hi` of 200. Return early; the `horizon_y == 0` path stays byte-for-byte as it is. Update the comment above the function to mention the alternative mode.

- [ ] **Step 5: Run tests to verify the code tests pass**

Run: `make -C tests test`
Expected: the two new camera tests pass; `test_tp52_json` still fails (JSON not updated yet), everything else passes.

- [ ] **Step 6: Set the TP52 value**

Add `"horizon_y":124` to the `camera` object in `boats/tp52.json`.

- [ ] **Step 7: Run the full suite**

Run: `make -C tests test`
Expected: all pass, including `test_trailing_view_frames_mast_and_hull_with_margin` and all scenario/golden tests unchanged.

- [ ] **Step 8: Document**

`docs/boat-schema.md`: add `horizon_y` to the `camera` row at line 21, and a row in the camera parameter table near line 41: `camera.horizon_y` | 0-200 | trailing view: pin the horizon to this screen row (0 = off, frame by masthead). Tall rigs pitch the camera down; this keeps the horizon high, at the cost of the masthead sitting nearer the top or cropping.

- [ ] **Step 9: Eyeball in the simulator**

Run the game or harness with the TP52 in trailing view (see `README.md` / `harness/`) and check horizon height against the J24 and that the masthead is visible. If the masthead crops too much, adjust `horizon_y` in `tp52.json` (table from this session: 118 → mast row -10, 124 → 5, 130 → 19).

- [ ] **Step 10: Commit**

```bash
git add src/sim/boatcfg.h src/sim/boatcfg.c src/sim/camera.c boats/tp52.json docs/boat-schema.md tests/test_camera.c tests/test_boatcfg.c
git commit -m "feat: camera.horizon_y pins the trailing horizon row, TP52 uses 124"
```
