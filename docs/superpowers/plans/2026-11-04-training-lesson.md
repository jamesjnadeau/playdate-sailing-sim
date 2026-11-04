# Training Section and Sailing Lesson Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Training menu section holding Controls and a guided Sailing Lesson that teaches the boat controls by making the player do each one, including getting stuck in irons and freeing the boat with the engine.

**Architecture:** A pure-C lesson state machine (`src/sim/training.c`) watches `Game` each frame and advances when the current step's goal has been held for a short dwell. `menu.c` gains a Training screen; `app.c` starts a free-range game with a steady wind and feeds the lesson; `hud.c` draws the current step's text, loaded from `config/training.md` (placeholders only).

**Tech Stack:** C99, Playdate SDK, the `tests/` harness (`t.h`, `make -C tests test`).

**Spec:** `docs/superpowers/specs/2026-11-04-training-lesson-design.md`

## Global Constraints

- **No prose written by Claude.** Every step text, title, hint and praise line in `config/training.md` is the literal placeholder `TODO` (owner writes the real text). No other player-facing wording is added beyond the menu labels "Training", "Controls", "Sailing Lesson", which are labels, not narrative.
- Training replaces the main-menu row "Controls" (same index 3); Controls moves under Training.
- No per-frame allocation; code builds for the normal, `SCENARIO=1` and `DEMO=1` variants.
- Free Range, Journey, Racing, ghosts and best times are unchanged.
- Lesson uses the code's real B behaviour: quick press toggles auto trim, hold starts the engine (`engine.h`).
- Wind angles: `g->wind.dir` is where the wind blows TOWARD; the wind's source is `wrap_pi(wind.dir + M_PI)`.

## Review Focus

- Player already has the engine on when entering the irons step: irons never happens. Expect the lesson to hold the engine off until the engine step.
- Player passes through a goal angle briefly (turning): must not advance; the goal must be held for `TRAIN_HOLD_S`.
- Player capsizes or wrecks mid-lesson: lesson must not advance and must offer the normal restart; restart returns to the current step, not step 1.
- `config/training.md` missing or with fewer blocks than steps: show a one-line notice, never crash.
- Wind shifts or gusts moving the goal under the player: lesson wind is steady (no shift, gust or variation).

---

### Task 1: Training screen in the main menu

**Files:**
- Modify: `src/sim/menu.h`, `src/sim/menu.c`, `tests/test_menu.c`
- Modify (compile fixes only): `src/pd/menu_draw.c`, `src/pd/scenario.c`, any other user of `MENU_ITEM_HELP` (`grep -rn MENU_ITEM_HELP src tests`)

**Interfaces:**
- Produces: `MENU_ITEM_TRAINING` (replaces `MENU_ITEM_HELP`, same index), `MENU_TRAINING` screen enum value, `MENU_START_TRAINING` action, `MENU_TRAINING_ROWS` (= 2) with `enum { TRAIN_ROW_CONTROLS, TRAIN_ROW_LESSON }`. `MENU_MAIN_NAMES[MENU_ITEM_TRAINING]` = `"Training"`. `menu_rows()` returns `MENU_TRAINING_ROWS` on that screen.

- [ ] **Step 1: Write failing tests in `tests/test_menu.c`**: `test_training_opens_two_rows` (main row `MENU_ITEM_TRAINING` + A → `screen == MENU_TRAINING`, `menu_rows == 2`, `sel == 0`); `test_training_controls_opens_help_and_b_returns_to_training` (A on row 0 → `MENU_HELP`; B → `MENU_TRAINING`, `sel == TRAIN_ROW_CONTROLS`); `test_training_lesson_returns_start_action` (row 1 + A returns `MENU_START_TRAINING`); `test_training_b_returns_to_main_on_training_row` (B → `MENU_MAIN`, `sel == MENU_ITEM_TRAINING`). Update the existing `test_help_opens_scrolls_and_returns` to open help via Training and expect B to return to `MENU_TRAINING`.
- [ ] **Step 2: Run** `make -C tests build/test_menu && tests/build/test_menu` → FAIL (compile error, symbols undefined).
- [ ] **Step 3: Implement** in `menu.c`/`menu.h`: rename item and label, add the screen, `update()` handling (UP/DOWN wrap over 2 rows, A/B as above), `menu_rows`, and make `help_update` return to `MENU_TRAINING` with `sel = TRAIN_ROW_CONTROLS`. Demo build: Training row stays open.
- [ ] **Step 4: Run** `make -C tests test` → all PASS (fix other tests that referenced `MENU_ITEM_HELP`).
- [ ] **Step 5: Commit** `git add -A && git commit -m "Menu: Training screen holding Controls and the Sailing Lesson"`.

### Task 2: Lesson state machine

**Files:**
- Create: `src/sim/training.h`, `src/sim/training.c`, `tests/test_training.c`

**Interfaces:**
- Consumes: `Game` (`game.h`): `g->b.heading`, `g->b.u`, `g->wind.dir`, `g->at.on`, `g->en.on`, `g->run`.
- Produces:

```c
typedef enum {
    TRAIN_DOWNWIND_AUTO, TRAIN_BEAM_AUTO, TRAIN_POINTS_AUTO,
    TRAIN_CLOSE_HAULED, TRAIN_IRONS, TRAIN_ENGINE_FREE,
    TRAIN_DOWNWIND_MANUAL, TRAIN_BEAM_MANUAL, TRAIN_POINTS_MANUAL,
    TRAIN_DONE, TRAIN_STEPS
} TrainStep;
typedef struct { TrainStep step; float held; unsigned seen; } Training;   /* held: s the goal has held; seen: points-of-sail bitmask */
void training_init(Training *t);
/* One sailing frame. Advances at most one step per call. Does nothing unless g->run == RUN_SAILING. */
void training_update(Training *t, const Game *g, float dt);
/* Whether the lesson wants the engine kept off this frame (every step before TRAIN_ENGINE_FREE). */
bool training_engine_locked(const Training *t);
float training_off_wind(const Game *g);   /* |angle| from the wind's source to the heading, rad, 0..pi */
```

Constants in `training.h` (tune in Task 5): `TRAIN_HOLD_S 2.0f`; downwind `off_wind > 150 deg`; beam `70..110 deg`; close reach `45..70 deg`; broad reach `110..150 deg`; close-hauled `35..50 deg`; irons `off_wind < 30 deg && u < 0.3 m/s`; free `!engine locked, en.on, off_wind > 45 deg, u > 0.5 m/s`. Auto steps require `g->at.on`; manual steps require `!g->at.on`. TRAIN_POINTS_* hold each of close reach and broad reach for `TRAIN_HOLD_S` (tracked in `seen`).

- [ ] **Step 1: Write failing tests** in `tests/test_training.c`, each building a `Game` via `game_init` and setting `b.heading`, `wind.dir`, `b.u`, `at.on`, `en.on` directly: `test_downwind_auto_advances_after_hold` (not before `TRAIN_HOLD_S`), `test_passing_through_angle_does_not_advance` (angle in goal for < hold, then out: `held` resets), `test_auto_step_needs_autotrim_on`, `test_manual_step_needs_autotrim_off`, `test_points_needs_both_close_and_broad_reach`, `test_irons_needs_low_speed_and_pointing_into_wind`, `test_engine_step_needs_engine_on_and_moving_free`, `test_engine_locked_until_engine_step`, `test_no_advance_when_capsized`, `test_done_is_terminal`, `test_nan_inputs_do_not_advance`.
- [ ] **Step 2: Run** `make -C tests build/test_training && tests/build/test_training` → FAIL (file missing).
- [ ] **Step 3: Implement** `training.c` per the interfaces; use `wrap_pi` from `mathx.h`; guard non-finite heading/speed/dt.
- [ ] **Step 4: Run** `make -C tests test` → PASS.
- [ ] **Step 5: Commit** `git add -A && git commit -m "Training: lesson state machine"`.

### Task 3: Lesson text file (placeholders)

**Files:**
- Create: `config/training.md`, `src/sim/trainingtext.h`, `src/sim/trainingtext.c`, `tests/test_trainingtext.c`

**Interfaces:**
- Consumes: `TRAIN_STEPS` (Task 2).
- Produces: `int trainingtext_parse(const char *md, TrainingText *out);` and `const char *trainingtext_get(const TrainingText *t, TrainStep s);` where `TrainingText` holds `TRAIN_STEPS` strings of at most `TRAINTEXT_LEN 160` chars. File format: one `# <step name>` heading per step in `TrainStep` order, the body is the text under it. `trainingtext_get` returns `"config/training.md is missing text for this step"` for a missing or empty block.

`config/training.md` is created with one heading per step (named as the `TrainStep` enumerators, lowercased) and body `TODO`. **Claude writes no other words in this file.**

- [ ] **Step 1: Write failing tests**: `test_parse_reads_one_block_per_step`, `test_missing_blocks_fall_back_to_notice`, `test_long_body_is_truncated_not_overflowed`, `test_null_or_empty_input_gives_all_notices`, `test_shipped_file_has_a_block_for_every_step` (reads `../config/training.md`).
- [ ] **Step 2: Run** the test → FAIL.
- [ ] **Step 3: Implement** parser (no allocation; copy into fixed buffers).
- [ ] **Step 4: Run** `make -C tests test` → PASS.
- [ ] **Step 5: Commit** `git add -A && git commit -m "Training: lesson text file with TODO placeholders"`.

### Task 4: Wire the lesson into the app and draw it

**Files:**
- Modify: `src/app/app.c`, `src/app/app.h`, `src/pd/menu_draw.c`, `src/pd/hud.c`, `src/pd/main.c` (as needed for build flags), `Makefile` only if sources need listing (they use wildcards)

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: `const Training *app_training(void);` (NULL outside a lesson), `const char *app_training_text(void);` (current step's text, NULL outside a lesson).

- [ ] **Step 1: Write failing test** in `tests/test_menu.c` or a new `tests/test_training_app.c` only if `app.c` is host-buildable; otherwise cover with the scenario in Task 5. (Check: `grep -n app tests/Makefile`; `app.c` is not in `SRC`, so verification here is via Task 5.)
- [ ] **Step 2: Implement `app.c`**: on `MENU_START_TRAINING` call `game_set_track(&game, NULL)`, copy a steady-wind preset into `game.weather.preset[preset_index]` (`wind_var = 0`, `shift_rate_deg = 0`, `gust_speed = 0`), set the starting heading downwind (heading = `wind.dir`), `training_init`, `sys_menu_build(true)`, `camera_snap()`. Each play frame call `training_update`, and force `game.en.on = false` while `training_engine_locked`. On capsize/wreck, A restarts via the existing path but keeps `Training.step`. `show_menu()` returns to the Training screen when leaving a lesson. Load `config/training.md` once next to `help_load()`.
- [ ] **Step 3: Implement drawing**: `menu_draw.c` draws the MENU_TRAINING list the same way the other lists are drawn; `hud.c` draws `app_training_text()` with the existing `message()` style (`hud.c:23`) while a lesson is active.
- [ ] **Step 4: Build all variants**: `make`, `make SCENARIO=1`, `make DEMO=1` → each prints no errors and produces its `.pdx`.
- [ ] **Step 5: Commit** `git add -A && git commit -m "Training: start the lesson from the menu and show its text"`.

### Task 5: Prove the lesson end to end and tune thresholds

**Files:**
- Create: `tests/scenarios/training_lesson.*` (follow the existing format in `tests/scenarios/`), possibly a `tests/test_training_sim.c`
- Modify: threshold constants in `src/sim/training.h`

- [ ] **Step 1: Write a headless test** (`test_training_sim.c`) that drives a real `Game` with scripted inputs (crank to steer, B quick press for auto trim) and asserts that: starting downwind with auto trim on completes `TRAIN_DOWNWIND_AUTO`; the boat steered into the wind with the engine off reaches the `TRAIN_IRONS` goal (this also confirms the sim really stalls the boat in irons and does not turn itself out; if it does not, report to the owner before continuing); with the engine on and the rudder over it reaches `TRAIN_ENGINE_FREE`.
- [ ] **Step 2: Run** `make -C tests test` → PASS; tune `TRAIN_*` constants until the script passes with margin and no goal is reachable by accident at the start heading.
- [ ] **Step 3: Run the full suite** `make -C tests test` and the scenario harness the repo documents → PASS.
- [ ] **Step 4: Commit** `git add -A && git commit -m "Training: end-to-end lesson test and tuned thresholds"`.

### Task 6: Hand the text back to the owner

- [ ] **Step 1:** List for the owner every `TODO` block in `config/training.md` with its step name and the goal it accompanies, plus the stale B-hold sentence in `config/help.md` (hold = engine, quick press = auto trim). Do not write replacement wording.
