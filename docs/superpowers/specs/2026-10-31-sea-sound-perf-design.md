# Cheaper sea sound: design

## Why

The game feels less crisp on the Playdate than it used to. You notice it most when cranking in Look, because a fast pan is where dropped frames show. Setting Sound to Off brings back the smoothness, so the procedural sea sound (`seasound_render`, added 2026-10-31) is using frame time. It runs on the audio thread 44,100 times a second, and every voice runs on every sample.

## Goals

- Look feels as crisp with Sound at Med as it does with Sound off.
- The sea sound stays the sound that was just tuned: wash, swell, slap, thump, gurgle, rigging whistle, cloth flop.
- The before and after can be measured, so we aren't guessing.

## Non-goals

- Speeding up the renderer (the sea lattice and the rest). That's a separate effort if frames still drop.
- Moving to the SDK's built-in synths or recorded loops.

## Anti-goals

- A sound that is noticeably duller, buzzier or more stepped than it is today.
- Breaking the host tests in `tests/test_seasound.c`, or making `seasound.c` depend on the Playdate API.

## Approaches considered

1. **Trim the synth only.** Do per-chunk what is now done per sample, skip silent voices, use a cheaper soft-clip. Sound unchanged, roughly 2–4× cheaper.
2. **Trim the synth and render at half rate (chosen).** All of #1, plus each sample is worked out at 22,050 Hz and expanded to the 44.1 kHz the mixer wants. Nearly every voice sits well below the new 11 kHz limit. Expected to cost roughly a fifth to a tenth of today.
3. **Use the SDK sound engine.** Cheapest at runtime, but a rewrite that changes the sound and drops the custom voices.

You picked #2.

## Design

**Measure first.** Add a debug-only frame-rate readout (`pd->system->drawFPS`) behind a compile-time switch, off by default. Use it to compare Sound Off against Sound Med before and after. Add a host benchmark that times `seasound_render` over a few seconds of audio. Host speed isn't device speed, but the ratio between the old and new code is a fair guide.

**Half-rate rendering.** The synth computes one internal sample per two output samples. Every rate-dependent constant is recomputed for 22,050 Hz, not just `RATE`. That includes:

- the `lp_a` cutoffs
- the whistle filter coefficient
- `flut_d`
- `k_fast` and `k_master`, together with the chunk length
- the slap and thump decays
- the per-sample flutter constants that are written as raw numbers today: `0.0007`, `0.022` and `0.012`

All of these must keep their current time constants and cutoffs in Hz. Output samples between computed ones are filled by linear interpolation (my call: it's barely more expensive than repeating each sample, and it avoids the faint buzz of a plain repeat). `seasound_render` must still handle an `n` that is odd or not a multiple of the chunk length.

**Trims.**
- Whistle pitch (`fc`, the `sinf`, and the `sqrtf` normaliser) is computed once per chunk from the wobble value instead of once per sample.
- Voices whose level is zero are skipped: slap, thump, gurgle, wind, flutter, wash, swell.
- The soft-clip `x / (1 + 0.4|x|)` is replaced with a curve that's cheaper on the device but close to it in shape (the builder decides which).

## Testing

- The existing `test_seasound.c` tests pass unchanged.
- New test: each voice's rms at the new rate is within about 15% of its rms before the change.
- New test: odd and short `n` values give finite, bounded output.
- On the device: Look with Sound at Med feels like Sound off, and the FPS readout holds 30.

## Left to the builder

- The exact soft-clip curve.
- Whether skipped voices keep using up the noise they'd have used (this decides whether the other voices hear the same random sequence).
- How the benchmark is wired into `make -C tests`.
