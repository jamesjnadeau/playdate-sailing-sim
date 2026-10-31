# Scenario Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive the real `src/pd/main.c` loop from text scenarios on a host build (CI, every push) and on the real Playdate simulator (on demand), and compare host frames to real-simulator goldens.

**Architecture:** A `-DSCENARIO` seam in `main.c` swaps input/time/seed for a scripted player and calls a frame-done hook. A host-only `harness/` supplies a stub `PlaydateAPI` (rasterizers lifted from `tools/frame_dump.c`) and a `host_main.c` that runs `eventHandler` plus the registered update callback. Frames are 1-bit 400x240 buffers compared by `tools/fbdiff.py` against two golden sets: real (tolerance, masks) and host (exact).

**Tech Stack:** C99 (gcc, host), Playdate SDK 3.1.2 headers + simulator, Python 3 + Pillow, GNU make, bash, GitHub Actions, Xvfb (refresh only).

**Spec:** `docs/superpowers/specs/2026-10-31-scenario-harness-design.md`

## Global Constraints

- Device and normal builds are unaffected unless `-DSCENARIO` is set (no scenario code on the device path).
- No second copy of the game loop: the harness drives the real `update()` in `main.c`.
- Scenario format: text, one command per line, `#` comments, times in frames at 30 fps; fixed `dt` = 1/30 s; fixed `fresh_seed` under `-DSCENARIO` (frame counter, not `getCurrentTimeMilliseconds`).
- `snap NAME` writes `NAME.fb`: the 400-bit-per-row buffer from the display frame (`LCD_ROWSIZE` 52 x 240 = 12480 bytes in memory; the spec says 12000 = 50 bytes x 240 trimmed rows, so write 50 bytes per row). `.state` holds `state` output at `%.3f`.
- Output naming: `<scenario>/<snapshot>.fb|.state`; one process / simulator launch per scenario; scenario ends with `end` or EOF.
- Host build flags: `-DSCENARIO -DTARGET_SIMULATOR=1 -ffp-contract=off -O1`, gcc.
- Never launch the simulator without `env -u WAYLAND_DISPLAY` plus `GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1` on an Xvfb display; the simulator cannot be quit from C, so scripts kill it (`pgrep -x PlaydateSimul | xargs -r kill`, never `pkill -f`) after `done.marker`.
- `make SCENARIO=1` builds a separate `.pdx` that never overwrites `SailingSim.pdx`.
- Fidelity check skips (with a warning) a snapshot that has no real golden, and fails once the golden exists. Regression check against host goldens is always blocking.
- `make -C tests test` keeps passing (ASan/UBSan, `-Werror`). `tools/frame_dump.c` and `tools/frames.sh` stay until scenarios cover what they show.
- Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Review Focus

- Scenario file with a typo (unknown command, bad number, missing arg): parse fails with a line number, never silently skipped (Task 1).
- Scenario that never reaches `end` or is empty: harness stops at EOF and still writes `done.marker`; no infinite loop (Tasks 1, 3).
- `snap` on a frame taken from the main menu (the early-return path in `update()`): captured, not skipped (Task 2).
- Missing real golden for a snapshot: warn and pass; present-but-different: fail with a side-by-side PNG (Task 6).
- Host run twice gives byte-identical `.fb`/`.state` (determinism: fixed seed and dt, no wall clock) (Task 3).

## File Structure

| Path | Responsibility |
|---|---|
| `src/pd/scenario.h/.c` | Pure scenario parser and per-frame player. No Playdate includes, so unit-testable in `tests/`. |
| `src/pd/scenario_io.c` | `-DSCENARIO` only: loads `scenario.txt`, writes `.fb`/`.state`, `done.marker` through `pd->file`. |
| `src/pd/main.c` | Modified: `#ifdef SCENARIO` seam in `update()`, `fresh_seed()`, and event init. |
| `harness/pdstub.c/.h`, `harness/host_main.c`, `harness/Makefile` | Host stub API, runner, build and golden targets. |
| `tools/fb2png.py`, `tools/fbdiff.py` | Frame to PNG; compare with masks and tolerance. |
| `tools/golden_refresh.sh` | Build `.pdx`, run the simulator per scenario under Xvfb, collect goldens. |
| `tests/scenarios/*.txt`, `tests/golden/real/`, `tests/golden/host/` | Inputs and the two golden sets. |
| `tests/test_scenario.c` | Unit tests for the player. |
| `.github/workflows/pr-check.yml`, `.github/workflows/golden-refresh.yml` | CI. |

---

### Task 1: Scenario parser and player

**Files:**
- Create: `src/pd/scenario.h`, `src/pd/scenario.c`, `tests/test_scenario.c`
- Modify: `tests/Makefile` (add `../src/pd/scenario.c` to `SRC`, `-I../src/pd` to `CFLAGS`)

**Interfaces:**
- Produces:
  ```c
  #define SC_MAX_CMDS 512
  #define SC_NAME_MAX 48
  typedef enum { SC_SYS_NONE, SC_SYS_RESTART, SC_SYS_MAIN, SC_SYS_BOAT, SC_SYS_WEATHER } ScSys;
  typedef struct {
      uint32_t cur, pushed, released;   /* BTN_* / PDButtons bit values: left 1, right 2, up 4, down 8, B 16, A 32 */
      float crank;                      /* degrees this frame */
      ScSys sys; int sys_arg;           /* system-menu action to apply BEFORE this frame's update */
  } ScFrame;
  typedef struct Scenario Scenario;     /* opaque-sized: declared fully in the header, fixed arrays, no malloc */
  bool scenario_parse(Scenario *s, const char *text, char *err, size_t errn);  /* err: "line N: ..." */
  bool scenario_poll(Scenario *s, ScFrame *f);        /* next frame's input; false once finished */
  typedef enum { SC_AFTER_NONE, SC_AFTER_SNAP, SC_AFTER_STATE, SC_AFTER_END } ScAfterKind;
  ScAfterKind scenario_after(Scenario *s, char name[SC_NAME_MAX]);  /* call after each frame until NONE */
  ```
  Commands: `wait N`, `press X`, `hold X N`, `crank DEG for N`, `snap NAME`, `state`, `sysmenu restart|main|boat N|weather N`, `end`. Button names: `left right up down A B`.

- [ ] **Step 1: Write failing tests** in `tests/test_scenario.c` (names → assertions):
  - `test_press_is_one_frame_then_release`: `"press A\nwait 1\nend"` → frame 0 `pushed==cur==32`; frame 1 `cur==0, released==32`.
  - `test_hold_n_frames`: `"hold B 3\nend"` → frames 0..2 have `cur==16`, only frame 0 has `pushed`; frame 3 has `released==16`.
  - `test_crank_for`: `"crank 15.5 for 2\nend"` → two frames with `crank==15.5f` (`T_NEAR`), third 0.
  - `test_wait_counts_frames`: `"wait 4\nend"` → exactly 4 frames then `scenario_poll` false.
  - `test_snap_after_frame`: `"wait 2\nsnap a\nwait 1\nsnap b\nend"` → after frame 1 `scenario_after` returns `SC_AFTER_SNAP` with name `"a"`, then `NONE`; after frame 2 `"b"`; then `SC_AFTER_END`.
  - `test_sysmenu`: `"sysmenu boat 2\nwait 1\nend"` → frame 0 has `sys==SC_SYS_BOAT, sys_arg==2`.
  - `test_errors_have_line_numbers`: `"wait 1\nfrobnicate\n"` fails with err containing `"line 2"`; also `wait x`, `press Z`, `hold A`, `snap` (no name), `snap a` as the first command (before any frame), and a 600-command script each fail.
  - `test_eof_ends`: `"wait 1"` (no `end`) → after frame 0, `scenario_after` returns `SC_AFTER_END`.
  - `test_comments_and_blank_lines`: `"# hi\n\n wait 1 # x\nend"` parses.
- [ ] **Step 2: Run** `make -C tests test` — expect compile failure (no `scenario.h`).
- [ ] **Step 3: Implement** `scenario.c`: pre-tokenise into a fixed `ScCmd` array; the player expands time commands per frame (`press` = pushed frame then a released frame, `hold X N` = N held frames then a released frame, `sysmenu` attaches to the next frame, `snap`/`state`/`end` are zero-time and surface through `scenario_after`). `snap` names are `[A-Za-z0-9_]+` ≤ `SC_NAME_MAX-1`.
- [ ] **Step 4: Run** `make -C tests test` — all pass.
- [ ] **Step 5: Commit** `feat: scenario parser and per-frame player`.

### Task 2: `-DSCENARIO` seam in main.c and scenario_io

**Files:**
- Create: `src/pd/scenario_io.c`
- Modify: `src/pd/main.c`, `src/pd/scenario.h` (add hook declarations)

**Interfaces:**
- Consumes: Task 1 API.
- Produces (declared in `scenario.h`, real functions under `SCENARIO`, no-op `static inline`/macros otherwise so non-SCENARIO objects are byte-identical in behaviour):
  ```c
  void scenario_init(PlaydateAPI *pd);            /* reads "scenario.txt"; on parse error logs it, writes done.marker, ends */
  bool scenario_poll_input(PDButtons *cur, PDButtons *pushed, PDButtons *released, float *crank, float *dt, ScFrame *f);
  void scenario_frame_done(PlaydateAPI *pd, const Game *g);  /* after render; drains scenario_after: snap/state/end */
  uint32_t scenario_seed(uint32_t counter);       /* deterministic fresh_seed */
  ```
  `scenario_io.c` includes `pd_api.h`; `scenario.c` stays pd-free.
  Output location: path prefix `NAME.fb` / `NAME.state` relative to the `pd->file` root (Data dir in the simulator, the stub's output dir on host); `done.marker` written on `end`/EOF/parse error. `.state` keys (one `key=value` per line): `app, mode, run, t, heading, speed (u), heel, awa, aws, wheel, x, y` using the actual `Game` fields (`g->t`, `g->run`, `g->mode`, `g->b.*`); builder confirms field names against `src/sim/boat.h` and `game.h`.
  The `app` value lives in `main.c`, so `scenario_frame_done` takes it through a `const char *app_name` argument (add it to the signature).

- [ ] **Step 1: Write the failing check** — `make SCENARIO=1` is not yet defined, so use the host build in Task 3 as the test. For this task, add a compile-only check: `gcc -fsyntax-only -DSCENARIO -DTARGET_SIMULATOR=1 -Isrc/sim -Isrc/pd -I$SDK/C_API src/pd/main.c src/pd/scenario_io.c` must succeed, and the same without `-DSCENARIO` must still succeed. Run both now; expect failure for the first.
- [ ] **Step 2: Implement** the seam: in `update()` replace the three reads at lines 109–113 with `scenario_poll_input` under `#ifdef SCENARIO`; apply `f.sys` before game logic by calling the existing `menu_restart/menu_main/menu_boat/menu_weather` (set the item value first with `setMenuItemValue(boat_item|weather_item, f.sys_arg)`, ignore when the item is NULL, and log it); call `scenario_frame_done` before **both** `return 1` paths (menu path at line 135 and the play path at 162, plus the finish-panel return at line 140). `fresh_seed()` calls `scenario_seed(++restart_count)` under `SCENARIO`. `scenario_init` runs in `kEventInit` just before `setUpdateCallback`.
- [ ] **Step 3: Verify** both syntax checks pass, and `git diff` shows the non-SCENARIO preprocessed `main.c` is unchanged apart from the guards (`gcc -E` before/after diff is empty modulo line markers).
- [ ] **Step 4: Commit** `feat: -DSCENARIO input/seed seam and snapshot writer`.

### Task 3: Host stub and runner

**Files:**
- Create: `harness/pdstub.h`, `harness/pdstub.c`, `harness/host_main.c`, `harness/Makefile`, `tests/scenarios/smoke.txt`

**Interfaces:**
- Consumes: Task 2 hooks; rasterizers from `tools/frame_dump.c` (`plot`, `s_clear`, `s_fillRect`, `s_drawRect`, `s_drawLine`, ellipses, `s_fillPolygon`, `s_fillTriangle`, `pat_white`).
- Produces: `harness/Makefile` targets `all` (builds `build/harness`), `run SCEN=name` (writes `build/out/<name>/…`), `run-all`, `update-golden` (copies `build/out/*` to `tests/golden/host/`). CLI: `build/harness <scenario.txt> <outdir> <assets_root>`; exit 0 only if `done.marker` was written.
  `pdstub.h`: `PlaydateAPI *pdstub_init(const char *outdir, const char *assets_root, const char *scenario_text);` and `int pdstub_run(void)` (calls the update callback until `done.marker` exists or 100000 frames, whichever first; non-zero on timeout).

- [ ] **Step 1: Write the failing test**: `tests/scenarios/smoke.txt` = `wait 2\nsnap menu_home\npress A\nwait 60\nsnap sailing\nstate\nend`. Run `make -C harness run SCEN=smoke` — fails (no Makefile).
- [ ] **Step 2: Implement** `pdstub.c`: `system` (buttons/crank unused because scenario supplies input; clock via frame counter; menu items stored with value and callback; `setMenuImage` ignores; `logToConsole` to stderr), `file` (open/read/close/listfiles/write against `assets_root` for reads — a staged copy of `boats config tracks` — and `outdir` for writes; add `open` write mode and `write` since `scenario_io.c` needs them), `display.setRefreshRate`, `graphics` functions listed in the spec (bitmaps/contexts can be a tiny second buffer with push/pop; `getDisplayFrame`/`getFrame` both return the 52-byte-stride buffer). Pack the 1-bit buffer MSB-first, row stride 52, matching the SDK. `drawText` draws nothing (masked later).
- [ ] **Step 3: Implement** `host_main.c` and `harness/Makefile` with the Global Constraints flags, compiling `src/pd/*.c`, `src/sim/*.c`, `harness/*.c` with `-I$(SDK)/C_API`; SDK path from `PLAYDATE_SDK_PATH` or `~/.Playdate/config` (same logic as the root Makefile). The root `Makefile`'s `STAGE` copying of `boats config tracks` is mirrored by a `build/assets` target.
- [ ] **Step 4: Run** `make -C harness run SCEN=smoke` — expect `build/out/smoke/menu_home.fb`, `sailing.fb`, `sailing.state`, `done.marker`; `.fb` is 12000 bytes.
- [ ] **Step 5: Determinism check**: run twice, `cmp` all outputs identical. Add this as `harness/Makefile` target `check-determinism` and run it.
- [ ] **Step 6: Review Focus tests**: add `tests/scenarios/empty.txt` (empty file) and `tests/scenarios/typo.txt` (`frobnicate`); `run SCEN=empty` exits 0 with `done.marker`; `run SCEN=typo` exits non-zero with the line number on stderr. Run and confirm.
- [ ] **Step 7: Commit** `feat: host harness running the real main loop against a stub API`.

### Task 4: Frame tools

**Files:**
- Create: `tools/fb2png.py`, `tools/fbdiff.py`, `tools/test_fbtools.py`

**Interfaces:**
- `fb2png.py IN.fb OUT.png [--scale 2]`: 400x240, 1 = white.
- `fbdiff.py EXPECTED.fb ACTUAL.fb [--mask x,y,w,h]... [--tolerance FRACTION] [--png OUT.png]` → exit 0 pass, 1 fail; prints `mismatch=<count> fraction=<f> masked=<count>`; `--png` writes expected | actual | diff side by side. A masks file `tests/scenarios/<name>.masks` (lines `snapshot x y w h`) is read by the CI wrapper in Task 6, not by this tool.
- Python is run with `-I`; Pillow only for PNG output, comparison itself is pure Python.

- [ ] **Step 1: Write failing tests** in `tools/test_fbtools.py` (unittest): identical frames → exit 0, `mismatch=0`; one flipped pixel inside a mask → pass; same flip outside the mask with `--tolerance 0` → fail; tolerance `0.001` over 96000 px allows 96 flips; wrong file size (not 12000 bytes) → exit 2 with message; `fb2png` output is 800x480.
- [ ] **Step 2: Run** `python3 -I -m unittest tools/test_fbtools.py` — fails.
- [ ] **Step 3: Implement** both tools to the interface above.
- [ ] **Step 4: Run** the tests — pass; also convert `build/out/smoke/sailing.fb` and view the PNG for a sanity check.
- [ ] **Step 5: Commit** `feat: fb2png and fbdiff tools`.

### Task 5: Initial scenarios and host goldens

**Files:**
- Create: `tests/scenarios/{menu_nav,free_sail,race_start,capsize_restart,boat_weather}.txt`, `tests/golden/host/<scenario>/*`

**Interfaces:**
- Consumes: Tasks 3–4. Produces: the scenario set and checked-in host goldens, refreshed with `make -C harness update-golden`.

- [ ] **Step 1: Write** the five scenarios covering the spec's list: menu navigation to free range; free-range sail with `hold A 20` autopilot and `crank`; racing start; capsize then restart (A after `RUN_CAPSIZED`; the builder finds crank/hold values that capsize, using `state` to verify `run=`); `sysmenu boat 1` / `sysmenu weather 2` / `sysmenu restart` / `sysmenu main`. Each has 2–4 `snap`s and ends with `state`.
- [ ] **Step 2: Verify each scenario reaches its intent**: grep `state` output (`app=`, `mode=`, `run=`) for each snapshot; fail the task if e.g. `capsize_restart` never shows a capsized run.
- [ ] **Step 3: Run** `make -C harness run-all update-golden`; run `check-determinism` for each scenario.
- [ ] **Step 4: Commit** `test: initial scenarios and host goldens`.

### Task 6: Regression and fidelity checks

**Files:**
- Create: `harness/check.sh`
- Modify: `harness/Makefile` (targets `check`, `check-regression`, `check-fidelity`)

**Interfaces:**
- `check-regression`: every `.fb` and `.state` under `build/out/` equals `tests/golden/host/` byte for byte (a `.state` comparison in `check.sh`, `.fb` via `cmp`); a snapshot missing from either side fails. On failure writes `build/diff/<scenario>/<snap>.png` via `fbdiff.py --png`.
- `check-fidelity`: for each snapshot with `tests/golden/real/<scenario>/<snap>.fb`, runs `fbdiff.py` with masks from `tests/scenarios/<scenario>.masks` and tolerance from `tests/scenarios/default.tolerance` (a single number; initial value `0.02`, calibrated in Task 9); no real golden → prints `WARN no real golden` and passes.
- `check` runs both.

- [ ] **Step 1: Write failing tests**: `harness/test_check.sh` fabricates a temp tree and asserts: a changed host golden makes `check-regression` fail and leaves a PNG; a missing real golden passes with a `WARN`; a present, differing real golden beyond tolerance fails (Review Focus).
- [ ] **Step 2: Run** `bash harness/test_check.sh` — fails.
- [ ] **Step 3: Implement** `check.sh` and the Makefile targets.
- [ ] **Step 4: Run** `bash harness/test_check.sh` and `make -C harness check` (real goldens do not exist yet: expect only WARNs).
- [ ] **Step 5: Commit** `feat: regression and fidelity checks`.

### Task 7: Simulator build and golden refresh script

**Files:**
- Create: `tools/golden_refresh.sh`
- Modify: `Makefile` (a `SCENARIO=1` variant: `PRODUCT = SailingSimScenario.pdx`, `UDEFS += -DSCENARIO`, `SRC += src/pd/scenario.c src/pd/scenario_io.c`; `scenario.c`/`scenario_io.c` are added to the normal `SRC` only when `SCENARIO=1`).

**Interfaces:**
- `tools/golden_refresh.sh [scenario ...]` (default all `tests/scenarios/*.txt` that have a `snap`): requires `xvfb` on PATH or `XVFB_BIN` and `PLAYDATE_SDK_PATH`; per scenario: wipe the Data dir (path read from the simulator's config; the builder determines it and the script prints it), copy the scenario into the `.pdx` as `scenario.txt`, start `env -u WAYLAND_DISPLAY DISPLAY=:99 GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 PlaydateSimulator <pdx>`, poll up to 120 s for `done.marker`, kill via `pgrep -x PlaydateSimul | xargs -r kill`, move outputs to `tests/golden/real/<scenario>/`. Exits non-zero on timeout and always kills the simulator and Xvfb it started (trap).

- [ ] **Step 1: Write failing check**: `make SCENARIO=1` fails (target missing).
- [ ] **Step 2: Implement** the Makefile variant; confirm `make` (no flag) output and `SailingSim.pdx` are unaffected (`make -n` diff before/after).
- [ ] **Step 3: Implement** `golden_refresh.sh`; refuse to run if `WAYLAND_DISPLAY` would be inherited (it unsets it explicitly) or if `DISPLAY` points at a non-scratch display unless `--on-current-display` is passed.
- [ ] **Step 4: Run** `tools/golden_refresh.sh smoke` under a locally unpacked Xvfb (`apt-get download xvfb`, `dpkg -x`, no root). Expect `tests/golden/real/smoke/{menu_home,sailing}.fb`, simulator killed, no stray processes (`pgrep -x PlaydateSimul` empty).
- [ ] **Step 5: Settle `getFrame` vs `getDisplayFrame`**: capture `menu_home` with both on the simulator; the one matching the host capture's menu layout (compare with `fbdiff`) wins; record the choice in `scenario_io.c` and in the spec's Risks section.
- [ ] **Step 6: Commit** `feat: scenario .pdx build and golden refresh script` (the smoke goldens are committed in Task 8).

### Task 8: Real goldens for all scenarios

**Files:**
- Create: `tests/golden/real/<scenario>/*.fb|.state`

- [ ] **Step 1: Run** `tools/golden_refresh.sh` for all scenarios (Xvfb as in Task 7). Expect every `snap` has a `.fb`.
- [ ] **Step 2: Compare real vs host `.state`** for each scenario within `1e-2` numeric tolerance (script `tools/statediff.py`, written here with a unit test in `tools/test_fbtools.py`); mismatch means the sim diverged, so stop and debug (`systematic-debugging`) before continuing.
- [ ] **Step 3: View** a few `.fb` through `fb2png.py` as a sanity check; commit `test: real-simulator goldens`.

### Task 9: Tune the stub, calibrate tolerance and masks

**Files:**
- Modify: `harness/pdstub.c`, `tests/scenarios/default.tolerance`, `tests/scenarios/<name>.masks`

- [ ] **Step 1: Measure**: run `fbdiff.py` of every host snap against its real golden; list mismatch fractions and PNGs.
- [ ] **Step 2: Tune** rasterizers (line caps/width, polygon fill rule, ellipse extent, dither patterns, `drawText` box) by comparing the diff PNGs; iterate until non-text mismatch is as low as practical.
- [ ] **Step 3: Mask** the text regions per snapshot in `.masks` and set `default.tolerance` to the worst remaining fraction plus a small margin; record the numbers in a comment atop `harness/Makefile`.
- [ ] **Step 4: Run** `make -C harness check` — passes with real goldens present; `update-golden` for the host set if rendering changed; commit `fix: tune host stub against real goldens`.

### Task 10: CI, docs and retirement

**Files:**
- Modify: `.github/workflows/pr-check.yml`, `README.md`
- Create: `.github/workflows/golden-refresh.yml`

- [ ] **Step 1: Add** to `pr-check.yml` after "Unit tests": `make -C harness run-all check`, then `actions/upload-artifact@v4` of `harness/build/diff/` with `if: failure()`.
- [ ] **Step 2: Create** `golden-refresh.yml` (`workflow_dispatch`): install `xvfb` and the simulator's GTK/WebKit/GL libraries, download the SDK like `pr-check.yml`, run `tools/golden_refresh.sh`, upload `tests/golden/real/` as an artifact (no commit). Mark in a comment that the first run is expected to need iteration.
- [ ] **Step 3: Document** in `README.md`: scenario format, running a scenario, refreshing goldens, the two checks, the Wayland warning.
- [ ] **Step 4: Verify locally**: `make -C tests test && make -C harness run-all check` passes from a clean checkout state (`git clean -nfd` reviewed first).
- [ ] **Step 5: Retire** `tools/frame_dump.c`/`frames.sh` only for outputs the scenarios replace; keep the rest and list what remains in the README.
- [ ] **Step 6: Commit** `ci: scenario harness checks and golden-refresh workflow`.
