# tools/briefs/

Saga briefs for the COR24 multi-agent ecosystem. Each brief is a self-contained spec for a saga: the agent reads it, develops on `feat/<slug>`, renames to `pr/<slug>` when ready, signals mike, mike relays.

## How briefs work

- **Filename convention:** `<target>-<saga-slug>.md`. `<target>` is either a single agent (e.g. `dcxas-cor24-asm-cli.md`) or a generic prefix (`dc-`, `dw-`) for multi-agent briefs.
- **Branch convention — feat→pr rename:**
  - **`feat/<slug>`** = work-in-progress. Agents create this with `dg-new-feature <slug>` (which cuts from `origin/dev`).
  - **`pr/<slug>`** = signal that the saga is **ready for relay**. Created by renaming `feat/<slug>` → `pr/<slug>` via `dg-mark-pr` once the work is complete and tested.
  - The branch shape **is** the readiness signal. Never start a saga on `pr/<slug>` directly — `dg-list-pr` would see it as ready and `dg-relay` could integrate WIP.
- **Workflow:** agent reads brief → `dg-new-feature <slug>` (creates `feat/<slug>` from `dev`) → implements + tests → `dg-mark-pr` (renames to `pr/<slug>`) → signals mike → mike `dg-relay`s → mike promotes `dev → main` when ready (separate, on-demand step).
- **Agents may draft cross-repo briefs.** If an agent diagnoses a blocker in another repo (e.g. a tc24r limitation), they can draft a brief for the responsible agent and drop it directly here. Mike reviews at relay time. See `feedback_agent_drafted_briefs.md` in the project memory.
- **Permissions:** dir is `drwxrwsr-x mike:devgroup`. Mike-authored briefs have mode `640 mike:devgroup`. Agent-drafted briefs inherit `<agent>:devgroup` ownership.

## Generic briefs (multi-agent — apply to any matching repo)

| Brief | Target | Purpose |
|---|---|---|
| `dc-migrate-toolchain.md` | any `dc*` agent | Migrate build scripts off `cor24-run --run`/`--assemble` and `$HOME/...` paths to PATH-resolved tools. Includes audit one-liner + full migration mapping table. |
| `dw-rebuild-pages.md` | any `dw*` agent (esp. apl, forth, macrolisp, pcode, plsw, snobol4) | Rebuild `pages/` after the cor24-isa path-dep migration so gh-pages reflects current source. |

## Agent-specific briefs — open

| Brief | Owner | Branch | Status |
|---|---|---|---|
| `dcemu-extract-isa.md` | dcemu | `pr/extract-isa` | ✅ shipped, relayed, promoted to main |
| `dcemu-remove-internal-assembler.md` | dcemu | `pr/remove-internal-assembler` | ✅ shipped, relayed, promoted to main |
| `dcpls-bootstrap-goldens.md` | dcpls | `pr/bootstrap-goldens` | 🟡 cleared to start; toolchain prereqs satisfied |
| `dcpls-bootstrap-plsw-toolchain.md` | dcpls | `pr/bootstrap-toolchain` | ✅ shipped, relayed, promoted to main |
| `dcsno-bootstrap-snobol4-toolchain.md` | dcsno | `pr/bootstrap-toolchain` | 🟡 cleared, placeholder branch only |
| `dcxas-cor24-asm-base-addr.md` | dcxas | `pr/cor24-asm-base-addr` | ✅ shipped, relayed, on PATH |
| `dcxas-cor24-asm-cli.md` | dcxas | `pr/cor24-asm-cli` | ✅ shipped, relayed, on PATH |
| `dcxas-depend-on-isa-not-emulator.md` | dcxas | `pr/depend-on-isa-not-emulator` | 🟡 cleared, prereq (dcemu's extract-isa) satisfied |
| `dcxtc-array-size-expressions.md` | dcxtc | `pr/array-size-expressions` | ✅ shipped, relayed, tc24r reinstalled |
| `dcxtc-string-literal-concatenation.md` | dcxtc | `pr/string-literal-concatenation` | ✅ shipped, relayed, tc24r reinstalled |

(Status as of 2026-05-08; mike updates this index when briefs land or new ones are added.)

## Where things live

| What | Where |
|---|---|
| Briefs (this dir) | `/disk1/github/softwarewrighter/devgroup/tools/briefs/` |
| Bare repos | `/disk1/github/softwarewrighter/devgroup/work/bare/` |
| Agent working clones | `/disk1/github/softwarewrighter/devgroup/work/<agent>/github/sw-embed/<repo>/` |
| PATH binaries (every d* user) | `/disk1/github/softwarewrighter/devgroup/work/bin/` |
| COR24 target artifacts (e.g. `plsw.lgo`) | `/disk1/github/softwarewrighter/devgroup/work/lib/cor24/` |
| Relay clones (mike only) | `/disk1/github/softwarewrighter/devgroup/work/relay/<repo>/` |

## Coordinator tools (mike-only, in `/disk1/.../scripts/`)

- `dg-relay <agent> <repo> <pr-branch>` — fetches an agent's `pr/<slug>` into the relay clone, merges into `dev`, pushes bare and GitHub
- `dg-release <repo> [tag]` — promotes `dev → main` with optional annotated tag
- `setup-devgroup-accounts.sh dev-users.tsv` — provisions/refreshes d* users (idempotent)
- `sync-bare-repos.sh [org]` — fetches every non-archived repo in the GitHub org into `work/bare/`

## Agent helpers (in `work/scripts/`, on PATH for d* users)

- `dg-list-pr` — list pending `pr/*` branches across all agent clones
- `dg-reap` — clean up local `pr/*` branches that are now reachable from `origin/dev`
- `dg-mark-pr`, `dg-new-feature`, `dg-new-fix`, `dg-policy` — saga lifecycle helpers
