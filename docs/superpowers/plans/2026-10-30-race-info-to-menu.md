# Race Info To Pause Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the top race-metrics bar, show the distance label above the mark indicator on screen (as it already is off screen), and move elapsed time plus mark info into the system pause-menu panel in place of the debug readout.

**Architecture:** Text formatting for the time and mark lines goes into `src/sim/hudinfo.c` so the headless C tests cover it. `src/pd/hud.c` loses the top bar and `hud_diag`; a new `hud_pause_panel` draws the menu bitmap from those formatters. The on-screen and off-screen indicators share one distance-label call.

**Tech Stack:** C99, Playdate C API, headless tests in `tests/` (`make -C tests`).

**Spec:** User request, 2026-10-30 (no separate spec): "racing metrics bar gone; distance to the marker above the marker when on screen, just like off screen; time elapsed to the menu; remove debug items from the pause menu and show current time and mark information."

## Global Constraints

- Pure formatting lives in `src/sim/` (no Playdate headers); drawing lives in `src/pd/`.
- Time format is the existing `MM:SS.S` (`%02d:%04.1f`, minutes = `(int)(t/60)`, seconds = `fmod(t,60)`).
- Distance format is the existing whole metres, `"%.0fm"`.
- The pause-menu bitmap is 400x240 (`CAM_W` x 240); the system menu shows its left ~200px, so lines must fit in ~196px.
- Nothing in `src/sim` calls into `src/pd`.

## Review Focus

- Free-sail (no track) pause: panel must not show race lines or crash on an empty/default track.
- Race finished (`r->finished` or `next >= nmarks`): panel must not index `marks[next]` out of range.
- Mark near the screen top: the on-screen distance label must stay inside the screen (no clipping at y<0).
- Very long times (>= 100 min) and very large distances: formatted strings must be truncated safely, not overflow.
- Non-finite distance or time (NaN): formatters write a sane string, never garbage.

---

## File Structure

- Modify `src/sim/hudinfo.h`, `src/sim/hudinfo.c`: add `hud_fmt_time`, `hud_fmt_mark`.
- Modify `tests/test_hudinfo.c`: tests for both.
- Modify `src/pd/hud.c`: strip the bar from `hud_race`, add on-screen distance label, replace `hud_diag` with `hud_pause_panel`.
- Modify `src/pd/render.h`: swap the `hud_diag` prototype for `hud_pause_panel`.
- Modify `src/pd/main.c:129`: call `hud_pause_panel`.

### Task 1: Time and mark formatters

**Files:**
- Modify: `src/sim/hudinfo.h`, `src/sim/hudinfo.c`
- Test: `tests/test_hudinfo.c`

**Interfaces:**
- Produces:
  - `void hud_fmt_time(double t, char *out, size_t n)` writes `MM:SS.S`; negative or non-finite `t` is treated as 0; always NUL-terminated for `n >= 1`.
  - `void hud_fmt_mark(const char *name, int index, int total, double dist_m, char *out, size_t n)` writes `"<name> <index>/<total>  <dist>m"` (e.g. `"B1 2/5  340m"`, `index` is 1-based as given); non-finite `dist_m` prints as `0`.

- [ ] **Step 1: Write failing tests** in `tests/test_hudinfo.c` (register them in the file's existing test list/main like the others):

```c
static void test_fmt_time(void) {
    char b[16];
    hud_fmt_time(0.0, b, sizeof b);      T_ASSERT(strcmp(b, "00:00.0") == 0);
    hud_fmt_time(75.34, b, sizeof b);    T_ASSERT(strcmp(b, "01:15.3") == 0);
    hud_fmt_time(-5.0, b, sizeof b);     T_ASSERT(strcmp(b, "00:00.0") == 0);
    hud_fmt_time(NAN, b, sizeof b);      T_ASSERT(strcmp(b, "00:00.0") == 0);
}
static void test_fmt_time_truncates(void) {
    char b[4];
    hud_fmt_time(7260.0, b, sizeof b);   /* 121:00.0 does not fit */
    T_ASSERT(strlen(b) == 3);
}
static void test_fmt_mark(void) {
    char b[32];
    hud_fmt_mark("B1", 2, 5, 340.4, b, sizeof b);   T_ASSERT(strcmp(b, "B1 2/5  340m") == 0);
    hud_fmt_mark("B1", 2, 5, NAN, b, sizeof b);     T_ASSERT(strcmp(b, "B1 2/5  0m") == 0);
}
```

- [ ] **Step 2: Run** `make -C tests` — Expected: compile FAIL, `hud_fmt_time` undeclared.
- [ ] **Step 3: Implement** both in `src/sim/hudinfo.c` with `snprintf` (declare in `hudinfo.h` with `#include <stddef.h>`); sanitize non-finite/negative inputs before formatting.
- [ ] **Step 4: Run** `make -C tests` — Expected: all binaries PASS.
- [ ] **Step 5: Commit** `git add src/sim/hudinfo.* tests/test_hudinfo.c && git commit -m "feat: hudinfo time and mark formatters"`

### Task 2: Remove the metrics bar; distance label above the mark

**Files:**
- Modify: `src/pd/hud.c` (`hud_race`, lines ~86-125)

**Interfaces:**
- Consumes: `camera_mark_view`, `race_mark_local` (unchanged).
- Produces: `hud_race(PlaydateAPI*, const Game*, const CameraPose*)` unchanged signature; draws only the indicator and its distance label; draws nothing once the race is done.

- [ ] **Step 1: Edit `hud_race`**: delete the black top `fillRect(0,0,CAM_W,18,…)` and both the `FINISHED` and `MARK … mm:ss` text lines; when `done`, just `return`. Remove the now-unused `mins` local. Keep `text`/`tri` helpers.
- [ ] **Step 2: Add on-screen label.** In the `mv.on_screen` branch, after `tri(...)`, draw `"%.0fm"` of `dist` centred above the triangle: `x` = `(int)x - 4*strlen(line)/…` (≈ 6px per char, centred on `mv.x`, clamped to `[2, CAM_W - 2 - width]`), `y` = `clampi((int)mv.y - 24, 2, CAM_H - HUD_H - 18)` so it sits just over the triangle (which spans `y-10..y`) and never leaves the screen top. The off-screen branch keeps its current placement but with its min y lowered from 20 to 2 now that the bar is gone. Both branches use the same `setDrawMode(kDrawModeFillWhite)`-on-black-or-draw-mode treatment the off-screen label uses today so text stays legible over the sea (check on device/simulator).
- [ ] **Step 3: Build** `make` (project root; produces `SailingSim.pdx`) — Expected: no warnings/errors.
- [ ] **Step 4: Verify in simulator** (Playdate Simulator): start a race, point at the first mark. Expected: no black bar at the top; "NNNm" over the triangle while the mark is on screen; turning away flips to the edge arrow with the same label; sail the mark near the top edge and confirm the label stays on screen.
- [ ] **Step 5: Commit** `git commit -am "feat: drop the race bar; show mark distance above the marker"`

### Task 3: Pause panel replaces diagnostics

**Files:**
- Modify: `src/pd/hud.c` (replace `hud_diag`), `src/pd/render.h:24`, `src/pd/main.c:129`

**Interfaces:**
- Consumes: `hud_fmt_time`, `hud_fmt_mark` (Task 1); `Game` fields `race`, `track`, `mode` (`GAME_RACE`), `b.x/y`.
- Produces: `void hud_pause_panel(PlaydateAPI *pd, const Game *g)` — draws on a white-cleared bitmap context, left ~196px:
  - Racing, not done: row 1 `"Time " + hud_fmt_time(r->t)`, row 2 `hud_fmt_mark(tr->marks[r->next].name, r->next+1, tr->nmarks, dist)` where `dist = hypot(mark_local - boat)` via `race_mark_local`.
  - Racing, done: row 1 `"FINISHED"`, row 2 `"Time " + hud_fmt_time(r->t)`.
  - Free sail (`g->mode != GAME_RACE`): draws nothing beyond the white clear.
  - Same `ROW` helper style (18px pitch, x=4, start y=6) as the old `hud_diag`.

- [ ] **Step 1: Implement** `hud_pause_panel`, delete `hud_diag` and its comment, update the prototype in `render.h` and the call in `main.c` (`hud_diag(pd, &game)` -> `hud_pause_panel(pd, &game)`).
- [ ] **Step 2: Build** `make` — Expected: no warnings; `grep -rn hud_diag src` returns nothing.
- [ ] **Step 3: Run tests** `make -C tests` — Expected: all PASS.
- [ ] **Step 4: Verify in simulator**: pause in a race (time ticking value matches the moment of pause, mark name/count/distance shown, none of the TWS/AWA/sail debug rows); pause in free sail (blank panel, no crash); finish a race then pause (FINISHED + final time).
- [ ] **Step 5: Commit** `git commit -am "feat: pause menu shows race time and mark info instead of debug rows"`

---

## Self-review notes

- Coverage: bar removal (T2), distance above marker (T2), time to menu (T3), debug removed (T3), mark info (T3).
- Behaviour change to confirm: with the bar gone, the in-game "FINISHED" banner is gone too; finish is only visible in the pause panel. If you want a transient on-screen finish message, that's a small follow-up.
