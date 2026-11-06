# Journey weather shifts — design

Written from the brainstorm conversation of 2026-11-06.

## Intent
A journey is endless, and in one fixed weather it gets samey. Instead the player chooses which weathers
are allowed, and during the journey the weather slowly drifts between them, one notch of wind at a
time, with a heads-up and a gentle 10 s change so it feels like weather building and easing, not a
scene cut.

## Goals / non-goals / anti-goals
- Goals: multi-select weather screen for Journey; periodic one-step weather shifts during a journey,
  announced, blended over 10 s; gap and blend times configurable.
- Non-goals: changing Race (keeps its single pick: ghosts are saved per weather), Free Range (keeps the
  system-menu weather picker), Training; saving the ticks across launches; a HUD weather readout beyond
  the announcement; wind *direction* changes on a shift.
- Anti-goals: waves visibly jumping or snapping at any point of a shift; the wind spinning round; a
  sunny-to-storm leap; a noticeable frame-rate drop outside the 10 s blend.

## Behaviour (as decided)
### Weather screen (Journey only)
```
  Journey · Laser
  Choose weathers
  [ ] mist
  [x] sunny
  [x] cloudy
  [x] choppy      <
  [x] rollers
  [x] storm
  [ ] hurricane
  ▶ Set sail
```
- One row per weather (lightest wind first, as now) with a tick box, then a "Set sail" row.
- A on a weather row toggles its tick. A on "Set sail" starts the journey; it does nothing when no
  weather is ticked. B steps back as today. Up/down move through all rows (the list scrolls as now).
- Default ticks: everything except mist and hurricane. Ticks are remembered while the game runs
  (like the single weather pick is today), not saved to disk.
- The race path through the same screen is unchanged (single pick, A starts the race).

### During a journey
- The first weather is a random ticked one (James's call was neighbours-only shifting; the random
  start is my call, so journeys differ from the first minute).
- After a random gap in [`journey_shift_min_s`, `journey_shift_max_s`] (default 60..180 s), the weather
  shifts **one step** up or down the wind order (mist < sunny < cloudy < choppy < rollers < storm <
  hurricane), skipping unticked weathers. Up or down is a coin flip when both exist; at either end of
  the ticked set it can only go one way. With one weather ticked it never shifts.
- On a shift the HUD shows "Weather turning: <name>" for the whole blend, in the praise message slot
  (my call: reuses a banner players already know). If praise is showing, the weather note wins.
- Over `weather_blend_s` (default 10 s) everything fades from the old weather to the new: wind mean,
  noise, gusts, direction-wander rate, fog, sky, clouds, rain, lightning, sun, and the sea. The wind's
  base direction and the wave directions carry over unchanged.
- The next gap is timed from the end of the blend.
- Restarting the journey (capsize, restart) re-rolls the start weather and timer. All randomness comes
  from the game seed, so a journey is reproducible for tests and scenarios.

### Config
In `config/world.json` beside the other journey settings (my call: that is where journey tuning lives):
`journey_shift_min_s: 60`, `journey_shift_max_s: 180`, `weather_blend_s: 10`. Loaded and clamped like
the other `WorldCfg` fields; min > max is swapped or clamped like `journey_min_m`/`journey_max_m`.

## Approaches considered (for the sea, the only real fork)
Everything except the sea blends as plain numbers. For the waves:
- **1. Stretch each wave into its partner, anchored at the boat (chosen).** Every preset builds the
  same 6-component layout, so each old component morphs into the matching new one: length, speed,
  direction and height. No extra cost; the sea visibly "stretches" out from the boat for 10 s
  (distant crests slide more than near ones). Build this first and judge it on the device.
- 2. Overlap the two seas: old components fade out while new ones fade in. Looks like a real sea
  changing, but up to 2x wave maths for 10 s. **Fallback** if the stretch looks wrong on the device.
- 3. Fade the old sea nearly flat, swap, grow the new one. No extra cost, but a strange lull mid-shift.
  Rejected.

Stretching was first ruled out because components are locked to integer wave numbers on `SEA_WRAP`,
anchored at the world origin, and the boat can be ~5 km from it: stretching there makes crests race.
James pushed back; anchoring the stretch at the boat removes that problem.

## Design
### Weather state in `Game`
- `Game` gains: the tick mask, `from`/`to` preset indices, blend progress, time to the next shift, the
  announcement timer, and a **current blended `WeatherPreset`** recomputed each frame (field-wise lerp
  of from and to; equals the preset itself when not blending).
- Every reader of `g->weather.preset[g->preset]` switches to the blended preset via one accessor
  (`render.c` `wx`/`fvis`, the sea draw at ~625, `journey_cull` in `app.c`, `wind_step` in
  `game_update`). Outside Journey the blended preset is just the chosen preset, so other modes behave
  exactly as today.
- Fields that don't lerp linearly (my calls, builder may refine):
  - `lightning_interval` and `gust_interval`: blend the *rate* (1/interval, 0 meaning none), so a 0 → 20
    blend does not pass through near-constant lightning.
  - `sun` (0/1): fade if the renderer can, else switch at the midpoint.
- Wind: keep the one `Wind`; each frame set its mean (`base`) from the blended `wind_base` and step it
  with the blended preset. `dir0` is never touched, so direction carries over.
- The shift logic (timer, neighbour pick, blend progress) lives in sim code, unit-testable without the
  Playdate, alongside `game_update`. Only Journey runs it.

### Sea
- Both the old and the new sea are built with `sea_init` from the same seed, so component i of each
  has the same role (harmonic, spread direction, phase seed). Component i morphs into component i.
- **At blend start**, re-anchor each component at the boat's position (xb, yb): store its current
  phase there, φb = k·(xb, yb) − ω·t + φ (computed in double with the existing wrap). During the
  blend the height at a point is Σ amp·cos(k·(x − xb, y − yb) + φb), where k, ω and amp are lerped
  from old to new and φb is advanced by −ω·dt each frame (integrated, not ω·t, so a changing ω
  never jumps the phase). Offsets from the boat are small, so floats stay precise without the wrap.
- Lerp k as a vector (old (m, n) to new (m, n) in floating point), not angle and length separately.
- **At blend end**, k and ω equal the new sea's exactly. Convert back to the world-anchored form by
  solving for φ: φ = φb − k·(xb, yb) + ω·t (double, wrapped), and switch to the new `Sea` with those
  phases, so the switch is seamless.
- `recentre` during a blend shifts the anchor with the boat.
- Same-length shifts (rollers ↔ storm) come out as pure height scaling with no special case.
- Time: callers currently fold time by `g->sea.period`. During a blend there is no single period;
  the sea API needs a form that does not depend on that fold (e.g. the sea owns its time/phase state
  and callers pass `g->t` or nothing). Builder's call, constrained by: no jump at blend start, end, or
  across a recentre; the existing precision far from the origin is kept outside blends.
- `seasound.c` (amp sum, period) and the render's dominant-wave and shading use (`render.c` ~624-631)
  follow the lerped values.
- **My call, after James asked "why not keep the boat at 0,0?":** the world stays world-anchored
  (`recentre` unchanged; buoys, trail, wake, race marks and ghosts all live in world coordinates), but
  the *sea* may keep a boat-following anchor for the whole journey, not only during blends: one code
  path within Journey, no end-of-blend switch back. If that proves awkward, anchoring only during a
  blend (as described above) is equally acceptable.
- **Journey only (my call, from the builder check):** Race, Free Range and Training keep today's
  world-anchored sea path bit-for-bit. Race relies on `ghost_seed` making wind and sea repeat exactly,
  and the integrated phase depends on frame-time history, so it must not reach those modes. Existing
  scenario goldens must not change.
- If the stretch looks wrong on the device, swap to approach 2 (overlap); the rest of the design is
  unchanged by that swap.

### Menu
- `Menu` gains a tick mask for the journey weather screen; `menu_rows` is weathers + 1 on that screen
  when `journey`; `menu_update` toggles on A or returns `MENU_START_JOURNEY` on "Set sail" with at
  least one tick. `menu_draw.c` draws tick boxes and the "Set sail" row for journey.
- `app.c` keeps the mask across menus (default: all but mist and hurricane) and passes it into the
  game when the journey starts.
- **After a journey (my call):** the journey's weather lives only in `Game`. It never writes back to
  `preset_index`, so the Race weather screen's starting row, ghost file names and the Free Range
  system-menu tick keep the last single-picked weather. Starting Free Range or a race after a journey
  re-applies `preset_index` (today `MENU_START_FREE` doesn't, so add `game_set_preset` there), so no
  journey weather or half-finished blend leaks into another mode. During a journey `g->preset` holds
  the blend's target weather; readers go through the blended-preset accessor.

## Testing
- `test_menu.c`: toggling, default mask, "Set sail" refused with none ticked, race path unchanged.
- `test_game.c` / new tests: neighbour pick skips unticked and respects ends; single-tick never shifts;
  timer within [min, max]; blended preset equals endpoints at w = 0 and 1; lightning rate blend.
- `test_sea.c`: sea height is continuous (frame to frame) across blend start, mid-blend, blend end and
  a recentre during a blend, including far (~5 km) from the origin; at blend end it matches the new
  sea; a same-length shift equals the old sea with lerped height.
- `test_weather.c`/`test_rocks.c`: new world fields load and clamp.
- On device: watch a choppy ↔ rollers blend and judge whether the stretch looks acceptable.

## Left to the builder
Exact struct and function names; the time-fold method for two periods; whether `sun` fades; message
wording beyond "Weather turning: <name>"; tick-box glyphs.
