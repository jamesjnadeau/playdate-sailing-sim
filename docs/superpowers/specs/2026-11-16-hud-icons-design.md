# HUD: heading and heel as right-justified icons

Status: awaiting approval. Mockup: `hud_mock.png` (sent in the session; 4x of the 400x20 bar).

## Intent
Heading and heel vanish from the bottom bar in TRIM and HOIST modes, because the bar space is
reused for the sail name and trim/hoist bar (`hud_shows_nav()` is false there). Make heading and
heel always visible, so the player can watch them while adjusting sails.

## Goals
- Heading and heel are drawn in every mode, as an icon plus number, right-justified next to the wind dial.
- The "HDG" and "HEEL" text labels are replaced by icons.
- Sail info (name + bar) keeps enough room in TRIM/HOIST, even with autotrim and autopilot on.

## Non-goals
- No change to the numbers shown, the wind dial, the mode cell, banners or any sim logic.
- No live-tilting heel icon.

## Constraints
- Playdate 400x240, 1-bit; bar is rows 220..239 (`HUD_H` = 20), mode cell is x 0..53.
- Icons drawn with the graphics primitives in `src/pd/hud.c` like `icon_gear`/`icon_compass` (the system font has no such glyphs).

## Approaches
1. **Right-justified cluster, always drawn (chosen).** Cluster order right to left: dial, wind speed, heel, heading, then AP and AT icons, with 1 px separators.
2. Keep labels, shrink to fit: too wide for the trim bar; rejected.
3. Heel icon that tilts with the real heel: cramped at 5-20 degrees; rejected by the user in favour of a fixed icon.

## Design (pixels, from the mockup; builder re-measures with the real font)
- Heading icon: glyph ➢ rotated to point up, ~11x11 px, left half filled, right half outlined. Number is 3 digits (`hud_heading_deg`), ~21 px. Cell about x 279..313.
- Heel icon: glyph ∢ with a horizontal base, ~12x12 px, second leg fixed at 30 degrees, small arc between the legs. Number is signed (`%+.0f`), ~19 px. Cell about x 322..355.
- Wind speed number moves to x 364..376 (right edge 376), adjoining the dial (x 380..396, unchanged).
- Separators: 1 px lines at about x 359 and x 317, and x 274 when AP/AT is shown.
- AP (compass + target heading) and AT (gear) icons move left of the cluster; worst case they start at x 218.
- Sail info area: x 54..279 normally, x 54..218 with AP and AT on. Today's trim bar ends at x 181, so it fits.
- Speed (`6.2kn`) stays in the left area in wheel/camera mode and is not drawn in TRIM/HOIST, as now.
- Camera mode: shows the same cluster (my call; it already showed nav).

## Left to the builder
- Exact pixel positions: compute from `getTextWidth` at run time and right-align, don't hard-code the mockup's numbers.
- Final bitmaps for the two icons at the sizes above; keep the heading arrow visually distinct from the AP compass.
- Update `tests/` / golden frames and `web/www/js/ui/hud.js`, which mirrors the HUD in the web build.
- Keep `hud_shows_nav()` only if it is still used for the left-hand speed cell.
