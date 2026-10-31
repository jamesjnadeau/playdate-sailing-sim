# Cheaper Sea Sound Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the device CPU cost of the procedural sea sound so Look stays crisp with Sound on, without changing how it sounds.

**Architecture:** `seasound_render` (src/sim/seasound.c, host-tested, no Playdate API) synthesises internally at 22,050 Hz and linearly interpolates up to the 44.1 kHz the mixer asks for. Slow-moving whistle maths moves to per-chunk and silent voices are skipped. A host benchmark and a level-guard test pin the speed-up and the sound; a compile-time FPS readout checks it on the device.

**Tech Stack:** C99, Playdate C SDK 3.1.2, host tests in `tests/` (`make -C tests`, gcc + ASan/UBSan).

**Spec:** `docs/superpowers/specs/2026-10-31-sea-sound-perf-design.md`

## Global Constraints

- `src/sim/seasound.c` and `seasound.h` keep no Playdate API dependency.
- The output rate the mixer sees stays `SEASOUND_RATE` = 44100; `seasound_render(SeaSound *s, int16_t *left, int16_t *right, int n)` keeps its signature and its return contract (0 = silent, nothing written; 1 = wrote `n` samples).
- Every time constant and cutoff keeps its value in seconds/Hz; only the per-sample coefficients change.
- All 14 existing tests in `tests/test_seasound.c` pass unchanged.
- Each voice's long-window rms stays within 15% of its pre-change value.
- The FPS readout is off in the default build.
- Match the file's existing style: short comments stating the physical meaning, `static` helpers, `float` math with `f` suffixes.

## Review Focus

1. Callers pass any `n` (1, odd, not a multiple of the chunk): output must be the same as one big call, with no seams. Test: `test_split_calls_match_one_call` (Task 4).
2. Sound fades to silent (render returns 0) and back: the first samples after resuming must not click from stale interpolation state. Test: `test_resume_after_silence_has_no_click` (Task 4).
3. A skipped voice switching on from zero (wind rising, a sail starting to luff) must not burst from stale filter state. Test: `test_voice_waking_from_zero_is_bounded` (Task 3).
4. Whistle at the top of its clamp (`whistle_hz` 4000) must stay stable at the lower internal rate. Test: `test_whistle_top_of_range_is_stable` (Task 4).
5. Treble: the wash must still get brighter with `bright` at the new rate. Covered by the existing `test_wash_gets_brighter_with_bright`. No new test, but a reviewer should confirm it still passes and is not passing only by luck.

---

### Task 1: Benchmark and level guard (measure before changing)

**Files:**
- Create: `tests/bench_seasound.c`
- Modify: `tests/Makefile` (add a `bench` target)
- Modify: `tests/test_seasound.c` (add the level-guard test)

**Interfaces:**
- Produces: `make -C tests bench` builds `tests/build/bench_seasound` at `-O2` without sanitizers and prints one line, `seasound_render: <X> ns per output sample`.
- Produces: `static float level_long(const SoundParams *p)` in `test_seasound.c`. It does `seasound_init`, sets `ss.p = *p`, settles with 20 renders of 4096, then returns the rms of `L` averaged over 24 further renders of 4096 (about 2.2 s, long enough for the 2.5–4.5 Hz flutter gating to average out).

- [ ] **Step 1: Write `tests/bench_seasound.c`.** Use the same `SoundParams` for every voice at once: wash 1, bright 0.6, swell 1, gurgle 1, pan 0.3, wind 1, whistle_hz 700, flutter 1. Set `slap_ev = thump_ev = 1` every 40 blocks. Render 44100 × 10 output samples in blocks of 256 (the mixer's typical block size). Time it with `clock_gettime(CLOCK_MONOTONIC)` and write the samples into a volatile sink so the work isn't optimised away.

- [ ] **Step 2: Add the Makefile target.** `bench: build/bench_seasound` then run it. The build rule is `$(CC) -std=c99 -O2 -I../src/sim -o $@ bench_seasound.c $(SRC) -lm`, with no `-fsanitize`. Leave the `test` target alone (`TESTS` globs `test_*.c`, so the bench isn't run by `make test`).

- [ ] **Step 3: Run `make -C tests bench` three times** and note the median ns/sample. This is the baseline for the Task 3 and 4 commit messages.

- [ ] **Step 4: Capture the baselines.** Temporarily print `level_long` for each of the five voices, using the same per-voice setup as `test_each_voice_is_audible_but_not_clipping` (bright 0.6, whistle_hz 700, pan 0, one field at 1). Then write the test with the printed values as constants:

```c
static void test_voice_levels_match_reference(void) {
    static const float ref[5] = { /* wash, swell, gurgle, wind, flutter: values printed on the pre-change code */ };
    /* same setup as test_each_voice_is_audible_but_not_clipping */
    for (int v = 0; v < 5; v++) {
        float r = level_long(&p);
        printf("voice %d long rms %.4f (ref %.4f)\n", v, r, ref[v]);
        T_ASSERT(fabsf(r - ref[v]) <= 0.15f * ref[v]);
    }
}
```

Add one more reference for the slap: the rms of the 4096 samples straight after `ss.slap_ev = 1.0f` on otherwise zero params, with a 15% tolerance (`test_slap_level_matches_reference`).

- [ ] **Step 5: Run `make -C tests`.** Expected: all tests pass, now 16 in test_seasound.

- [ ] **Step 6: Commit.**

```bash
git add tests/bench_seasound.c tests/Makefile tests/test_seasound.c
git commit -m "test: sea sound benchmark and voice-level reference (baseline <X> ns/sample)"
```

---

### Task 2: On-device FPS readout

**Files:**
- Modify: `Makefile` (add an `FPS=1` switch next to the `SCENARIO=1` one)
- Modify: `src/pd/main.c` (`update()`, at both `return 1` exits that follow a draw)

**Interfaces:**
- Produces: `make FPS=1` (and `FPS=1 tools/deploy.sh` if that script passes the environment through to `make`; otherwise run `make FPS=1` before deploying) builds with `-DSHOW_FPS`.

- [ ] **Step 1: Makefile.** `ifeq ($(FPS),1)` → `UDEFS += -DSHOW_FPS` and `VARIANT := $(VARIANT)-fps`, placed after the SCENARIO block so the variant stamp forces a rebuild when the switch flips.

- [ ] **Step 2: main.c.** Under `#ifdef SHOW_FPS`, call `pd->system->drawFPS(0, 0);` as the last drawing call in both the menu branch and the play branch of `update()`, so it is drawn over everything. The harness stub doesn't implement `drawFPS`, which is fine because the default build never compiles the call.

- [ ] **Step 3: Verify.** Run `make` and then `make FPS=1`; both build without warnings. Run `make -C harness` (or `harness/check.sh`) and it passes. In the simulator, `make FPS=1` shows the counter at the top left.

- [ ] **Step 4: Commit.**

```bash
git add Makefile src/pd/main.c
git commit -m "feat: FPS=1 build shows a frame-rate readout for device profiling"
```

---

### Task 3: Per-chunk whistle maths and skipped silent voices

**Files:**
- Modify: `src/sim/seasound.c` (`seasound_render`)
- Test: `tests/test_seasound.c`

**Interfaces:**
- Consumes: `level_long`, the reference tests and `make -C tests bench` from Task 1.

Decisions (the spec left these to the builder):
- **Noise is always drawn.** All three `rnd()` calls (n0, n1, n2) still run on every internal sample, so switching one voice on or off never changes the random sequence the others hear. Three xorshifts cost little.
- **Skipping is per chunk.** A voice is skipped for the whole chunk when its level is below `1e-4f`: `c->wash`, `c->swell`, `s->slap_env`, `s->thump_env`, `c->gurgle`, `c->wind`, `c->flutter`. Its filter state is frozen, not cleared. Voices wake on a ramp (`k_fast` smoothing or an envelope), so stale state is masked.
- **The flutter phase and gate keep advancing even while flutter is skipped**, so flaps don't bunch up when it resumes. Only the filter and amp lines are skipped.
- **The burble and wobble smoothers keep running every sample.** They are one multiply-add each, and the whistle reads `wobble` once per chunk.
- **The soft-clip stays `x / (1 + 0.4|x|)`.** The spec allowed a cheaper curve. After Task 4 it runs at half the rate anyway, and keeping it avoids any change in tone. Revisit only if the benchmark still shows it as significant.

- [ ] **Step 1: Write the failing test** `test_voice_waking_from_zero_is_bounded`. Settle with all params at zero except wash 1 (20 renders). Then set wind = 1, whistle_hz = 900 and flutter = 1. In each of the next 4 renders, every sample must have `abs(L[i]) < 30000`. A plain bounds check like this may already pass on the current code. Its job is to guard the skip logic, so it is fine for it to pass before Step 2.

- [ ] **Step 2: Move the whistle maths to per chunk.** At the start of each chunk, compute `fc` from the current `s->wobble`, `f = 2 sinf(π·clamp(fc, 100, 4000)/RATE)`, and the gain `0.5f / sqrtf(f * 6.0f) * 0.577f * 2.0f`. The per-sample loop keeps only the wobble update and the SVF update.

- [ ] **Step 3: Skip silent voices** as described in the decisions above.

- [ ] **Step 4: Run `make -C tests`.** Expected: everything passes, including both reference tests from Task 1.

- [ ] **Step 5: Run `make -C tests bench` three times.** Expected: the median is below the Task 1 baseline. Record it.

- [ ] **Step 6: Commit.**

```bash
git add src/sim/seasound.c tests/test_seasound.c
git commit -m "perf: sea sound whistle maths per chunk, skip silent voices (<before> -> <after> ns/sample)"
```

---

### Task 4: Half-rate synthesis with linear interpolation

**Files:**
- Modify: `src/sim/seasound.h` (new render-side fields in `SeaSound`)
- Modify: `src/sim/seasound.c`
- Test: `tests/test_seasound.c`

**Interfaces:**
- Consumes: everything from Task 3.
- Produces (internal to seasound.c):
  - `#define DECIM 2`, the output samples per internal sample.
  - `RATE` becomes `((float)SEASOUND_RATE / DECIM)`. Every use of `RATE` (lp_a, the SVF `f`, flut_d, the k_fast and k_master settings in `seasound_init`, slap_d, thump_d, flut_phase) then follows automatically.
  - `CHUNK` stays 32 and now counts internal samples (64 output samples), so coefficient updates still happen every ~1.45 ms.
- New `SeaSound` render-side fields:
  - `float out_prev[2], out_cur[2];` hold the last two internal stereo samples, after master gain and soft-clip, as floats in -1..1.
  - `int sub;` is the output position within the current internal sample (0..DECIM-1).
  - `int chunk_left;` is the number of internal samples left before the next coefficient update.

The flutter constants are written as raw numbers today, each a per-sample coefficient at 44.1 kHz. Convert each with `a' = 1 - (1 - a)^DECIM` and keep the old value in the comment:

| Today (44.1 kHz) | Half rate | Line |
| --- | --- | --- |
| `0.0007f` | `0.0013995f` | flut_amp swell (~30 ms) |
| `0.022f` | `0.043516f` | flut_lp (~150 Hz) |
| `0.012f` | `0.023856f` | flut_body (~80 Hz) |

Render loop shape. This is the algorithm the signature doesn't determine; the body of `synth_one` is today's per-sample block, moved as is:

```c
for (int i = 0; i < n; i++) {
    if (s->sub == 0) {
        if (s->chunk_left == 0) { /* per-chunk: smoothing, events, coefficients, whistle f and skip flags */ s->chunk_left = CHUNK; }
        s->out_prev[0] = s->out_cur[0]; s->out_prev[1] = s->out_cur[1];
        synth_one(s, ..., &s->out_cur[0], &s->out_cur[1]);   /* one internal sample, soft-clipped, -1..1 */
        if (--s->chunk_left == 0) { /* end-of-chunk: slap/thump envelope decay */ }
    }
    const float w = (float)(s->sub + 1) / DECIM;   /* the last sub-sample lands exactly on out_cur */
    left[i]  = (int16_t)(clampf(s->out_prev[0] + (s->out_cur[0] - s->out_prev[0]) * w, -1, 1) * 30000.0f);
    right[i] = ...same for [1]...;
    if (++s->sub == DECIM) s->sub = 0;
}
```

Per-chunk state that the current code keeps in locals (the `a_*` / `g_*` coefficients, `slap_d`, `thump_d`, `flut_d`, `flut_hz`, `gl`/`gr`, the whistle `f` and gain, the skip flags) must move into `SeaSound` render-side fields. A chunk can now span two `seasound_render` calls, and the chunk has to pick up where it left off. A `struct { ... } k;` member keeps them grouped.

On the silent early return (`master_target <= 0 && master < 1e-4`), also zero `out_prev`, `out_cur` and `sub`, so a resume ramps up from silence instead of interpolating from a stale sample.

- [ ] **Step 1: Write the failing tests.**

```c
/* any block size gives the same samples as one big call */
static void test_split_calls_match_one_call(void);
    /* all five voices at 1 (as in the bench), seed via seasound_init; render 4096 in one call into A.
       re-init, render the same 4096 as calls of n = 1, 3, 100, 7, 1, 64, ... (cycle these sizes) into B.
       T_ASSERT(memcmp(A, B, sizeof A) == 0) for both channels */

static void test_resume_after_silence_has_no_click(void);
    /* wash 1; render 20 blocks; set_active(0) until render returns 0; set_active(1);
       render 4096: T_ASSERT(abs(L[0]) < 200 && abs(L[1]) < 200) - master ramps from 0 */

static void test_whistle_top_of_range_is_stable(void);
    /* wind 1, whistle_hz 4000; 40 renders of 4096; every sample finite-range: abs < 30000,
       and rms of the last block > 0.005 (still sounding, not decayed or blown up) */
```

Also extend `test_bad_state_stays_finite` with `seasound_render(&ss, L, R, 1) == 1` and `seasound_render(&ss, L, R, 0)` returning without writing.

- [ ] **Step 2: Run `make -C tests`.** Expected: `test_split_calls_match_one_call` fails today. The current code restarts its chunk at every call, so chunk boundaries move with the call sizes and the output differs. The other new tests may already pass; they guard the change.

- [ ] **Step 3: Implement half-rate synthesis** as specified above.

- [ ] **Step 4: Run `make -C tests`.** Expected: all pass, including both Task 1 reference tests (each voice within 15%) and the existing `test_wash_gets_brighter_with_bright`. If a voice falls outside 15%, check its rate-dependent constant before touching any gains.

- [ ] **Step 5: Run `make -C tests bench` three times.** Expected: the median is at most 0.5× the Task 3 figure. Record it.

- [ ] **Step 6: Run the scenario harness** (`harness/check.sh`). Expected: it passes. Sound isn't part of the goldens, so nothing should move.

- [ ] **Step 7: Commit.**

```bash
git add src/sim/seasound.c src/sim/seasound.h tests/test_seasound.c
git commit -m "perf: synthesise the sea sound at 22 kHz and interpolate to 44.1 kHz (<before> -> <after> ns/sample)"
```

---

### Task 5: Device check (with the user)

- [ ] **Step 1:** `make FPS=1` then `tools/deploy.sh` (rebuilds with the same flag; if deploy.sh calls plain `make`, run it as `FPS=1 tools/deploy.sh` after confirming it forwards the variable).
- [ ] **Step 2:** The user sails at Sound Med and cranks hard in Look, then does the same at Sound Off. Expected: the readout holds 30 in both, and Look feels the same.
- [ ] **Step 3:** The user listens at Sound Med in light and storm weather. Expected: no audible change in wash, whistle or cloth flop, and no buzz or clicks.
- [ ] **Step 4:** Rebuild without `FPS=1` before any release build.
