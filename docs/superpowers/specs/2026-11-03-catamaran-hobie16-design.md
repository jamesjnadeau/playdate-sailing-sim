# Catamarans, starting with the Hobie 16 — design

Written from the brainstorm conversation on 2026-11-03. Proposals of Claude's that James did not
explicitly decide are marked *(my call)*.

## Intent
James wants catamarans in the game: a Hobie 16 now, a Lagoon 42 cruising cat later. The point of a
beach cat is the feel of **flying a hull**: the windward hull lifts clear of the water, the boat gets
faster, and the player holds it there with the helm and the sheet without capsizing. The boat must
also *look* like a catamaran: two hulls with water between them.

## Goals
- Boat files can describe a two-hull boat. The game draws two hulls joined by **two square-section
  crossbeams, evenly spaced along the boat**. No trampoline is drawn.
- Whether each hull is in the water is worked out from geometry, and the drawing and the physics use
  the same answer: a hull you see lifting is a hull that has stopped dragging.
- Flying a hull is **rewarded**: less drag, so more speed. Past the balance point the boat capsizes.
- The player manages it with the controls they already have: steering (head up / bear away) and
  trimming the sail. That is the challenge.
- `boats/hobie16.json`, a racer (`boat_class` 0), that feels right to sail.

## Non-goals (this round)
- The Lagoon 42 and its cabin (see "Next round" below).
- A trapeze control, crew-position control, or any new button or crank mode.
- Pitchpoling, twin rudders (one rudder is modelled), daggerboards, a drawn trampoline.
- Full per-hull buoyancy (righting stays a table).

## Anti-goals
- Any change in how existing monohull boats sail or look. A boat file without the new fields must
  behave bit-for-bit as today (existing tests, goldens and baseline ghosts unchanged).
- A hull drawn clear of the water while the physics still drags it, or the reverse.
- Dropping the Playdate below its frame budget because there are two hulls to draw.

## Approaches considered
1. **Fixed thresholds.** The file sets a lift-off heel and a drag factor. Cheapest, but the drawing and
   physics can disagree. Rejected.
2. **Geometry-driven immersion (chosen by James).** Hull count and spacing in the file; each hull's
   immersion (0..1) comes from heel, spacing and draft. Drawing and drag both use it. Righting stays the
   existing table.
3. **Full per-hull buoyancy.** Righting and drag emerge from geometry, no table. Most realistic, but it
   replaces the tuned model every boat depends on, is hard to tune and costs CPU per frame. Rejected.

## Design

### Boat file (new fields, all in `hull`, ranges in `src/sim/boatcfg.c` as usual)
| Field | Range | Default | Meaning |
|---|---|---|---|
| `hulls` | 1-2 | 1 | number of hulls |
| `hull_spacing` | 0-20 | 0 | m between the hull centrelines (ignored when `hulls` is 1) |
| `draft` | 0.05-3 | 0.3 | m, a hull's depth below the waterline at rest; sets the lift-off heel |
| `fly_drag` | 0-1 | 0.3 | fraction of resistance removed when the windward hull is fully clear |

- `hull.beam` stays the **overall** beam, so its existing uses (rock collision half-width, sea sampling
  across the boat, which side is hidden) keep meaning the whole boat. Each hull's own width is
  `beam − hull_spacing`, shaped by the existing `look.hull_width` profile *(my call: no extra field)*.
- `docs/boat-schema.md` and `tools/gen_boat_json.c` defaults gain the fields; `boats/dinghy.json` must
  still match the generated file.

### Immersion (new, in `src/sim`, testable without drawing)
- For a two-hull boat each hull gets an immersion value 0..1: 1 = at its rest draft, 0 = its keel line
  is clear of the sea.
- **The boat heels about its leeward hull** once heeled: the leeward hull stays at its rest draft, and
  the windward hull rises by `hull_spacing × sin(heel)`. Windward immersion = `1 − rise / draft`, clamped.
  With the Hobie's ~0.25 m draft and ~1.95 m spacing, lift-off is about 7°.
- The sea's height under each hull counts: `sea.c` already samples the surface at ±beam/2 across the
  boat, which on a cat is under the hulls *(my call: use those samples so a wave under the windward hull
  sets it back down, rather than flat-water geometry)*.
- The value is stored on the boat state each step, so render, sound and tests read the same number.

### Physics (`src/sim/boat.c`)
- Resistance on a two-hull boat: `R(speed) × (1 − fly_drag × (1 − windward immersion))`.
  `heel_drag` is set to 0 in the Hobie file.
- Righting: the existing table, shaped for a cat: a steep climb to lift-off, then falling to zero at the
  balance point (~50° on the Hobie), with `capsize_deg` a little past it. The table holds the hull's
  own weight; the crew's moment is the existing `crew_mass × hiking_arm` term, with a long
  trapeze-length `hiking_arm` in the Hobie file. Crew hiking stays automatic.
- Monohulls skip all of this (`hulls` = 1 → identical code path to today).

### Auto-trim (`src/sim/autotrim.c`)
- On a two-hull boat whose sails have `reef_levels` 0, over `autotrim_heel_deg` it **eases the sheet**
  instead of reefing, and sheets back in once under the release threshold *(my call: keyed on both, so
  monohulls with `reef_levels` 0 keep reefing exactly as today, and the Lagoon, which reefs, will reef)*.
  The Hobie file sets the limit so auto-trim holds a hull just flying.
- The manual hoist control is unchanged: the player can still lower a Hobie's sails by hand
  *(my call)*.

### Drawing (`src/pd/render.c`)
- `draw_hull` is run once per hull, offset ±`hull_spacing/2` across the boat, each with its own
  waterline/foam (via `side_wl`) so a hull clear of the sea shows no foam and its full topsides.
- The heel transform pivots about the leeward hull for two-hull boats, matching the immersion model, so
  the leeward hull does not sink as heel builds.
- Hidden-side and deck-facing tests are per hull.
- **Two crossbeams**, square section, spanning hull to hull at deck height, at one third and two thirds
  of the hull length *(my reading of "evenly spaced")*. Section side ≈ 2.5 % of hull length *(my
  call)*. Drawn with the deck shade. The mast sits on the forward beam: the Hobie file puts `mast_x`
  there.
- Ghosts and the trailing follower draw from a recorded pose with no immersion in it. They
  **recompute** immersion from the pose's heel and the sea at their own position, through the same
  function as the physics *(my call: the ghost file format and the shipped ghosts stay unchanged)*.
- The leeward-hull pivot is drawing only. The physics position stays the boat's centreline, so near the
  balance point the drawn hulls sit up to ~0.6 m to one side of the collision footprint *(my call:
  acceptable; moving the physics position with heel is a much bigger change)*.
- No cockpit is drawn on a two-hull boat this round (the Hobie sets `cockpit_depth` 0).
- Two wakes, one astern of each hull.

### The Hobie 16 file (`boats/hobie16.json`)
Real-world starting points; the builder tunes for feel. LOA 5.04 m, overall beam 2.41 m, hull spacing
~1.95 m, hull mass ~145 kg, crew 2 × 75 kg on trapeze, main ~13.8 m², jib ~5.1 m², mast ~8.1 m.
No daggerboards: the `board_*` foil stands in for the asymmetric hulls' grip. `boat_class` 0 (racer).

## Constraints
- Must pass `tools/boat_check.c` (1-8° of weather helm close-hauled).
- As a racer it needs baseline ghosts for every track (`make` ghosts target), shipped in `ghosts/`.
- Frame time on device: check with `tools/check_performance.sh`; two hulls must not push it over budget.
- File stays under 16 KB.

## Next round (decided, not built now)
Lagoon 42 as a cruiser (`boat_class` 1). On a two-hull boat the cockpit fields become a **raised cabin
block**: `cockpit_x0`/`x1` its aft/forward ends, `cockpit_w0`/`w1` its half-widths (a narrower front
gives the tapered nose), `cockpit_depth` its height above the beams, 0 = no cabin. A solid box sitting
on the beams. Monohulls keep the cockpit well.

## Left to the builder
- Exact tuning numbers for the Hobie (righting table, resistance, `fly_drag`, foils, camera).
- How the leeward-hull pivot is computed when heel changes sign (should be continuous through upright).
- Test scenarios; whether flying should also feed the sea sound (nice, not required).
- Feedback beyond the visual hull lift: none is added *(my call; no HUD banner)*.
