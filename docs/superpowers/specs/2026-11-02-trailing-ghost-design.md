# Trailing ghost — design

Small change; written from the brainstorm conversation.

## Intent
In journey and free range there is nobody else on the water. A faint ghost boat that sails the path you
just sailed, about 30 m behind you, gives the sea company and shows your own wake of decisions.

## Behaviour (as decided)
- Modes: journey and free range only. Race mode is untouched (it already has the race ghost).
- The ghost replays the player's own track: same heading, heel, wheel, sail trim, hoist and boom side. It
  sits at the point on the trail **30 m of path length behind** the boat. Its lag in seconds is therefore
  30 / speed: shorter when fast, longer when slow. (James chose distance over a fixed time delay, since the
  two cannot both hold.)
- Drawn like the race ghost: the boat's own hull and rig routines, halved fills, dithered outline.

## Goals / non-goals
- Goals: the above; the trail survives the floating-origin shifts; bounded memory.
- Non-goals: a toggle, a HUD gap/arrow, collision, a ghost that reacts to wind or terrain, any change to
  the race ghost, saving the trail.
- Anti-goals: the ghost drifting, jumping or flickering when the origin shifts or the boat slows; a ghost
  drawn on top of the camera.

## Approaches considered
- **A. Breadcrumb trail in `Game` (chosen).** A ring of recent poses, spaced by distance, recorded by
  `game_update`; the ghost is read off the trail at 30 m back. Shifts with the origin like `Journey`.
- B. A second simulated boat steered to hold 30 m. Rejected: a second sim and autopilot per frame, and it
  would not match the player's moves.
- C. Fixed time delay on a pose history. Rejected: distance is 0 at rest and speed-dependent otherwise.

## Design
- New `src/sim/follower.{c,h}`: `Follower` ring of `FollowSample`s (pose + cumulative path length `s`).
  A sample is appended when the boat has moved `FOLLOW_STEP_M` (0.25 m) from the newest; otherwise the
  newest is overwritten, so the newest is always the boat's current pose. 256 samples cover at least 64 m.
- `follower_pose(f, behind_m, &out)` interpolates the sample pair around path length `s_head - behind_m`;
  false until the boat has sailed `behind_m` (my call: the ghost appears once there is a full 30 m of
  trail, rather than sitting on the start line).
- `Game` owns a `Follower follow`; `reset` clears it; `game_update` records after `recentre`, which shifts it.
  Recording is skipped in race mode.
- Render: `draw_ghost` is split so a local-frame pose can be drawn; `render_frame` draws the follower when
  the mode is not race.
- Memory: ~33 KB added to the static `Game`.

## Consequences to know
- The default trailing camera looks forward, so a ghost 30 m astern is behind the camera. It shows when the
  boat turns, when the player cranks the look round, or in a gybe. No indicator is added (non-goal).

## Left to the builder
- Interpolation of angle fields (use `wrap_pi` for heading), tests' exact scenario numbers.
