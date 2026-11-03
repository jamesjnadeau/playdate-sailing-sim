# Parallel Tests Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut `make -C tests test` from about 2m40s to roughly 15-25 s on 8 cores, without weakening any test.

**Architecture:** Compile the sim sources once into shared objects that every test links against. Run each test as its own make target (a stamp file) so make's `-j` parallelises build and run. Split `test_baseline`'s 27-race matrix into per-(track, boat) shards by giving the binary command-line arguments.

**Tech Stack:** GNU Make 4.4, C99, gcc with ASan/UBSan (flags unchanged).

**Spec:** `docs/superpowers/specs/2026-11-02-parallel-tests-design.md`

## Global Constraints

- Keep the existing flags exactly: `-std=c99 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined`.
- `make -C tests` and `make -C tests test` (used in `README.md` and `.github/workflows/pr-check.yml`) must keep working, and `test` stays the default goal.
- `./build/test_baseline` with no arguments still runs all four tests.
- A failing test makes make exit non-zero and shows that test's output.
- Plain `make test` is parallel by default; `make -j1 test` still works serially.
- `bench` and `clean` targets keep working.
- Work happens in the `parallel-tests` worktree, branched from local `main` (the uncommitted ghost work the spec mentions has since been merged into `main`).

## Review Focus

- A typo'd track or boat given to `test_baseline` must fail loudly, not pass having run zero races (Task 2 test).
- A failure inside one shard must fail the whole `make test` and print that shard's output, under `-j8` (Task 1 and 2 verification).
- Editing a header must rebuild the objects that include it (Task 1 verification).
- `make clean` must not first build `test_baseline` to generate the shard list (Task 2).
- A fresh checkout with no `tests/build/` must work with a single `make test` (Task 2 verification).

---

### Task 1: Shared objects, per-test stamps, parallel by default

**Files:**
- Modify: `tests/Makefile` (full rewrite of the `test` and `build/%` rules; leave `bench`, `build/bench_seasound` and `clean` as they are)

**Interfaces:**
- Produces: variable `OBJS` (shared objects in `build/obj/`), variable `RUNS` (the list of `build/<test>.ok` stamps `test` depends on), and a pattern rule `build/%.ok` that runs `build/%` and logs to `build/%.log`. Task 2 adds more stamps to `RUNS` and relies on these.

- [ ] **Step 1: Record the baseline.** Run `rm -rf tests/build && time make -C tests test 2>&1 | tail -3` and note the wall time (about 2m40s expected) in your commit message later.

- [ ] **Step 2: Add shared object rules.** Compile each of `../src/sim/*.c` and `../src/pd/scenario.c` to `build/obj/<name>.o` using the existing `CFLAGS` plus `-MMD -MP`. Use `vpath %.c ../src/sim ../src/pd` and a pattern rule `build/obj/%.o: %.c`. Include the generated `.d` files with `-include $(wildcard build/obj/*.d build/*.d)`.

- [ ] **Step 3: Link tests against objects.** Replace the `build/%` rule with `build/%: %.c t.h $(OBJS)` that compiles the test file with `$(CFLAGS) -MMD -MF build/$*.d` and links `$(OBJS) $(LDLIBS)`.

- [ ] **Step 4: Add per-test stamps.** Define `RUNS := $(TESTS:%.c=build/%.ok)` and a rule `build/%.ok: build/%` that runs `./$<` with stdout and stderr to `build/$*.log`. On success print one line `ok  $*` followed by the log's last line (the `N tests passed` summary). On failure print the entire log and exit 1. Make `test: $(RUNS)` and keep it the first target.

- [ ] **Step 5: Parallel by default.** Add `MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4) --output-sync=target` near the top.

- [ ] **Step 6: Verify speed-up and equivalence.** Run `rm -rf tests/build && time make -C tests test`. Expected: exits 0, one `ok` line per test (37), and the wall time is roughly the length of `test_baseline` plus a few seconds.

- [ ] **Step 7: Verify failure surfaces.** Temporarily change one assertion in `tests/test_rudder.c` to a false condition, run `make -C tests test`, and expect a non-zero exit with that test's assertion message printed. Revert the change.

- [ ] **Step 8: Verify header dependencies and serial mode.** Run `touch src/sim/mathx.h && make -C tests test -n | grep -c 'obj/'`; expect the objects that include it to be rebuilt (count greater than 0). Then run `make -j1 -C tests test` and expect it to pass.

- [ ] **Step 9: Commit.**

```bash
git add tests/Makefile
git commit -m "test: shared sim objects, per-test stamps, parallel make test"
```

---

### Task 2: Shard `test_baseline`

**Files:**
- Modify: `tests/test_baseline.c` (replace the loop body of `test_all_courses_all_boats_finish` with a helper, and replace `main`)
- Modify: `tests/Makefile`

**Interfaces:**
- Consumes: `RUNS` and the `build/%.ok` rule from Task 1.
- Produces: `./build/test_baseline` accepting these arguments:
  - none: runs all four tests (today's behaviour).
  - `core`: runs only `test_sunfish_finishes_windward_leeward`, `test_passes_marks_wide` and `test_deterministic`.
  - `list`: prints one `<track> <boat>` pair per line (27 lines) and exits 0.
  - `<track> <boat>`: runs that one matrix row, exits 0 if it finished and `min_mark_m >= BASELINE_MIN_CLEAR_M`, and exits 1 on an unknown track or boat.

- [ ] **Step 1: Write the failing check.** Run `./tests/build/test_baseline list | wc -l` after Task 1. Expected: it does not print 27 (the binary ignores arguments and runs for 76 s), so interrupt it with Ctrl-C. Also run `./tests/build/test_baseline nosuchtrack sunfish; echo $?`, which must fail non-zero once the feature exists.

- [ ] **Step 2: Extract the row.** In `test_baseline.c`, add `static int run_row(const char *track, const char *boat)` that does one iteration of the current loop body (load, `baseline_sail`, the same `printf` format) and returns 1 when the row is bad (not finished or `min_mark_m < BASELINE_MIN_CLEAR_M`). Have `test_all_courses_all_boats_finish` call it for every pair and keep `T_ASSERT(bad == 0)`.

- [ ] **Step 3: Replace `main`.** Change to `int main(int argc, char **argv)` implementing the argument table above. Validate the track and boat against `TRACKS` and `BOATS` and print an error to stderr and return 1 if either is unknown. For a valid row, return `run_row(...)` as the exit status after printing `1 test passed`-style output that the stamp can show (use `T_RUN`-compatible output, or print `1 test passed` on success).

- [ ] **Step 4: Verify the binary.**
  - `make -C tests build/test_baseline && ./tests/build/test_baseline list | wc -l` prints `27`.
  - `./tests/build/test_baseline windward_leeward sunfish` exits 0 in a few seconds.
  - `./tests/build/test_baseline nosuchtrack sunfish; echo $?` prints an error and `1`.
  - `./tests/build/test_baseline core` exits 0.

- [ ] **Step 5: Generate shard stamps in the Makefile.** Add a rule `build/baseline_shards.mk: build/test_baseline` that writes `BASELINE_SHARDS := <track>.<boat> ...` (the `list` output with the space in each pair turned into a dot; track names contain no dots). Include it with `-include`, skipped when `$(MAKECMDGOALS)` is `clean`. Add a rule `build/test_baseline.%.ok: build/test_baseline` that runs the binary with the two words obtained by splitting `$*` on the dot, logging and reporting exactly like the generic stamp. Add `build/test_baseline.core.ok` the same way with argument `core`. Remove `build/test_baseline.ok` from `RUNS` and append the shard stamps and the core stamp instead.

- [ ] **Step 6: Verify the whole suite.** Run `rm -rf tests/build && time make -C tests test`. Expected: exit 0 from a clean tree with a single command, about 28 baseline `ok` lines plus 36 others, and the wall time is well under the 80 s that Task 1 reached. Record the number.

- [ ] **Step 7: Verify failure and clean.**
  - Edit `BASELINE_MIN_CLEAR_M`'s use in `run_row` to demand an impossible clearance, run `make -C tests test`, and expect non-zero exit with the `<-- FAIL` row in the output. Revert.
  - `make -C tests clean` must not compile anything (watch the output).

- [ ] **Step 8: Commit.**

```bash
git add tests/Makefile tests/test_baseline.c
git commit -m "test: shard test_baseline by course and boat"
```

---

## Self-review notes

- Spec coverage: shared objects, stamps and parallel default are Task 1; sharding is Task 2. The optional `test_shipped_ghosts` sharding is deliberately not planned (about 5 s, below the 80 s floor once `test_baseline` is split).
- Names used across tasks (`OBJS`, `RUNS`, `build/%.ok`, `run_row`, argument forms) match.
- Out of scope per spec: removing the races tests 1 and 2 repeat, and changing sanitizer flags.
