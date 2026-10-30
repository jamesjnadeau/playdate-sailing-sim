# Sailing sim for Playdate — design

Date: 2026-10-30. Status: draft for approval.
Reference: `docs/boat-model-analysis.md` (how the demo "At the Helm" behaves; used for *observed behavior and feel only*, nothing is copied).

## Intent and why
Build our own Playdate sailing game in C. The player is at the helm of a small boat on an endless open sea (islands come later), in the same
first-person, heel-and-wave-bobbing view the user likes in the demo. Unlike the demo, the boat is a **physical simulation**
defined entirely by a **runtime-loaded config**, so different boats (dinghy first, then keelboats, multi-sail rigs) are data, not code.
The challenge is the environment: weather, big waves, gusts. The sim does not forgive mistakes, but restarting is instant.

## Goals
- Boat, rig, controls feel and look are all described by a config file loaded at runtime from the .pdx; a built-in dinghy is the fallback.
- Real-aerodynamics sail model; main + jib on the dinghy; more sails (spinnaker, code zero, storm jib…) are just more config entries.
- Failure is real: stalling in irons, luffing, broaching, capsize, striking a rock. Easy reset.
- Keep the demo's view feel: an endless open sea with horizon/pitch/roll camera, spray, streaks, clouds.
- **Scope for now (user-decided): sailing and the sea, with rock hazards instead of islands.** No islands, goals or map; they come later as a separate project.
- **Rock hazards (user-decided):** rocks scattered across the open sea. They are obstacles to avoid, not destinations. See "Rock hazards" below.
- Tunable in the Playdate Simulator without recompiling.

## Non-goals / anti-goals
- No copying demo code, tables, sprites, sounds or island generation constants; implementation is clean-room from the observed behavior.
- No crew/damage/repair sim, tides or currents, multiplayer, racing rules, sprite art for boats (all boat art is generated from config geometry).
- Anti-goal: a game that is "realistic" but unplayable on one crank + buttons, or where reset is slow.

## Constraints
- Playdate C SDK, 400×240 1-bit, 30 fps target. Physics runs at a fixed substep (suggest 120 Hz) independent of frame rate; dt clamped.
- Config parsed with the Playdate JSON API; every field validated, clamped and defaulted; a bad file must never crash — fall back to the field's
  default and show a one-line warning (log + on-screen on first load).
- SI units in configs. A single `world_units_per_metre` maps to the world/camera scale.
- Memory budget: sail count ≤ 8, config parsed once at boat load.

## Approaches considered
1. Per-sail list with a polar-like efficiency per sail (simple, close to the demo's feel).
2. Weighted original single polar split by area (least work, jib is cosmetic).
3. **Full aerodynamics per sail (chosen by the user):** lift/drag from angle of attack, slot effect, centre of effort, heel and foil forces.
Chosen: 3, because the goal is a sailing simulator, not a reskin. Risk: tuning effort and CPU; mitigated by tables, fixed substeps and
the config being tunable at runtime.

## Controls (user-decided)
Crank-driven, same as before.
- **Modes are latched, not hold-to-use (user-decided):** pressing a direction selects that mode, and the crank keeps controlling it after release, until a
  different direction is pressed. A HUD shows the active mode.
- **UP** = wheel mode (crank turns the wheel/tiller).
- **RIGHT** = sail trim mode; **LEFT** = hoist/reef mode. **Pressing the same button again selects the next sail** (main → jib → …). The selected sail
  is shared between trim and hoist modes. *(My call: the user said "the sail select button" — I mapped it onto the existing trim/hoist buttons.)*
- **Deploying extra sails** = hoisting them (a sail at hoist 0 is stowed and contributes nothing). No extra button. *(My call.)*
- **DOWN** = camera-look mode: crank rotates camera yaw (head turn). Selecting any other mode returns the camera to centre at a configurable rate. *(Return behavior is my call.)*
- **A** = autopilot (hold toggles; a tap steps the target heading, skipping into-wind headings). **B** = anchor toggle. (The map returns with islands.)
- **Reset:** system-menu item "restart" plus a quick-restart prompt on capsize or a hard rock strike (A to restart immediately). Reset = same boat,
  start position, fresh weather seed. Without islands, position still matters for the sea pattern, so the sea is generated from position and time. *(My call.)*

## Camera (user-decided)
Mimics a sailor compensating for tilt: camera roll follows the boat's heel up to a configurable `camera.roll_limit_deg` (**default 10°**),
then stops; the deck, wheel and rig keep tilting with the boat, so the horizon stays steadier than the hull. Pitch/heave bob and head yaw offset
come on top. Smoothing is configurable. Camera height and lateral heel bias from the demo's feel are config values.

## Physics model (design level)
State: position, heading, surge/sway velocity, yaw rate, heel (roll), plus wave pitch/heave from the sea state.
- **Wind:** true wind (speed, direction, gusts, shifts) → apparent wind at the rig. Weather presets scale wave height, gusts and shifts.
- **Sails:** per sail, apparent wind at its centre of effort → angle of attack from sail trim angle → CL/CD from a per-sail coefficient table
  (stall, luffing when AoA is negative or too small). Slot effect: a jib in front of the main modifies the main's effective AoA/CL by a configurable factor.
  Force acts at the sail's centre of effort (height and position along the hull) → drive, side force, yaw moment (weather helm) and heeling moment.
- **Hull:** speed-dependent resistance with a hull-speed limit, yaw damping, and added resistance from heel and waves.
- **Foils:** keel/centreboard lateral lift from sideways angle of attack → leeway emerges; rudder lift from speed and rudder angle → yaw moment; stalls at high angle.
- **Heel:** heeling moment vs righting moment (hull form + crew hiking weight, configurable). Heel reduces effective sail area and increases leeway.
- **Waves:** keep the demo's feel (multi-point hull sampling → pitch/heave, slamming, spray). *The wave field itself must be our own
  implementation (e.g. configurable sum of travelling waves driven by the weather preset); the demo's water code is not decoded and not copied.*
- **Failures:** irons (no steerage when head-to-wind and slow), broach (rudder stall + big heel), capsize (heel past `capsize_deg`), rock strike (see "Rock hazards"). Capsize and a hard rock strike end the run
  with the quick-restart prompt.

## Boat config (JSON, runtime)
File: `boats/<name>.json` inside the .pdx; active boat chosen from a menu item. Top-level groups (all values SI, all optional with defaults):
- `hull`: length, beam, mass, resistance curve, hull speed, yaw inertia/damping, righting-moment curve, crew hiking mass, capsize angle.
- `foils`: keel/board area, lift slope, stall angle; rudder area, max angle, lift slope, stall angle.
- `sails[]`: name, area, luff/foot dimensions, centre-of-effort (x,z), trim range, hoist range/reef levels, CL/CD tables vs angle of attack,
  luff/flutter parameters, slot-effect coefficients, wind-angle window where it works (spinnaker), *and render geometry* (shape, camber, mesh size, colours/dither).
- `controls`: wheel turns, crank gain per mode (wheel, trim, hoist, camera), weather-helm feel, anchor timing.
- `look`: hull outline/width table, deck/mast/rigging geometry, wheel/tiller style, panel/compass options.
- `camera`: height, lateral heel bias, `roll_limit_deg` (10), roll/pitch smoothing, yaw range and return rate.
- `world_scale`: `world_units_per_metre`.
Rocks are not part of the boat file. A separate world config holds `rock_cell_size`, `rock_density`, rock size range, `rock_bump_speed`, `rock_wreck_speed`.
A schema document ships with the repo; the built-in dinghy is the same data compiled in.

## First deliverable: the dinghy
~4 m two-sail sloop-rigged dinghy: main + jib, centreboard, one rudder, a hiking crew. Spinnaker as a second config example to prove "more sails".
Target feel: lively, easy to capsize in gusts, clearly punished by bad tacks; numeric starting values are the builder's to tune.

## Waves act on the boat (user-decided)
Waves are physical, not just visual. The hull samples the sea height at a few points (suggest 5–7 along the hull, plus 2 across) **once per frame**, not per
physics substep; the resulting forces are held constant across that frame's substeps. Effects, all scaled by config values:
- pitch and heave from the sampled surface (coupled spring/damper), feeding the camera bob and the sail angle of attack;
- added resistance in waves (steep faces and pitching cost speed);
- slamming when the bow re-enters at speed: impulse, speed loss, spray, camera jolt;
- surfing: a downhill force along a steep wave face adds speed when running with the waves;
- broaching: on a steep face with high speed and big rudder angle or high heel, a yaw moment spins the boat beam-on, which can lead to capsize.
Wave height, period and steepness come from the weather preset. The wave field is our own sum of travelling waves.

## Rock hazards (user-decided; details are my calls)
Rocks replace islands as the thing to steer around. There is no goal; surviving and sailing well is the game.
- **Placement:** our own deterministic, seeded scatter over a cell grid (cell size and density in a world config), generated on demand around the boat and
  dropped when far away. The start area is kept clear. Same seed gives the same rocks, so a restart with the same seed is reproducible.
- **Kinds:** (a) *visible rocks* that stand above the water at all heights; (b) *awash rocks* that wave troughs expose and crests cover, so they are
  dangerous in big seas and visible mainly as breaking white water. Weather changes visibility (mist hides distant rocks).
- **Warning cues:** surf and spray break around rocks, scaled by wave height; rocks are drawn in the 1-bit scene with the same fog as the sea.
- **Collision:** hull footprint against rock circles. Contact removes the inward velocity component and damps speed by the impact speed.
  Below `rock_bump_speed` it is a bump (speed loss, camera jolt, noise). Above `rock_wreck_speed` the run ends (holed). Between them the boat
  takes a heading kick and loses speed. Waves add to impact (heave at the moment of contact). Both thresholds are config values.
- **Cost:** only rocks inside the view and a small collision radius are processed per frame; cap the live count (suggest 64).
- **Out of scope here:** islands, shores, goals, map.

## Calls made after the builder check (mine, for review)
- **Input semantics:** modes are latched (see Controls). Pressing RIGHT or LEFT while already in that mode selects the next sail; UP and DOWN have no sail
  selection. A HUD shows the active mode and selected sail. Camera yaw returns to centre when leaving camera mode.
- **Platform and performance:** target is the Rev-A hardware at 30 fps; the Simulator is for tuning only. Physics substep 60–120 Hz (builder picks by profiling),
  single-precision floats. Profile on device early; if over budget cut rendering detail before physics fidelity.
- **Heel** is a dynamic state (inertia, damping, wave excitation), not quasi-static, so gust-driven capsize and rolling in waves are dramatic.
- **Acceptance for the dinghy:** can sail upwind at roughly 40–50° off true wind; a clean tack takes about 4–8 s and loses noticeable speed; top speed
  near hull speed on a reach in moderate wind; capsizes in sustained gusts if the player ignores heel; recovery by restarting takes under 3 s.
- **Goals:** none for now. The player just sails; a HUD shows speed, heading, apparent wind and heel. Islands, goals and the map are deferred.
  The anchor (`A`) stays; the map (`B`) is deferred. *(My call.)*
- **Weather:** a separate global weather config (not part of the boat file) with four presets (sunny, cloudy, mist, storm) as in the demo's menu; selectable
  from the menu and evolving slowly during a run. Each sets wind base/variability/shift rate/gust, wave height and period, visibility.
- **Schema:** the builder designs it. Tables are arrays of `[x, y]` pairs, angles in degrees, lengths in metres; each field has a documented min/max/default.
  Boat config reloads on a menu action (no live file watching).
- **Reference doc:** `docs/boat-model-analysis.md` is available to the builder; it is incomplete (weather, water, terrain, audio undecoded), and its world/island/goal
  sections are not needed until islands are added.
- **Milestone order:** (1) physics sandbox with debug HUD and a flat sea, (2) config loader, (3) camera and rendering, (4) waves and weather, (5) rock hazards (placement, drawing, collision). Islands, goals and the map are out of scope for now.

## Left to the builder
- Concrete coefficient tables, masses and gains for the dinghy; tuning method (a debug HUD of AoA, CL/CD, heel moment, apparent wind is advisable).
- Wave field implementation and cost; physics substep count.
- Rock placement algorithm and drawing (clean-room, not the demo's island code).
- Rendering: horizon/sea/sky/sail/deck/rocks drawing in 1-bit with dithering, matched to the camera description above.
- Audio: out of scope for the first milestone.
- Project layout and build (C SDK Makefile, simulator + device).
