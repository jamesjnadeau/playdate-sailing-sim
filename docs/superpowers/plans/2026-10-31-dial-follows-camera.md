# Wind Dial Follows the Camera Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the player swings the look camera, the HUD wind dial (needle = wind, top tick = heading/bow) rotates so "up" on the dial is where the camera is looking.

**Architecture:** The dial in `hud_draw` currently measures angles from the bow. The look yaw (`Controls.camera_yaw`, rad, + = toward starboard, the view faces `heading + camera_yaw` in both trailing and near views) is subtracted from every dial angle. The subtraction lives in one presentation-free helper in `hudinfo` so the headless tests cover it.

**Tech Stack:** C; helper in `src/sim/hudinfo.[ch]`, drawing in `src/pd/hud.c` (not unit-tested), tests via `make -C tests`.

**Spec:** none; the request is the spec: "when in the sim, and I change the camera angle I'm looking at, I want the wind direction and heading direction indicators to rotate relative to the camera rotation".

## Global Constraints

- "Heading indicator" = the fixed tick at the top of the wind dial (it marks the bow). It moves to where the bow is relative to the view; the needle moves with it.
- With `camera_yaw == 0` the dial draws exactly as today (needle at `awa + π`, bow tick at the top).
- Only the HUD dial changes. The course map is north-up and the `HDG ###` number is a compass heading; neither depends on the camera, so both stay as they are.
- `hudinfo.c` stays free of Playdate headers.

## Review Focus

- Look straight behind (`camera_yaw = ±π`): the bow tick is at the bottom and the needle is rotated half a turn from today.
- `camera_yaw` and `awa` non-finite: the helper returns a finite angle (0 for a non-finite yaw, i.e. today's drawing).
- Look to starboard (`camera_yaw = +π/2`): the bow tick moves to the left of the dial (the bow is now on the left of the view), not the right.
- Angles that sum past ±π (`awa + π - camera_yaw`) come out wrapped, so drawn endpoints never depend on large angles.

---

### Task 1: Dial angle helper and HUD use

**Files:**
- Modify: `src/sim/hudinfo.h`, `src/sim/hudinfo.c`
- Modify: `src/pd/hud.c:96-102` (the wind dial block in `hud_draw`)
- Test: `tests/test_hudinfo.c`

**Interfaces:**
- Consumes: `wrap_pi(float)` from `mathx.h` (-> (-π, π], non-finite -> 0); `Controls.camera_yaw` (rad).
- Produces: `float hud_dial_angle(float angle_from_bow, float look_yaw)` in `hudinfo.h`: the screen angle on the dial (rad, clockwise from the top, wrapped to (-π, π]) of something at `angle_from_bow` (rad, + = starboard of the bow) when the view faces `look_yaw` (rad, + = toward starboard). A non-finite `look_yaw` counts as 0; a non-finite `angle_from_bow` gives 0.

- [ ] **Step 1: Write the failing tests** in `tests/test_hudinfo.c`, registered in `main`:
  - `test_dial_angle_unchanged_when_looking_ahead`: `hud_dial_angle(0.7f, 0.0f)` is `0.7` and `hud_dial_angle(0.0f, 0.0f)` is `0` (`T_NEAR`, 1e-6).
  - `test_dial_angle_rotates_against_the_look`: bow (`angle_from_bow = 0`) with `look_yaw = π/2` gives `-π/2` (bow now on the left); with `look_yaw = -π/2` gives `+π/2`; a wind needle at `π/2` with `look_yaw = π/2` gives `0`.
  - `test_dial_angle_behind_and_wraps`: bow with `look_yaw = π` gives `π` (magnitude π, i.e. bottom: assert `fabsf(r) ≈ π`); `hud_dial_angle(3.0f, -3.0f)` is within (-π, π] and `≈ 6 - 2π`.
  - `test_dial_angle_non_finite`: `hud_dial_angle(0.7f, NAN)` is `0.7`; `hud_dial_angle(NAN, 0.3f)` is `0`; `hud_dial_angle(0.7f, INFINITY)` is `0.7`.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C tests 2>&1 | tail -20`
Expected: build fails with undeclared/undefined `hud_dial_angle`.

- [ ] **Step 3: Implement `float hud_dial_angle(float angle_from_bow, float look_yaw)`** in `src/sim/hudinfo.c`, declared with a one-comment doc in `hudinfo.h`. `wrap_pi(angle_from_bow - look_yaw)`, with `look_yaw` replaced by 0 when non-finite (include `mathx.h` and `<math.h>` if not already).

- [ ] **Step 4: Run tests to verify they pass**

Run: `make -C tests 2>&1 | tail -20`
Expected: all test binaries pass, including the four new tests.

- [ ] **Step 5: Use it in `hud_draw`** (`src/pd/hud.c`). In the wind dial block: `float look = g->ctl.camera_yaw;` needle angle `a = hud_dial_angle(b->awa + 3.14159265f, look)`; the bow tick, currently the vertical line at `(cx, cy - R ± 2)`, is drawn along `bow = hud_dial_angle(0.0f, look)` at radius `R`: from `R - 2` to `R + 2` using `(sinf(bow), -cosf(bow))`. Update the block comment to say the dial is relative to the camera's view (up = where the camera looks). Keep the line widths and colours.

- [ ] **Step 6: Build the device sources and check by eye**

Run: `make 2>&1 | tail -20`
Expected: builds with no new warnings. Then run the simulator (`make run` or the project's usual sim target), enter look mode (DOWN), turn the crank: the tick circles the dial opposite the crank direction and the needle keeps its true bearing to the world; at `camera_yaw = 0` the dial matches the old drawing.

- [ ] **Step 7: Commit**

```bash
git add src/sim/hudinfo.h src/sim/hudinfo.c src/pd/hud.c tests/test_hudinfo.c
git commit -m "feat: wind dial rotates with the camera look" -- src/sim/hudinfo.h src/sim/hudinfo.c src/pd/hud.c tests/test_hudinfo.c
```

Note: those four files already carry staged edits from other in-progress work (`hudinfo.*`, `test_hudinfo.c`); the commit will include them. Land or stash that work first if a clean commit is wanted.
