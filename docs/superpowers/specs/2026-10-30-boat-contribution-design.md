# Boat contribution flow: design

## Intent
Make it easy for an outsider to add a boat and for the maintainer to review it. A contributor
should only have to add `boats/<name>.json`. Every PR (boat or not) gets a rendered sail-and-pan
clip attached as a downloadable artifact, so the reviewer can watch the boat instead of reading JSON.

## Goals
- A boat PR touches only `boats/<name>.json`; the boat appears in the game menu with no code edit.
- A PR template tells the contributor what to include and links `docs/boat-schema.md`.
- A GitHub Action on every PR: runs the unit tests, validates changed boat JSON, renders a GIF per
  changed boat, uploads GIFs as an artifact and writes a job summary (loader warnings, key numbers).
- Non-boat PRs that change `src/` or `tools/` render one default boat (j24) so visual changes are visible.

## Non-goals
- No inline PR comment, hosted preview page or `pr-previews` branch (a later upgrade, "approach B").
- No secrets and no `pull_request_target`/`workflow_run` workflows. Fork PRs run unprivileged.
- No change to the physics or to the boat schema.

## Anti-goals
- A contributor needing to edit C, the Makefile or a boat list.
- A workflow that gives untrusted PR code access to a write token.

## Constraints
- `tools/gif_clip.c` hardcodes `boats/j24.json` and only writes PGM frames. It must take the boat path
  from the environment (`BOAT=`), and something must stitch frames into a GIF (Pillow is already used by `tools/frames.sh`).
- `gif_clip.c` and `frame_dump.c` include the Playdate SDK's `pd_api.h`; CI has no SDK. The SDK
  is a license-gated download, so CI must not depend on fetching it from an unofficial source.
- Boat names are a compile-time array in `src/pd/assets.c` with `ASSETS_BOATS 5`, used by the menu
  and the system options menu (`main.c`). It becomes a startup scan of `boats/*.json`.
- Boat files are read into a 16 KB buffer; keep the validator on the same limit.

## Approaches considered
- **A. Artifact + job summary (chosen).** Safe for forks, no privileged steps, works today.
- B. Inline PR comment via a second `workflow_run` workflow and a previews branch: nicest to
  review, but needs a privileged workflow handling untrusted output. Deferred; A is built so B bolts on.
- C. Hosted preview page via GitHub Pages: polish we do not need yet.

## Design
1. **Data-driven boat list.** Scan `boats/` at startup via the Playdate file API, sorted by name with
   `sunfish` (the default boat) first, capped at 16. `ASSET_BOAT_NAMES`, the menu and the options menu use the runtime list.
2. **Parameterised clip.** `gif_clip.c` reads `BOAT` (default `boats/j24.json`). A `tools/boat_clip.sh <boat.json> <out.gif>`
   wrapper builds it, runs it and assembles the GIF.
3. **Validator.** A small host tool (reusing `boatcfg_load`) prints loader warnings for a JSON file and exits non-zero on parse failure or any warning; CI runs it on changed boats.
4. **Workflow `.github/workflows/pr-check.yml`** on `pull_request`: `make -C tests test`; diff against the base to find changed `boats/*.json`;
   validate; render clips; `upload-artifact`; write `$GITHUB_STEP_SUMMARY`.
5. **PR template** `.github/PULL_REQUEST_TEMPLATE.md`, plus `.github/PULL_REQUEST_TEMPLATE/boat.md` for `?template=boat.md` links
   (checklist: real-world reference, units, schema doc read, expected warnings none). Short CONTRIBUTING section pointing at it.

## Decisions for you
- **SDK headers in CI (my call, needs your OK):** the clip tools only need the `pd_api.h` type declarations, since the
  graphics calls are stubbed. Proposed: check whether the SDK license permits vendoring `C_API/` headers;
  if it does, vendor the headers under `third_party/`; if not, the workflow downloads the SDK from play.date at run time with caching.
  The plan's first task confirms which.

## Left to the builder
Exact GIF settings (size, fps, palette), summary layout, the scan's handling of a malformed or oversized file, and the test for the scan.
