# Journey gate perpendicular to the leg — design

## Intent
In journey mode a buoy's gate ray points on past the buoy (along the incoming leg's heading), so the drawn
line makes no sense and is awkward to cross. The gate should be at 90 degrees to the previous-buoy to
this-buoy leg so the boat crosses it side-on while sailing by.

## Goals
- Journey gate = ray from the buoy, length unchanged, at the incoming heading -/+ 90 degrees.
- Still one-sided and directional, as in racing: the boat must cross the ray, so it passes on the gate side.
- The gate side is the inside of the turn (the existing rounding rule: rounded towards the way the course turns next).
  Starboard rounding: gate = incoming - 90 deg. Port: gate = incoming + 90 deg.
- Turn arrow (3D view) and map arrow still arrive at / open towards the right place.

## Non-goals
Race-mode tracks (they author `gate_deg`), `gate_crossed`, the render code, a full (two-sided) line.

## Approaches
A (chosen): set `gate` to its final heading in `spawn()` when the successor decides the rounding; the newest
buoy keeps a provisional starboard gate until then (never targeted before then: JOURNEY_AHEAD = 2).
B: store incoming heading per buoy, derive gate at each use: more churn. C: separate `side` field: two angles, rejected.

## Left to the builder
`gate` no longer equals the incoming heading, so the map arrow's "open towards where the boat arrives" must use
incoming = gate + rounding * pi/2.
