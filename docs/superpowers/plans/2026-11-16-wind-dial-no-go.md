# Wind Dial No-Go Wedge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The wind dial shows true wind, with a grey wedge of the boat's no-go angle on the side the wind comes from.

**Architecture:** Rename the per-boat `autopilot_no_go_deg` to `no_go_deg` and set new per-boat values. Add two pure
helpers to `src/sim/hudinfo.c` (true wind relative to the bow, and which dial pixels are in the wedge), then use
them in the Playdate HUD (`src/pd/hud.c`) and the web HUD (`web/src/web_json.c` → `web/www/js/ui/hud.js`).

**Tech Stack:** C99 (Playdate SDK, native tests in `tests/` with `t.h`), Emscripten web build, vanilla JS + SVG, Playwright check.

**Spec:** `docs/superpowers/specs/2026-11-16-wind-dial-no-go-design.md`

## Global Constraints

- One shared per-boat angle: `controls.no_go_deg`, used by the irons warning, autopilot, Journey, the baseline ghost and the dial. No separate dial setting, no fallback for the old name.
- Angles: J/24 42, dinghy 45, Sunfish 45, Hobie 16 50, Reynolds 33 50. TP52 45 and longship 65 unchanged.
- The dial needle points where the true wind is going; the wedge is centered on where it comes from, ±`no_go_deg`.
- The number beside the dial shows true wind speed (`g->wind.speed`), in the same units each build shows today (Playdate m/s, web knots).
- Playdate wedge: 50% checkerboard of white pixels strictly inside the ring, drawn before ring, needle and notch.
- No highlight of the bow notch when inside the wedge.
- Don't edit old plan docs under `docs/superpowers/plans/` that mention the old name.

## Review Focus

1. Sign of the true-wind angle: a stopped boat must show the same angle as `boat_apparent_wind`'s `awa`, or the needle points the wrong way. Pinned in Task 3.
2. Wind from dead astern: the wedge straddles ±π and must not vanish or invert at the wrap. Pinned in Task 3.
3. A narrow 42° wedge on the 8 px dial must still light pixels. Pinned in Task 3.
4. Camera looking sideways or behind: wedge and needle must stay exactly opposite. Pinned in Task 3 (both use `hud_dial_angle` with the same look).
5. Non-finite heading or wind (the helpers already guard `look_yaw`): no NaN reaches drawing; no pixels lit. Pinned in Task 3.

---

### Task 1: Rename `autopilot_no_go_deg` to `no_go_deg`

**Files:**
- Modify: `src/sim/boatcfg.h:64`, `src/sim/boatcfg.c:116`, `src/sim/game.c:49,206`, `src/sim/autopilot.c:25`, `src/sim/baseline.c:41`
- Modify: `boats/dinghy.json`, `boats/sunfish.json`, `boats/j24.json`, `boats/hobie16.json`, `boats/reynolds33.json`, `boats/disabled/tp52.json`, `boats/disabled/longship.json`
- Modify: `docs/boat-schema.md:19`, `tests/test_boatcfg.c:280`

**Interfaces:**
- Produces: `ControlsCfg.no_go_deg` (float, degrees, default 45, range 0..90), JSON key `controls.no_go_deg`.

- [ ] **Step 1:** Change `tests/test_boatcfg.c:280` to `c.controls.no_go_deg >= 60`. Run `make -C tests test`. Expected: compile error, no member `no_go_deg`.
- [ ] **Step 2:** Rename the field, the `FF(...)` entry, every C use, every boat JSON key, and the schema doc entry. Values unchanged. Confirm `grep -rn autopilot_no_go src boats tests web docs/boat-schema.md` prints nothing.
- [ ] **Step 3:** Run `make -C tests test`. Expected: all pass. This includes `test_shipped_ghosts`, since no values changed.
- [ ] **Step 4:** Commit: `Boats: autopilot_no_go_deg becomes no_go_deg, one angle for the whole game`

### Task 2: Per-boat angles and regenerated ghosts

**Files:**
- Modify: `boats/j24.json` (42), `boats/hobie16.json` (50), `boats/reynolds33.json` (50)
- Modify: `ghosts/*_j24_*.bin`, `ghosts/*_hobie16_*.bin` (regenerated)
- Test: `tests/test_boatcfg.c`

**Interfaces:**
- Consumes: `ControlsCfg.no_go_deg` from Task 1.

- [ ] **Step 1: Write the failing test** `test_shipped_no_go_angles` in `tests/test_boatcfg.c`, loading each shipped boat with the existing `load_boat` helper:

```c
static void test_shipped_no_go_angles(void) {
    static const struct { const char *f; float deg; } want[] = {
        { "../boats/j24.json", 42 }, { "../boats/dinghy.json", 45 }, { "../boats/sunfish.json", 45 },
        { "../boats/hobie16.json", 50 }, { "../boats/reynolds33.json", 50 },
        { "../boats/disabled/tp52.json", 45 }, { "../boats/disabled/longship.json", 65 },
    };
    for (int i = 0; i < 7; i++) { BoatCfg c; load_boat(want[i].f, &c); T_NEAR(c.controls.no_go_deg, want[i].deg, 1e-6); }
}
```
Register it with `T_RUN`.
- [ ] **Step 2:** Run `make -C tests test`. Expected: FAIL in `test_boatcfg` on j24.
- [ ] **Step 3:** Set the three JSON values.
- [ ] **Step 4:** Run `make -C tests test`. Expected: `test_boatcfg` passes. `test_shipped_ghosts` (`test_shipped_baselines_are_what_the_pilot_sails_now`) FAILS for j24 and hobie16. That proves the ghosts depend on the angle.
- [ ] **Step 5:** Run `make ghosts`, then `git status ghosts`. Expected: only `*_j24_*` and `*_hobie16_*` files changed.
- [ ] **Step 6:** Run `make -C tests test`. Expected: all pass, including Journey and baseline tests. If a Journey or baseline test pins a j24/hobie16 leg, update its expected value to the new angle, never the other way.
- [ ] **Step 7:** Commit: `Boats: J/24 points to 42°, catamarans to 50°; baseline ghosts regenerated`

### Task 3: Dial helpers in `hudinfo`

**Files:**
- Modify: `src/sim/hudinfo.h`, `src/sim/hudinfo.c` (next to `hud_dial_angle`, line 99)
- Test: `tests/test_hudinfo.c`

**Interfaces:**
- Produces:
  - `float hud_true_wind_from_bow(float heading, float wind_dir);` returns the angle from the bow to where the true wind comes from, in (-π, π], same convention as `BoatState.awa`. `wind_dir` is `Wind.dir` (the way the wind blows; the source is `wind_dir + π`, as in `game.c:205`). Non-finite input → 0.
  - `bool hud_no_go_pixel(int dx, int dy, int r, float center, float half);` true when pixel offset (dx, dy) from the dial centre (screen axes, y down) is strictly inside radius `r - 1`, not the centre itself, within `half` rad of dial angle `center` (0 up, clockwise, as `hud_dial_angle`), and `((dx + dy) & 1) == 0`. Non-finite `center`/`half` or `half <= 0` → false.

- [ ] **Step 1: Write the failing tests** in `tests/test_hudinfo.c`, and register each with `T_RUN`:
  - `test_true_wind_matches_apparent_when_stopped`: a `BoatState` with zero velocity, `heading = 0.4f`, and a `Wind` with `dir = 2.0f, speed = 6`. Call `boat_apparent_wind`, then `T_NEAR(hud_true_wind_from_bow(0.4f, 2.0f), awa, 1e-5)`. Repeat with heading -2.5.
  - `test_true_wind_head_to_wind`: `T_NEAR(hud_true_wind_from_bow(1.0f, wrap_pi(1.0f + M_PI)), 0.0, 1e-5)`.
  - `test_true_wind_non_finite`: NaN heading → 0; INFINITY wind_dir → 0.
  - `test_no_go_pixels_42_deg_on_playdate_dial`: count pixels over dx, dy ∈ [-8, 8] with `r = 8, center = 0, half = 42°`. Expect a count > 0, and every lit pixel has dy < 0 (up side).
  - `test_no_go_pixels_symmetric_across_wrap`: `center = M_PI`, `half = 45°`. Some pixels lit, all with dy > 0, and the count equals the count for `center = 0` (by the 180° symmetry of the checker on a square grid).
  - `test_no_go_pixels_stay_inside_ring`: no lit pixel has dx² + dy² ≥ (r-1)², and (0,0) is never lit.
  - `test_no_go_pixels_degenerate`: `half = 0` → none lit; `half = NAN` → none; `half = M_PI/2` lights some pixels with dy > 0 at center 0 (the half disc includes the sides).
- [ ] **Step 2:** Run `make -C tests test`. Expected: link error, undefined `hud_true_wind_from_bow`.
- [ ] **Step 3:** Implement both in `src/sim/hudinfo.c` using `wrap_pi`. For the pixel's dial angle use `atan2f(dx, -dy)`, which matches `hud_dial_angle`'s 0-up, clockwise convention, and compare with `fabsf(wrap_pi(ang - center)) < half`.
- [ ] **Step 4:** Run `make -C tests test`. Expected: all pass.
- [ ] **Step 5:** Commit: `HUD: helpers for true wind off the bow and the no-go wedge pixels`

### Task 4: Playdate dial on true wind with the wedge

**Files:**
- Modify: `src/pd/hud.c:119` (speed text) and `src/pd/hud.c:150-162` (dial)

**Interfaces:**
- Consumes: `hud_true_wind_from_bow`, `hud_no_go_pixel` (Task 3); `g->boat.controls.no_go_deg` (Task 1); `g->wind.dir`, `g->wind.speed`.

- [ ] **Step 1:** Change the speed text to `fminf(99.0f, g->wind.speed)`, keeping the format and slot width.
- [ ] **Step 2:** In the dial block compute `from = hud_true_wind_from_bow(b->heading, g->wind.dir)`, `wc = hud_dial_angle(from, look)`, `half = no_go_deg * π/180`. Before `drawEllipse`, loop dx, dy over [-R, R] and set a white pixel at (cx+dx, cy+dy) where `hud_no_go_pixel(dx, dy, R, wc, half)`. Use `fillRect(x, y, 1, 1, kColorWhite)`, or write to the frame buffer if a helper for that already exists in `src/pd/`. Then the needle angle is `hud_dial_angle(from + π, look)`. Ring and notch are unchanged. Update the block comment to say true wind and the wedge.
- [ ] **Step 3:** Run `make` (Playdate build). Expected: builds clean with no warnings. Run `make -C tests test`. Expected: all pass.
- [ ] **Step 4:** Manual check in the Playdate Simulator: open `SailingSim.pdx`, Free Range, J/24. Turn head to wind: the notch sits in the checkered wedge, and the "in irons" banner appears once stalled. Bear away until the notch just leaves the wedge: about 42° off the wind. Pan the camera: the wedge and needle stay opposite. Note the result in the commit message.
- [ ] **Step 5:** Commit: `HUD: the wind dial shows true wind and shades the no-go wedge`

### Task 5: Web dial on true wind with the wedge

**Files:**
- Modify: `web/src/web_json.c:304-318`, `web/www/js/ui/hud.js:43-47,54,88-91`, `web/www/style.css:64-68`
- Test: `web/test/hud-bar.mjs`

**Interfaces:**
- Consumes: `hud_true_wind_from_bow` (Task 3), `controls.no_go_deg` (Task 1).
- Produces: HUD JSON keys `windDial` (true-wind needle angle, rad), `noGo` (half angle, rad), `tws` and `twsKn` (replacing `aws`/`awsKn`). The wedge's SVG element is `<path class="nogo">` inside `.dial`, before `.ring`.

- [ ] **Step 1:** In `web/test/hud-bar.mjs`, rename `awsKn` to `twsKn` in the test HUD data and add `noGo: 0.785`. Assert that `.dial .nogo` has a non-empty `d` attribute and a nonzero bounding box. Run `node web/test/hud-bar.mjs`. Expected: FAIL (no `.nogo`).
- [ ] **Step 2:** In `web_json.c`, set `windDial` to `hud_dial_angle(from + π, yaw)` with `from = hud_true_wind_from_bow(b->heading, g->wind.dir)`, emit `noGo`, and replace `aws`/`awsKn` with `tws`/`twsKn` from `g->wind.speed`. Keep `awa` (other code may read it; grep `web/www/js` before touching it).
- [ ] **Step 3:** In `hud.js`, add `<path class="nogo"/>` first in the dial SVG. On update, when `windDial` or `noGo` changes, set its `d` to a sector of radius 9.4 centered on `windDial + π`, spanning ±`noGo`: `M0 0 L x1 y1 A 9.4 9.4 0 large 1 x2 y2 Z` with `x = r·sin a, y = -r·cos a`, `large = noGo*2 > π ? 1 : 0`. Read `h.twsKn` for the number. In `style.css`, set `.dial .nogo { fill: var(--ink); opacity: 0.3; stroke: none; }`.
- [ ] **Step 4:** Run `node web/test/hud-bar.mjs`. Expected: PASS. Run `make -C web test` (parity, JS unit tests, smoke). Expected: all pass.
- [ ] **Step 5:** Manual check: `make -C web serve`, sail a J/24 head to wind, and confirm it matches the Playdate behaviour from Task 4.
- [ ] **Step 6:** Commit: `Web HUD: the wind dial shows true wind and shades the no-go wedge`
