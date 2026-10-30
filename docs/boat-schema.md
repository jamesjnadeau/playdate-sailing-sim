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
| `controls` | `wheel_turns crank_gain_wheel/trim/hoist/camera rudder_rate_deg trim_rate_deg hoist_rate weather_helm anchor_time` |
| `look` | `hull_width freeboard mast_x mast_height forestay_x shroud_width wheel_style panel_compass hull_shade` |
| `camera` | `height x_offset heel_bias roll_limit_deg roll_smooth pitch_smooth yaw_range_deg return_rate` |

`sails`: empty array falls back to the default main+jib; more than 8 keeps the first 8.
Adding a sail (e.g. a spinnaker) is one more entry; see `boats/spinnaker_example.json` (Task 15).
