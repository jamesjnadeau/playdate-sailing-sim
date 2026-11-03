# Faster test confirmation via parallelism

## Intent
`make test` in `tests/` takes about 2m40s. The aim is less wall-clock time between "I changed something" and "I know whether it still works", without weakening what the tests check.

## Where the time goes (measured 2026-11-02, 8 cores)
- `test_baseline` runs for about 76 s in a single process. It does 31 baseline-pilot races: a 27-race course x boat matrix, plus 4 races in the other tests.
- Every other test runs in under 6 s (`test_shipped_ghosts` about 5 s, the rest under 1 s).
- Every test binary recompiles all of `src/sim/*.c` with ASan/UBSan, about 6.5 s each across 37 binaries, one after another. The run loop is also serial.

## Goals
- A full `make test` on a warm tree finishes in roughly 15-25 s on this 8-core machine. This is an estimate and will be measured.
- Plain `make test` is parallel by default, with no `-j` to remember.
- Editing one sim file rebuilds that file's object once, not 37 binaries from scratch.

## Non-goals
- No change to the sanitizer or optimisation flags, and no reduction in simulated work or assertions.
- No removal of redundant races (tests 1 and 2 repeat races the matrix also runs). That is a separate saving.
- No separate "fast mode".

## Anti-goals
- A failure must still exit non-zero and show the failing assertion's output, not a swallowed log.
- `./build/test_baseline` with no arguments must still run everything, as it does now.

## Approaches considered
- **A. Makefile-only parallelism** (shared objects, `-j`, one stamp per test). Needs no test changes, but the 76 s test sets the floor, so the whole suite would still take about 80 s.
- **B. A plus sharding `test_baseline`** (chosen, as the only one that gets under the 76 s floor). The matrix is a pair of independent loops with no shared state across iterations, so it splits cleanly.
- Ruled out: per-test caching or skipping. It saves time but means a run can pass without running everything.

## Design
1. **Shared objects.** Compile each `src/sim/*.c` and `src/pd/scenario.c` once into `build/obj/*.o` with the current flags plus `-MMD -MP`, so header changes trigger the right rebuilds. Each test links its own `.c` against those objects.
2. **One stamp per test.** `build/<test>.ok` runs the binary and writes its output to `build/<test>.log`. `make test` depends on all stamps. A failed stamp prints its log and fails the build; output stays grouped per test (`--output-sync=target` or equivalent).
3. **Parallel by default.** `MAKEFLAGS += -j$(shell nproc)` in `tests/Makefile`, so a plain `make test` is parallel and `make -j1 test` still runs serially.
4. **Sharding `test_baseline`.** The binary accepts optional arguments:
   - no args: runs everything (today's behaviour);
   - `core`: runs only the three non-matrix tests;
   - `<track> <boat>`: runs one matrix row, with the same print format and the same pass criterion (`finished` and `min_mark_m >= BASELINE_MIN_CLEAR_M`).
   The Makefile generates one stamp per (track, boat) pair plus `core`, 28 jobs in total. The track and boat lists stay in `test_baseline.c` as the single source of truth. The Makefile needs the same list, so the builder decides how to avoid duplicating it. For example, the binary could print the pairs with a `list` argument that the Makefile reads.
5. **`test_shipped_ghosts`** (5 s) can use the same mechanism if it shards as easily. It is optional and worth doing only if cheap.

## Left to the builder
- How the Makefile obtains the shard list (see 4).
- Whether the top-level `Makefile` or other scripts call `tests/Makefile` and need updating.
- Verifying the result: before/after wall time, a deliberately broken assertion proving failure still surfaces, and `./build/test_baseline` with no arguments still passing.

## Uncommitted work
When this was written, `tests/test_baseline.c` and `src/sim/baseline.*` had uncommitted changes. They have since been merged into `main`, and the work is built on that.
