# Catamarans (Hobie 16) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boat files can describe a two-hull boat that flies its windward hull for less drag, drawn as two hulls joined by two square crossbeams, and `boats/hobie16.json` ships as a racer.

**Architecture:** Four new `hull` fields. A pure geometry module (`hullgeom`) gets the leeward-hull pivot and the per-hull immersion function. Physics (`boat_step`) and drawing (`render.c`) both use those, so a hull drawn clear of the sea is a hull that has stopped dragging. Every new branch is keyed on `hulls == 2`. Monohulls go through today's code untouched.

**Tech Stack:** C99, Playdate SDK, in-repo test harness `tests/t.h` (`make -C tests`, ASan/UBSan, `-Werror`).

**Spec:** `docs/superpowers/specs/2026-11-03-catamaran-hobie16-design.md`

## Global Constraints

- Monohull boats behave and look bit-for-bit as today:
  - every existing test passes, with no edits to existing assertions;
  - every golden frame in `tests/golden` is unchanged;
  - every shipped ghost in `ghosts/` is unchanged;
  - `make -C tests` stays green after every task.
- New `hull` fields, with these exact ranges and defaults:

  | Field | Range | Default |
  |---|---|---|
  | `hulls` | 1–2 | 1 |
  | `hull_spacing` | 0–20 | 0 |
  | `draft` | 0.05–3 | 0.3 |
  | `fly_drag` | 0–1 | 0.3 |

- `hull.beam` stays the **overall** beam. Each hull's gunwale width is `beam − hull_spacing`, shaped by `look.hull_width`.
- No new button, crank mode, HUD banner or ghost-file field.
- `boats/hobie16.json`:
  - `boat_class` 0;
  - passes `tools/boat_check` with 1–8° of weather helm close-hauled;
  - is under 16 KB;
  - has baseline ghosts for every track.
- Device frame time stays within budget (`tools/check_performance.sh`).
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Heel passing through zero while a hull is flying, e.g. on a tack.** The drawn boat must not jump sideways, because the pivot is continuous: rotating by 0 is the identity. Pinned by `test_pivot_continuous_through_upright` in Task 2.
2. **A wave crest under the windward hull while heeled.** That hull counts as immersed again: no drag saving, and foam is drawn on it. Pinned by `test_wave_under_windward_hull_reimmerses` in Task 2 and `test_cat_sea_heights_under_each_hull` in Task 3.
3. **A boat file with `hulls` 2 but `hull_spacing` 0, or spacing ≥ beam.** Loading clamps or warns, nothing divides by zero, and there is no NaN in immersion or drawing. Pinned by `test_cat_spacing_sanitised` in Task 1 and `test_immersion_degenerate_inputs_finite` in Task 2.
4. **A Hobie heeled past capsize, or with sails dropped by hand.** The capsize/restart flow works as for monohulls, and auto-trim never raises a stowed sail. Pinned by `test_hobie_capsizes_when_sheeted_hard_in_a_gale` in Task 4 and `test_cat_ease_leaves_stowed_sail` in Task 5.
5. **A ghost or follower Hobie drawn from a recorded pose.** It shows the same flying hull as the live boat at the same heel and sea. Render derives the cut from geometry and the sea, never from `BoatState.immersion`, so this holds by construction. Checked with frame dumps in Task 6.

---

### Task 1: Config fields

**Files:**
- Modify: `src/sim/boatcfg.h` (`HullCfg`)
- Modify: `src/sim/boatcfg.c` (field table and the load-time sanity pass)
- Modify: `boats/dinghy.json` (regenerate)
- Modify: `docs/boat-schema.md`
- Test: `tests/test_boatcfg.c`

**Interfaces:**
- Produces: `HullCfg` gains these fields, in this order after `capsize_deg`:
  - `int hulls;`
  - `float hull_spacing;`
  - `float draft;`
  - `float fly_drag;`

- [ ] **Step 1: Write the failing tests**

```c
static void test_cat_fields_default(void) {
    BoatCfg c; boatcfg_default(&c);
    T_ASSERT(c.hull.hulls == 1);
    T_NEAR(c.hull.hull_spacing, 0.0f, 1e-6f);
    T_NEAR(c.hull.draft, 0.3f, 1e-6f);
    T_NEAR(c.hull.fly_drag, 0.3f, 1e-6f);
}
static void test_cat_fields_load_and_clamp(void) {
    const char *s = "{\"hull\":{\"hulls\":2,\"beam\":2.41,\"hull_spacing\":1.95,\"draft\":0.25,\"fly_drag\":0.4}}";
    BoatCfg c; char w[W];
    T_ASSERT(boatcfg_load(&c, s, strlen(s), w, W));
    T_ASSERT(c.hull.hulls == 2);
    T_NEAR(c.hull.hull_spacing, 1.95f, 1e-5f);
    T_NEAR(c.hull.draft, 0.25f, 1e-5f);
    T_NEAR(c.hull.fly_drag, 0.4f, 1e-5f);
    const char *bad = "{\"hull\":{\"hulls\":5,\"draft\":0.0,\"fly_drag\":2}}";
    T_ASSERT(!boatcfg_load(&c, bad, strlen(bad), w, W));
    T_ASSERT(c.hull.hulls == 2);
    T_NEAR(c.hull.draft, 0.05f, 1e-6f);
    T_NEAR(c.hull.fly_drag, 1.0f, 1e-6f);
}
/* Review Focus 3: two hulls need room between them and must fit inside the overall beam */
static void test_cat_spacing_sanitised(void) {
    const char *s = "{\"hull\":{\"hulls\":2,\"beam\":2.0,\"hull_spacing\":0}}";
    BoatCfg c; char w[W];
    T_ASSERT(!boatcfg_load(&c, s, strlen(s), w, W) && strstr(w, "hull_spacing"));
    T_ASSERT(c.hull.hull_spacing > 0 && c.hull.hull_spacing < c.hull.beam);
    const char *wide = "{\"hull\":{\"hulls\":2,\"beam\":2.0,\"hull_spacing\":3}}";
    T_ASSERT(!boatcfg_load(&c, wide, strlen(wide), w, W) && strstr(w, "hull_spacing"));
    T_ASSERT(c.hull.hull_spacing < c.hull.beam);
}
```

Register all three with `T_RUN`. The existing `test_dinghy_json_reproduces_default` and `test_to_json_round_trips` must also pass.

- [ ] **Step 2: Run the tests and check they fail**

Run: `make -C tests build/test_boatcfg.ok`

Expected: a compile error, `no member named 'hulls'`.

- [ ] **Step 3: Implement the fields**

1. Add the four fields to `HullCfg` in `boatcfg.h`, each with a one-line comment in the struct's existing style.
2. Register them in the `boatcfg.c` field table with the ranges and defaults from Global Constraints. `hulls` is an int field, registered the way `boat_class` is.
3. Add a sanity rule in the place where `boatcfg.c` already checks fields against each other. When `hulls == 2` and `hull_spacing` is not in `[0.2·beam, 0.9·beam]`:
   - clamp it into that range;
   - append a warning naming `hull_spacing`.
4. Regenerate `boats/dinghy.json` with the command in the header comment of `tools/gen_boat_json.c`, writing into `build/` rather than `/tmp`.
5. Add the four rows to `docs/boat-schema.md`, worded like the spec's field table.

- [ ] **Step 4: Run the full suite**

Run: `make -C tests`

Expected: every target prints `ok`.

- [ ] **Step 5: Commit**

```bash
git add src/sim/boatcfg.h src/sim/boatcfg.c boats/dinghy.json docs/boat-schema.md tests/test_boatcfg.c
git commit -m "boatcfg: hulls, hull_spacing, draft and fly_drag for catamarans"
```

---

### Task 2: Pivot and immersion geometry

**Files:**
- Modify: `src/sim/hullgeom.h`, `src/sim/hullgeom.c`
- Test: `tests/test_hullgeom.c`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `float hull_pivot(float heel, float spacing);`
    - returns the lateral position (boat frame, + starboard) of the line the boat heels about;
    - that is `+spacing/2` when `heel > 0` (starboard hull leeward) and `−spacing/2` when `heel < 0`;
    - returns 0 when `heel == 0` or `spacing <= 0`.
  - `void hull_heel_about(float heel, float pivot, float y, float z, float *l, float *up);`
    - rotates the boat-frame point `(y, z)` by `heel` about `(pivot, 0)`;
    - writes `*l = pivot + (y−pivot)·cos h + z·sin h` and `*up = z·cos h − (y−pivot)·sin h`;
    - with `pivot == 0` this is exactly render.c's `heel_xf`.
  - `void cat_immersion(float heel, float spacing, float draft, float sea_port, float sea_stbd, float *port, float *stbd);`
    - takes the sea heights relative to the boat's mean waterline under each hull centre, at `y = ∓spacing/2`;
    - computes each keel point `(∓spacing/2, −draft)` through `hull_heel_about` with `hull_pivot(heel, spacing)`;
    - each hull's immersion is `clamp((sea − keel_up)/draft, 0, 1)`;
    - non-finite inputs give 1 for both hulls.

- [ ] **Step 1: Write the failing tests**

```c
static void test_upright_cat_fully_immersed(void) {
    float p, s; cat_immersion(0, 1.95f, 0.25f, 0, 0, &p, &s);
    T_NEAR(p, 1, 1e-6f); T_NEAR(s, 1, 1e-6f);
}
static void test_windward_hull_lifts_off_near_7_deg(void) {   /* asin(0.25/1.95) = 7.37 deg */
    float p, s;
    cat_immersion(deg2rad(7.0f), 1.95f, 0.25f, 0, 0, &p, &s);
    T_ASSERT(p > 0 && p < 0.1f); T_NEAR(s, 1, 1e-3f);          /* heel > 0: port is windward */
    cat_immersion(deg2rad(7.8f), 1.95f, 0.25f, 0, 0, &p, &s);
    T_NEAR(p, 0, 1e-6f);
}
static void test_immersion_mirrors_with_heel(void) {
    float p1, s1, p2, s2;
    cat_immersion(deg2rad(4), 1.95f, 0.25f, 0, 0, &p1, &s1);
    cat_immersion(deg2rad(-4), 1.95f, 0.25f, 0, 0, &p2, &s2);
    T_NEAR(p1, s2, 1e-6f); T_NEAR(s1, p2, 1e-6f);
}
/* Review Focus 2 */
static void test_wave_under_windward_hull_reimmerses(void) {
    float p, s; cat_immersion(deg2rad(12), 1.95f, 0.25f, 0.45f, 0, &p, &s);
    T_ASSERT(p > 0.5f);
}
/* Review Focus 1 */
static void test_pivot_continuous_through_upright(void) {
    float l1, u1, l2, u2;
    hull_heel_about(1e-4f, hull_pivot(1e-4f, 1.95f), -0.975f, 0.2f, &l1, &u1);
    hull_heel_about(-1e-4f, hull_pivot(-1e-4f, 1.95f), -0.975f, 0.2f, &l2, &u2);
    T_NEAR(l1, l2, 1e-3f); T_NEAR(u1, u2, 1e-3f);
}
static void test_pivot_zero_matches_origin_rotation(void) {
    float l, u; hull_heel_about(0.3f, 0, 0.5f, 0.2f, &l, &u);
    T_NEAR(l, 0.5f * cosf(0.3f) + 0.2f * sinf(0.3f), 1e-6f);
    T_NEAR(u, 0.2f * cosf(0.3f) - 0.5f * sinf(0.3f), 1e-6f);
}
/* Review Focus 3 */
static void test_immersion_degenerate_inputs_finite(void) {
    float p, s;
    cat_immersion(NAN, 1.95f, 0.25f, 0, 0, &p, &s); T_ASSERT(p == 1 && s == 1);
    cat_immersion(0.2f, 0, 0.25f, 0, 0, &p, &s);    T_ASSERT(isfinite(p) && isfinite(s));
    cat_immersion(0.2f, 1.95f, 0, 0, 0, &p, &s);    T_ASSERT(isfinite(p) && isfinite(s));
}
```

- [ ] **Step 2: Run the tests and check they fail**

Run: `make -C tests build/test_hullgeom.ok`

Expected: a compile error, `implicit declaration of function 'cat_immersion'`.

- [ ] **Step 3: Implement the three functions in `hullgeom.c`**

Guard against `draft <= 0` with `fmaxf(draft, 0.05f)`. Each declaration in `hullgeom.h` gets a comment in the existing style: frame, sign convention, and what is clamped.

- [ ] **Step 4: Run the tests and check they pass**

Run: `make -C tests build/test_hullgeom.ok`

Expected: `ok  test_hullgeom`.

- [ ] **Step 5: Commit**

```bash
git add src/sim/hullgeom.h src/sim/hullgeom.c tests/test_hullgeom.c
git commit -m "hullgeom: leeward-hull pivot and per-hull immersion for catamarans"
```

---

### Task 3: Sea height under each hull

**Files:**
- Modify: `src/sim/boat.h` (`WaveForces`)
- Modify: `src/sim/sea.c` (`sea_hull_forces`)
- Test: `tests/test_sea.c`

**Interfaces:**
- Consumes: `HullCfg.hulls` and `HullCfg.hull_spacing` from Task 1.
- Produces: `WaveForces` gains `float sea_port, sea_stbd;`. These are the sea heights under the port and starboard hull centres relative to `heave`, in metres, and are 0 on a flat sea. They are appended after `heave`, so zero-initialised `WaveForces` stay a flat sea.

- [ ] **Step 1: Write the failing tests**

```c
/* Review Focus 2: a cat samples under its hulls, not at the overall beam */
static void test_cat_sea_heights_under_each_hull(void) {
    /* set up a sea whose surface tilts across the boat, using the same helpers as the other
       tests in this file; cfg = default with hulls 2, beam 2.41, hull_spacing 1.95 */
    WaveForces f = sea_hull_forces(&sea, &b, &cfg, t);
    T_ASSERT(isfinite(f.sea_port) && isfinite(f.sea_stbd));
    T_ASSERT(f.sea_stbd != f.sea_port);
    /* within one sample of the true surface at y = +-0.975 */
    T_NEAR(f.sea_stbd, sea_height_at_offset(+0.975f) - f.heave, 0.02f);
}
static void test_monohull_wave_forces_unchanged(void) {
    /* same state with the default dinghy: drag_mult, surge, yaw, heel, pitch and heave must equal,
       bit for bit, the values that sea_hull_forces returned before this change */
}
```

Before changing `sea.c`, capture the expected values for `test_monohull_wave_forces_unchanged`:
1. Write the test so it prints the six floats with `%a`.
2. Run it on the current code and paste the printed values in as literals.
3. Compare with `==`.

`sea_height_at_offset` is a small static helper in the test. It calls the file's existing public height query at the world point `±0.975 m` across the heading.

- [ ] **Step 2: Run the tests and check they fail**

Run: `make -C tests build/test_sea.ok`

Expected: a compile error, `no member named 'sea_port'`.

- [ ] **Step 3: Implement**

In `sea_hull_forces`, use a lateral sample width `wid`:
- `wid = hull_spacing` when `hulls == 2`;
- `wid = beam` otherwise. This is the existing variable, so the monohull arithmetic is unchanged.

Pass `wid` to both `sample()` calls and divide `cross` by `wid`.

Set `f.sea_port = a.h[7] - mean` and `f.sea_stbd = a.h[8] - mean`. Run both through `fin()` like the other fields.

- [ ] **Step 4: Run the full suite**

Run: `make -C tests`

Expected: all `ok`, with goldens and baseline unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/sim/boat.h src/sim/sea.c tests/test_sea.c
git commit -m "sea: sample under each hull of a catamaran and report the heights"
```

---

### Task 4: Flying-hull drag in `boat_step`

**Files:**
- Modify: `src/sim/boat.h` (`BoatState`)
- Modify: `src/sim/boat.c` (`boat_init`, `boat_step` hull section)
- Test: `tests/test_boat.c`

**Interfaces:**
- Consumes: `cat_immersion` (Task 2), and `WaveForces.sea_port` / `sea_stbd` (Task 3).
- Produces: `BoatState` gains `float immersion[2]; /* port, starboard: 0 clear of the sea .. 1 at rest draft */`.
  - Derived each step.
  - Always `{1, 1}` on a monohull.
  - Set to `{1, 1}` by `boat_init`.

- [ ] **Step 1: Write the failing tests**

Add a `static void cat_cfg(BoatCfg *c)` helper. It starts from `boatcfg_default` and sets:
- `hulls` 2, `beam` 2.41, `hull_spacing` 1.95, `draft` 0.25;
- `fly_drag` 0.3, `heel_drag` 0.

```c
static void test_monohull_immersion_is_full(void) {
    /* default dinghy, sim_init(90, 6, 3), 5 s of steps: immersion stays exactly {1, 1} */
}
static void test_cat_immersion_tracks_heel(void) {
    /* cat_cfg, flat sea (zero WaveForces), sails stowed (hoists 0), b.heel = deg2rad(15),
       one boat_step: immersion[0] (port, windward) == 0, immersion[1] == 1 */
}
static void test_flying_hull_has_less_drag(void) {
    /* cat_cfg, zero wind, hoists 0, u = 4 m/s. State A heel 0, state B heel 15 deg; hold heel
       (heel_rate 0) and take one 1/30 s step each. Expect B.u > A.u, and the loss ratio
       (4 - B.u) / (4 - A.u) within 0.02 of 1 - fly_drag = 0.7 */
}
static void test_hobie_flies_a_hull_on_a_beam_reach(void) {
    /* boats/hobie16.json, sim_init(90, 6, 3), auto-hiking crew, sheets at autotrim_best_trim, 40 s:
       not capsized, heel between 7 and 40 deg for the last 10 s, mean windward immersion < 0.5,
       and final u exceeds the same run with fly_drag 0 by at least 3% */
}
/* Review Focus 4 */
static void test_hobie_capsizes_when_sheeted_hard_in_a_gale(void) {
    /* hobie16.json, sim_init(90, 14, 3), trims sheeted hard, wheel 0: |heel| passes capsize_deg within 20 s */
}
```

The two `hobie_` tests load the file Task 7 writes. Write them in this task, but register them with `T_RUN` in Task 7, when the file exists.

- [ ] **Step 2: Run the tests and check they fail**

Run: `make -C tests build/test_boat.ok`

Expected: a compile error, `no member named 'immersion'`.

- [ ] **Step 3: Implement**

In `boat_step`, when `h->hulls == 2`:
1. Call `cat_immersion(b->heel, h->hull_spacing, h->draft, wf->sea_port, wf->sea_stbd, &b->immersion[0], &b->immersion[1])`.
2. Take the windward immersion: `b->immersion[0]` when `b->heel >= 0`, otherwise `b->immersion[1]`.
3. Multiply the existing `drag` expression by `(1 − fly_drag·(1 − windward))`.

Otherwise set `b->immersion[0] = b->immersion[1] = 1` and leave the `drag` line byte-for-byte as it is. Righting and crew hiking are untouched.

- [ ] **Step 4: Run the full suite**

Run: `make -C tests`

Expected: all `ok`. In particular `test_baseline` shards and goldens are unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/sim/boat.h src/sim/boat.c tests/test_boat.c
git commit -m "boat: a catamaran's flying hull sheds fly_drag of its resistance"
```

---

### Task 5: Auto-trim eases the sheet on a catamaran

**Files:**
- Modify: `src/sim/autotrim.h` (`Autotrim`)
- Modify: `src/sim/autotrim.c` (`autotrim_update`)
- Test: `tests/test_autotrim.c`

**Interfaces:**
- Consumes: `HullCfg.hulls` (Task 1).
- Produces: `Autotrim` gains `float ease; /* rad let out past the best trim while over the heel limit (cats without reefs) */`. It is zeroed by `autotrim_init`.

- [ ] **Step 1: Write the failing tests**

```c
static void test_cat_eases_instead_of_reefing(void) {
    /* default cfg with hulls 2, hull_spacing 0.8 * beam, every sail reef_levels 0. Hold heel at
       2 x autotrim_heel_deg for 3 s of autotrim_update: every hoist_target stays 1.0 and every
       trim_target exceeds autotrim_best_trim for that wind by > 5 deg (clamped at trim_max_deg) */
}
static void test_cat_sheets_back_in(void) {
    /* continue the previous state with heel 0 for 5 s: ease back to 0 and trim_target within 1e-3
       of best trim */
}
static void test_monohull_without_reefs_still_reefs(void) {
    /* hulls 1, reef_levels 0, same over-heel hold: hoist_target drops below 1, as today */
}
/* Review Focus 4 */
static void test_cat_ease_leaves_stowed_sail(void) {
    /* cat cfg, one sail with hoist_target 0: after the over-heel hold it is still 0 and its trim untouched */
}
```

- [ ] **Step 2: Run the tests and check they fail**

Run: `make -C tests build/test_autotrim.ok`

Expected: `test_cat_eases_instead_of_reefing` fails because a hoist dropped.

- [ ] **Step 3: Implement**

When `cfg->hull.hulls == 2` and the sail's `reef_levels == 0`:
1. Leave that sail's hoist alone.
2. Change `at->ease` once per update, not per sail:
   - while over the heel limit, grow it by `deg2rad(trim_rate_deg)·0.5·dt`;
   - below `limit·HEEL_RELEASE`, shrink it at the same rate;
   - keep it ≥ 0.
3. Set `trim_target = min(best + ease, deg2rad(trim_max_deg))`. Check the units against `autotrim_best_trim`'s return value.

Sails that do reef, and every monohull, take the existing path unchanged.

- [ ] **Step 4: Run the full suite**

Run: `make -C tests`

Expected: all `ok`.

- [ ] **Step 5: Commit**

```bash
git add src/sim/autotrim.h src/sim/autotrim.c tests/test_autotrim.c
git commit -m "autotrim: ease the sheet, not the hoist, on a catamaran without reefs"
```

---

### Task 6: Drawing two hulls and the crossbeams

**Files:**
- Modify: `src/pd/render.c`, including:
  - `heel_xf`, `side_wl`, `far_side`;
  - `draw_hull`, `draw_waterline`, `draw_cockpit` call site;
  - the boat draw sequence and `draw_ghost_pose` / `draw_ghost`;
  - a new `draw_crossbeams`.
- Test: frame dumps (`tools/frames.sh`) and goldens (`make -C tests`).

**Interfaces:**
- Consumes: `hull_pivot`, `hull_heel_about` (Task 2), and `HullCfg.hulls` / `hull_spacing` / `draft` (Task 1).
- Produces: nothing other tasks call.

Approach:
- **Current hull.** Add file statics alongside `ghost_pass`:
  - `cur_hull_y`: lateral centre of the hull being drawn, 0 on a monohull;
  - `cur_hull_hb`: its gunwale half-width, `0.5·beam` on a monohull and `0.5·(beam − hull_spacing)` on a cat.

  A helper `set_hull(const Game *g, int i)` sets them for hull `i` (−1 port, +1 starboard, 0 monohull). Reset them to the monohull values after each per-hull pass, as `ghost_pass` is reset.
- **Heel transform.** `heel_xf` gains the `Game`'s config. When `hulls == 2` it calls `hull_heel_about(heel, hull_pivot(heel, hull_spacing), y, z, …)`. Otherwise it keeps today's two lines, so monohulls stay bit-identical. The pivot applies to everything drawn in the boat frame, rig included.
- **Per-hull geometry.**
  - Hull-geometry points add `cur_hull_y` to `y`. This covers `draw_hull`, `side_wl` and the foam ring.
  - `side_wl`, `far_side` and `draw_waterline` use `cur_hull_hb` where they now use `0.5·beam`.
  - A cat's keel line in `side_wl` is `−draft`, not `HULL_KEEL`, so the drawn lift-off matches `cat_immersion`.
  - The cut is computed by rotating the section through `heel_xf`, with the iteration loop `side_wl` already has. This replaces the call to `hull_side_z`, which rotates about the origin and does not know the hull offset.
  - Monohulls keep the `hull_side_z` path.
  - `far_side` measures the eye's offset from the hull's heeled centre.
  - `draw_waterline` draws the foam ring and wake once per hull, so there are two wakes.
- **Draw order on a cat.** Far hull (the hull whose centre is farther from the eye), then `draw_crossbeams`, then near hull. The existing `rig_behind` rule (centreline `deck_faces_eye`) still decides whether the rig goes before or after this group. Apply the same order in `draw_ghost_pose` and `draw_ghost`. Their hulls come from the recorded heel and the live sea, so ghosts fly hulls with no extra state.
- **`draw_crossbeams`.** Two boxes, one at each station `x = ±L/6` (stations 1/3 and 2/3 from the bow).
  - Each box runs from `y = −spacing/2` to `+spacing/2`, with `z` from `freeboard − s` to `freeboard`, where `s = 0.025·L`.
  - Fill the top face and the end face that points toward the eye with the deck shade, then outline. Ghosts use the lighter ghost ink.
- **Cockpit.** No `draw_cockpit` call when `hulls == 2`.

- [ ] **Step 1: Run the goldens on unchanged code for a baseline**

Run: `make -C tests`

Expected: all `ok`, which is the reference that monohull frames must keep.

- [ ] **Step 2: Implement the per-hull refactor for monohulls only**

Do the `cur_hull_*` statics and the `heel_xf` / `side_wl` / `far_side` / `draw_waterline` parameterisation, still with one hull.

Run: `make -C tests`

Expected: all `ok`, with every golden byte-identical.

- [ ] **Step 3: Add the two-hull branches, crossbeams and draw order**

Render frames of a two-hull test cfg at three heels: 0°, 5° and 20°. Use `tools/frames.sh build/frames-cat` with a temporary scenario, or `frame_dump` with the cfg loaded the way the longship is, at `tools/frame_dump.c:169`.

Expected, checked by looking at the PNGs:
- two hulls with water between them;
- two square beams at deck height;
- at 20° the windward hull is clear of the sea with no foam, and the leeward hull has not sunk;
- at 0° both hulls have foam.

- [ ] **Step 4: Run the full suite**

Run: `make -C tests`

Expected: all `ok`, with the goldens unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/pd/render.c
git commit -m "render: draw a catamaran as two hulls on square crossbeams, heeled about the leeward hull"
```

---

### Task 7: `boats/hobie16.json`, ghosts and budget

**Files:**
- Create: `boats/hobie16.json`
- Create: `ghosts/*hobie16*`, one per track, generated
- Modify: the boat list or lists the game, `test_baseline` and `test_shipped_ghosts` read, if they are not directory-driven
- Modify: `tests/test_boat.c` (register the two `hobie_` tests from Task 4)
- Modify: `tests/test_boatcfg.c` (add `test_hobie16_json`)

**Interfaces:**
- Consumes: everything above.

Starting values (spec: real-world numbers, then tune for feel):

| Group | Values |
|---|---|
| Hull | `length` 5.04, `beam` 2.41, `hulls` 2, `hull_spacing` 1.95, `draft` 0.25, `fly_drag` 0.3, `heel_drag` 0, `mass` 145, `crew_mass` 150, `hiking_arm` ≈ 2.2 (trapeze), `capsize_deg` 55 |
| Righting (deg → N·m) | 0→0, 3→700, 7→1390, 15→1340, 25→1250, 35→1000, 45→450, 50→0, 60→−600 (hull weight × half spacing at lift-off ≈ 1390) |
| Sails | main 13.8 m², jib 5.1 m², `reef_levels` 0 on both |
| Look | `mast_height` 8.1, `mast_x` = L/6 = 0.84 (on the forward beam), `cockpit_depth` 0 |
| Class | `boat_class` 0 |
| Foils | `board_*` stands in for the hulls' grip; start from `boats/j24.json` scaled by area |
| Auto-trim | set `autotrim_heel_deg` so auto-trim holds a hull just flying (≈ 12°) |

- [ ] **Step 1: Write the failing test**

```c
static void test_hobie16_json(void) {
    /* load ../boats/hobie16.json: no warnings, size < 16384, hulls 2, boat_class 0,
       asin(draft / hull_spacing) between 5 and 10 deg, every sail reef_levels 0 */
}
```

Also register `test_hobie_flies_a_hull_on_a_beam_reach` and `test_hobie_capsizes_when_sheeted_hard_in_a_gale` in `test_boat.c`.

- [ ] **Step 2: Run the tests and check they fail**

Run: `make -C tests build/test_boatcfg.ok build/test_boat.ok`

Expected: the tests fail because the file cannot be read.

- [ ] **Step 3: Write the file and tune it until these pass**

```bash
make -C tests build/test_boatcfg.ok build/test_boat.ok
```

```bash
cc -std=c99 -O1 -Isrc/sim -o build/boat_check tools/boat_check.c src/sim/*.c src/pd/scenario.c -lm
```

```bash
build/boat_check boats/hobie16.json
```

Expected: both test targets print `ok`, and `boat_check` reports 1–8° of weather helm close-hauled with no failures. Use the compile line in the header of `tools/boat_check.c` if it differs from the one above.

- [ ] **Step 4: Generate ghosts and run everything**

```bash
make ghosts
```

```bash
git status --short ghosts/
```

```bash
make -C tests
```

Expected:
- `git status` shows only new `hobie16` ghost files, and no existing ghost modified;
- every `test_baseline <track> hobie16` shard prints `ok`;
- the whole suite is `ok`.

- [ ] **Step 5: Check the frame budget and the look**

```bash
tools/check_performance.sh
```

Then sail the Hobie in the simulator with `tools/sim.sh`: reach until a hull flies, tack through it, and capsize once.

Expected:
- frame time within budget, as the script reports it;
- the hull visibly lifts as speed builds;
- no sideways jump on the tack;
- the capsize/restart flow works.

- [ ] **Step 6: Commit**

```bash
git add boats/hobie16.json ghosts/ tests/test_boat.c tests/test_boatcfg.c
git commit -m "boats: Hobie 16 catamaran, with baseline ghosts"
```
