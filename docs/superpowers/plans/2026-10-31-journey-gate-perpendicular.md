# Journey Gate Perpendicular Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Journey buoys' gate rays sit at 90 degrees to the incoming leg, on the inside of the turn.

**Architecture:** `spawn()` in `src/sim/journey.c` already fixes a buoy's rounding when its successor spawns. It also sets `gate = incoming - rounding * pi/2` then. Everything downstream (`gate_crossed`, 3D gate/arrow render) already reads `gate` and `rounding`; only the map arrow's gap angle needs adjusting.

**Tech Stack:** C, project test runner (`tests/t.h`, `tests/test_journey.c`).

**Spec:** `docs/superpowers/specs/2026-10-31-journey-gate-perpendicular-design.md`

## Global Constraints

- Race mode (`race.c`, track files) is untouched.
- Heading frame: `gate`/headings are `atan2(y, x)` radians, x north; heading grows clockwise for starboard. For rounding `r` (ROUND_STARBOARD = 1, ROUND_PORT = -1): travel heading at the gate = `gate + r * pi/2`.
- The working tree has unrelated uncommitted changes: `git add` only the files each task names.

## Review Focus

- First buoy (previous point is the start, not a buoy): gate perpendicular to start to buoy leg once its successor exists.
- Sailing past on the wrong side of the buoy must not pass it.
- Crossing the line's back extension (opposite side of the buoy from the ray) must not pass it.
- Leg that turns exactly straight (`h == course`): rounding resolves to starboard, gate still perpendicular.
- `journey_drop_oldest` / `journey_shift` keep gates valid (gate is an angle; unaffected).

---

### Task 1: Perpendicular gate in `spawn()`

**Files:**
- Modify: `src/sim/journey.c` (`spawn`, ~lines 47-57), `src/sim/journey.h:12` (gate comment)
- Test: `tests/test_journey.c`

**Interfaces:**
- Consumes: existing `JourneyBuoy { x, y, gate, rounding }`, `wrap_pi`.
- Produces: invariant for every buoy with a successor (index < n-1): `wrap_pi(gate + rounding*pi/2 - incoming) == 0`, where incoming is the heading of the leg from the previous buoy (or the start) to it. The newest buoy has `rounding = ROUND_STARBOARD` and `gate = incoming - pi/2` (provisional).

- [ ] **Step 1: Write the failing tests** in `tests/test_journey.c`, and register with `T_RUN`:
  - `test_gate_perpendicular_to_leg`: for seeds 1..20, `journey_init(&j, 100, 300, seed, 0, 0, 0.3f, 0, 0)`; for each buoy k < j.n-1, incoming = `atan2(y - py, x - px)` with (px,py) the previous buoy or (0,0). Assert `fabsf(wrap_pi(b->gate + b->rounding * 1.5707963f - incoming)) < 1e-3f`. Also assert the newest buoy's `rounding == ROUND_STARBOARD` and the same identity with rounding = 1.
  - `test_gate_passes_only_on_gate_side`: init as above, take `journey_next`, incoming per the identity (`gate + rounding*pi/2`), unit travel `(cos h, sin h)`, unit gate `(cos g, sin g)`. Moving from `b - 5*travel + 3*gate` to `b + 5*travel + 3*gate` (copies of Journey each time) → `journey_update` returns true. Same move with `-3*gate` → false. Move along the gate line's back extension side, from `b - 5*travel - 3*gate`... covered by previous; plus a move parallel to the leg but crossing the line 3 m *behind the gate ray's origin side* is the `-3*gate` case, so assert that one is false.
  - Edit `test_legs_stay_out_of_the_wind` (line ~132): the assertion on `j.buoy[k].gate` must use the incoming heading `wrap_pi(gate + rounding*1.5707963f)` instead of `gate`; likewise line ~150 for `j.buoy[j.n-1]`.
  - Edit the comment on `sail_to_next` to say "crosses its gate ray, buoy on the side the rounding names" (helper logic is unchanged).

- [ ] **Step 2: Run to verify failure** — Run: `make test` (or the project's test target; check `Makefile`). Expected: the two new tests FAIL.

- [ ] **Step 3: Implement** in `spawn()`: where `prev->rounding` is decided, compare `wrap_pi(h - j->course) >= 0.0f` (the incoming heading of `prev`, since `j->course` is not yet updated) instead of `h - prev->gate`, then set `prev->gate = wrap_pi(j->course - prev->rounding * 1.5707963f)`. For the new buoy set `b->gate = wrap_pi(h - 1.5707963f)` with the provisional starboard rounding. Update the `gate` comment in `journey.h` to: "rad, heading of the ray from the buoy the boat crosses to pass: 90 deg to the incoming leg, on the inside of the turn; travel heading there = gate + rounding * pi/2".

- [ ] **Step 4: Run to verify pass** — `make test`. Expected: all tests PASS, including the pre-existing journey tests (`sail_to_next` still crosses correctly).

- [ ] **Step 5: Commit**

```bash
git add src/sim/journey.c src/sim/journey.h tests/test_journey.c docs/superpowers
git commit -m "feat: journey gate is perpendicular to the incoming leg"
```

### Task 2: Journey map arrow gap

**Files:**
- Modify: `src/pd/hud.c:338` (call in `draw_journey_map`)

**Interfaces:**
- Consumes: Task 1's invariant (incoming heading = `gate + rounding * pi/2`).

- [ ] **Step 1: Change** the gap argument of `map_round_arrow(pd, mx, my, b->rounding, ...)` from `b->gate + 3.14159265f` to `b->gate + (float)b->rounding * 1.57079633f + 3.14159265f` (gap centred on where the boat arrives from, i.e. behind the incoming leg), and fix its comment.

- [ ] **Step 2: Verify build and look** — Run `make` (simulator/test build per Makefile) and confirm it compiles; run the journey scenario/simulator if available (see `/run`) and check the map arrow's open side faces the previous buoy and the 3D gate line sits to the inside of the turn, side-on to the leg. Expected: no warnings; visual check matches.

- [ ] **Step 3: Commit**

```bash
git add -p src/pd/hud.c   # hud.c already has unrelated uncommitted edits: stage only the map-arrow hunk
git commit -m "fix: journey map arrow opens towards the incoming leg"
```
