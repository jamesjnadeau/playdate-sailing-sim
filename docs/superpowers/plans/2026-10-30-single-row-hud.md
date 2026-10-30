# Single-Row HUD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shrink the bottom HUD from two 36px rows to one 20px row ("design C", segmented cells): the selected d-pad mode becomes a glyph in an inverted cell, and navigation numbers show only while the wheel is selected.

**Architecture:** Presentation-free decisions (mode glyph, heading rounding, sail bar fill, which cells show) move into a small pure module `src/sim/hudinfo.[ch]` that the headless tests cover. `src/pd/hud.c` only draws from those values. The HUD height becomes one shared macro so the camera's horizon clamp tracks the new bar.

**Tech Stack:** C99, Playdate C API; headless tests in `tests/` (`make -C tests test`, ASan/UBSan, `t.h` macros). Device/simulator build via top-level `Makefile`.

**Builds on:** `docs/superpowers/plans/2026-10-30-menu-and-racing.md` (implemented). Racing added `hud_race` in `src/pd/hud.c` (top race banner + next-mark indicator) and `camera_mark_view(c, wx, wy, bottom, out)` in `src/sim/camera.c`, whose `bottom` is the lowest usable screen row; both currently assume the old 36px bar (`CAM_H - 36`).

**Spec:** none beyond the user's request. Request, verbatim: "make the current bottom bar UI shorter by combining the 2 rows into one. First we can turn the current selected dpad item into an emoji, and we can only show the navigation information when the wheel is selected." Chosen mockup: design C (see Decisions).

## Decisions made in this plan (not in the request)

- **Design C layout, left to right:** inverted glyph cell (22px wide, full bar height), then mode cells, a gap, then `AP nnn` / `AT` badges, wind-dial.
  - **WHEEL:** cells `6.2kn` | `045°` | `∠+12` (speed, heading, heel), separated by 1px white lines, then `AWS` number before the dial.
  - **TRIM / HOIST:** sail name cell + the existing 70px fill bar (trim fraction or hoist fraction). No nav cells.
  - **CAMERA:** a single `Look` cell.
- **Glyphs** are the Playdate system-font d-pad glyphs, drawn as UTF-8: WHEEL=`⬆` (UP), TRIM=`➡` (RIGHT), HOIST=`⬅` (LEFT), CAMERA=`⬇` (DOWN), matching the button map in `controls.h`.
- **AP / AT badges and the wind dial stay visible in every mode** (state indicators, not "navigation info"). The AWS number is navigation info and shows with the wheel only.
- Heading display is rounded then wrapped, so it never reads `360` (the current `%03.0f` of 359.6 does).
- Warning strip in `main.c` is at the top (y=36) and is unaffected.
- **Race HUD follows the shorter bar:** the race banner (rows 0..17, top of screen) is unchanged. The next-mark indicator's usable area ends at `CAM_H - HUD_H`: `hud_race` passes that as `bottom` to `camera_mark_view`, and the off-screen distance label's y clamp upper bound becomes `CAM_H - HUD_H - 18` (it was `CAM_H - 50`, i.e. 36px bar + 14). So a buoy projecting into the reclaimed rows 204..219 now gets the on-screen triangle instead of an edge arrow.
- `render_frame` draw order is unchanged: `hud_draw`, then `hud_race` (race mode only), then `hud_prompt`.

## Global Constraints

- HUD height is `HUD_H` = `20` px, defined once in `src/sim/camera.h`; the bar occupies rows `CAM_H - HUD_H` .. `CAM_H - 1` (220..239).
- Screen is `CAM_W` x `CAM_H` = 400 x 240, 1-bit; HUD draws white on black, `kDrawModeFillWhite` for text, restored to `kDrawModeCopy` after.
- Tests build with `-Wall -Wextra -Werror` and ASan/UBSan; no unused leftovers (`MODE_NAMES` goes away).
- `src/sim` must not include Playdate headers (tests compile it on the host).
- Mode glyph strings are UTF-8 in a `const char *`; draw them with `kUTF8Encoding`, other text with `kASCIIEncoding`.
- No literal `36` for the bar height remains in `src/` or `tests/`: `grep -n "CAM_H - 36\|204" src tests -r` finds nothing HUD-related after Task 2.
- Wind dial keeps its meaning: tick shows where the wind comes from relative to the bow (up), uses `b->awa`.

## Review Focus

- Heading 359.6 deg (and exactly 2*PI, and -0.0): displays 0..359, never 360 or a negative number.
- Non-finite heading / sail fraction inputs: no NaN reaches `snprintf("%d")` or a rect width; bar fraction is 0.
- Sail with `trim_max_deg == trim_min_deg` (zero range): bar fraction finite, in 0..1.
- `g->ctl.sail` valid but `sail_count` of 1 (dinghy-style single sail): name cell still draws; no out-of-range index.
- Long sail names (config is user JSON): name cell truncates to fit and never overlaps the AP badge.
- Race mode: next mark sitting just above the new bar (rows 204..219) shows the triangle over the buoy, not an edge arrow; an off-screen arrow at the bottom edge and its distance label never overlap the bar.
- Camera horizon clamp must follow the shorter bar, or the sea shows a 16px dead strip / horizon is clamped for the old bar.

---

### Task 1: `hudinfo` pure helpers

**Files:**
- Create: `src/sim/hudinfo.h`, `src/sim/hudinfo.c`
- Test: `tests/test_hudinfo.c` (picked up by the wildcard in `tests/Makefile`)

**Interfaces:**
- Consumes: `Mode` from `controls.h`; `SailCfg` (`trim_min_deg`, `trim_max_deg`) from `boatcfg.h`; `clampf` from `mathx.h`.
- Produces (in `hudinfo.h`):
  ```c
  /* UTF-8 d-pad glyph for the mode's button; "?" for an out-of-range Mode. */
  const char *hud_mode_glyph(Mode m);
  /* true only for MODE_WHEEL: speed/heading/heel/AWS cells are shown. */
  bool hud_shows_nav(Mode m);
  /* Radians to whole degrees 0..359: rounded, wrapped, non-finite -> 0. */
  int hud_heading_deg(float rad);
  /* 0..1 fill for the sail bar. HOIST: hoist as given. TRIM: (trim - min)/(max - min) in
   * the SailCfg's degrees converted from `trim_rad`. Other modes and non-finite inputs -> 0.
   * A zero or negative trim range -> 0. */
  float hud_sail_fraction(Mode m, const SailCfg *s, float trim_rad, float hoist);
  ```

- [ ] **Step 1: Write failing tests in `tests/test_hudinfo.c`** (include `t.h`, `hudinfo.h`, `mathx.h`; use the file's sibling tests for the `t.h` macro style). Tests and assertions:
  - `test_glyph_per_mode`: `strcmp(hud_mode_glyph(MODE_WHEEL), "⬆") == 0`, TRIM `"➡"`, HOIST `"⬅"`, CAMERA `"⬇"`; `hud_mode_glyph((Mode)99)` equals `"?"`.
  - `test_shows_nav_only_for_wheel`: true for `MODE_WHEEL`; false for TRIM, HOIST, CAMERA.
  - `test_heading_wraps_and_rounds`: `hud_heading_deg(0.0f)==0`; `hud_heading_deg(deg2rad(359.6f))==0`; `hud_heading_deg(deg2rad(359.4f))==359`; `hud_heading_deg(2*PI)==0`; `hud_heading_deg(-deg2rad(90))==270`; `hud_heading_deg(-0.0f)==0`.
  - `test_heading_non_finite_is_zero`: `NAN`, `INFINITY` -> 0.
  - `test_sail_fraction_trim`: sail with `trim_min_deg=0, trim_max_deg=90`; `trim_rad=deg2rad(45)` gives 0.5 +-1e-4; below min clamps to 0; above max clamps to 1.
  - `test_sail_fraction_hoist_and_other_modes`: HOIST with hoist 0.25 -> 0.25; hoist 1.5 -> 1; WHEEL and CAMERA -> 0.
  - `test_sail_fraction_degenerate`: `trim_max_deg == trim_min_deg` -> 0 (finite); `trim_rad = NAN` -> 0; `hoist = NAN` in HOIST -> 0.
  Use a local `SailCfg` zero-initialised then set the two fields.

- [ ] **Step 2: Run to verify failure**

  Run: `make -C tests test 2>&1 | tail -20`
  Expected: build error, `hudinfo.h: No such file`.

- [ ] **Step 3: Implement the four functions in `src/sim/hudinfo.[ch]`**

  Heading: wrap with `fmodf(deg, 360.0f)`, add 360 if negative, round with `floorf(x + 0.5f)`, then `% 360` the int. Fraction: guard with `isfinite`, `clampf` the result to 0..1.

- [ ] **Step 4: Run to verify pass**

  Run: `make -C tests test 2>&1 | tail -20`
  Expected: all tests including `test_hudinfo` pass, no sanitizer output.

- [ ] **Step 5: Commit**

  ```bash
  git add src/sim/hudinfo.h src/sim/hudinfo.c tests/test_hudinfo.c
  git commit -m "feat: hudinfo helpers for the single-row HUD"
  ```

---

### Task 2: Single-row `hud_draw` and shared HUD height

**Files:**
- Modify: `src/sim/camera.h` (add `#define HUD_H 20`), `src/sim/camera.c:6` (`HUD_TOP_PX` becomes `(CAM_H - HUD_H)`), `src/pd/hud.c` (`hud_draw`, drop `MODE_NAMES`; in `hud_race` the `camera_mark_view` bottom at `hud.c:92` and the label clamp at `hud.c:102`)
- Test: `tests/test_camera.c` (update existing tests) plus simulator check

**Interfaces:**
- Consumes: `hud_mode_glyph`, `hud_shows_nav`, `hud_heading_deg`, `hud_sail_fraction` (Task 1); `Game` fields used today (`g->b`, `g->ctl`, `g->boat.sails`, `g->ap.on/target`, `g->at.on`).
- Produces: `void hud_draw(PlaydateAPI *pd, const Game *g)` with unchanged signature; draws only inside rows `CAM_H - HUD_H` .. `CAM_H - 1`. `hud_race` keeps its signature; its indicator stays inside rows 18 .. `CAM_H - HUD_H - 1`.

- [ ] **Step 1: Point the camera tests at `HUD_H` in `tests/test_camera.c`**

  - `test_trailing_view_frames_mast_and_hull_with_margin` (around line 137): change `T_NEAR(sy, 204.0 - m, 3.0)` to `T_NEAR(sy, (CAM_H - HUD_H) - m, 3.0)`. This is the horizon-clamp check: it fails to compile until the macro exists, then fails on value until `camera.c` uses it.
  - The `camera_mark_view` tests (lines ~149-183): replace every `CAM_H - 36` with `CAM_H - HUD_H`, including the `T_ASSERT(v.y < CAM_H - 36)`.
  - Add `test_mark_view_bottom_bound`: with the same pose setup as the existing on-screen mark-view test, find a world point whose `camera_project` row is between `CAM_H - 36` and `CAM_H - HUD_H` (scan distance ahead along the bow until `sy` lands in that band; `T_ASSERT` one was found). Assert `camera_mark_view(..., CAM_H - HUD_H, &v)` gives `v.on_screen` true, and `camera_mark_view(..., CAM_H - 36, &v)` gives false with `v.y <= CAM_H - 36 - MARK_EDGE_PAD + 1e-3`.

- [ ] **Step 2: Run to verify failure**

  Run: `make -C tests test 2>&1 | tail -20`
  Expected: `HUD_H` undeclared, then (after adding only the macro) the trailing-view assertion fails while `camera.c` still uses 204.

- [ ] **Step 3: Add `HUD_H` to `camera.h` and make `camera.c` use `(CAM_H - HUD_H)` for `HUD_TOP_PX`**

- [ ] **Step 4: Run to verify pass**

  Run: `make -C tests test 2>&1 | tail -20`
  Expected: all tests pass.

- [ ] **Step 5: Rewrite `hud_draw` in `src/pd/hud.c` to the design-C layout**

  Keep the file's helper `text()` and add `text_utf8()` using `kUTF8Encoding`. Layout rules (all x from the left, `top = CAM_H - HUD_H`):
  - Fill `0, top, CAM_W, HUD_H` black. Glyph cell: white `fillRect(0, top, 22, HUD_H)`, glyph drawn in `kDrawModeFillBlack` centred in it, then back to `kDrawModeFillWhite` for everything else.
  - If `hud_shows_nav(mode)`: three cells starting at x=22, each padded 6px each side, text `"%.1fkn"` (`fabsf(b->u) * 1.94384f`), `"%03d"` of `hud_heading_deg(b->heading)` plus a degree mark if the font has it (else omit), `"%+.0f"` of `b->heel` degrees; 1px white `drawLine` separators between cells; `"%.0f"` of `b->aws` placed just left of the dial. Cell width = measured with `pd->graphics->getTextWidth` is not required; use fixed 52/44/44 px widths.
  - Else if mode is TRIM or HOIST: sail name cell (name truncated to 8 chars) then the 70px bar via `drawRect` + `fillRect` width `(int)(66 * hud_sail_fraction(...))`, same drawing the current code does at `hud.c:43-53`, moved to the bar's new row.
  - Else (CAMERA): the literal `"Look"`.
  - Right edge, every mode: `AP nnn` (using `hud_heading_deg(g->ap.target)`) when `g->ap.on`; `AT` when `g->at.on`; wind dial with radius 8 at `(CAM_W - 12, top + HUD_H / 2)` drawn as the current code (`awa` tick plus bow notch), scaled to R=8.
  - Text baseline: `top + 2`; if glyphs clip on the simulator, adjust this one constant, nothing else.
  - End with `setDrawMode(kDrawModeCopy)`.

- [ ] **Step 5b: Update `hud_race` in `src/pd/hud.c`**

  `camera_mark_view(c, mx, my, (float)(CAM_H - HUD_H), &mv)`, and the label clamp `clampi((int)mv.y - 6, 20, CAM_H - HUD_H - 18)`. Nothing else in `hud_race` changes.

- [ ] **Step 6: Build and run the simulator**

  Run: `make 2>&1 | tail -20`
  Expected: builds `SailingSim.pdx` with no warnings.
  Then open it in the Playdate Simulator and check each of: UP (wheel cells + AWS + dial), RIGHT/LEFT (sail name + bar moves with the crank, re-press cycles sail), DOWN (`Look`), hold A (AP badge), hold B (AT badge), capsize prompt still centred. Then main menu → Racing → Two Buoys: the race banner still sits at the top, the next-mark triangle appears over buoy B, and when B is behind/beside, the edge arrow and its distance label stay above the bar. Confirm the sea now reaches the bar (no 16px dead strip) and the horizon does not jump when switching modes. Confirm the four glyphs render; if any does not, report it before choosing a fallback (ASCII `^ > < v` in the same cell).

- [ ] **Step 7: Commit**

  ```bash
  git add src/sim/camera.h src/sim/camera.c src/pd/hud.c tests/test_camera.c
  git commit -m "feat: single-row HUD with mode glyph; nav info only on the wheel"
  ```
