# Training section and sailing lesson - design

## Intent
Players who have never sailed can't tell what the crank, B and the d-pad do for a boat. Add a **Training**
item to the main menu holding the existing **Controls** help and a new guided **Sailing Lesson** that teaches
the boat controls by having the player actually do each thing. All narration is written by the project owner
(Playdate publishing rules require human-written narrative); the code ships only empty placeholders.

## Goals
- Main menu: Training replaces the Controls row. Training opens a two-row screen: Controls, Sailing Lesson.
- A lesson that starts downwind, moves to wind from the side, and walks through every point of sail, first with
  auto trim on, then trimming the sail by hand with the crank.
- A step where the player sails close to the wind and gets stuck in irons, followed by a step where the
  lesson prompts them to start the engine (hold B) to get unstuck.
- Each step advances only when the player has done it.

## Non-goals / anti-goals
- No prose written by Claude: no step text, titles, hints or praise lines. Placeholders only.
- No new physics. In irons is whatever the existing sim produces; the lesson detects it.
- No effect on Free Range, Journey, Racing, ghosts or best times.

## Steps (goal checked from Game state each frame)
1. Downwind, auto trim on.
2. Wind from the side (beam reach), auto trim on.
3. Sail each remaining point of sail (close reach, broad reach), auto trim on.
4. Close to the wind, then point into it until boat speed falls below a threshold (in irons).
5. Start the engine (hold B) and use it to get the bow off the wind and moving again.
6. Auto trim off. Repeat 1-3 trimming the sail by hand with the crank (Trim mode, RIGHT).
7. Finish; return to the Training screen.

## Approaches considered
- **A. Lesson module in the sim code (chosen).** New `src/sim/training.c/.h`: a step table, each step with a
  goal predicate over `Game` (wind angle, autotrim on/off, boat speed, engine on, trim input). Pure C, so it is
  unit-testable like the rest of `src/sim`.
- B. Step checks inside `app.c`: faster, but untestable headless and tangles the app flow.
- C. Steps defined in a data file with a goal mini-language: more flexible than six steps justify.

## Design
- `menu.c`: replace `MENU_ITEM_HELP` ("Controls") with `MENU_ITEM_TRAINING` ("Training"); add a
  `MENU_TRAINING` screen with rows Controls (opens the existing help screen, B returns to Training) and
  Sailing Lesson (returns a new `MENU_START_TRAINING` action). Demo build: Training stays open (Controls is
  read-only); the lesson uses the demo's existing run rules, to be confirmed.
- `app.c`: on `MENU_START_TRAINING` start a free-range game with a fixed steady wind from a fixed direction and
  a forgiving boat (dinghy or sunfish, builder's call), then call `training_update` each play frame. B pauses
  to the existing "go back" question.
- `training.c`: owns current step, tracks the goal, exposes `training_text(step)` for the HUD. Step text is
  loaded from `config/training.md` (one block per step, like `config/help.md`), shipped with `TODO` bodies.
- HUD draws the current step's text using the existing HUD message style.

## Constraints
- Playdate: C, no allocation per frame, same build targets (normal, scenario, demo).
- `config/help.md` currently says holding B toggles auto trim, but `engine.c` makes a hold start the engine and
  a quick press toggle auto trim. The lesson must use the code's behaviour; help.md needs correcting (owner's
  wording).

## Left to the builder / owner
- Owner: write every step's text in `config/training.md`.
- Builder: boat and wind choice; thresholds for "close to the wind", "in irons" and "downwind" (tune in a
  scenario test); confirm the boat actually stalls and can't turn itself out of irons so step 5 is needed.
