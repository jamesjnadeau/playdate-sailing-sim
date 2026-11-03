# Shared HUD Message Style Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The rudder-assist and engine banners are drawn in the same style as the buoy-rounding praise message, through one shared drawing function, with the colours inverted from today: white behind, black in front.

**Architecture:** `src/pd/hud.c` gets one static function, `message(pd, s, y)`, that owns the whole look (centring, the back layer, the front layer). `hud_praise` and the banner loop in `hud_draw` both call it and keep their current rows. The look lives in two constants next to it, so changing the style later is a one-place edit.

**Tech Stack:** C99, Playdate SDK, host harness in `harness/` (golden framebuffer regression).

**Spec:** none written; the request is the spec: "use the style of the buoy success messages for the other on-screen messages, leave them in the same position, share the same drawing engine, invert it: white in the background, black up front."

## Global Constraints

- Same rows as today. Praise text at `y = 26`. Banner `row` r (0 = assist when shown, else engine; 1 = engine when assist is also shown) at `y = top - 16 - 16 * r + 1`, where `top = CAM_H - HUD_H` (today's box top plus the 1px text inset).
- Banners lose their black box and white outline; praise never had a box. Nothing is drawn behind the text except the white back layer.
- Back layer: white, offset `(+1, +1)` from the front layer (today's praise shadow offset, colour inverted). Front layer: black.
- Text is horizontally centred on `CAM_W` using `getTextWidth` for every message (banner text was left-aligned inside a 188px box; it is now centred like praise).
- Message text and the conditions that show it (`hud_assist_text`, `hud_engine_text`, `praise_text`, `praise_left`) are unchanged. `make -C tests` stays green with no edited assertions.

## Review Focus

- Black front text over the dark sea or a dark hull is hard to read if the white layer is only a 1px shadow. Expected: the glyphs stay legible; check the harness frames by eye and, if not, widen the back layer in the shared function only (see Task 1, Step 4).
- Assist and engine banners shown together must not overlap each other or the status bar: rows stay 16px apart, as today.
- A praise message is up while a banner is shown: praise is at `y = 26` near the top, banners sit just above the bar, so they cannot collide (unchanged geometry).
- Empty or very long strings: `getTextWidth` of a long string can exceed `CAM_W`; centre clamps at x >= 0 rather than drawing from a negative offset.
- The draw mode must be left as `kDrawModeCopy` afterward, as every other HUD routine assumes.

---

### Task 1: Shared message drawer, used by praise and banners

**Files:**
- Modify: `src/pd/hud.c` (add `message`; edit the banner loop at ~lines 110-121; edit `hud_praise` at ~lines 202-214)
- Regenerate: `tests/golden/host/*` for any scenario whose frames show a banner or praise (and adjust `tests/scenarios/*.masks` only if the fidelity check fails afterwards)

**Interfaces:**
- Consumes: existing `static void text(PlaydateAPI *pd, const char *s, int x, int y)`, `CAM_W`.
- Produces: `static void message(PlaydateAPI *pd, const char *s, int y)`: draws `s` centred horizontally at row `y`, white back layer at `(+1,+1)` then black front layer, and leaves the draw mode `kDrawModeCopy`.

There is no headless test for drawing (`src/pd` is not linked into `tests/`); the regression gate is the host harness goldens.

- [ ] **Step 1: Baseline.** Run `make -C tests && make -C harness check-regression`. Expected: both pass before any edit. Note which scenarios contain a banner or praise (the engine and assist banners need the engine on or a slow, helm-turned boat; `grep -l` the scenario scripts in `tests/scenarios/` for `engine`, or note the frames that change in Step 5).

- [ ] **Step 2: Add `message` in `src/pd/hud.c`**, just below `text()`, with two file-level constants for the look (`MSG_BACK_DX`, `MSG_BACK_DY`, both 1). Draw order: set `kDrawModeFillWhite`, `text` at `(x + MSG_BACK_DX, y + MSG_BACK_DY)`; set `kDrawModeCopy`, `text` at `(x, y)`. `x = (CAM_W - width) / 2` clamped to >= 0, width from `pd->graphics->getTextWidth(NULL, s, strlen(s), kASCIIEncoding, 0)`. Restore `kDrawModeCopy` at the end. Add a one-line comment saying this is the single place the on-screen message style lives.

- [ ] **Step 3: Route both callers through it.** In `hud_praise`, replace the width / shadow / front-text block with `message(pd, s, 26)`. In `hud_draw`, replace the body of the banner loop (the `fillRect`, `drawRect`, draw-mode and `text` calls) with `message(pd, banners[i], top - 16 - 16 * row++ + 1)`. Update the `hud_praise` doc comment (white shadow behind black text now) and drop the "No box behind it" claim only if it is no longer accurate (it still is).

- [ ] **Step 4: Build and look.** Run `make -C harness run-all` and view the frames that show a banner or praise (`tools/fbdiff.py` output in `harness/build/diff`, or open the `.fb` frames). Expected: black text with a white offset layer, centred, in the same rows as before. If black-on-dark is not legible, change only `message` to draw the white layer at all eight neighbouring offsets (an outline) before the black text; callers do not change.

- [ ] **Step 5: Update goldens and run the full gate.**
  Run: `make -C harness update-golden && make -C tests && make -C harness check`
  Expected: all pass. `git diff --stat tests/golden` lists only scenarios that actually show a banner or praise; any other changed golden means an unintended change, so investigate before committing.

- [ ] **Step 6: Commit**

```bash
git add src/pd/hud.c tests/golden docs/superpowers/plans/2026-11-03-shared-hud-message-style.md
git commit -m "hud: draw assist/engine banners with the praise style via one shared message drawer, inverted (white behind, black in front)"
```

---

## Self-review notes

- Coverage: shared engine (Task 1 Steps 2-3), same positions (Global Constraints rows), inversion (back/front colours), style applied to assist and engine banners (Step 3). One task, since the change is a single function plus two call sites that must land together to keep goldens consistent.
- Judgement calls to confirm: banner text is now centred (it was left-aligned in a box), and the box and outline are gone, matching the praise look.
