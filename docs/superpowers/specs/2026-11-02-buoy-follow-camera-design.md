# Buoy-follow camera — design

Small change; written from the brainstorm conversation.

## Intent
Racing, you want to see where you are going. A look mode that keeps the camera pointed at the next buoy,
so you don't have to crank the look round every leg.

## Behaviour (as decided)
- **Toggle:** holding DOWN for `controls.camera_hold_s` toggles between *behind-follow* (today's trailing
  camera, look yaw 0) and *buoy-follow*. This replaces DOWN-hold-to-recentre. Recentre stays on the second
  UP press (`up_again`).
- **Buoy-follow:** camera yaw is driven so the camera faces the next buoy. Pitch/elevation is untouched
  (`camera_pitch` stays whatever the player set; the crank can still not be used for it while following).
- **Swing:** whenever the target changes (entering follow, passing a buoy so a new one is next, leaving
  follow) the yaw blends from where it is now to the live target over `camera.buoy_swing_s`
  (default 2 s) with a smoothstep. The target is the live bearing to the buoy, so the camera keeps tracking
  as the boat moves; the blend restarts only on target change.
- **Leaving follow by hold:** target becomes "behind" (look yaw 0), same swing time.
- **Leaving follow by crank:** any crank turn while in look mode (MODE_CAMERA) exits follow; the camera
  stays at its current yaw, no snap, and the crank carries on from there (same pattern as the crank
  cancelling recentre).

## Goals / non-goals
- Goals: the above; one configurable time; no change to behind-follow or manual look when follow is off.
- Non-goals: following anything but the next buoy; changing pitch/elevation; a HUD indicator (not asked
  for — can be added later).
- Anti-goals: a visible snap on any transition; follow state surviving into a mode with no course.

## Approaches
- **A. In controls (chosen).** `Controls` owns the follow flag and drives `camera_yaw`; the game passes
  the buoy bearing in. `camera.c` unchanged. Keeps toggle, crank takeover and blend in one place.
- B. In camera. `CameraPose` computes the bearing itself. Rejected: look state split across two modules;
  crank takeover must read the camera back.
- Easing: smoothstep blend over exactly `buoy_swing_s` (chosen) vs exponential approach (settles
  asymptotically, so the configured time would only be approximate).

## Design
- `Controls`: add `camera_follow` (bool), blend state (`follow_from` yaw, `follow_t` seconds, previous
  target-valid/target-kind to detect target change).
- `controls_update` gains one input: the bearing to the next buoy relative to the boat's heading, plus
  a valid flag (e.g. `const float *buoy_rel` / NaN for none — builder's call; callers are `game.c:126`
  and `tests/test_controls.c`).
- Hold-DOWN handler: `down_fired` currently sets `camera_recentre`; it instead toggles `camera_follow`
  and restarts the blend. The existing tap-to-toggle-vertical-look path is unchanged.
- While following, MODE_CAMERA crank input exits follow (sets `camera_target_yaw = camera_yaw`).
  Follow persists if the player leaves CAMERA mode with another button (wheel/trim/hoist), since steering
  while watching the buoy is the point; crank in WHEEL/TRIM/HOIST mode does not exit it. (My call.)
- `game.c`: compute the relative bearing from `journey_next()` in journey mode and the equivalent next
  mark in race mode; "none" otherwise or when no buoy is left. With no target, the camera holds its yaw
  (my call), and follow stays on so it picks up the next buoy when it spawns.
- Config: `CameraCfg.buoy_swing_s`, via `FF(CameraCfg, buoy_swing_s, 2, 0, 10)` in `boatcfg.c`
  (0 = immediate). Default means existing boat JSONs need no edit.
- Header comment in `controls.h` updated (DOWN hold now toggles follow).

## Tests (tests/test_controls.c)
Hold toggles follow on and off; yaw is at the start value at t=0, the target at t=swing, monotonic between;
target change mid-follow restarts the blend with no jump; crank in CAMERA exits follow with no yaw jump;
no-target holds yaw; swing 0 is immediate; NaN dt / bearing safe. Existing recentre-by-hold tests change
to the UP-again path.

## Left to the builder
Exact signature of the new input; which struct holds the blend state; race-mode next-mark lookup.

## Calls I made (check these)
1. Follow persists when you switch to wheel/trim/hoist; only a crank turn *in look mode* exits it.
2. No next buoy: camera holds its yaw and follow stays on.
3. Leaving by hold swings back to behind over `buoy_swing_s`.
