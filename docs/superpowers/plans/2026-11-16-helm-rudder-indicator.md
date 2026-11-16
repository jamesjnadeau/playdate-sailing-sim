# Helm Rudder Indicator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In HELM mode the status bar shows the current rudder position: a thin track, a fixed short centre tick, and a thick marker that moves right when the bow is turning right.

**Architecture:** Two pure helpers in `src/sim/hudinfo.c` (track span, marker x) are unit-tested on the host. `hud_draw` in `src/pd/hud.c` calls them in HELM mode and draws into the gap between the mode cell and the instrument cluster, where TRIM/HOIST draw the sail bar.

**Tech Stack:** C99, Playdate C API, host test suite in `tests/` (`t.h` macros), host harness goldens in `harness/` and `tests/golden/`.

**Spec:** `docs/superpowers/specs/2026-11-16-helm-rudder-indicator-design.md`

## Global Constraints

- Marker right = bow turns right (starboard): `BoatState.wheel` (-1..1) is already positive for that, so marker x = centre + wheel * half-width, **no sign flip**.
- Shown in `MODE_WHEEL` (HELM) only, white on the black bar, also while the autopilot is steering.
- Track: 1 px line at bar mid-height, starts at x 60 (same left edge as the sail bar), ends 6 px before `cl.left`, at most 80 px wide, **odd** width so there is a true centre pixel; hidden when under 15 px.
- Centre tick: fixed, 2 px wide, 6 px tall. Marker: 3 px wide, 12 px tall, drawn over the tick. Non-finite `wheel` -> marker at centre.
- No change to the other instruments, other modes, or the bar's layout.
- Build flags are `-Wall -Wextra -Werror`; the tests run under ASan/UBSan.
- Commit trailer: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Review Focus

- `wheel` outside -1..1 (from a bad input or a future rate overshoot) must not draw the marker off the track: clamp.
- `wheel` NaN/inf must not crash or draw garbage: marker at centre.
- AP + AT both on (widest cluster, smallest gap) must hide the track cleanly rather than overlap the cluster or the mode cell.
- Track width must stay odd at every `left`, so centre never shifts by half a pixel as the cluster widens.
- Switching HELM -> TRIM must leave no stray track pixels (the bar is refilled black each frame, so only the draw call needs to be gated on mode).

---

## File Structure

- `src/sim/hudinfo.h`, `src/sim/hudinfo.c`: add `hud_helm_span`, `hud_helm_marker_x`.
- `tests/test_hudinfo.c`: tests for both.
- `src/pd/hud.c`: draw the indicator in `hud_draw`.
- `tests/golden/host/*`, `tests/scenarios/*.masks`: refreshed/extended for the new pixels.

### Task 1: Pure helpers

**Files:**
- Modify: `src/sim/hudinfo.h` (declare after `hud_sail_bar_span`), `src/sim/hudinfo.c` (define after `hud_sail_bar_span`)
- Test: `tests/test_hudinfo.c`

**Interfaces:**
- Consumes: `HudCluster.left` (the `left` argument), existing from `hud_cluster_layout`.
- Produces:
  - `bool hud_helm_span(int left, int *x, int *w);` sets `*x = 60`, `*w` = `min(left - 6 - 60, 80)` reduced by 1 if even; returns false (with `*w = 0`) when that width is under 15.
  - `int hud_helm_marker_x(int x, int w, float wheel);` returns the marker's centre pixel: `x + w / 2 + lroundf(clamp(wheel, -1, 1) * (w / 2))`; non-finite `wheel` -> `x + w / 2`.

- [ ] **Step 1: Write the failing tests** in `tests/test_hudinfo.c`, and register each in `main` with `T_RUN`:

```c
static void test_helm_span(void) {
    int x, w;
    T_ASSERT(hud_helm_span(173, &x, &w) && x == 60 && w == 79);   /* capped at 80, then made odd */
    T_ASSERT(hud_helm_span(105, &x, &w) && x == 60 && w == 39);   /* already odd */
    T_ASSERT(hud_helm_span(82, &x, &w) && w == 15);               /* 16 -> 15: smallest shown */
    T_ASSERT(!hud_helm_span(80, &x, &w) && w == 0);               /* 14 -> 13: hidden */
    T_ASSERT(!hud_helm_span(70, &x, &w) && w == 0);
}

static void test_helm_span_always_odd_and_clear_of_cluster(void) {
    for (int left = 60; left < 300; left++) {
        int x, w;
        if (!hud_helm_span(left, &x, &w)) continue;
        T_ASSERT(w % 2 == 1 && w <= 80 && x + w <= left - 6);
    }
}

static void test_helm_marker_x(void) {
    T_ASSERT(hud_helm_marker_x(60, 79, 0.0f) == 99);              /* centre */
    T_ASSERT(hud_helm_marker_x(60, 79, 1.0f) == 138);             /* full starboard: right end */
    T_ASSERT(hud_helm_marker_x(60, 79, -1.0f) == 60);             /* full port: left end */
    T_ASSERT(hud_helm_marker_x(60, 79, 0.5f) == 119);
    T_ASSERT(hud_helm_marker_x(60, 79, -0.5f) == 79);             /* symmetric about centre */
}

static void test_helm_marker_clamps_and_survives_non_finite(void) {
    T_ASSERT(hud_helm_marker_x(60, 79, 3.0f) == 138);
    T_ASSERT(hud_helm_marker_x(60, 79, -3.0f) == 60);
    T_ASSERT(hud_helm_marker_x(60, 79, NAN) == 99);
    T_ASSERT(hud_helm_marker_x(60, 79, INFINITY) == 99);
}
```

(Add `#include <math.h>` if the file lacks it.)

- [ ] **Step 2: Run to verify failure**

Run: `make -C tests build/test_hudinfo`
Expected: compile error, `hud_helm_span` / `hud_helm_marker_x` undeclared.

- [ ] **Step 3: Implement** both functions per the Interfaces block in `src/sim/hudinfo.c`, with doc comments in `hudinfo.h` in the style of `hud_sail_bar_span`. Use `clampf` as `hud_sail_fraction` does and `isfinite` for the guard; `lroundf` for rounding.

- [ ] **Step 4: Run to verify pass**

Run: `make -C tests build/test_hudinfo.ok`
Expected: PASS, no sanitizer output.

- [ ] **Step 5: Commit**

```bash
git add src/sim/hudinfo.h src/sim/hudinfo.c tests/test_hudinfo.c docs/superpowers
git commit -m "HUD: helm indicator span and marker helpers"
```

### Task 2: Draw it, refresh goldens

**Files:**
- Modify: `src/pd/hud.c` (in `hud_draw`, after the TRIM/HOIST block)
- Modify: `tests/golden/host/**`, and `tests/scenarios/*.masks` if needed (Step 4)

**Interfaces:**
- Consumes: `hud_helm_span`, `hud_helm_marker_x` (Task 1); `cl.left`, `top`, `HUD_H`, `b->wheel`, `mode` already in `hud_draw`.
- Produces: the visible indicator.

- [ ] **Step 1: Draw the indicator** in `hud_draw` when `mode == MODE_WHEEL` and `hud_helm_span(cl.left, &hx, &hw)` is true, with the draw mode already `kDrawModeFillWhite`/`kColorWhite` as the neighbouring code uses. With `my = top + HUD_H / 2`, `cx = hx + hw / 2`, `mx = hud_helm_marker_x(hx, hw, b->wheel)`:
  - track: `drawLine(hx, my, hx + hw - 1, my, 1, kColorWhite)`
  - centre tick: `fillRect(cx, my - 3, 2, 6, kColorWhite)`
  - marker: `fillRect(mx - 1, my - 6, 3, 12, kColorWhite)` (drawn last, so it covers the tick).

- [ ] **Step 2: Build everything and run the unit tests**

Run: `make -C tests && make -C harness run-all`
Expected: tests pass; every scenario runs with no errors.

- [ ] **Step 3: Check it by eye.** Run a HELM scenario (`make -C harness run SCEN=free_sail`), convert a sailing frame from `harness/build/out/free_sail/` to PNG with `python3 tools/fbdiff.py <fb> <fb> --png /tmp/helm.png` (or compare against its golden to get a diff PNG) and open it. Confirm: track in the gap, centre tick, marker at centre with wheel 0; if a scenario turns the wheel, the marker moves to the right for a right turn. If the frames only show wheel 0, add a short `free_sail`-style scenario step turning the wheel, or run the real game in the Playdate Simulator and confirm both directions.

- [ ] **Step 4: Fidelity check.** Run `make -C harness check-fidelity`. The `tests/golden/real` frames come from the Playdate Simulator and cannot be regenerated here, so they lack the indicator. If fidelity fails only in the new region, add one mask rect per affected scenario to its `tests/scenarios/<name>.masks` covering the indicator gap (`<snapshot> 60 219 <w> 20`, using the widest span, 80 px, for each sailing snapshot, same format as the existing lines) and note in the commit that real goldens should be re-captured in the Simulator later to drop the masks. Do not touch `tests/golden/real`.

- [ ] **Step 5: Refresh host goldens and run all checks**

Run: `make -C harness update-golden && make -C harness check && make -C tests`
Expected: all pass. `git diff --stat tests/golden/host` shows only frames that include the HELM bar changed.

- [ ] **Step 6: Commit**

```bash
git add src/pd/hud.c tests/golden/host tests/scenarios
git commit -m "HUD: helm mode shows the rudder position"
```
