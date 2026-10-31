# Boat config schema

`boats/<name>.json` is loaded at runtime; `boats/dinghy.json` is generated from the
built-in default (`tools/gen_boat_json.c`) and a test asserts they are identical.
SI units; angles in degrees. Missing fields take the default, out-of-range numbers are
clamped, wrong types are ignored; each raises a warning naming the field
(`hull.mass: -5 outside [5, 20000], clamped`) and the boat still loads. The authoritative
min/max/default table lives in `src/sim/boatcfg.c` (one line per field).

Tables are arrays of `[x, y]` pairs, 2-16 points, x strictly ascending; a bad table is
ignored (default kept) with a warning.

| Group | Fields |
|---|---|
| top level | `world_units_per_metre` |
| `hull` | `length beam mass crew_mass hiking_arm hull_speed resistance(speed→N) heel_drag yaw_inertia yaw_damping heel_inertia heel_damping righting(heel°→N·m) capsize_deg` |
| `foils` | `board_*` (area, lift_slope, stall_deg, cd0, x), `rudder_*` (area, max_deg, lift_slope, stall_deg, x) |
| `sails[]` (1-8) | `name area luff foot ce_x ce_z trim_min_deg trim_max_deg reef_levels reef_frac cl cd luff_aoa_deg luff_drag flutter_hz slot_gain awa_min_deg awa_max_deg shape mesh_w mesh_h camber shade tack_x tack_z` |
| `controls` | `wheel_turns crank_gain_wheel/trim/hoist/camera rudder_rate_deg trim_rate_deg hoist_rate weather_helm anchor_time autopilot_hold_s autopilot_step_deg autopilot_no_go_deg autopilot_crank_deg autopilot_kp autopilot_kd autotrim_hold_s autotrim_smooth_s autotrim_heel_deg autotrim_hoist_min` |
| `look` | `hull_width freeboard mast_x mast_height forestay_x shroud_width wheel_style panel_compass hull_shade cockpit_x0 cockpit_x1 cockpit_w0 cockpit_w1 cockpit_depth` |
| `camera` | `height x_offset far_height frame_margin heel_bias roll_limit_deg roll_smooth pitch_smooth heave_gain pitch_gain yaw_range_deg return_rate` |

`sails`: empty array falls back to the default main+jib; more than 8 keeps the first 8.
Adding a sail (e.g. a spinnaker) is one more entry. `boats/j24.json` (J/24 keelboat, 7.3 m) is main + jib; `boats/tp52.json` (TP52 racer, 15.9 m) adds a spinnaker (`shape` 2) that only draws
force for `awa_min_deg` to `awa_max_deg`; `boats/sunfish.json` (the default boat) is a single large lateen mainsail with the mast well forward. Boat files are read into a 16 KB buffer, so keep them compact.
Righting-moment tables should fold in crew weight for big boats (`crew_mass` is capped at 500).

The cockpit is a four-sided well in the deck: `cockpit_x0`/`cockpit_x1` are its aft/forward ends (m forward of the
boat centre), `cockpit_w0`/`cockpit_w1` the half-width at those ends (so unequal widths give a trapezoid), and
`cockpit_depth` the drop to its floor; a depth of 0 means no cockpit.

Final field ranges for the sail entries, with the camera and control fields most often tuned:

| Field | Range | Notes |
|---|---|---|
| `shape` | 0, 1, 2 | 0 main, 1 headsail, 2 spinnaker (drawing only) |
| `awa_min_deg`, `awa_max_deg` | 0-180 | min must not exceed max, else both reset |
| `mesh_w`, `mesh_h` | 2-16 | drawn mesh lines; renderer caps at 12 wide, 16 high |
| `camber` | 0-0.3 | bulge as a fraction of chord |
| `shade` | 0-1 | 0 white .. 1 black fill |
| `camera.roll_limit_deg` | 0-45 | horizon roll cap; deck still tilts by full heel |

The table in `src/sim/boatcfg.c` stays authoritative if this page and the code ever differ.
