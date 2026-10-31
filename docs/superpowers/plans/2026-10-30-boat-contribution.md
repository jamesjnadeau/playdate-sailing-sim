# Boat Contribution Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A boat PR is one `boats/<name>.json`; every PR runs checks and gets a rendered sail-and-pan GIF as a downloadable artifact.

**Architecture:** A pure, host-testable `boatlist` module builds the menu's boat names from a directory listing, and `assets.c` feeds it from the Playdate file API at startup. A host validator (`boat_check`), a boat-parameterised clip tool and one orchestration script (`tools/pr_check.sh`) hold all CI logic, so it runs locally; the workflow only calls the script and uploads its output.

**Tech Stack:** C99 (sim + Playdate C API), bash, Python 3 + Pillow (GIF assembly, already used by `tools/frames.sh`), GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-30-boat-contribution-design.md`

## Global Constraints

- A boat PR touches only `boats/<name>.json`; no C, Makefile or list edits.
- Boat files are read into a 16 KB buffer (`FILE_MAX 16384` in `src/pd/assets.c`); the validator uses the same limit.
- Boat list capped at 16, sorted by name with `sunfish` first.
- Workflow triggers on `pull_request` only; no secrets, no `pull_request_target`, no `workflow_run`.
- Output is an artifact plus `$GITHUB_STEP_SUMMARY`; no PR comments, no previews branch.
- Non-boat PRs that change `src/`, `tools/` or `config/` render `boats/j24.json`.
- Tests build with `-Wall -Wextra -Werror` and ASan/UBSan (`tests/Makefile`); new `src/sim/*.c` files are picked up by its wildcard.

## Review Focus

- Directory entries that are not boats (`README.md`, `.DS_Store`, `x.json.bak`, a subdirectory `old/`) must not appear in the menu. Pinned in Task 1.
- Names that are too long or contain spaces, quotes or `/` must be skipped, never truncated into a clash with another boat. Pinned in Task 1.
- More than 16 boat files must not overflow; `sunfish` stays first. Pinned in Task 1.
- An empty or unreadable `boats/` directory must still leave one menu entry. Pinned in Task 1.
- A malformed or over-16 KB boat file must fail the validator with a message naming the file. Pinned in Task 3.
- A PR that deletes or renames a boat, or only changes docs, must not try to render a missing file and must pass. Pinned in Task 5.

---

### Task 1: `boatlist` module

**Files:**
- Create: `src/sim/boatlist.h`, `src/sim/boatlist.c`
- Test: `tests/test_boatlist.c`

**Interfaces:**
- Produces:
  ```c
  #define BOATLIST_MAX 16
  #define BOATLIST_NAME_LEN 24          /* including NUL */
  typedef struct {
      int count;
      char name[BOATLIST_MAX][BOATLIST_NAME_LEN];
      const char *ptr[BOATLIST_MAX];    /* valid after boatlist_finish; pointers into name[] */
  } BoatList;
  void boatlist_init(BoatList *l);
  bool boatlist_add(BoatList *l, const char *filename);   /* true if accepted as a boat */
  void boatlist_finish(BoatList *l);    /* sort, sunfish first, fill ptr; empty -> single "sunfish" */
  ```

- [ ] **Step 1: Write failing tests in `tests/test_boatlist.c`** (use `t.h` like other tests). Test names and assertions:
  - `test_accepts_json_strips_extension`: add `"j24.json"` → true, `l.name[0]` is `"j24"`.
  - `test_rejects_non_boats`: `"README.md"`, `".DS_Store"`, `"x.json.bak"`, `"old/"`, `".json"`, `".hidden.json"` all return false; count stays 0.
  - `test_rejects_bad_names`: `"my boat.json"`, `"a\"b.json"`, `"a/b.json"`, and a 24-character stem `"abcdefghijklmnopqrstuvwx.json"` return false; a 23-character stem is accepted. Allowed characters are `[a-z0-9_-]`; uppercase is rejected.
  - `test_sorted_sunfish_first`: add `tp52, j24, sunfish, longship, dinghy` → after finish order is `sunfish, dinghy, j24, longship, tp52`; `ptr[i]` equals `name[i]`.
  - `test_cap_and_order`: add 20 names `b00..b19` plus `sunfish` → count is 16, `name[0]` is `sunfish`, no read past the arrays (ASan).
  - `test_duplicate_ignored`: adding `j24.json` twice gives count 1.
  - `test_empty_gets_default`: finish on an empty list → count 1, `name[0]` is `"sunfish"`.
- [ ] **Step 2: Run** `make -C tests test` — expect compile failure (no `boatlist.h`).
- [ ] **Step 3: Implement** the three functions in `src/sim/boatlist.c`; `boatlist_finish` sorts with `qsort` or insertion sort over `name[]`, moving `sunfish` to index 0.
- [ ] **Step 4: Run** `make -C tests test` — expect all tests PASS including `build/test_boatlist`.
- [ ] **Step 5: Commit** `feat: add boatlist to build the boat menu from directory entries`

### Task 2: Data-driven boat list in the game

**Files:**
- Modify: `src/pd/assets.h`, `src/pd/assets.c`, `src/pd/main.c`, `src/pd/menu_draw.c`
- Modify: `docs/boat-schema.md` (add a short "Adding a boat" section: drop a file in `boats/`, it appears in the menu; link the PR template)

**Interfaces:**
- Consumes: `BoatList`, `boatlist_init/add/finish` from Task 1.
- Produces (replacing `ASSET_BOAT_NAMES` and `ASSETS_BOATS`):
  ```c
  void assets_scan_boats(PlaydateAPI *pd);        /* lists "boats" via pd->file->listfiles, once at startup */
  int assets_boat_count(void);
  const char *const *assets_boat_names(void);     /* count entries, stable until the next scan */
  void assets_boat_raceable(PlaydateAPI *pd, bool out[BOATLIST_MAX]);   /* signature array size only */
  ```

- [ ] **Step 1: Implement** the scan in `assets.c` with a `static BoatList boats;` and a listfiles callback that calls `boatlist_add`; `assets_load` and `assets_boat_raceable` iterate `boats.count` and build paths from `boats.name[i]`; an out-of-range index clamps to 0 as today.
- [ ] **Step 2: Update callers.** `main.c`: call `assets_scan_boats(pd)` before the first `assets_boat_raceable`/`show_menu`; `boat_raceable` becomes `bool [BOATLIST_MAX]`; `menu_init(&menu, ntracks, assets_boat_count())`; the options-menu item uses `assets_boat_names()` and `assets_boat_count()`. `menu_draw.c` uses `assets_boat_names()[k]`. Remove every `ASSETS_BOATS` and `ASSET_BOAT_NAMES` reference.
- [ ] **Step 3: Verify build.** Run `grep -rn "ASSETS_BOATS\|ASSET_BOAT_NAMES" src tools tests` — expect no output. Run `tools/sim.sh --no-run` — expect a clean build of `SailingSim.pdx` with no warnings.
- [ ] **Step 4: Verify in the simulator** (needs a person): `tools/sim.sh`; the boat screen lists `sunfish, dinghy, j24, longship, tp52`, `longship` shows "(free range only)" if it is a cruiser, and the system menu boat option switches boats. Then copy `boats/j24.json` to `boats/zz_test.json`, rebuild, confirm `zz_test` appears last, delete the copy.
- [ ] **Step 5: Commit** `feat: list boats from the boats directory instead of a hardcoded array`

### Task 3: Boat validator

**Files:**
- Create: `tools/boat_check.c`
- No unit-test file: the tool is exercised by Steps 2-3 here and by `tools/pr_check.sh` in Task 5.

**Interfaces:**
- Produces: host binary `boat_check <file.json>...`. For each file it prints `<file>: ok` or `<file>: <message>` and exits 1 if any file is unreadable, larger than 16383 bytes, or `boatcfg_load` returns false (any warning). Prints one line per file, nothing else.

- [ ] **Step 1: Implement** `tools/boat_check.c` using `boatcfg_default` and `boatcfg_load` from `src/sim/boatcfg.h`; build line: `cc -std=c99 -O1 -Wall -Wextra -I src/sim tools/boat_check.c src/sim/*.c -lm -o build/boat_check`.
- [ ] **Step 2: Baseline run.** Build it, then `build/boat_check boats/*.json`. Expected: `ok` for all five and exit 0. If a shipped boat warns, fix the boat JSON (not the tool) and mention it in the commit.
- [ ] **Step 3: Failure cases.** In the scratchpad directory create `bad_range.json` (`{"hull":{"mass":-5}}`), `bad_syntax.json` (`{"hull":`), and `big.json` (20000 bytes of spaces inside a valid object). Expect exit 1 and a line naming each file with the loader's message (`hull.mass: -5 outside [5, 20000], clamped` for the first); a nonexistent path also exits 1 with `<file>: cannot read`.
- [ ] **Step 4: Commit** `feat: add boat_check validator for boat JSON files`

### Task 4: Boat-parameterised clip to GIF

**Files:**
- Modify: `tools/gif_clip.c` (read `BOAT`, default `boats/j24.json`; print a clear error and exit 1 if the file cannot be opened instead of silently using the dinghy)
- Create: `tools/boat_clip.sh`

**Interfaces:**
- Produces: `tools/boat_clip.sh <boat.json> <out.gif>` — exit 0 on success, last stdout line is the clip tool's `frames N, min |heel| ..., max luff ..., panned ...` summary.
- `PD_API_INCLUDE` env var gives the directory containing `pd_api.h`; default `${PLAYDATE_SDK_PATH:-$HOME/Developer/PlaydateSDK-3.1.2}/C_API`.

- [ ] **Step 1: Resolve SDK headers for CI.** Read the license in the local SDK (`$HOME/Developer/PlaydateSDK-3.1.2/` license/readme files) and the play.date SDK download terms. If redistribution of `C_API/` headers is permitted, vendor them under `third_party/playdate/C_API` and set that as the CI `PD_API_INCLUDE`; otherwise the workflow (Task 6) downloads the SDK at run time with `actions/cache`. Record the decision and the license text relied on in the commit message. If the license is unclear, stop and ask the maintainer.
- [ ] **Step 2: Parameterise the tool.** Replace the hardcoded path with `getenv("BOAT")`, keeping the J/24 default; update the header comment to mention `BOAT`.
- [ ] **Step 3: Write `tools/boat_clip.sh`** modelled on `tools/frames.sh`: compile `tools/gif_clip.c src/sim/*.c src/pd/render.c src/pd/hud.c` with `-I "$PD_API_INCLUDE"`, run with `BOAT` and a temp `FRAME_DIR`, then assemble the PGM frames into one looping GIF with Pillow (`python3 -I`), 1x size, at most 5 MB (reduce fps or frame count if larger), then delete the temp frames.
- [ ] **Step 4: Verify.** `tools/boat_clip.sh boats/longship.json build/clips/longship.gif` — expect exit 0, a GIF whose first/last frames differ and whose file is under 5 MB (`ls -l`); open it and confirm a longship with a square sail pans. Then `tools/boat_clip.sh /nonexistent.json build/x.gif` — expect exit 1 and a message naming the file.
- [ ] **Step 5: Commit** `feat: render a sail-and-pan GIF for any boat file`

### Task 5: PR check orchestration script

**Files:**
- Create: `tools/pr_check.sh`

**Interfaces:**
- Consumes: `tools/boat_check` build line (Task 3), `tools/boat_clip.sh` (Task 4).
- Produces: `tools/pr_check.sh <base-ref> <outdir>` — writes GIFs to `<outdir>/<boat>.gif` and `<outdir>/summary.md` (markdown: one row per boat with validator result and the clip's summary line; or a one-line "nothing to render" note). Exit 1 if any changed boat fails validation or any render fails; exit 0 otherwise. Does not run the unit tests (the workflow does).
- Selection: changed boats = `git diff --name-only --diff-filter=AM <base-ref>...HEAD -- 'boats/*.json'` (deleted and renamed-away files excluded); if none and `git diff --name-only <base-ref>...HEAD -- src tools config` is non-empty, render `boats/j24.json`; otherwise render nothing.

- [ ] **Step 1: Implement** the script with `set -euo pipefail`, building `boat_check` once, validating all selected boats before rendering any.
- [ ] **Step 2: Scenario checks** in a throwaway clone (`git clone . "$SCRATCH/repo"`, branch off `main` in each case, run the script there with base `main`):
  - copy `boats/j24.json` to `boats/zz_new.json` → one GIF `zz_new.gif`, summary has a row, exit 0.
  - append a newline to `src/sim/boat.c` (no boat changes) → renders `j24.gif` only.
  - change only `README.md` → no GIF, summary says nothing to render, exit 0.
  - `git rm boats/tp52.json` → no render attempted, exit 0.
  - add `boats/bad.json` containing `{"hull":{"mass":-5}}` → exit 1, the summary row names `boats/bad.json` and the warning.
- [ ] **Step 3: Commit** `feat: add pr_check.sh to validate and render clips for a pull request`

### Task 6: Workflow, PR templates and contributor docs

**Files:**
- Create: `.github/workflows/pr-check.yml`, `.github/PULL_REQUEST_TEMPLATE.md`, `.github/PULL_REQUEST_TEMPLATE/boat.md`
- Modify: `README.md` (a "Contributing a boat" paragraph linking `docs/boat-schema.md` and the `?template=boat.md` compare URL)

**Interfaces:**
- Consumes: `tools/pr_check.sh <base-ref> <outdir>` (Task 5), the SDK decision from Task 4 step 1.

- [ ] **Step 1: Write the workflow.** Trigger `pull_request`; `permissions: contents: read`; one job on `ubuntu-latest`: `actions/checkout` with `fetch-depth: 0`; install Pillow; provide `PD_API_INCLUDE` per the Task 4 decision (vendored path, or download-and-cache step); `make -C tests test`; `tools/pr_check.sh "origin/${{ github.base_ref }}" out`; `cat out/summary.md >> "$GITHUB_STEP_SUMMARY"` (use `if: always()`); `actions/upload-artifact` of `out/*.gif` with `if-no-files-found: ignore`. Pin actions to released major versions.
- [ ] **Step 2: Write the templates.** Default template: what changed, how tested, and a line saying boat PRs should use `?template=boat.md`. Boat template checklist: only `boats/<name>.json` added; name is lowercase `[a-z0-9_-]`, at most 23 characters; real-world boat or source for the numbers; SI units per `docs/boat-schema.md`; `boat_check` reports ok; says whether it should race or be `boat_class` 1 (cruiser); note that the review GIF appears as the `boat-clips` artifact on the PR's checks.
- [ ] **Step 3: Verify locally.** Parse the workflow with `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/pr-check.yml'))"` and run `actionlint` if installed; expect no errors. Re-run `tools/pr_check.sh main build/pr-out` from the branch and confirm it matches what the workflow calls.
- [ ] **Step 4: Commit** `feat: PR workflow rendering boat clips, plus PR templates`
- [ ] **Step 5: First real run (needs the maintainer).** After pushing, open a draft PR that copies `boats/j24.json` to `boats/zz_test.json`; confirm the check passes, the `boat-clips` artifact contains `zz_test.gif`, and the job summary table renders. Close the PR without merging.

---

## Self-review notes

- Spec coverage: data-driven list (Tasks 1-2), parameterised clip and GIF (4), validator (3), workflow with tests/validate/render/artifact/summary (5-6), non-boat fallback render (5), templates and docs (6), SDK-header decision (4 step 1, 6 step 1). Non-goals (comments, previews branch, secrets) appear only as constraints.
- Interfaces are consistent across tasks: `BoatList`/`BOATLIST_MAX` (1→2), `boat_check` (3→5), `boat_clip.sh` (4→5), `pr_check.sh` (5→6), `PD_API_INCLUDE` (4→6).
