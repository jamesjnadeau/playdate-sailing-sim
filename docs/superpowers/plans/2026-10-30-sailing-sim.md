# Sailing Sim Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Playdate C-SDK first-person sailing sim on an open sea: runtime-loaded boat config, full sail aerodynamics, wave-coupled hull, weather presets, rock hazards, quick restart.

**Architecture:** All simulation lives in `src/sim/` as pure C99 with no Playdate headers, so it builds and unit-tests on the host with `gcc`. `src/pd/` is a thin platform layer (input, drawing, file loading, menu). Config files are parsed by our own small JSON parser (`json.c`) from a byte buffer the platform layer reads, so the loader is host-testable too.

**Tech Stack:** C99, Playdate C SDK (not installed on this machine yet: tasks 1 and 13+ need `PLAYDATE_SDK_PATH`), gcc + a tiny assert harness for host tests, `float` throughout.

**Spec:** `docs/superpowers/specs/2026-10-30-sailing-sim-design.md` (reference only for feel: `docs/boat-model-analysis.md`; nothing is copied from the demo).

## Global Constraints

- 400×240 1-bit display, 30 fps target on Rev-A hardware; physics fixed substep 120 Hz (`SIM_DT = 1/120`), frame dt clamped to [0.005, 0.1].
- Single-precision floats; SI units in configs, degrees in files, tables as `[x,y]` pair arrays; each field has documented min/max/default.
- Bad config must never crash: clamp/default each field, record a one-line warning; built-in dinghy is the fallback and is the same data as `boats/dinghy.json`.
- Sail count ≤ 8. Live rocks ≤ 64. Hull wave samples: 5–7 along the hull plus 2 across, once per frame; wave forces held constant across that frame's substeps.
- Controls: UP wheel, RIGHT trim, LEFT hoist/reef, DOWN camera yaw; modes are latched (stay selected after release); pressing RIGHT/LEFT again while already in that mode selects the next sail; A anchor; B unused. Crank drives the active mode.
- Camera roll follows heel only up to `camera.roll_limit_deg` (default 10); deck/rig keep tilting.
- Failures are not softened: irons, luff, broach, capsize (heel > `capsize_deg`), hard rock strike. Restart (menu item or A on the prompt) takes under 3 s.
- Weather presets: sunny, cloudy, mist, storm, in a global weather config separate from the boat file.
- Clean-room: no demo code, tables, constants, sprites, sounds.

## Review Focus

- Config file missing, empty, truncated, wrong types, NaN/negative/huge values, zero sails, nine sails: loads with defaults and warnings, never crashes.
- Wind speed 0 and head-to-wind from rest: boat stays controllable (drifts, no NaN), no divide by zero in apparent wind.
- Very large frame dt (device stall) and first frame: clamped, substep count bounded.
- Pressing several direction buttons in one frame, crank spinning during mode change: one deterministic mode wins; camera yaw returns to centre.
- Boat position far from origin (long run): rock hashing and sea pattern keep working, floats keep precision (rebase origin or wrap sea phase).
- Rock exactly under the start position / restart on the same seed: start area clear; same rocks reappear.

---

## File Structure

- `Makefile` (device/simulator build via SDK `setup.mk`), `tests/Makefile` (host tests), `tests/t.h` (assert macros)
- `src/sim/mathx.[ch]` clamp, wrap, `table_eval`, rng hash
- `src/sim/json.[ch]` minimal JSON DOM parser
- `src/sim/boatcfg.[ch]` boat config structs, defaults, loader, `dinghy_default()`
- `src/sim/weather.[ch]` weather config, presets, wind state
- `src/sim/sail.[ch]` per-sail aerodynamics
- `src/sim/boat.[ch]` boat state + physics step (hull, foils, heel)
- `src/sim/sea.[ch]` wave field, hull sampling, wave forces
- `src/sim/rocks.[ch]` rock placement and collision
- `src/sim/controls.[ch]` latched-mode input state machine
- `src/sim/game.[ch]` ties it together: `game_update`, failures, reset
- `src/sim/camera.[ch]` camera pose from boat state
- `src/pd/main.c`, `src/pd/render.[ch]`, `src/pd/hud.c`, `src/pd/assets.c` platform layer
- `boats/dinghy.json`, `boats/spinnaker_example.json`, `config/weather.json`, `config/world.json`, `docs/boat-schema.md`

---

### Task 1: Scaffold and host test harness

**Files:** Create `tests/t.h`, `tests/Makefile`, `tests/test_smoke.c`, `.gitignore`

**Interfaces:** Produces `T_ASSERT(cond)`, `T_NEAR(a,b,eps)`, `T_RUN(fn)`, and `make -C tests test` which compiles every `tests/test_*.c` with `src/sim/*.c` (`-std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined`) and runs each.

- [ ] **Step 1:** Write `tests/test_smoke.c` with `test_harness_works` asserting `T_NEAR(1.0f, 1.0f+1e-7f, 1e-5f)`.
- [ ] **Step 2:** Run `make -C tests test`; expect FAIL (no Makefile).
- [ ] **Step 3:** Implement `t.h` and `tests/Makefile`; a failing assert prints file:line and exits nonzero.
- [ ] **Step 4:** Run `make -C tests test`; expect `1 test passed`.
- [ ] **Step 5:** Commit `chore: host test harness`.

### Task 2: Math utilities

**Files:** Create `src/sim/mathx.[ch]`, `tests/test_mathx.c`

**Interfaces:** Produces `float clampf(float,float,float)`, `float wrap_pi(float)` (→ (−π,π]), `float deg2rad(float)`, `typedef struct {float x,y;} Pt; float table_eval(const Pt*, int n, float x)` (linear interpolation, clamps at ends, n==0 returns 0), `uint32_t hash2(int32_t,int32_t,uint32_t)`, `float hashf(uint32_t)` (→ [0,1)).

- [ ] **Step 1:** Tests: `wrap_pi(3π)≈π` and `wrap_pi(-3π/2)≈π/2`; `table_eval` at midpoint of (0,0),(10,100) is 50, below/above range clamps, n=0 → 0; `hash2` deterministic and `hash2(1,2,s) != hash2(2,1,s)`; `hashf` always in [0,1) over 10000 values.
- [ ] **Step 2:** Run; FAIL. **Step 3:** Implement. **Step 4:** Run; PASS. **Step 5:** Commit.

### Task 3: JSON parser

**Files:** Create `src/sim/json.[ch]`, `tests/test_json.c`

**Interfaces:** Produces `typedef struct JsonDoc JsonDoc; JsonDoc* json_parse(const char* buf, size_t len, char* err, size_t errlen)` (NULL + message on failure, never crashes on garbage), `void json_free(JsonDoc*)`, node API `const JsonNode* json_root(const JsonDoc*)`, `json_get(node,"key")`, `json_array_len`, `json_array_at`, `json_is_number/string/bool/array/object`, `json_num(node, float def)`, `json_str(node, const char* def)`, `json_bool(node, bool def)`. Nesting depth limit 16, size limit 64 KiB.

- [ ] **Step 1:** Tests: parses `{"a":1.5,"b":[1,2,[3]],"c":"x","d":true}` and reads each; `json_num` on missing/wrong-type returns default; fails cleanly on `{"a":`, `[1,`, `}{`, empty buffer, 100-deep nesting, and a 1 MiB buffer.
- [ ] **Step 2:** Run; FAIL. **Step 3:** Implement recursive descent (arena allocation, no per-node malloc if easy). **Step 4:** PASS under ASan. **Step 5:** Commit.

### Task 4: Boat config structs, defaults, loader

**Files:** Create `src/sim/boatcfg.[ch]`, `boats/dinghy.json`, `docs/boat-schema.md`, `tests/test_boatcfg.c`

**Interfaces:** Consumes json API, `table_eval`. Produces `BoatCfg` with the spec's groups (`hull`, `foils`, `sails[8]` + `sail_count`, `controls`, `look`, `camera`, `world_units_per_metre`), `void boatcfg_default(BoatCfg*)` (the built-in dinghy), `bool boatcfg_load(BoatCfg* out, const char* buf, size_t len, char* warn, size_t warnlen)` (always fills `out`; returns false if any warning was raised). Every field has a registered min/max/default (table in `boatcfg.c`, mirrored in `docs/boat-schema.md`). Dinghy values: ~4 m, main + jib, centreboard, one rudder, hiking crew, `camera.roll_limit_deg=10`, `capsize_deg` chosen by tuning (start 70).

- [ ] **Step 1:** Tests: loading `boats/dinghy.json` reproduces `boatcfg_default` field-for-field; out-of-range `hull.mass=-5` clamps to its min with a warning mentioning `hull.mass`; wrong type, missing group, zero sails (falls back to the default rig), nine sails (keeps 8, warns); garbage buffer returns false and `out` equals the default; `camera.roll_limit_deg` absent → 10.
- [ ] **Step 2:** FAIL. **Step 3:** Implement with a field-descriptor table so adding a field is one line. **Step 4:** PASS. **Step 5:** Commit.

### Task 5: Weather presets and wind state

**Files:** Create `src/sim/weather.[ch]`, `config/weather.json`, `tests/test_weather.c`

**Interfaces:** Produces `WeatherCfg` (4 presets: wind base/variability/shift rate/gust, wave height/period/steepness, visibility), `weather_default`, `weather_load(buf,len,warn,warnlen)`, `typedef struct {float dir, speed; ...} Wind; void wind_init(Wind*, const WeatherPreset*, uint32_t seed); void wind_step(Wind*, const WeatherPreset*, float dt)`.

- [ ] **Step 1:** Tests: over 600 simulated seconds per preset, speed stays in [0, 1.5×(base+gust max)] and storm mean > sunny mean; same seed gives identical series; different seed differs; direction shifts stay bounded per second.
- [ ] **Step 2:** FAIL. **Step 3:** Implement own slow noise + gust events (clean-room). **Step 4:** PASS. **Step 5:** Commit.

### Task 6: Sail aerodynamics

**Files:** Create `src/sim/sail.[ch]`, `tests/test_sail.c`

**Interfaces:** Consumes `SailCfg`. Produces `typedef struct {float fx, fy, heel_moment, yaw_moment, luff, aoa;} SailForce; SailForce sail_force(const SailCfg*, float trim_rad, float hoist, float awa_rad, float aws, float heel_rad, float slot_cl_mult)`. Forces in boat frame (x forward, y starboard); wind speed 0 returns zeros.

- [ ] **Step 1:** Tests (dinghy main): beam reach with correct trim gives `fx>0`; trimmed on the wrong side or too far in at close-hauled makes `aoa` beyond stall and drive drops; AoA ≈ 0 gives `luff>0.5` and near-zero force; hoist 0 gives zeros; force scales ~aws²; `heel_rad=60°` reduces drive vs 0°; `slot_cl_mult>1` increases main force; `aws=0` and `awa=0` produce no NaN.
- [ ] **Step 2:** FAIL. **Step 3:** Implement: AoA from trim and apparent wind, CL/CD from per-sail tables, force at centre of effort, moments from its (x,z). **Step 4:** PASS. **Step 5:** Commit.

### Task 7: Boat dynamics (hull, foils, heel) and dinghy acceptance

**Files:** Create `src/sim/boat.[ch]`, `tests/test_boat.c`

**Interfaces:** Consumes `BoatCfg`, `Wind`, `sail_force`. Produces `BoatState {x,y,heading,u,v,r,heel,heel_rate,wheel,trims[8],hoists[8],anchor,...}`, `BoatInputs {float wheel_target, trim_target[8], hoist_target[8]; bool anchor;}`, `void boat_init(BoatState*, const BoatCfg*)`, `void boat_step(BoatState*, const BoatCfg*, const Wind*, const BoatInputs*, const WaveForces*, float dt)` where `WaveForces` is declared in `boat.h` as `{float drag_mult, surge, yaw, heel, pitch, heave;}` (zeros = flat sea). Speed-dependent resistance with hull-speed limit; centreboard lateral lift (leeway emerges); rudder lift scales with speed and angle and stalls; dynamic heel (inertia, damping, righting moment from hull form + hiking).

- [ ] **Step 1:** Acceptance tests on a flat sea with a scripted helm in `tests/test_boat.c`: `test_upwind_angle` (best close-hauled course settles 40–50° off true wind at 10 kn); `test_top_speed_near_hull_speed` (beam/broad reach, moderate wind, within 10% of `hull.hull_speed`); `test_tack_takes_4_to_8s_and_loses_speed`; `test_irons_no_steerage` (head to wind from slow speed: heading rate stays < 5°/s for 5 s); `test_capsize_in_gust` (sheets cleated, 25 kn: `|heel| > capsize_deg` within 30 s); `test_no_nan_zero_wind`.
- [ ] **Step 2:** FAIL. **Step 3:** Implement; tune dinghy numbers in `boatcfg_default` and `boats/dinghy.json` (keep them identical, Task 4 test enforces it). **Step 4:** PASS. **Step 5:** Commit.

### Task 8: Latched-mode controls

**Files:** Create `src/sim/controls.[ch]`, `tests/test_controls.c`

**Interfaces:** Produces `typedef enum {MODE_WHEEL, MODE_TRIM, MODE_HOIST, MODE_CAMERA} Mode; typedef struct {Mode mode; int sail; float camera_yaw; float camera_target...} Controls; void controls_update(Controls*, const BoatCfg*, uint32_t pressed, float crank_delta_deg, float dt, BoatInputs* out)`; button bits `BTN_UP/DOWN/LEFT/RIGHT/A/B` in `controls.h`.

- [ ] **Step 1:** Tests: after pressing RIGHT with no button held, crank still changes the selected sail's trim target; pressing RIGHT again advances main→jib→(wraps); LEFT in hoist mode the same, sail selection shared; pressing UP then crank moves the wheel, not trim; DOWN then crank changes camera yaw within the configured range; leaving camera mode returns yaw to 0 at `camera.return_rate`; UP+RIGHT in one frame resolves deterministically (priority UP>RIGHT>LEFT>DOWN, documented in header); crank clamps trim/hoist ranges from config; single-sail boat ignores re-press.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** PASS. **Step 5:** Commit.

### Task 9: Wave field, hull sampling, wave forces

**Files:** Create `src/sim/sea.[ch]`, `tests/test_sea.c`

**Interfaces:** Produces `Sea`, `void sea_init(Sea*, const WeatherPreset*, uint32_t seed)`, `float sea_height(const Sea*, float x, float y, float t)` (sum of travelling waves, ≤ 6 components), `WaveForces sea_hull_forces(const Sea*, const BoatState*, const BoatCfg*, float t)` (5–7 samples along hull plus 2 across; slam, pitch/heave coupling, added resistance, surf force on steep faces, broach yaw). Sea phase uses wrapped coordinates so far-from-origin positions keep precision.

- [ ] **Step 1:** Tests: height amplitude ≈ preset wave height; periodic in time with preset period; `sea_hull_forces` on flat sea (height 0 preset) is all zeros; storm gives larger `drag_mult` than sunny at the same speed; slam is nonzero only when bow re-enters at speed above a configured threshold; surfing force is positive running down a steep face and zero beam-on; evaluating at x=1e6 gives finite results and the same statistics.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** PASS. **Step 5:** Commit.

### Task 10: Rock hazards

**Files:** Create `src/sim/rocks.[ch]`, `config/world.json`, `tests/test_rocks.c`

**Interfaces:** Produces `WorldCfg {rock_cell_size, rock_density, rock_r_min, rock_r_max, rock_bump_speed, rock_wreck_speed, start_clear_radius}` with `world_default/world_load`, `typedef struct {float x,y,r; uint8_t awash;} Rock; int rocks_near(const WorldCfg*, uint32_t seed, float x, float y, float radius, Rock* out, int max)` (deterministic, ≤ 64 returned), `typedef enum {HIT_NONE, HIT_BUMP, HIT_STRIKE, HIT_WRECK} HitKind; HitKind rocks_collide(const WorldCfg*, const Rock*, int n, BoatState*, const BoatCfg*, float heave)` (removes inward velocity, damps speed, heading kick for STRIKE; wave heave raises effective impact speed).

- [ ] **Step 1:** Tests: same seed/area gives identical rocks, different seed differs; nothing within `start_clear_radius` of the origin; density 0 gives none; slow contact gives BUMP, mid speed STRIKE with heading changed, above `rock_wreck_speed` WRECK; boat velocity afterwards has no inward component; works at position (1e5, -1e5); result count never exceeds 64.
- [ ] **Step 2:** FAIL. **Step 3:** Implement per-cell hash placement; awash flag from hash. **Step 4:** PASS. **Step 5:** Commit.

### Task 11: Game loop, failures, reset

**Files:** Create `src/sim/game.[ch]`, `tests/test_game.c`

**Interfaces:** Consumes everything above. Produces `Game`, `void game_init(Game*, const BoatCfg*, const WeatherCfg*, const WorldCfg*, int preset, uint32_t seed)`, `void game_update(Game*, uint32_t pressed, float crank_delta, float frame_dt)` (clamps dt, runs ≤ 12 substeps, samples sea once per frame, applies wave forces constant across substeps, then rock collisions), `typedef enum {RUN_SAILING, RUN_CAPSIZED, RUN_WRECKED} RunState; void game_restart(Game*, uint32_t new_seed)`, `void game_set_preset(Game*, int)`.

- [ ] **Step 1:** Tests: frame_dt of 5 s runs at most 12 substeps and stays finite; capsize beyond `capsize_deg` sets `RUN_CAPSIZED` and freezes controls; WRECK sets `RUN_WRECKED`; `game_restart` restores start state in a single call and resets state to `RUN_SAILING`; same seed after restart reproduces the same rocks; first frame with dt=0 is safe.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** PASS. **Step 5:** Commit.

### Task 12: Camera

**Files:** Create `src/sim/camera.[ch]`, `tests/test_camera.c`

**Interfaces:** Produces `typedef struct {float x,y,height,yaw,pitch,roll; float deck_roll, deck_pitch, deck_bob;} CameraPose; void camera_update(CameraPose*, const Game*, const BoatCfg*, float dt)`; `bool camera_project(const CameraPose*, float wx,float wy,float wz, float* sx, float* sy, float* depth)` (focal and horizon from config, 400×240).

- [ ] **Step 1:** Tests: heel 30° gives camera roll == `roll_limit_deg` (10) while `deck_roll` == 30°; heel 5° gives camera roll ≈ 5° (after smoothing settles); a point straight ahead at the horizon projects to the horizon line and screen centre; points behind the camera return false; yaw offset from camera mode turns the view without turning the boat.
- [ ] **Step 2:** FAIL. **Step 3:** Implement with configurable smoothing. **Step 4:** PASS. **Step 5:** Commit.

### Task 13: Playdate project, loading, main loop (needs SDK)

**Files:** Create `Makefile`, `src/pd/main.c`, `src/pd/assets.c`, `pdxinfo`, `Source/` layout; copy `boats/`, `config/` into the .pdx

**Interfaces:** Produces `eventHandler`, file loading via `pd->file->open/read` into a buffer fed to `boatcfg_load/weather_load/world_load` with warnings logged and shown for the first 3 s; menu items: boat, weather (4 presets), restart, reload config; fallback to built-in config when a file is missing.

- [ ] **Step 1:** Install the Playdate SDK, set `PLAYDATE_SDK_PATH`, confirm `pdc` runs. **Step 2:** Build; expect a launchable `.pdx` in the Simulator showing a blank screen. **Step 3:** Wire `game_update` with real input (buttons, `getCrankChange`) and `restart` menu item. **Step 4:** Verify: renaming `boats/dinghy.json` still launches (warning shown); a corrupt JSON launches with the default boat. **Step 5:** Commit.

### Task 14: Rendering (needs SDK)

**Files:** Create `src/pd/render.[ch]`, `src/pd/hud.c`

**Interfaces:** Consumes `CameraPose`, `camera_project`, `Game`. Produces `void render_frame(PlaydateAPI*, const Game*, const CameraPose*)`: sky and clouds (24, slow drift), horizon, dithered sea with wave bands from `sea_height` samples, wind streaks, spray particles (from slam/speed), rocks (awash rocks drawn mainly as surf, fog by weather visibility), deck/wheel/rig tilted by full heel, sails from config geometry (mesh/camber/flutter from `SailCfg`), HUD (mode + selected sail, speed, heading, apparent wind, heel), capsize/wreck restart prompt.

- [ ] **Step 1:** Draw sea/sky/horizon; verify visually in the Simulator at heel 0 and 30° (horizon roll capped, deck tilting). **Step 2:** Add sails, deck, HUD. **Step 3:** Add rocks and surf; verify a rock approaches, scales and collides. **Step 4:** Add the restart prompt and verify A restarts in under 3 s. **Step 5:** Commit.

### Task 15: Second boat example and device profiling (needs SDK, device)

**Files:** Create `boats/spinnaker_example.json`; modify `docs/boat-schema.md`

- [ ] **Step 1:** Add a spinnaker as a third sail entry (works only in a wind-angle window); test: `boatcfg_load` on it yields 3 sails (add to `tests/test_boatcfg.c`). **Step 2:** Sideload to hardware, read the frame time in the profiler; expected ≥ 30 fps in the storm preset. If not, cut rendering detail first (streaks, cloud count), then substep rate, before reducing physics fidelity. **Step 3:** Commit `docs/boat-schema.md` with final field ranges.

---

## Spec coverage notes

Controls, camera, config, aero model, waves, weather, rocks, failures and restart each map to tasks 4–12; the visual feel items (spray, streaks, clouds) are Task 14; "tunable without recompiling" is the runtime config and reload menu in Task 13. Islands, goals and map are explicitly out of scope.

Deviation from the spec to confirm: the spec said to parse JSON with the Playdate JSON API. This plan uses our own parser (Task 3) so the loader is testable on the host and one code path serves device and tests.
