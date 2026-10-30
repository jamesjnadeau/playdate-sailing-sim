# Main Menu and Racing Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A main menu in front of the sim offering **Free Range** (today's game) and **Racing**, where the boat rounds buoys in order; each buoy is passed by crossing an invisible ray from it, the next buoy is marked on screen, and an edge arrow points to it when it is off screen.

**Architecture:** Pure, tested modules in `src/sim/`: `race.[ch]` (track data, JSON loading, ray-crossing test, race progress), `menu.[ch]` (menu state machine) and `camera_mark_view` in `camera.[ch]` (marker / edge-arrow geometry). `Game` gains an optional track: `game_set_track` switches it into racing, and `reset()` places the boat at the track start and turns the wind to the track's angle. The Playdate layer (`src/pd/`) adds track-file loading, the menu screen, buoy drawing and the race HUD; `main.c` switches between menu and play.

**Tech Stack:** C99, Playdate C API (SDK 3.1.2); headless tests in `tests/` (`make -C tests test`, ASan/UBSan, `t.h` macros: `T_ASSERT`, `T_NEAR`, `T_RUN`, `T_DONE`).

**Spec:** none beyond the user's request (no design doc). Request, verbatim: "i need a menu before this sim so I can add more functionality. I want to extend the game to have more modes. Free range mode is what currently exists. I want another mode called racing where we have buoys that you have to race around, bascally there is an invisibal line that extneds to infinitie at a certain heading that each boat must cross to get past that bouy. There should be something on the screen indicating which direction the next one is if it's off screen. An indicator should show over the next one when it is on screen. There will be many tracks, and I'd like to eventually have a track editor built in. For now, start with 2 buoys, and have the wind come in at a 45 degree angle to the line they create."

## Decisions made in this plan (not in the request)

- **The "invisible line" is a ray** from the buoy's centre at the buoy's `gate_deg` heading, to infinity. Crossing the opposite extension (behind the buoy) never counts.
- **Each buoy has a rounding side** (`"port"` or `"starboard"`): the crossing counts only in the direction a boat moves when it keeps the buoy on that side. A wrong-way crossing is ignored (no penalty, no undo).
- **Only the next buoy is live.** Buoys are rounded in list order; rounding the last one **finishes** the race. No laps, no start line: the race clock starts at reset (game start or restart).
- **After finishing you keep sailing.** The clock freezes and the HUD shows the time. Restart (system menu, or A after a capsize) resets the race.
- **Track format is JSON in `tracks/*.json`**, shipped in the `.pdx` like `boats/` and `config/`, so a later in-game editor can write the same format to the data folder (`listfiles` already merges both). If no track loads, Racing uses the built-in `track_default`.
- **Wind comes from the track:** `wind_from_deg` sets the wind's base direction; gusts and shifts from the weather preset still wobble around it. Changing weather mid-race keeps the track's base direction.
- **Two-buoy track** ("Two Buoys"): A at (0, 0), B at (400, 0), so the A→B line has heading 0°. Wind from 45° (45° to the line, B lies upwind on a close-hauled beat). Course: round B to port (ray heading 0°, beyond B), then round A to port (ray heading 180°, beyond A) to finish. Start at (−60, 30), heading −20° (a close reach, not in irons).
- **Menu:** Main screen lists Free Range, Racing. Racing opens a track list (even with one track, so more tracks need no new UI). Up/down move (wrapping), A selects, B goes back. Every start from the menu reloads configs (as "reload config" does today).
- **System menu:** the Playdate SDK documents at most 3 custom items and the game already adds 4; replace "reload config" with **"main menu"** so the count does not grow (the main menu now reloads configs on start).
- **Indicator:** on screen, a downward triangle drawn above the next buoy (projected at `MARK_IND_Z` = 4 m), with the distance in metres. Off screen, a triangle on the screen edge pointing toward the buoy; a buoy behind the camera gets a left/right edge arrow at the horizon row on the side it is nearer to (straight behind → right).
- Buoys are drawn at the local sea height (bob with the waves) and fade with fog like everything else; the indicator always shows, even in mist.

## Global Constraints

- World frame (`camera.h`, `boat.c`): heading `h` points along `(cos h, sin h)`; starboard is `(-sin h, cos h)`, so heading increases turning to starboard and `+y` is to starboard of heading 0. JSON angles are degrees in this convention.
- `Wind.dir` is where the wind blows **toward**; it comes **from** `wrap_pi(dir + PI)`. So `dir0 = wrap_pi(deg2rad(wind_from_deg) + PI)`.
- The boat is recentred (`recentre` in `game.c`) in steps of `5 * SEA_WRAP` = 5120 m; buoy positions must move with it.
- Use `wrap_pi`, `clampf`, `deg2rad` from `mathx.h`. Non-finite inputs never produce NaN state or NaN screen positions.
- Tests build with `-std=c99 -Wall -Wextra -Werror` and ASan/UBSan; new `tests/test_*.c` files are picked up by the wildcard in `tests/Makefile`.
- Config loaders follow `world_load`'s contract: always fill `out` from defaults first, clamp out-of-range values, collect `"; "`-joined warnings, return false on any warning.
- Playdate drawing uses 1-bit colours and `render_pattern(level)` (0 white .. 16 black); screen is `CAM_W` x `CAM_H` (400 x 240) and the HUD panel covers the bottom 36 px.

## Review Focus

- Crossing the line **behind** the buoy (the ray's opposite extension) while sailing past it: must not count. (Task 1 test `test_gate_ignores_back_extension`.)
- A boat that rounds the right way after first crossing the ray the wrong way (sailed past on the wrong side, then came back round): the second, correct crossing counts once. (Task 1 test `test_gate_wrong_way_then_right_way`.)
- Recentring mid-race: after `recentre` shifts the boat by 5120 m, the next buoy's local position shifts by the same amount and no spurious crossing fires. (Task 2 test `test_recentre_moves_marks_with_boat`.)
- Capsize restart and weather change in racing: restart puts the boat back at the track start with the clock at 0 and buoy 0 next; a weather change keeps the wind base angle at the track's. (Task 2 tests `test_restart_resets_race`, `test_preset_change_keeps_track_wind`.)
- A buoy exactly behind the camera, or a camera with non-finite pose: the indicator lands on a screen edge with finite coordinates. (Task 4 tests `test_mark_behind_goes_to_side_edge`, `test_mark_view_nonfinite_is_safe`.)

---

### Task 1: Tracks, gate crossing and race progress

**Files:**
- Create: `src/sim/race.h`, `src/sim/race.c`, `tracks/two_buoys.json`
- Test: `tests/test_race.c`

**Interfaces:**
- Consumes: `json.h` (`json_parse`, `json_get`, `json_array_len`, `json_array_at`, `json_num`, `json_str`, `json_free`), `mathx.h`.
- Produces (in `race.h`):
  ```c
  #define TRACK_MAX_MARKS 16
  #define TRACK_NAME_LEN 24
  enum { ROUND_PORT = -1, ROUND_STARBOARD = 1 };

  typedef struct {
      char name[8];
      float x, y;          /* m, track frame */
      float gate;          /* rad, heading of the ray from the mark */
      int rounding;        /* ROUND_PORT or ROUND_STARBOARD */
  } Mark;

  typedef struct {
      char name[TRACK_NAME_LEN];
      float wind_from;                 /* rad, heading the wind comes from */
      float start_x, start_y, start_heading;   /* m, m, rad */
      int nmarks;                      /* 1..TRACK_MAX_MARKS */
      Mark marks[TRACK_MAX_MARKS];
  } Track;

  typedef struct {
      int next;            /* index of the mark to round next; == nmarks once finished */
      bool finished;
      double t;            /* s since start; frozen at the finish */
      double ox, oy;       /* m; track frame = local frame + (ox, oy) (follows recentring) */
  } Race;

  void track_default(Track *out);   /* the two-buoy track below */
  bool track_load(Track *out, const char *buf, size_t len, char *warn, size_t warnlen);

  /* Did the move p0 -> p1 cross the ray from (mx, my) at heading `gate`, in the direction of
     `rounding`? All in one frame. Non-finite input -> false. */
  bool gate_crossed(float mx, float my, float gate, int rounding,
                    double x0, double y0, double x1, double y1);

  void race_init(Race *r);
  /* Local (game-frame) position of mark i. */
  void race_mark_local(const Race *r, const Track *t, int i, double *x, double *y);
  /* Advances the clock by dt (unless finished) and checks the next mark against the
     boat's move (x0,y0)->(x1,y1) in the local frame. Returns true if a mark was rounded. */
  bool race_update(Race *r, const Track *t, double x0, double y0, double x1, double y1, float dt);
  /* The local frame moved by (dx, dy): local coordinates had (dx, dy) subtracted. */
  void race_shift(Race *r, double dx, double dy);
  ```
- JSON format (`tracks/two_buoys.json`, the same values `track_default` returns):
  ```json
  {
    "name": "Two Buoys",
    "wind_from_deg": 45,
    "start": { "x": -60, "y": 30, "heading_deg": -20 },
    "marks": [
      { "name": "B", "x": 400, "y": 0, "gate_deg": 0,   "rounding": "port" },
      { "name": "A", "x": 0,   "y": 0, "gate_deg": 180, "rounding": "port" }
    ]
  }
  ```
  Loader rules: missing/non-number fields keep the default's value with a warning; positions clamped to ±20000 m; `rounding` other than `"port"`/`"starboard"` → port + warning; names truncated to fit; `marks` missing, empty or not an array → keep the default marks + warning; more than `TRACK_MAX_MARKS` → first 16 + warning; bad JSON → full default + the parse error as the warning.

- [ ] **Step 1: Write failing tests in `tests/test_race.c`** (include `t.h`, `race.h`, `<string.h>`). Use `#define D2R(d) ((d) * (float)M_PI / 180.0f)`.
  - `test_gate_port_rounding_counts`: mark (0,0), gate 0, port: `gate_crossed(0,0,0,ROUND_PORT, 10,5, 10,-5)` is true (east of the mark moving toward −y is what keeping it to port does).
  - `test_gate_wrong_direction_ignored`: same mark, `(10,-5)->(10,5)` is false; with `ROUND_STARBOARD` it is true.
  - `test_gate_ignores_back_extension`: `(-10,5)->(-10,-5)` (crossing the line on the far side, behind the mark) is false.
  - `test_gate_endpoint_on_ray_counts_once`: `(10,5)->(10,0)` true; then `(10,0)->(10,-5)` false (start-on-ray does not count again: crossing needs `s0 > 0 && s1 <= 0` for port, mirrored for starboard).
  - `test_gate_nonfinite_false`: any NaN coordinate → false.
  - `test_default_track_is_two_buoys`: `track_default`: `nmarks == 2`; mark 0 at (400, 0), mark 1 at (0, 0); `wind_from` ≈ `D2R(45)`; the angle between `atan2(m0.y-m1.y, m0.x-m1.x)` and `wind_from`, wrapped, is ≈ 45° (±1e-4).
  - `test_load_matches_default`: load the JSON above as a string literal; returns true, empty warning, and every field equals `track_default` (positions ±1e-4, angles ±1e-5, names `strcmp`).
  - `test_load_bad_rounding_and_overflow_warn`: a track with `"rounding": "left"` and 17 marks → returns false, `nmarks == 16`, that mark's `rounding == ROUND_PORT`, warning non-empty.
  - `test_load_garbage_gives_default`: `track_load(&t, "{", 1, ...)` → false, `t` equals `track_default`.
  - `test_race_rounds_in_order_and_finishes`: default track, `race_init`; the move that would round A to port, `(-10,-5)->(-10,5)`, made first does **not** advance (`next == 0`); a move rounding B to port (`(410,5)->(410,-5)`) returns true and `next == 1`; then rounding A to port (`(-10,-5)->(-10,5)`: west of A moving toward +y) returns true, `finished`, `next == 2`.
  - `test_race_clock_freezes_at_finish`: `race_update` with no crossing and `dt = 0.5` twice → `t` ≈ 1.0; after finishing, more updates leave `t` unchanged.
  - `test_race_wrong_way_then_right_way`: for mark B, a wrong-way move `(410,-5)->(410,5)` returns false; then the right-way move `(410,5)->(410,-5)` returns true; `next == 1`.
  - `test_race_shift_moves_local_marks`: `race_shift(&r, 5120, 0)`; `race_mark_local` of mark 0 is (400 − 5120, 0); a move rounding B in the **old** local coordinates no longer counts, the shifted one does.

- [ ] **Step 2: Run to verify failure**
  Run: `make -C tests test`
  Expected: build fails, `race.h: No such file or directory`.

- [ ] **Step 3: Implement `race.c`** (signatures above). `gate_crossed` algorithm — not determined by the tests alone:
  ```
  d = (cos gate, sin gate); p0 = (x0-mx, y0-my); p1 = (x1-mx, y1-my)
  s0 = cross(d, p0); s1 = cross(d, p1)            // cross(a,b) = a.x*b.y - a.y*b.x
  port:      need s0 > 0 && s1 <= 0;  starboard: need s0 < 0 && s1 >= 0
  f = s0 / (s0 - s1); q = p0 + f*(p1 - p0)
  return dot(d, q) >= 0                            // on the ray, not its back extension
  ```
  Compute in `double`. `race_update` converts the next mark to local with `race_mark_local` and calls `gate_crossed` with local coordinates; `race_mark_local` is `track − (ox, oy)`; `race_shift` adds `(dx, dy)` to `(ox, oy)`. Create `tracks/two_buoys.json` with the JSON above.

- [ ] **Step 4: Run tests**
  Run: `make -C tests test`
  Expected: every `== build/test_*` passes, including `test_race`.

- [ ] **Step 5: Commit**
  ```bash
  git add src/sim/race.h src/sim/race.c tracks/two_buoys.json tests/test_race.c
  git commit -m "feat: race tracks with ray gates and mark-by-mark progress"
  ```

---

### Task 2: Racing in the game loop

**Files:**
- Modify: `src/sim/game.h`, `src/sim/game.c` (`reset`, `start_weather`, `game_update`, `recentre`), `src/sim/weather.h`, `src/sim/weather.c`
- Test: `tests/test_game.c` (new tests), `tests/test_weather.c` (one test)

**Interfaces:**
- Consumes: Task 1 `Track`, `Race`, `race_init`, `race_update`, `race_shift`.
- Produces:
  ```c
  /* weather.h: sets the base direction (rad, blowing toward); the current dir keeps its offset from it. */
  void wind_set_base(Wind *w, float dir0);

  /* game.h */
  typedef enum { GAME_FREE, GAME_RACE } GameMode;
  /* in Game: */  GameMode mode; Track track; Race race;
  /* NULL -> free range. Otherwise copies the track and restarts on it with the current seed. */
  void game_set_track(Game *g, const Track *t);
  ```
  `game_init` leaves `mode == GAME_FREE` (existing callers unchanged).

- [ ] **Step 1: Write failing tests**
  - In `tests/test_weather.c`, `test_set_base_keeps_offset`: after `wind_init`, record `off = wrap_pi(w.dir - w.dir0)`; `wind_set_base(&w, 1.0f)` → `w.dir0 == 1.0f`, `wrap_pi(w.dir - 1.0f)` ≈ `off` (1e-5); a following `wind_step` keeps `dir` within `shift`-range of 1.0 (|wrap_pi(dir − 1.0)| < 1.0).
  - In `tests/test_game.c` (helper `race_fresh()` = `fresh(1)` then `track_default(&tr); game_set_track(&g, &tr);`):
    - `test_set_track_places_boat_and_wind`: `g.mode == GAME_RACE`; `g.b.x` ≈ −60, `g.b.y` ≈ 30, `g.b.heading` ≈ `D2R(-20)`; `wrap_pi(g.wind.dir0 + PI)` ≈ `D2R(45)`; `g.race.next == 0`, `g.race.t == 0`.
    - `test_race_advances_when_boat_rounds_b`: put the boat at local (410, 5), heading −90°, `u = 5`; run `game_update(&g, 0, 0, 0, 1.0f/30)` up to 60 frames → `g.race.next == 1`.
    - `test_restart_resets_race`: set `g.race.next = 1; g.race.t = 50;` then `game_restart(&g, 9)` → boat at the start, `next == 0`, `t == 0`, still `GAME_RACE`.
    - `test_preset_change_keeps_track_wind`: `game_set_preset(&g, WEATHER_STORM)` → `wrap_pi(g.wind.dir0 + PI)` ≈ `D2R(45)`.
    - `test_recentre_moves_marks_with_boat`: set `g.b.x = 5200` (past the 5120 m step), one `game_update` → `g.b.x` < 200 and `race_mark_local(&g.race, &g.track, 0, ...)` x ≈ 400 − 5120; `next` unchanged.
    - `test_free_range_unchanged`: `fresh(1)` → `mode == GAME_FREE`, boat at (0, 0); `game_set_track(&g, NULL)` after a race restores free range at (0, 0).

- [ ] **Step 2: Run to verify failure**
  Run: `make -C tests test`
  Expected: compile errors (`game_set_track`, `wind_set_base` undeclared).

- [ ] **Step 3: Implement**
  - `wind_set_base`: shift `dir` by `dir0_new − dir0_old` (wrapped) and store the new `dir0`.
  - `start_weather`: after `wind_init`, if `g->mode == GAME_RACE`, `wind_set_base(&g->wind, wrap_pi(g->track.wind_from + PI))`.
  - `reset`: in race mode, after `boat_init`, set `b.x/b.y/b.heading` from the track start and `race_init(&g->race)`; `start_weather` runs after the mode is known.
  - `game_set_track(g, t)`: copy (or `mode = GAME_FREE` for NULL), then `reset(g)`.
  - `game_update`: save `(b.x, b.y)` before the substep loop; after it, if `mode == GAME_RACE && run == RUN_SAILING`, `race_update(&g->race, &g->track, x0, y0, b.x, b.y, dt)`. Then `recentre`.
  - `recentre`: compute each axis's shift once, apply it to the boat, and call `race_shift(&g->race, dx, dy)` (harmless in free range).

- [ ] **Step 4: Run tests**
  Run: `make -C tests test`
  Expected: all pass (existing `test_game`, `test_smoke` included).

- [ ] **Step 5: Commit**
  ```bash
  git add src/sim/game.h src/sim/game.c src/sim/weather.h src/sim/weather.c tests/test_game.c tests/test_weather.c
  git commit -m "feat: racing game mode with track start, wind and mark progress"
  ```

---

### Task 3: Menu state machine

**Files:**
- Create: `src/sim/menu.h`, `src/sim/menu.c`
- Test: `tests/test_menu.c`

**Interfaces:**
- Consumes: `BTN_UP`, `BTN_DOWN`, `BTN_A`, `BTN_B` from `controls.h`.
- Produces:
  ```c
  typedef enum { MENU_MAIN, MENU_TRACKS } MenuScreen;
  typedef enum { MENU_NONE, MENU_START_FREE, MENU_START_RACE } MenuAction;
  enum { MENU_ITEM_FREE, MENU_ITEM_RACE, MENU_MAIN_ITEMS };
  extern const char *const MENU_MAIN_NAMES[MENU_MAIN_ITEMS];   /* "Free Range", "Racing" */

  typedef struct {
      MenuScreen screen;
      int sel;        /* highlighted row on the current screen */
      int ntracks;    /* rows on the track screen */
      int track;      /* chosen track once MENU_START_RACE is returned */
  } Menu;

  void menu_init(Menu *m, int ntracks);   /* main screen, row 0; ntracks < 0 -> 0 */
  /* One frame of input; pressed is the BTN_* newly-pressed mask. */
  MenuAction menu_update(Menu *m, uint32_t pressed);
  ```

- [ ] **Step 1: Write failing tests in `tests/test_menu.c`**
  - `test_starts_on_main_first_row`: `menu_init(&m, 1)` → `screen == MENU_MAIN`, `sel == 0`.
  - `test_down_up_wrap`: DOWN → `sel == 1`; DOWN → `sel == 0` (wraps); UP → `sel == 1`.
  - `test_a_on_free_starts_free`: A on row 0 → returns `MENU_START_FREE`.
  - `test_racing_opens_track_list`: DOWN, A → returns `MENU_NONE`, `screen == MENU_TRACKS`, `sel == 0`.
  - `test_track_pick_starts_race`: `menu_init(&m, 3)`, open tracks, DOWN, DOWN, A → `MENU_START_RACE`, `track == 2`.
  - `test_b_goes_back_to_racing_row`: on tracks, B → `screen == MENU_MAIN`, `sel == MENU_ITEM_RACE`; B on main does nothing.
  - `test_no_tracks_racing_stays`: `menu_init(&m, 0)`; DOWN, A → `screen == MENU_MAIN`, `MENU_NONE`.
  - `test_multiple_buttons_one_frame`: `pressed = BTN_DOWN | BTN_A` on main → moves first then selects: returns `MENU_NONE` and opens tracks (document this order: B, then UP/DOWN, then A).

- [ ] **Step 2: Run to verify failure** — `make -C tests test`; expected: `menu.h: No such file or directory`.

- [ ] **Step 3: Implement `menu.c`.** Rows on the track screen are `ntracks`; process B, then UP/DOWN, then A within one call.

- [ ] **Step 4: Run tests** — `make -C tests test`; expected: all pass.

- [ ] **Step 5: Commit**
  ```bash
  git add src/sim/menu.h src/sim/menu.c tests/test_menu.c
  git commit -m "feat: main menu state machine with mode and track selection"
  ```

---

### Task 4: Next-mark indicator geometry

**Files:**
- Modify: `src/sim/camera.h`, `src/sim/camera.c`
- Test: `tests/test_camera.c` (new tests; reuse its existing pose-building helpers)

**Interfaces:**
- Consumes: `camera_project`, `CameraPose`, `CAM_W`, `CAM_H`, `CAM_HORIZON_Y`.
- Produces:
  ```c
  #define MARK_IND_Z 4.0f      /* m above the sea where the on-screen marker anchors */
  #define MARK_EDGE_PAD 12.0f  /* px kept between an edge arrow and the screen edge */

  typedef struct {
      bool on_screen;   /* true: (x, y) is the point above the mark */
      float x, y;       /* px */
      float angle;      /* off screen: screen direction toward the mark, rad (0 = right, +pi/2 = down) */
  } MarkView;

  /* bottom: lowest usable row (the HUD panel top). Always fills `out` with finite values. */
  void camera_mark_view(const CameraPose *c, double wx, double wy, float bottom, MarkView *out);
  ```
  Rules: project `(wx, wy, MARK_IND_Z)`. If it projects inside `[0, CAM_W) x [0, bottom)`, on screen at that point. If it projects but lies outside, clamp to the padded box `[PAD, CAM_W-PAD] x [PAD, bottom-PAD]` along the line from the box centre and set `angle = atan2(sy - cy, sx - cx)`. If it does not project (behind), `rel = wrap_pi(atan2(wy - c->y, wx - c->x) - c->yaw)`: `rel >= 0` (starboard, includes straight behind) → right edge `(CAM_W - PAD, CAM_HORIZON_Y)`, `angle = 0`; else left edge, `angle = PI`. Any non-finite result → right-edge fallback.

- [ ] **Step 1: Write failing tests**
  - `test_mark_ahead_is_on_screen`: camera at the origin, yaw 0, height 3; mark at (200, 0) → `on_screen`, `x` ≈ `CAM_W/2` (±1), `y < CAM_H - 36`.
  - `test_mark_far_starboard_goes_to_right_edge`: mark at (20, 200) (ahead but far right) → not on screen, `x` ≈ `CAM_W - MARK_EDGE_PAD` (±1), `|angle| < PI/2`.
  - `test_mark_behind_goes_to_side_edge`: mark at (−100, 0) exactly behind → not on screen, `x` ≈ `CAM_W - MARK_EDGE_PAD`, `angle == 0`; mark at (−100, −30) → left edge, `angle` ≈ PI.
  - `test_mark_view_nonfinite_is_safe`: NaN mark coordinates and, separately, a pose with NaN yaw → all of `x`, `y`, `angle` finite.

- [ ] **Step 2: Run to verify failure** — `make -C tests test`; expected: `camera_mark_view` undeclared.

- [ ] **Step 3: Implement `camera_mark_view` in `camera.c`.**

- [ ] **Step 4: Run tests** — `make -C tests test`; expected: all pass.

- [ ] **Step 5: Commit**
  ```bash
  git add src/sim/camera.h src/sim/camera.c tests/test_camera.c
  git commit -m "feat: screen placement for the next-mark indicator"
  ```

---

### Task 5: Main menu screen and mode switching on the Playdate

**Files:**
- Create: `src/pd/tracks.h`, `src/pd/tracks.c`, `src/pd/menu_draw.c`
- Modify: `src/pd/main.c`, `src/pd/render.h` (declare `menu_draw`), `Makefile` (`SRC`, `STAGE`), `.gitignore`

**Interfaces:**
- Consumes: Task 1 `Track`, `track_load`, `track_default`; Task 2 `game_set_track`; Task 3 `Menu`, `menu_init`, `menu_update`, `MENU_MAIN_NAMES`.
- Produces:
  ```c
  /* tracks.h */
  #define TRACKS_MAX 16
  /* Loads every tracks/*.json (sorted by file name) into out; files that fail to parse are
     skipped with a warning appended. If none load, out[0] = track_default and returns 1. */
  int tracks_load_all(PlaydateAPI *pd, Track *out, int max, char *warn, size_t warnlen);

  /* render.h */
  void menu_draw(PlaydateAPI *pd, const Menu *m, const Track *tracks);
  ```

- [ ] **Step 1: Staging.** `Makefile`: add `src/pd/tracks.c src/pd/menu_draw.c` to `SRC`; `STAGE` also removes and copies `tracks` into `Source/`. `.gitignore`: add `Source/tracks/`.

- [ ] **Step 2: `tracks_load_all`.** Collect names with `pd->file->listfiles("tracks", cb, ud, 0)` (keep only `*.json`, at most `max`), sort with `qsort`/`strcmp`, read each like `assets.c`'s `read_file` (reuse it: expose `assets_read_file(PlaydateAPI*, const char *path, char **buf)` from `assets.c`/`assets.h` rather than duplicating), and `track_load` it. A file whose load returns false but parsed (warning only) is still kept; a file with a JSON parse error is skipped.

- [ ] **Step 3: `menu_draw`.** White screen; title "SAILING" centred at y 30; main screen rows `MENU_MAIN_NAMES` at y 100 + 30·i; track screen header "RACING" and rows of track names. The selected row is drawn white-on-black in a filled bar (`kDrawModeFillWhite`), others black. Footer hint at y 220: "A select  B back" on the track screen, "A select" on main. If a config warning is pending, `main.c` still draws it on top.

- [ ] **Step 4: `main.c` app state.** `static enum { APP_MENU, APP_PLAY } app = APP_MENU;` plus a `Menu menu`, `Track tracks[TRACKS_MAX]`, `int ntracks`. plus `static const Track *cur_track` (NULL = free range). `begin(bool reload)` keeps its signature and, after `game_init`, calls `game_set_track(&game, cur_track)` when `cur_track` is non-NULL, so the "boat" system item keeps the race. At init: load tracks, `menu_init(&menu, ntracks)`, show the menu (no game yet; `begin` runs on selection). In `update`: in `APP_MENU`, `menu_update` → on `MENU_START_FREE` set `cur_track = NULL`, on `MENU_START_RACE` set `cur_track = &tracks[menu.track]`; then `begin(true)`, switch to `APP_PLAY`, and drop that frame's `pushed` so the A press does not reach the autopilot. Replace the "reload config" system item with **"main menu"**: sets `app = APP_MENU`, reloads tracks, `menu_init` with the new count. In `APP_MENU`, the pause-menu image (`kEventPause`) is skipped, and "restart" does nothing.

- [ ] **Step 5: Build and run in the simulator**
  Run: `make -C tests test && tools/sim.sh`
  Expected: tests pass; the simulator opens on the menu. Check by hand: up/down wraps; A on Free Range starts today's game at the origin; Racing → "Two Buoys" → A starts at the track start; B on the track list returns to main; system menu "main menu" returns to the menu; "restart" in a race resets the boat to the start.

- [ ] **Step 6: Commit**
  ```bash
  git add Makefile .gitignore src/pd/tracks.h src/pd/tracks.c src/pd/menu_draw.c src/pd/main.c src/pd/render.h src/pd/assets.c src/pd/assets.h
  git commit -m "feat: main menu with free range and racing track selection"
  ```

---

### Task 6: Buoys and race HUD

**Files:**
- Modify: `src/pd/render.c` (`draw_marks`, `render_frame`), `src/pd/hud.c` (`hud_race`), `src/pd/render.h`

**Interfaces:**
- Consumes: Task 1 `race_mark_local`; Task 2 `Game.mode/track/race`; Task 4 `camera_mark_view`, `MarkView`, `MARK_IND_Z`; existing `camera_project`, `fvis`, `render_pattern`, `sea_view_*` / `sea_height`.
- Produces: `void hud_race(PlaydateAPI *pd, const Game *g, const CameraPose *c);` in `render.h`.

- [ ] **Step 1: `draw_marks` in `render.c`** (only when `g->mode == GAME_RACE`), called in `render_frame` where `draw_rocks` sits. Each mark: base at `sea_height` under it (`g->t` mod `g->sea.period`, as the hull uses), top 2.5 m above; project both; skip if either fails or depth > `fvis(g)`. Draw a filled triangle (apex at top, base half-width = 0.8 m projected at base depth, min 2 px) in black, outlined white; the next mark drawn on top. Farther marks first (sort by depth; two marks need no sort beyond a swap but use the same `qsort` pattern as `draw_rocks` to be ready for many).

- [ ] **Step 2: `hud_race` in `hud.c`**, called from `render_frame` after `hud_draw` (race mode only).
  - Top banner, black bar `y 0..18`, white text: racing → `"MARK %s %d/%d  %4.0fm  %02d:%04.1f"` (next mark name, `next+1`, `nmarks`, distance from boat to the next mark, clock as mm:ss.s); finished → `"FINISHED  %02d:%04.1f"`.
  - Indicator (not finished): `camera_mark_view(c, mx, my, CAM_H - 36, &mv)` for the next mark (local position). On screen → a 10 px wide down-pointing filled triangle with its tip at `(mv.x, mv.y)`. Off screen → a 12 px filled triangle at `(mv.x, mv.y)` pointing along `mv.angle`, with the distance in metres beside it (inside the screen).
  - Shift the config warning in `main.c` to stay below the banner (it already sits at y 36).

- [ ] **Step 3: Build and run in the simulator**
  Run: `make -C tests test && tools/sim.sh`
  Expected: tests pass. In Racing → Two Buoys, check by hand: buoy B ahead and slightly right of the start with the marker above it and the banner reading `MARK B 1/2`; wind dial shows wind ~45° off the A→B line; turning away moves the marker to an edge arrow pointing back at B; rounding B with it to port switches to `MARK A 2/2`; passing B on the wrong side does not; rounding A to port shows `FINISHED` with a frozen time and hides the indicator; "mist" weather hides the buoys but not the indicator.

- [ ] **Step 4: Commit**
  ```bash
  git add src/pd/render.c src/pd/hud.c src/pd/render.h src/pd/main.c
  git commit -m "feat: draw race buoys, next-mark indicator and race banner"
  ```
