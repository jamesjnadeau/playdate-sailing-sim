# Journey Weather Shifts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In Journey the player ticks which weathers are allowed, and every 1–3 minutes the weather moves one notch to a ticked neighbour. The change is announced and blended over 10 s, waves included.

**Architecture:** A pure sim module (`weathershift`) owns the tick mask, the timers and the neighbour pick. `game.c` turns its state into a blended `WeatherPreset` (`g->wx`) that every reader uses. For the length of a blend the sea switches into a boat-anchored form, where wave vectors, speeds and heights morph from the old sea to the new. At the end of the blend it converts back to today's world-anchored integer form. Outside a blend, and in every mode except Journey, the code takes exactly today's path, so Race ghosts and the scenario goldens don't change.

**Tech Stack:** C99, Playdate SDK, the host test suite in `tests/` (`make -C tests`, `t.h` asserts).

**Spec:** `docs/superpowers/specs/2026-11-06-journey-weather-shifts-design.md`

## Global Constraints

- Race, Free Range and Training must stay bit-for-bit as today. `make -C tests` must pass with **no golden or baseline changes**, `journey_start` scenario included.
- Wind order (lightest first): mist < sunny < cloudy < choppy < rollers < storm < hurricane, which is the existing `WEATHER_*` enum order.
- Default ticks: everything except mist and hurricane. Ticks are kept for the session only and never written to disk.
- Config lives in `config/world.json`: `journey_shift_min_s: 60`, `journey_shift_max_s: 180`, `weather_blend_s: 10`.
- Shifts move one step to the nearest ticked weather up or down. Use a coin flip when both directions exist and the only available direction at an end. With one weather ticked there is never a shift.
- The next gap is timed from the **end** of a blend.
- The HUD text during a blend is `Weather turning: <name>` in the praise slot, and it wins over praise.
- The wind's `dir0` and the wave directions are never changed by a shift.
- `src/app/app.c`, `src/pd/hud.c`, `src/pd/main.c`, `src/pd/render.h`, `README.md`, `config/weather.json` and `tools/*` carry **uncommitted user changes**. Never commit those changes. For `app.c` and `hud.c`, stage only your own hunks with `git add -p`.
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Capsize or restart mid-blend:** expected a clean settled start in a freshly rolled weather, with no anchored sea and no half blend. Test in Task 5.
2. **Leaving a journey mid-blend for Free Range or Race:** the anchored sea, the blended `wx` and the journey weather must not leak. Test in Task 5 (game side) and Task 8 (app re-applies `preset_index`).
3. **A recentre during a blend** (boat crosses ±5·1024 m): the sea must not jump. Test in Task 5.
4. **Long frames** (`dt` clamped to 0.1 s) at the blend's end: progress must not overshoot past 1 into odd weights, and the final weather must equal the preset exactly. Test in Task 3.
5. **Menu edge cases:** with nothing ticked, Set sail does nothing. Backing out and coming back keeps the ticks. Toggling never changes the race's single pick (`m->weather`). Test in Task 7.

---

### Task 1: World config fields

**Files:**
- Modify: `src/sim/rocks.h:19` (WorldCfg), `src/sim/rocks.c:20-32,66-69` (field table, DEFAULTS, min/max fix-up)
- Modify: `config/world.json`
- Test: `tests/test_rocks.c`

**Interfaces:**
- Produces: `WorldCfg.journey_shift_min_s`, `.journey_shift_max_s`, `.weather_blend_s` (float, seconds). They are appended after `journey_max_m`, in that order.

- [ ] **Step 1: Write the failing test** `test_weather_shift_fields_load_and_clamp`:
  - The defaults are `world_default` → 60, 180, 10.
  - Loading `{"journey_shift_min_s": 1, "weather_blend_s": 999}` returns false (warns). The min clamps to 5 and the blend clamps to 120.
  - Loading `{"journey_shift_min_s": 200, "journey_shift_max_s": 100}` returns false and gives a max of 200 (raised to the min, like `journey_max_m`).

  `test_shipped_config_equals_default` already covers the JSON.
- [ ] **Step 2: Run** `make -C tests build/test_rocks && tests/build/test_rocks`. Expected: compile FAIL (no such member).
- [ ] **Step 3: Implement.**
  - Add `F(journey_shift_min_s, 5, 3600)`, `F(journey_shift_max_s, 5, 3600)` and `F(weather_blend_s, 1, 120)`.
  - Extend DEFAULTS with `60.0f, 180.0f, 10.0f`.
  - Add the max < min warning and raise, the same as the journey_m pair.
  - Add the three keys to `config/world.json`.
- [ ] **Step 4: Run** `make -C tests`. Expected: all ok.
- [ ] **Step 5: Commit** `src/sim/rocks.h src/sim/rocks.c config/world.json tests/test_rocks.c` with the message "World config: journey weather shift gap and blend times".

### Task 2: Preset blend and the default journey mask

**Files:**
- Modify: `src/sim/weather.h`, `src/sim/weather.c`
- Test: `tests/test_weather.c`

**Interfaces:**
- Produces:
  - `#define WEATHER_JOURNEY_DEFAULT (((1u << WEATHER_PRESETS) - 1u) & ~((1u << WEATHER_MIST) | (1u << WEATHER_HURRICANE)))`
  - `void weather_blend(WeatherPreset *out, const WeatherPreset *a, const WeatherPreset *b, float w);`
    - w is clamped to [0, 1]. All float fields lerp, except that `lightning_interval` and `gust_interval` blend as a rate (1/x, where 0 means none, so 1/0 counts as 0). The output is 0 when the blended rate is 0.
    - `name` is a's when w < 0.5 and b's otherwise.
    - `sun` lerps. The renderer's existing `sun > 0.5f` test makes it switch at the midpoint.

- [ ] **Step 1: Write the failing tests:**
  - `test_blend_endpoints_exact`: for every preset pair, `memcmp` of the output equals a at w = 0 and b at w = 1. Also w = −1 → a and w = 2 → b.
  - `test_blend_lightning_as_rate`: a.lightning_interval = 0, b = 20, w = 0.5 → 40. Both 0 → 0.
  - `test_blend_gust_as_rate`: 10 and 30 at w = 0.5 → 15.
  - `test_blend_midpoint_linear`: wind_base, wave_period and visibility at w = 0.25 equal `a + 0.25*(b-a)`.
  - `test_journey_default_mask`: bits for sunny, cloudy, choppy, rollers and storm are set, and mist and hurricane are clear.
- [ ] **Step 2: Run** `make -C tests build/test_weather && tests/build/test_weather`. Expected: compile FAIL.
- [ ] **Step 3: Implement** `weather_blend` in `weather.c`. Use exact copies at the endpoints (`*out = w <= 0 ? *a : *b`) so the endpoint test holds bit-for-bit.
- [ ] **Step 4: Run** `make -C tests`. Expected: all ok.
- [ ] **Step 5: Commit** with the message "Weather: blend two presets, journey default mask".

### Task 3: Weather shift scheduler

**Files:**
- Create: `src/sim/weathershift.h`, `src/sim/weathershift.c` (both Makefiles pick up `src/sim/*.c` automatically)
- Test: `tests/test_weathershift.c`

**Interfaces:**
- Consumes: `hash2`, `hashf` (mathx.h), `WEATHER_PRESETS`.
- Produces:
```c
typedef enum { WSHIFT_NONE, WSHIFT_START, WSHIFT_END } WShiftEvent;
typedef struct {
    uint32_t mask;      /* bit per preset allowed; never 0 after init */
    uint32_t seed, n;   /* every draw is hashf(hash2(salt, n++, seed)) */
    int from, to;       /* presets; equal when settled */
    float progress;     /* 0..1 through the blend; 1 when settled */
    float gap;          /* s until the next shift starts (settled only) */
} WeatherShift;
/* Nearest ticked preset above / below cur, picked by coin (0..1) when both exist; -1 when neither. */
int wshift_neighbour(uint32_t mask, int cur, float coin);
/* mask & valid bits == 0 -> mask = 1u << fallback. Picks a random ticked start (fallback itself when only it
   is ticked, with no draw), settles there, rolls the first gap. Returns the start preset. */
int wshift_init(WeatherShift *s, uint32_t mask, int fallback, uint32_t seed, float gap_min, float gap_max);
/* Settled: gap -= dt; at <= 0 picks the neighbour and returns WSHIFT_START (one tick -> re-rolls gap, NONE).
   Blending: progress += dt / blend_s, clamped to 1; on reaching 1 settles (from = to), rolls the gap, returns END. */
WShiftEvent wshift_step(WeatherShift *s, float dt, float gap_min, float gap_max, float blend_s);
/* Eased blend weight: smoothstep(progress) — 0 at the start, 1 when settled. */
float wshift_weight(const WeatherShift *s);
```
The gap is `gap_min + (gap_max - gap_min) * hashf(...)`.

- [ ] **Step 1: Write the failing tests** in `tests/test_weathershift.c`, using the existing `t.h` pattern:
  - `test_neighbour_skips_unticked`: mask {sunny, choppy, storm}, cur choppy. coin 0.1 → sunny, coin 0.9 → storm (pick one convention: coin < 0.5 goes down).
  - `test_neighbour_ends_go_one_way`: cur sunny → choppy whatever the coin, cur storm → choppy.
  - `test_neighbour_single_tick_none`: mask {cloudy}, cur cloudy → −1.
  - `test_init_start_is_ticked_and_varies`: over seeds 1..50 every start is in the mask and at least 2 distinct starts appear.
  - `test_init_empty_mask_uses_fallback`: mask 0, fallback 3 → returns 3, mask == 1<<3.
  - `test_gap_within_range`: over 200 seeds the gap after init and after each END lies in [60, 180].
  - `test_single_tick_never_shifts`: step 1/30 for 1000 s → never START.
  - `test_blend_lifecycle`: min = max = 5, blend 2. The first START comes within 5 ± 1/30 s, at which point from ≠ to and both are ticked neighbours. END comes after 2 s more, then from == to, progress == 1, and the next START is 5 s after END.
  - `test_long_frames_clamp_progress` (Review Focus 4): during a blend, steps of dt 0.1 with blend 0.25 never give progress > 1, `wshift_weight` stays in [0, 1], and END fires exactly once.
- [ ] **Step 2: Run** `make -C tests build/test_weathershift`. Expected: FAIL (missing header).
- [ ] **Step 3: Implement** `weathershift.c` to the contracts above. Use distinct `hash2` salts for the start, gap and coin draws.
- [ ] **Step 4: Run** `make -C tests`. Expected: all ok.
- [ ] **Step 5: Commit** with the message "Weather shift scheduler: ticked-neighbour shifts with gap and blend timing".

### Task 4: Boat-anchored sea and morph

**Files:**
- Modify: `src/sim/sea.h`, `src/sim/sea.c` (`height`, `sea_view_init`, `base_phase`, plus new functions)
- Modify: `src/pd/render.c:290-301`, to delete `dominant_wave` and call `sea_dominant`
- Test: `tests/test_sea.c`

**Interfaces:**
- Produces new `Sea` fields, all zero after `sea_init`, and new functions:
```c
    bool anchored;                    /* phases held at (ax, ay); t is ignored while set */
    double ax, ay;                    /* anchor, world m */
    float kx[SEA_MAX_COMPONENTS], ky[SEA_MAX_COMPONENTS];   /* rad/m, used while anchored */
    double phb[SEA_MAX_COMPONENTS];   /* phase at the anchor, wrapped to [0, 2pi) */

/* World form -> anchored at (x, y) at time t; heights are unchanged. */
void sea_anchor(Sea *s, double x, double y, double t);
/* Moves the anchor (phb follows k·delta, in double); heights are unchanged. */
void sea_move_anchor(Sea *s, double x, double y);
/* The world origin moved by (dx, dy) (recentre): anchor -= (dx, dy), phases untouched. */
void sea_shift_anchor(Sea *s, double dx, double dy);
/* Anchored only: phb -= omega * dt. */
void sea_advance(Sea *s, float dt);
/* Anchored only: k (as a vector), omega, amp and period lerp from a to b by w; phb untouched.
   a and b must be sea_init'ed with the same seed (component i is the same wave). */
void sea_morph(Sea *s, const Sea *a, const Sea *b, float w);
/* Back to world form as `target` (m, n, omega, amp, period, gains), with phases solved so heights
   at time t are unchanged: phase = phb - 2pi·fmod(m·xw + n·yw, WRAP)/WRAP + fmod(omega·t, 2pi). */
void sea_unanchor(Sea *s, const Sea *target, double t);
/* Dominant component: unit travel direction and wavelength (m); false on an empty sea. Uses kx/ky when anchored. */
bool sea_dominant(const Sea *s, float *nx, float *ny, float *wl);
```
- While anchored, the phase at (x, y) is `phb[i] + kx·(x − ax) + ky·(y − ay)`, computed in double, in `height`, `base_phase` and `sea_view_init`. The `t` argument is ignored, so callers keep passing `fmod(g->t, period)` unchanged. When `anchored` is false every function takes exactly today's code path.

- [ ] **Step 1: Write the failing tests** in `tests/test_sea.c`, with tolerance 2e-3 m unless stated:
  - `test_anchor_preserves_heights_far_from_origin`: a choppy sea, seed 9, at (5123.7, −4096.2), t = 37.3. Heights from `sea_height`, `sea_view_height` and the `sea_hull_forces` heave at offsets up to 50 m match before and after `sea_anchor`.
  - `test_move_anchor_preserves_heights`: move the anchor by (37, −12) and expect the same heights.
  - `test_advance_matches_world_time`: anchor at t, then 300 × `sea_advance(1/30)`. Heights equal the un-anchored sea at t + 10.
  - `test_unanchor_roundtrip`: as above, then `sea_unanchor(&s, &orig, t + 10)`. `anchored` is false, m, n, omega and amp equal orig's, and heights equal orig at t + 10.
  - `test_morph_continuous_far_from_origin`:
    - a = choppy, b = rollers, same seed, anchored at (5000, 5000).
    - 300 frames of dt 1/30: `sea_morph(w = smoothstep(k/300))`, `sea_move_anchor` at 2 m/s, `sea_advance`.
    - At the boat and at 40 m ahead, the frame-to-frame height change stays ≤ 0.15 m.
    - Then `sea_unanchor(b)`: the height change across the switch is ≤ 2e-3.
  - `test_morph_same_length_scales_height`: b = a with every amp × 2. At w = 0.5 the anchored heights are 1.5 × a's.
  - `test_dominant_anchored_matches_world`: `sea_dominant` gives the same direction and length before and after `sea_anchor`.
- [ ] **Step 2: Run** `make -C tests build/test_sea && tests/build/test_sea`. Expected: compile FAIL.
- [ ] **Step 3: Implement** the API in `sea.c` and move `dominant_wave` out of render.c as `sea_dominant`. Wrap `phb` with `fmod(…, 2π)` after every update so it stays small.
- [ ] **Step 4: Run** `make -C tests` (every golden must be unchanged) and `make` (the Playdate build compiles render.c). Expected: all ok, build ok.
- [ ] **Step 5: Commit** `src/sim/sea.[ch] src/pd/render.c tests/test_sea.c` with the message "Sea: boat-anchored form that morphs between two seas".

### Task 5: Game integration

**Files:**
- Modify: `src/sim/game.h`, `src/sim/game.c` (`start_weather`, `reset`, `recentre`, `game_update`, new functions)
- Test: `tests/test_game.c`

**Interfaces:**
- Consumes:
  - `WeatherShift` / `wshift_*` (Task 3)
  - `weather_blend`
  - the `sea_*` anchor API (Task 4)
  - the WorldCfg fields (Task 1)
- Produces:
```c
/* in Game */
    uint32_t journey_weathers;  /* ticks for the next journey start; 0 = just `preset` */
    WeatherShift shift;         /* used when mode == GAME_JOURNEY */
    WeatherPreset wx;           /* the weather in force: preset[preset], or the blend during a shift */
    Sea sea_from, sea_to;       /* the blend's endpoint seas */

void game_set_journey_weathers(Game *g, uint32_t mask);   /* stored; applies at the next journey (re)start */
const WeatherPreset *game_weather(const Game *g);        /* &g->wx — every reader uses this */
int game_weather_turning(const Game *g);                  /* the target preset during a blend, else -1 */
```
- Behaviour:
  - `start_weather`:
    - In Journey, call `g->preset = wshift_init(&g->shift, g->journey_weathers, g->preset, g->seed, min, max)` before reading the preset.
    - In all modes, then set `g->wx = preset[g->preset]`. `wind_step` uses `&g->wx`, so other modes get identical values.
  - Journey only, at the top of `game_update`, call `wshift_step`:
    - **START:** `sea_init` both `sea_from` (from) and `sea_to` (to) with `g->seed`, then `sea_anchor(&g->sea, b.x, b.y, g->t)` and set `g->preset = to`.
    - **While blending:** set `g->wx = weather_blend(from, to, wshift_weight)`, call `sea_morph(&g->sea, &sea_from, &sea_to, weight)` and `sea_move_anchor(boat)`, and set `g->wind.base = g->wx.wind_base`.
    - **END:** call `sea_unanchor(&g->sea, &g->sea_to, g->t)`, set `g->wx = preset[to]` and `g->wind.base = wx.wind_base`.
  - Next to `g->t += dt`: `if (g->sea.anchored) sea_advance(&g->sea, dt)`. The anchored phase always means "at time g->t".
  - `recentre`: `if (g->sea.anchored) sea_shift_anchor(&g->sea, dx, dy)`.
  - Restarts and mode changes go through `reset` → `start_weather` → `sea_init` (which clears `anchored`) and `wshift_init`, so nothing survives a restart.

- [ ] **Step 1: Write the failing tests** in `tests/test_game.c`. Use a WorldCfg with shift min = max = 5 and blend 2 unless stated.
  - `test_journey_start_weather_from_ticks`: mask {sunny, storm}. Over seeds 1..30, `g.preset` is always one of them, both appear, and `game_weather_turning` is −1 at start.
  - `test_journey_mask_zero_keeps_preset`: no `game_set_journey_weathers`, preset choppy → still choppy after `game_set_journey`, and `g.wx` equals preset[choppy] (memcmp).
  - `test_journey_shift_blends_and_settles`:
    - Mask {cloudy, choppy}, stepping 1/30 with no input.
    - Turning is −1 before 5 s and equals the other weather after.
    - Mid-blend `wx.wind_base` lies strictly between the two, `g.sea.anchored` is true, and `g.wind.dir0` is unchanged throughout.
    - By 7.2 s turning is −1, `g.preset` is the target, `wx` memcmp-equals preset[target], `sea.anchored` is false, and `wind.base` equals target wind_base.
    - Over the whole run, the frame-to-frame `|Δ g.wf.heave|` stays ≤ 0.3 m.
  - `test_restart_mid_blend_is_clean` (RF1): start a blend, then `game_restart(&g, 77)`. Expected: turning −1, not anchored, `wx` equals preset[g.preset], and preset is in the mask.
  - `test_leaving_journey_mid_blend` (RF2): mid-blend, `game_set_preset(&g, WEATHER_SUNNY)` then `game_set_track(&g, NULL)`. Expected: mode FREE, not anchored, `wx` equals preset[sunny], and no shift over 20 s.
  - `test_recentre_during_blend` (RF3): boat at x = 5119, u = 5 m/s, heading +x, then blend. The boat crosses 5120 and recentres mid-blend, and `|Δ heave|` per frame stays ≤ 0.3 m.
  - `test_race_weather_untouched`: in race mode, `wx` equals the preset every frame and `game_weather_turning` is −1 for 400 s with a nonzero `journey_weathers`.
- [ ] **Step 2: Run** `make -C tests build/test_game && tests/build/test_game`. Expected: compile FAIL.
- [ ] **Step 3: Implement** as described under Interfaces.
- [ ] **Step 4: Run** `make -C tests`. Expected: all ok. Every golden and the baseline are unchanged, including `journey_start`, which uses mask 0.
- [ ] **Step 5: Commit** with the message "Game: journey weather shifts with a blended preset and morphing sea".

### Task 6: Readers and the HUD banner

**Files:**
- Modify: `src/pd/render.c:113-114` (`fvis` and `wx` return `game_weather(g)`), `src/pd/render.c:625` (`wp = game_weather(g)`)
- Modify: `src/app/app.c:302` (visibility via `game_weather(g)`); stage the hunk with `git add -p`
- Modify: `src/pd/hud.c:215-218` (`hud_praise`); stage with `git add -p`

**Interfaces:**
- Consumes: `game_weather`, `game_weather_turning` (Task 5), `WEATHER_NAMES`.
- `seasound.c` and `camera.c` need no change, because `sea_morph` keeps `sea.c[i].amp` and `sea.period` lerped and the anchored sea ignores `t`.

- [ ] **Step 1: Confirm no other readers remain.** Run `grep -n "preset\[g->preset\]\|preset\[game.preset\]" src/pd src/app src/sim/camera.c src/sim/seasound.c`. Expected: no hits outside `start_weather`/`game.c` after the edits.
- [ ] **Step 2: Implement.** `hud_praise` shows `Weather turning: <WEATHER_NAMES[i]>` with `message(pd, buf, 26)` when `game_weather_turning(g) >= 0`, and the praise otherwise.
- [ ] **Step 3: Build** with `make` and `make -C tests`. Expected: build ok, all tests ok.
- [ ] **Step 4: Commit** only these hunks, with the message "Render, HUD: read the blended weather; announce weather turning".

### Task 7: Menu multi-select

**Files:**
- Modify: `src/sim/menu.h`, `src/sim/menu.c` (`menu_rows`, `menu_row_enabled`, `enter_weather`, the weather branch of `update`)
- Modify: `src/pd/menu_draw.c:383-388`
- Test: `tests/test_menu.c`

**Interfaces:**
- Produces:
```c
/* in Menu */
    uint32_t weather_mask;   /* journey weather screen: bit per ticked weather row */
void menu_set_weather_mask(Menu *m, uint32_t mask);
```
- Behaviour, on the weather screen with `journey` set:
  - Rows are `nweather + 1`; the last row is "Set sail". The screen opens with `sel` on "Set sail".
  - A on a weather row toggles its bit and returns MENU_NONE.
  - A on "Set sail" returns MENU_START_JOURNEY if `weather_mask & ((1u << nweather) - 1)` is nonzero, and MENU_NONE otherwise. `menu_row_enabled` is false for "Set sail" when nothing is ticked.
  - `m->weather` is never written on this screen.
  - The race path is unchanged.
- Drawing:
  - Labels are `"[x] sunny"` / `"[ ] mist"` and `"Set sail"`.
  - The title is "Choose weathers".
  - The hint bar shows A as `"TICK"` on a weather row and the existing `go` on Set sail.

- [ ] **Step 1: Write the failing tests:**
  - `test_journey_weather_rows_and_start_row`: nweather 7, journey → `menu_rows` is 8 and `sel` is 7 on entry.
  - `test_journey_weather_toggle`: A on row 0 sets bit 0, a second A clears it, and the return is MENU_NONE.
  - `test_set_sail_refused_when_none_ticked` (RF5): mask 0 → A on Set sail returns MENU_NONE and the row is disabled. Tick one → MENU_START_JOURNEY.
  - `test_ticks_survive_back_and_reenter` (RF5): toggle, B to boats, A back in → same mask.
  - `test_journey_toggle_leaves_race_pick` (RF5): `m->weather` is unchanged after toggles.
  - `test_race_weather_path_unchanged`: not journey → rows equal nweather and A returns MENU_START_RACE with `m->weather = sel`. Existing tests also cover this.
- [ ] **Step 2: Run** `make -C tests build/test_menu && tests/build/test_menu`. Expected: FAIL.
- [ ] **Step 3: Implement** `menu.c`, then `menu_draw.c`. Use static label buffers sized for `WEATHER_PRESETS + 1` rows.
- [ ] **Step 4: Run** `make -C tests` and `make`. Expected: all ok, build ok.
- [ ] **Step 5: Commit** with the message "Menu: journey weather screen ticks weathers, Set sail starts".

### Task 8: App wiring and the on-device check

**Files:**
- Modify: `src/app/app.c` (add a static, plus the hunks at lines ~256, ~355 and ~372). Stage with `git add -p`.

**Interfaces:**
- Consumes: `menu_set_weather_mask`, `Menu.weather_mask` (Task 7), `game_set_journey_weathers` (Task 5), `WEATHER_JOURNEY_DEFAULT` (Task 2).

- [ ] **Step 1: Implement** these changes in `app.c`:
  - Add `static uint32_t journey_weathers = WEATHER_JOURNEY_DEFAULT;`, and call `menu_set_weather_mask(&menu, journey_weathers)` beside `menu_set_weather`.
  - **MENU_START_JOURNEY:**
    - Set `journey_weathers = menu.weather_mask`.
    - Don't touch `preset_index`, and drop the `else game_set_preset`.
    - Keep `if (new_boat) begin(true);`, then call `game_set_journey_weathers(&game, journey_weathers); game_set_journey(&game);`.
  - **MENU_START_FREE:** call `game_set_preset(&game, preset_index);` before `game_set_track(&game, NULL);` (RF2).
- [ ] **Step 2: Build and run.** Run `make`, `make -C tests` and `make -C web test`. Expected: all ok. The web parity test exercises the same sim.
- [ ] **Step 3: Check on the simulator or device.**
  - Journey → defaults show sunny..storm ticked. Untick all, and Set sail is dimmed and does nothing.
  - Tick only choppy and rollers, and set `journey_shift_min_s = journey_shift_max_s = 15` in a local, uncommitted `config/world.json`. Sail and watch a blend: the banner shows for about 10 s, the wind builds, and the waves stretch with no snap at the start or end.
  - Back out to Free Range: the weather is the last single-picked one, and there's no banner.
  - Restore `world.json`. If the stretch looks wrong, report it to James: the agreed fallback is overlapping the two seas (spec, approach 2).
- [ ] **Step 4: Commit** only the hunks from this task, with the message "App: journey weather ticks persist for the session; Free Range re-applies its weather".
