# Helm rudder indicator (short design)

## Intent
In HELM mode the status bar shows nothing about where the rudder is. The player should see the current rudder position at a glance.

## Goals
- A thin horizontal track with a thick vertical marker showing rudder position, and a shorter thick fixed tick marking centre.
- Marker right = bow turning right (starboard). `BoatState.wheel` (-1..1) is positive for a starboard turn (positive yaw), so position = centre + wheel * half-width, no sign flip. (User decision.)
- Shown while the autopilot steers too, since it drives `wheel`.

## Non-goals / anti-goals
- No change to the bar's other instruments or their positions.
- Not shown in TRIM, HOIST or LOOK, and no second bar row.

## Approaches
One sensible way: HELM leaves the gap between the mode cell and the instrument cluster empty (TRIM/HOIST put the sail bar there), so the indicator goes there. Showing it in every mode would need a second row or would crowd out the sail bar.

## Design
- Pure helper in `src/sim/hudinfo.c/.h` returning the track span (x, width) from `cl.left`, and the marker x from `wheel`. Host-testable.
- Drawing in `hud_draw` (`src/pd/hud.c`), HELM mode only, white on the black bar:
  - Track: 1px line at bar mid-height, `HUD_PAD` margins, up to 80px wide, odd width so there is a true centre pixel. Narrows when AP/AT widen the cluster. Hidden if too narrow (as `hud_sail_bar_span` does).
  - Centre tick: fixed, 2px wide, ~6px tall.
  - Marker: 3px wide, ~12px tall, drawn over the tick. Position clamped to the track; non-finite `wheel` sits at centre.
- Tests: host unit tests for centre, both stops, clamping, non-finite input, narrow span. Refresh HELM goldens.

## Builder's calls
Pixel sizes, the 80px cap, the odd-width rule and the hide-when-narrow behaviour are my choices; adjust freely.
