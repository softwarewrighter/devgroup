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

## 🏛️ Architecture: compilers, wrappers, demos

Each language repo (`sw-cor24-<lang>`) builds **a compiler/interpreter** for that language on COR24. That compiler is the repo's main artifact. Demos under `examples/` exercise the compiler.

Wrappers on PATH (`work/bin/<lang>`) invoke the compiler. **Two valid packagings for the shipped compiler** — mike picks at install time:

- **Bundled `.lgo`** (e.g. `plsw.lgo`): single self-contained image. Wrapper is a thin pass-through: `exec cor24-emu --lgo $TOOLROOT/../lib/cor24/<lang>.lgo "$@"`.
- **Script-composed**: wrapper is a shell script that runs `cor24-emu --lgo <interpreter>.lgo --load-binary <compiler>.sno@<addr> ...`. Useful when the compiler is itself a program in another language that runs inside an interpreter image (e.g. Fortran-in-SNOBOL4 — `fortran` could compose `snobol4.lgo` + `fortran-compiler.sno` at runtime instead of pre-bundling).

Both packagings are equivalent for users; the choice is about build-time bundling. Future briefs/sagas don't constrain agents to one or the other.

Demos and demo outputs (e.g. `examples/hello.f`, `examples/hello.lgo`) live in the repo's own `examples/` directory and are **not** installed to `work/lib/cor24/` — only compilers/interpreters live there.

## 🚀 Critical path — Fortran hello-world live demo

| # | Brief | Status |
|---|---|---|
| 1 | `dcemu-lgo-load-binary-merge.md` | ✅ shipped, on PATH |
| 2 | `dcsno-bootstrap-snobol4-toolchain.md` | ✅ shipped; canonical `--lgo` wrapper now works |
| 3 | `dcftn-fortran-hello-world.md` | ✅ shipped (Path A); `fortran` wrapper on PATH |
| 4 | `dwftn-hello-world-demo.md` | 🔵 in flight (rework after first attempt went out of scope) |

The first three landed and verified end-to-end. dwftn's web demo is the only remaining piece for the Fortran live demo to ship.

## Generic briefs (multi-agent — apply to any matching repo)

| Brief | Target | Purpose |
|---|---|---|
| `dc-migrate-toolchain.md` | any `dc*` agent | Migrate build scripts off `cor24-run --run`/`--assemble` and `$HOME/...` paths to PATH-resolved tools. Includes audit one-liner + full migration mapping table. |
| `dw-rebuild-pages.md` | any `dw*` agent (esp. apl, forth, macrolisp, pcode, plsw, snobol4) | Rebuild `pages/` after the cor24-isa path-dep migration so gh-pages reflects current source. |

## Agent-specific briefs

Status legend: 🟢 ready to start (no prereqs) · 🟡 gated (waiting on prereq) · 🔵 in flight (agent has feat/ branch, not yet pr/) · ✅ shipped + relayed + promoted to main · 🆕 just relayed, awaiting promotion

| Brief | Owner | Status |
|---|---|---|
| Brief | Owner (target) | Drafted by | Status |
|---|---|---|---|
| `dcemu-extract-isa.md` | dcemu | mike | ✅ |
| `dcemu-lgo-load-binary-merge.md` | dcemu | dcsno | ✅ shipped; cor24-emu reinstalled |
| `dcemu-remove-internal-assembler.md` | dcemu | mike | ✅ |
| `dcftn-fortran-hello-world.md` | dcftn | mike | ✅ shipped (Path A); `fortran` wrapper on PATH |
| `dcftn-fti-m1-resume.md` | dcftn | mike | 🔵 in flight on `feat/m1-resume`; SNOBOL4 nested-call fix unblocks inline IDENT(SUBSTR(...)) |
| `dcpls-bootstrap-goldens.md` | dcpls | mike | ✅ shipped; `just test` is now a green CI gate |
| `dcpls-bootstrap-plsw-toolchain.md` | dcpls | mike | ✅ |
| `dcpls-rebuild-plsw-lgo.md` | dcpls | mike | 🟢 ready (rebuild `plsw.lgo` against `c7e1262` so `pl-sw` actually emits `.zero N`) |
| `dcsno-bootstrap-snobol4-toolchain.md` | dcsno | mike | ✅ shipped; `snobol4.lgo` + wrapper on PATH |
| `dcsno-claude-md-snolib-drift.md` | dcsno | mike | 🟢 ready (CLAUDE.md prohibits snolib.plsw but it's now canonical) |
| `dcsno-combined-goto-parser.md` | dcsno | dcftn | ✅ shipped; combined-goto syntax in lexer/exec |
| `dcsno-nested-call-drops-gotos.md` | dcsno | dcftn | ✅ shipped; nested-call gotos work, `snobol4.lgo` rebuilt+reinstalled |
| `dcsno-rebuild-snobol4-artifacts.md` | dcsno | mike | 🟡 gated on `dcpls-rebuild-plsw-lgo` (rebuild snobol4.{lgo,bin} so `sno_main.s` shrinks from 261 KB to ~7 KB) |
| `dcsno-storage-allocation-runtime.md` | dcsno | dcpls | ✅ shipped (design doc landed; runtime impl future) |
| `dcxas-cor24-asm-base-addr.md` | dcxas | mike | ✅ |
| `dcxas-cor24-asm-cli.md` | dcxas | mike | ✅ |
| `dcxas-depend-on-isa-not-emulator.md` | dcxas | mike | ✅ shipped, dev-only (promotion is mike's call) |
| `dcxtc-array-size-expressions.md` | dcxtc | mike | ✅ |
| `dcxtc-codegen-dce.md` | dcxtc | (agent-drafted) | 🟡 open |
| `dcxtc-codegen-string-storage-bugs.md` | dcxtc | (agent-drafted) | 🟡 open |
| `dcxtc-rebase-codegen-baselines.md` | dcxtc | (agent-drafted) | 🟡 open |
| `dcxtc-stdlib-heap-variant.md` | dcxtc | (agent-drafted) | 🟡 open |
| `dcxtc-string-literal-concatenation.md` | dcxtc | dcpls | ✅ |
| `dwftn-hello-world-demo.md` | dwftn | mike | 🔵 in flight (first attempt went out of scope; redoing per brief) |

(Status as of 2026-05-09; mike updates this index when briefs land or new ones are added.)

### Recent shipped sagas without an associated brief

These were agent-initiated follow-ons or fixes that didn't have a pre-written brief but landed and were relayed:

| Saga | Owner | What |
|---|---|---|
| `pr/spi-emu` | dcemu | SPI peripheral emulation (mirrors prior I2C work) |
| `pr/cli-loader-output-to-stderr` | dcemu | `--quiet` mode now routes loader logs to stderr |
| `pr/fortran-wrapper-fix` | dcftn | scripts/fortran heredoc-embedded for self-contained PATH install |
| `pr/snolib-extraction` | dcsno | extracted `src/snolib.plsw` runtime library |
| `pr/combined-goto-parser` | dcsno | combined `:S(...) :F(...)` goto syntax (acts on dcftn's brief) |
| `pr/goto-precedence` | dcsno | edge-case fix for `:S(L1):S(L2)` and `:F(L1):F(L2)` precedence |
| `pr/storage-allocation-doc` | dcpls | design doc for PL/SW storage allocation + dcsno brief |

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
