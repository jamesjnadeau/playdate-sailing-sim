# At the Helm (demo) – boat model analysis

Source: `AtTheHelmDemo.pdx/pdex.dylib` (arm64 simulator build, unstripped, -O0), decoded with a custom
disassembler/lifter. Offsets are into the `Sim` struct (0x2a88 bytes). Confidence: **H** = read directly,
**M** = inferred from lifted code with some lifter garbling, **L** = guess.

## Frame loop
`sim_update(dt)`: advance time (+0x8) → `update_weather` → `update_wind` → controls mode select →
`helm_step` (wheel physics) → sail/trim → speed → heading → position → sea/heave/pitch/roll → events (flags at +0x14 drive audio/hints).

## Controls (H)
- **Wheel** `+0x174` in degrees, clamped ±540 (1.5 turns). Rudder angle `+0x178 = wheel/540 * 32°`.
  `helm_step` adds a physical-feeling wheel: a spring (k=250, c=16, substeps) pulls the crank-driven
  wheel toward a *weather helm* force: `8·sign(windErr)·heelMoment` (reduced as wheel is turned
  with it, zero at 120°), plus gusty noise `(50·leeway + 3·noise)·WX[0x3c]`. Wheel is user-driven by the crank
  while held; when released it eases back (decay 1.2/s).
- **Mode** `+0x28`: 0 default, 1 = trim sail angle with crank, 2 = hoist/reef with crank (button bits in `+0x24`).
- **Sail angle** `+0x17c` (rad, ±80°): `+= crankDelta * 0.00097` in mode 1.
- **Sail hoist** `+0x180` (0..1): `-= crankDelta / 2880` in mode 2.
- **Anchor** `+0x184` states 0 up,1 dropping(1.8s),2 down,3 raising(2.4s); `+0x18c` = anchor drag 0..1.

## Wind (M)
- `+0xc4` wind direction (rad), `+0xc8` target direction; direction shifts by ±`WX[3]` * rand every `WX[2]*(0.8r+0.6)` s,
  turn rate max `max(0.5°, WX[3]/30)` per s. Start 200°.
- `+0xcc` wind speed 0..22: base `+0xd0` (eased, 12 s) + `WX[1]*oscillation(sin 0.05t, 0.13t, 0.31t)` + gust `+0xe4`.
  Gust: rand every ~150±150 s, 10–18 s long, amplitude `base*(0.25–0.5)`, shaped by sin(π·phase).
- Start base wind 7.0. Tutorial pins the wind (flag `+0x29f4`).
- Weather presets (`WX`, 4×16 floats: sunny/cloudy/mist/storm) hold wind base/variation/shift period/shift size,
  sky tones, fog distances, wave scale, etc. Cycle order sunny→mist→cloudy→storm.

## Sail physics (M/H)
- `+0x190` = wrapPi(windDir − heading) → apparent wind angle `a` (|a| = `+0x2c`, side = `±1`).
- **Trim quality** `trim_error`: `err = −side·sail − best(|a|)`, `q = clamp(1 − (err/50°)², 0, 1)`, `best(|a|) = π/2 − clamp(|a|−π/2, ±5°)`.
- "Perfect trim": `|err| < 9° (or 7° if a=0)` and hoist>0.5 and wind>1.5 and q>1.658?; fires chime flag.
- `polar(|a|)`: 13 points at 15°: `[0,0,0,0.2,0.3,0.4,0.6,0.75,0.9,1.0,0.9,0.8,0.7]` (no-go <45°, best at 135°, 0.7 run).
- `sail_target`: `eff = min(1.15·wind, 11)`; drive = `eff · polar(a) · q · hoist`; heel term from `cos(sail)²`; multiplied by `(1+0.15·luffPower)`.
- Power multiplied by `(1 − min(1.3·|turnRate|, 0.35))` (turning costs speed) and `(1 − anchorDrag)`.
- **Boat speed** `+0x10c`: first-order lag to target, time constant 5 s (→0.9 s when anchored). Snaps to 0 when anchored and <0.05.
- Sail luff/flap state `+0x1a8..0x1b0` : spring (180, 14) toward 1−…; drives flap audio/visual (`+0x19c` = sail fill 0..1.3).

## Hull / steering (M)
- **Turn rate** `+0x110` = `rudder · (0.125 + 0.344·v/(v+5)) ·(0.5 if anchored)`, low-passed 1 s. Heading `+0x108 += rate·dt`.
- **Leeway** `+0x114`: target `side · heel(+0x1f0) / (1+|v|/8) · (1−anchor)`, lag 1.5 s. 
- Position `(+0x100,+0x104)` integrates `v` along heading and leeway across it.
- **Heel** `+0x1e8 = v²/100·hoist·(0.75·trimAngle+0.25)·sin|a|`, `+0x1f0 = clamp(0.35·that / 20°)`; heel angle `+0x118/0x11c` is a spring (k=6, c=2.8) toward `side·heel − 0.05·rate·v`.

## Waves (M)
- Sea sampled at 7 points along hull (FZ/FW tables) and fitted with a plane → pitch `0xb4`, heave `0xb8`;
  roll from transverse samples (`0x88-0x8c` / 3.2). Hull-vs-wave slam check using RZ/RW/RH tables (9×2 points),
  sets slam flag `+0x14 |= 0x400`, with `+0x144` intensity. Vertical spring (k=10, c=5) `+0x124/0x128`;
  camera-facing values `+0x150..0x16c`, `+0x164` = camera height (2.6·cos(pitch…)).
- Deck acceleration `+0x13c` computed for slam detection (threshold 4.0).

## Frame order & input (H)
`ship_frame(dt)`: `sim_update` → `world_update` → `onboard_update` → `scene_update` → `audio_update`; then
`map_render` if map shown, `scene_render`, `gfx_begin`, hints, optional debug overlay. dt clamped to [0.005, 0.1]; 30 fps.
Input (`eventHandler`, buttons in `G+0x24`, crank delta in `G+0x18`):
- LEFT = mode 2 (hoist/reef), RIGHT = mode 1 (sail trim), UP = mode 0 (wheel), crank applies to the current mode.
- B toggles the map (crank/buttons zeroed while shown), A toggles anchor. Menu: "weather", "music".

## Camera (H) — the part to keep
`make_cam`: focal length 200, screen centre x=200, **horizon y=145** (400x240 screen).
- Roll (`sim+0x15c`) and pitch (`sim+0x160`) are *screen* roll/pitch; camera height `+0x164` (≈2.6 + heave);
  lateral/back offset `+0x168` (springs toward the heel side, 0.35..1.25) placed along the heading.
- Camera position = boat position + offset·(cos h, −sin h), looking along heading h.
- `w2c`: dx,dy relative to boat; `x = dx·sin h − dy·cos h`, `z = dx·cos h + dy·sin h`, `y = wz − camHeight`.
- `proj`: depth `d = z·cos(pitch) + y·sin(pitch)`; reject d < 0.05; screen = centre + focal·(x,y)/d, rotated by roll;
  pitch also shifts horizon by `200·tan(pitch)`.
- Feeding values: `+0x150` = smoothed 0.35·(heel+roll); `+0x154/0x158` smoothed pitch/heave; `+0x15c = e8 − 0x150`,
  `+0x160 = e4 − 0x154`. In `scene_render`, view roll `= −(+0x150)`, and deck/wheel layers are offset by
  `−200·(+0x154) − 200·(+0x158)/5` (so deck bobs against the world).

## Scene (H)
- Render order: `deck_cover_update`, `make_cam`, `pick_islands`, `sea_render` (heightfield water), sun, clouds,
  `sea_restore_sky`, islands, wind streaks, spray, layer0 (hull/deck), **sail**, layer1, rope/flag, wheel (clip ymax 215→240),
  blit layer, compass, panel. Sea/sky rendered into a separate layer, with per-stage profile timers.
- 48 sea streaks (advected by wind-relative boat velocity, wrap ±70). 24 clouds drift at `0.0015·dt·wind/8`,
  golden-ratio spaced, elevation 4–22°. Spray: 48 particles, rate `v²·0.12·(0.4+WX[0x3c])·3·sprayIntensity`, gravity 230, life 0.5–0.9 s.
- Hull half-width `hullw(z)`: 152-entry table over z=0..15.1 (3.2 for z≤8, rounding to 0 at z=15).

## Sail rendering (M)
A 5×7 grid mesh (5 rows up the sail, 7 across), projected as polys with tone shading:
- Geometry in boat space: boom pivot (6.96, 8.0) → hoist shortens the luff length `0.8 + (1−hoist)·5.26`; width 5.4·(1 − 0.07·sin(πu)).
- Rotated by sail angle `+0x17c`; camber/belly = `sideSign·fill·shape(u,v)` with fill `+0x194`, plus flutter `+0x198·0.4·sin(11t + 4u + 5v)`
  (t = `sim+0x8`); the leech (top row) has an extra `0.08·sin(9v+3t)` flutter.
- Each quad is filled with `gfx_tone` (120..255 shade from lighting/tilt/camber); battens drawn on alternating rows, boom, mast lines.
- Plus a boom/sheet pair of lines, a second pennant/flag polygon (tone 170, 7-point wavy strip, `12·luff+6` long), and a 4-vertex gooseneck polygon (tone 140).
(We will draw our own sail; only the *parameters* matter: luff length, chord, camber, flutter amplitude/frequency.)

## World (M)
- Cells are 300 units square: `cell = floor((c+150)/300)`. Cell (0,0) always exists (start); cells exist by `hash2(cx,cy,7)` and `is_forced`.
- `island_gen(cx,cy)` (seeded by `hash2(cx,cy,99)`): radius R = 20+55·r, centre = cell·300 ± 60 jitter,
  inner radius = R·(0.5r+0.5)+10(?) , 12 radial shore radii `R·(0.8+0.4r)` (lerped around the compass by `island_shore`),
  plus 12 each of two further radius sets (0.5..0.7×, 0.22..0.6× of the second size) used for hills/peaks, and two palm/feature points (±0.12R).
- `world_update`: scans a 13×13 cell window around the boat, keeps up to 48 islands in a cache (180-byte records), drops any >1600 away,
  marks as *discovered* (list of ≤256 cells at `+0x23cc`) any island within visibility distance (`world_vis_end` = 0.8·fog dist, clamped 250..1000, ×0.92)
  and within a view cone `|bearing| < 45° + atan(R/dist)`.
- **Collision**: probe 3.5 units ahead of the boat; if inside `shore(angle)+6`, push out, remove inward velocity component, damp speed ×(1−0.8·min(impact,1)); impact < −1 sets event 0x10 (bump).
- **Goals**: `world_pick_target`: random bearing, distance 350..950 (≥400 early), picks the island there (up to 200 tries, avoids cells already used); arrival when within 150/125/100 of shore (by goal counter) sets `+0x29e8=6` timer and event 0x20, then picks the next target. 15 goals ends the tutorial pinning.

## Not yet decoded
`update_weather`/WX layout, `sea_render`/water height field, `draw_island` (terrain shading), `draw_deck/mast/bulwark/wheel`, `draw_compass`, `draw_panel_dynamic`, audio mixing.
The lifter is garbled in places (call args, libm stubs); constants above are read directly from the listings, structure inferred.
Tools/lifts live in the session scratchpad (`adis.py`, `lift.py`, `L/*.c`).

## Parameters to expose in our boat config
polar table, no-go angle, best trim curve & tolerance, speed time constant, hull max speed (wind clamp 11), turn gain/speed curve,
leeway gain, heel gain/limit/spring, rudder max angle, wheel turns, weather-helm gain, sail areas/hoist, anchor timings, wave response tables.
