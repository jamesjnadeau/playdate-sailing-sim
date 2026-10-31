# Scenario Harness Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the known limitations and deferred minor findings left after the scenario harness merged (see `2026-10-31-scenario-harness.md`).

**Architecture:** No new subsystems. Six independent tasks tighten the existing pieces: `tools/golden_refresh.sh`, the scenario parser (`src/pd/scenario.c`, `src/pd/scenario_io.c`, `src/pd/main.c`), the host harness (`harness/`), the comparison tools (`tools/fbdiff.py`, `tools/statediff.py`), the test suites, and the GitHub workflows.

**Tech Stack:** C (Playdate SDK 3.1.2 and the host stub), bash, Python 3 (stdlib `unittest`), GitHub Actions.

**Spec:** `docs/superpowers/specs/` scenario-harness design doc (the one the 2026-10-31 plan names). Behaviour changes below must keep its CLI contract: `build/harness <scenario.txt> <outdir> <assets_root>`, exit 0 ok, 1 timeout, 2 parse error or empty scenario.

## Global Constraints

- Framebuffer is 400x240, 1 bit per pixel, 12000-byte `.fb`, MSB first, 1 = white.
- Real goldens (`tests/golden/real/`) are only ever produced by `tools/golden_refresh.sh`. Never hand-edit them, and do not regenerate them unless a task says so (this plan changes comparison logic, not the real captures).
- Host goldens (`tests/golden/host/`) are regression references: identical bytes, else `fbdiff` and `statediff` at the tolerances in `tests/scenarios/regression.tolerance` (defaults 0.0005 and 0.001).
- Real-vs-host fidelity tolerance is 0.0065 global unless Task 5 replaces it.
- `-DSCENARIO` code must stay out of the normal game build (`src/pd/scenario_io.c` is under `#ifdef SCENARIO`).
- Python tests run with `python3 -I -m unittest discover -s tools -p test_fbtools.py`; harness checks with `harness/check.sh` and `harness/test_check.sh`; baseline before this plan: 23 Python tests, 26 `test_check.sh` cases, 72 harness checks, 9 `tests/` tests. Counts only go up.
- Untracked files `src/sim/seasound.*` and the longship plan belong to someone else; do not touch or stage them.

## Review Focus

- A scenario name like `../x` or `a b` passed to `golden_refresh.sh` must be refused before any `rm -rf`.
- `sysmenu boat 49er` (name starting with a digit) must select the boat named `49er`, not index 49.
- `sysmenu boat nosuchboat` must fail the scenario (exit 2), not just log.
- A scenario file longer than 16 KB must be an error, not silently truncated.
- A missing or empty `regression.tolerance` must fall back to the defaults, and a comments-only scenario must be an error.

---

### Task 1: golden_refresh.sh safety and robustness

**Files:**
- Modify: `tools/golden_refresh.sh`
- Test: `harness/test_golden_refresh.sh` (new; runs only argument-validation paths, needs neither SDK nor Xvfb)

**Interfaces:**
- Produces: `golden_refresh.sh` exits 2 with a `golden_refresh:` message for any scenario name not matching `^[A-Za-z0-9_-]+$`, before touching the filesystem.

- [ ] **Step 1: Write `harness/test_golden_refresh.sh`** asserting exit 2 and no deletion for names `../x`, `a/b`, `.`, `..`, `x y`, and exit 2 for `typo`. Create a sentinel directory `tests/golden/real/../sentinel` style path in a temp copy of the repo layout so the test can prove nothing outside `tests/golden/real/` is removed. Wire it into `harness/test_check.sh`'s runner or the CI workflow alongside it.
- [ ] **Step 2: Run it; expect FAIL** on at least the `../x` case.
- [ ] **Step 3: Validate names** in the scenario-list loop in `golden_refresh.sh` with the regex above; additionally `realpath -m` the output directory and require its parent to equal `$ROOT/tests/golden/real`.
- [ ] **Step 4: Harden the bundle-ID and Data checks.** After computing `$DATA`, refuse if `$DATA` or `$DATA_ROOT` is a symlink (`[ -L ... ]`), so a sibling-symlink bundle ID cannot pass the `dirname` check.
- [ ] **Step 5: Per-run scratch state.** Replace the fixed Xvfb display pick with `Xvfb -displayfd` (read the chosen number from the fd) so two concurrent runs cannot race; keep a separate `mktemp` log for Xvfb and for the simulator.
- [ ] **Step 6: Clean up on every exit path.** In `cleanup()`, `rm -f "$PDX/scenario.txt"` so a failed run does not leave a stale scenario in the .pdx.
- [ ] **Step 7: Rebuild guard.** Make `build/.variant` handling in the Makefile rebuild when switching between normal and `SCENARIO=1` builds even when source timestamps are unchanged; add a `harness/test_check.sh` case or a short Makefile check that switching variants relinks.
- [ ] **Step 8: Run** `bash harness/test_golden_refresh.sh` (expect PASS) and, with the SDK present, `tools/golden_refresh.sh boat_weather` (expect real goldens byte-identical via `git diff --stat tests/golden/real` empty).
- [ ] **Step 9: Commit** `fix: golden_refresh validates scenario names, guards symlinks, uses -displayfd`.

### Task 2: Scenario parser strictness

**Files:**
- Modify: `src/pd/scenario.c`, `src/pd/scenario_io.c`, `src/pd/main.c`, `harness/host_main.c`
- Test: `tests/scenarios/` new negative scenarios plus cases in `harness/test_check.sh`

**Interfaces:**
- Consumes: the `scenario_sys` name lookup in `src/pd/main.c` and the `sysmenu` grammar in `src/pd/scenario.c`.
- Produces: all of the below are parse or run errors (harness exit 2; on device `done.marker` begins `error`).

- [ ] **Step 1: Write failing scenarios and `test_check.sh` cases:** `sysmenu boat nosuchboat`; `sysmenu boat 49er` with a fixture boat named `49er` in the stub assets (resolves by name); `sysmenu weather 99` (out of range); `sysmenu boat 99`; two `sysmenu` lines with no `wait` between them; a scenario of only comments and blank lines; a scenario file over 16 KB.
- [ ] **Step 2: Run; expect FAIL** for each (today: log-only, index-parse, overwrite, empty accepted or truncated).
- [ ] **Step 3: Boat selection.** Try name lookup first; fall back to an index only when the whole token is a valid in-range integer and no boat has that name. Unknown name or out-of-range index is an error returned to the caller (not a log line). Range-check `weather N` against the weather table size.
- [ ] **Step 4: Pending-sysmenu overwrite.** A second `sysmenu` while one is still pending is a parse error naming both lines.
- [ ] **Step 5: Size limits.** Replace the 16 KB silent truncation in `scenario_io.c` and `host_main.c` with a hard error when the file does not fit; report the limit in the message. Make the `ctx_stack` push return an error on overflow instead of writing past the end.
- [ ] **Step 6: Comments-only scenarios** are an error (exit 2, same as an empty scenario).
- [ ] **Step 7: Warnings.** Build with `-Wextra`, fix what it reports, including the `setRefreshRate` float/int cast; keep the Playdate SDK build warning-clean for the files touched.
- [ ] **Step 8: Run** `make -C harness`, `harness/check.sh`, `harness/test_check.sh` (expect all PASS, new totals higher), and `make SCENARIO=1 simulator` still builds.
- [ ] **Step 9: Commit** `fix: scenario parser rejects unknown boats, overwrites, oversize and comment-only files`.

### Task 3: Harness code cleanups

**Files:**
- Modify: `harness/host_main.c`, `harness/pdstub.c`, `src/pd/scenario_io.c`, `harness/check.sh`, `harness/test_check.sh`
- Test: existing harness checks (behaviour-preserving) plus one new case for write failure

**Interfaces:**
- Produces: a single shared `read_fb` helper used by both host_main and the stub (if both are linked into one binary; otherwise a static copy documented as intentional); `.state` floats printed with `%.3f`.

- [ ] **Step 1: Failing case:** point the harness at an unwritable outdir and assert a non-zero exit with a message (today `write_all` ignores write errors).
- [ ] **Step 2: Fix `write_all`:** remove the dead mkdir loop, check every `write` result, propagate failure.
- [ ] **Step 3: Deduplicate `read_fb`** into one definition.
- [ ] **Step 4: `.state` precision.** Change `%.4f` to `%.3f` per the spec; regenerate `tests/golden/host/**/*.state` with the harness only, review the diff is formatting-only, and re-run `golden_refresh.sh` is NOT needed (real `.state` compared at 1e-2).
- [ ] **Step 5: Script hygiene in `harness/check.sh`/`test_check.sh`:** quote every loop variable; reorder so t14 runs before t15; make t10 mask only the intended region rather than the whole frame, adjusting its expectation so it still fails when the unmasked region differs.
- [ ] **Step 6: Run** all baseline suites (expect PASS, counts not lower).
- [ ] **Step 7: Commit** `chore: harness cleanups (write_all, read_fb, state precision, script quoting)`.

### Task 4: Fill test gaps

**Files:**
- Test: `harness/test_check.sh`, `tools/test_fbtools.py`
- Modify: `harness/check.sh` only if a test exposes a bug

- [ ] **Step 1: Add three cases to `harness/test_check.sh`:** (a) no `regression.tolerance` file, (b) an empty one, (c) one with only comments: each must behave as defaults 0.0005 and 0.001 (assert via a frame differing by an amount between default and a custom tolerance); and a "real goldens exist for a scenario but zero snapshots were compared" case that must fail with a clear message.
- [ ] **Step 2: Run; expect FAIL** where behaviour is missing, fix `check.sh` minimally.
- [ ] **Step 3: Run** `harness/test_check.sh` (expect PASS, 29+ cases) and the Python suite.
- [ ] **Step 4: Commit** `test: cover tolerance fallbacks and zero-compared branch`.

### Task 5: Comparison sensitivity

**Files:**
- Modify: `tools/fbdiff.py`, `tools/statediff.py`, `harness/check.sh`, `tests/scenarios/*.masks` (format only if needed)
- Test: `tools/test_fbtools.py`

**Interfaces:**
- Produces: `fbdiff.py` gains a `--max-cluster N` check (fails if any 8-connected cluster of differing pixels, after masks, has more than N pixels, default off so existing callers are unchanged); a per-scenario real tolerance read from `tests/scenarios/real.tolerance` (`name value` lines, falling back to 0.0065); masks apply per snapshot (`snapname: x0 y0 x1 y1`) with the old whole-scenario form still accepted.

- [ ] **Step 1: Failing tests in `test_fbtools.py`:** a 6x6 differing blob (36 px, far below 0.0065 of 96000 px) fails with `--max-cluster 20` and passes without it; a per-snapshot mask masks only its snapshot; a per-scenario tolerance overrides the global one and an unknown scenario falls back.
- [ ] **Step 2: Implement** the three features with the interfaces above; `harness/check.sh` passes `--max-cluster` for real-vs-host comparisons using a value chosen from the current real/host drift (measure it from the existing goldens and record the measured maximum and chosen value in the commit message).
- [ ] **Step 3: Tighten quiet scenarios** by adding `real.tolerance` entries only where measured drift is well below 0.0065; do not loosen anything.
- [ ] **Step 4: Run** the Python suite and `harness/check.sh` (expect PASS) and confirm a deliberate 6x6 edit to a copy of a host golden is now caught.
- [ ] **Step 5: Commit** `feat: cluster-size check, per-scenario tolerance, per-snapshot masks`.

### Task 6: CI supply chain and drift detection

**Files:**
- Modify: `.github/workflows/pr-check.yml`, `.github/workflows/golden-refresh.yml`
- Create: `.github/workflows/golden-drift.yml`

**Interfaces:**
- Produces: all `uses:` pinned to full commit SHAs (comment keeps the tag); SDK download verified against a SHA-256 stored in the workflow; a weekly scheduled `golden-drift.yml` that runs the simulator capture and fails when it differs from `tests/golden/real/` (it must not commit anything).

- [ ] **Step 1: Pin actions** by resolving each tag to its commit SHA with `gh api repos/<owner>/<repo>/git/ref/tags/<tag>`; record the tag in a trailing comment.
- [ ] **Step 2: SDK checksum.** Compute SHA-256 of the SDK archive currently used (`sha256sum`), add it as an env value and verify with `sha256sum -c` before extraction in both workflows.
- [ ] **Step 3: `golden-drift.yml`:** `on: schedule` (weekly) plus `workflow_dispatch`; reuse the refresh job's steps, then `git diff --exit-code tests/golden/real` as the pass condition.
- [ ] **Step 4: Verify** with `gh workflow run golden-drift.yml` on a branch after the file reaches `main` (dispatch requires the default branch; allow ~20 s for indexing) and confirm success; confirm `pr-check` is green on a PR.
- [ ] **Step 5: Commit** `ci: pin actions by SHA, verify SDK checksum, add weekly golden drift check`.

---

## Self-Review

- **Coverage:** every item in the follow-up list maps to a task: blindness to small changes (5), symlink and `rm -rf` guards (1), sysmenu issues and parser limits (2), test gaps (4), cleanups from earlier tasks (3), shared LOG/racy display/scenario.txt/.variant (1), single tolerance and mask repetition (5), unpinned actions/SDK checksum/drift (6).
- **Order:** tasks are independent; recommended order 1, 2, 4, 3, 5, 6 (Task 4 tests rely on Task 2 behaviour for the zero-compared message only if it touches the parser, otherwise they can run any time).
- **Known soft spots:** file and function names in Tasks 3 and 5 were taken from the earlier plan's interfaces; the implementer should confirm exact locations with `codegraph explore` before editing.
