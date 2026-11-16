# Wind dial: true wind and a no-go wedge

## Why

Players can't see how close to the wind they can point until the "stuck in irons" banner tells them they went
too far. A grey wedge on the wind dial shows the headings the boat can't sail, before they get there.

## What changes

- **The dial shows true wind.** The needle still points where the wind is going, but from the true wind
  (`g->wind.dir` relative to the heading), not the apparent wind (`b->awa`). Up is still where the camera looks,
  and the bow notch is unchanged.
- **A grey no-go wedge** fills the inside of the dial on the side the wind comes from, directly opposite the
  needle, spanning the boat's no-go angle either side. When the bow notch is inside the wedge, the boat is
  pointing too close to the wind. This is the same test the irons warning and the autopilot use, so the wedge
  edge is exactly where they draw the line.
- **One per-boat angle.** `controls.autopilot_no_go_deg` is renamed `controls.no_go_deg`. It's still shared by
  the irons warning, autopilot, Journey leg placement and the baseline ghost, and now drives the wedge too.
- **New angles:** J/24 42°, dinghy 45°, Sunfish 45°, Hobie 16 50°, Reynolds 33 50°. The disabled boats
  (TP52 45°, longship 65°) are renamed only and keep their angles. The shipped baseline ghosts for the J/24
  and Hobie 16 are regenerated, since the baseline pilot sails by this angle.

## Drawing

- Playdate (`src/pd/hud.c`): the dial is 16 px, white on the black status bar. The wedge is a 50% checkerboard
  of white pixels inside the ring (not on it), drawn before the ring, needle and notch so those stay solid.
- Web (`web/www/js/ui/hud.js`, fed by `web/src/web_json.c`): a grey filled SVG wedge in the existing dial,
  under the ring and needle. `web_json.c` sends the true-wind needle angle and the no-go angle.
- No extra highlight when the bow is inside the wedge. The red notch in the preview was only to show the test.
  The irons banner already warns when the boat stalls.

## Approaches considered

- **A. Wedge on true wind, needle stays apparent.** Wedge matches the game but sits off the needle's
  opposite when sailing fast.
- **B. Wedge on apparent wind.** Tidy, but a boat sailing well close-hauled (about 30° apparent at 45° true)
  shows inside the wedge, so players learn to ignore it.
- **C. Whole dial on true wind (chosen).** Needle and wedge line up exactly and agree with the irons warning
  and autopilot. Cost: the needle no longer shows what the sails feel.

## My calls (correct any of these)

- **The number next to the dial switches to true wind speed**, so the whole cell talks about one wind.
- Changing the J/24 and catamaran angles moves where Journey puts marks and the path the baseline ghost sails
  for those boats. That's accepted, not worked around.
- Renaming the setting with no fallback for the old name: every boat file lives in this repo.

## Not doing

- No change to sail physics, trim, or how the irons warning works.
- No new setting just for the dial.

## Left to the builder

- Exact dither pattern and pixel boundary at the wedge edges, as long as it reads at real size on device.
- Updating `docs/boat-schema.md`, `tests/test_boatcfg.c` and the HUD tests for the rename and the new needle.
