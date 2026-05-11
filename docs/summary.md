# Summary

> A one-page status snapshot. For design, see
> [`overview.md`](overview.md); for the why, see
> [`purpose.md`](purpose.md).

## In one paragraph

`devgroup` is the operations repo for a single-host, multi-agent AI
coding workspace where each agent runs as its own Unix user with
filesystem-enforced sandboxing. The repo holds the provisioning
scripts, the coordinator's branch/relay tooling (`dg-relay`,
`dg-release`, `dg-reap`, `dg-list-pr`, …), the docs explaining the
design and operational workflow, and an indexed library of saga
briefs that capture each in-flight piece of work assigned to one or
more agents. The work itself happens in other repositories (the
`sw-cor24-*` and `web-sw-cor24-*` family); this repo coordinates
them.

## What this repo contains

- `scripts/` — setup, account provisioning, bare-repo sync, relay /
  release / list-pr / reap helpers, shared rust toolchain installer.
- `tools/briefs/` — one markdown file per saga / piece of work
  assigned to an agent. The `README.md` there is the live index.
- `docs/` — design notes, usage guide, branching strategy, format
  specs (currently `.lgo`), agent onboarding briefing.
- `work/` (gitignored; built by the setup script) — bare repo
  mirrors, coordinator relay clones, per-user worker sandboxes, the
  shared toolchain binaries on every worker's PATH, the shared
  loadable artifacts under `lib/cor24/`.

## What lives outside this repo

The actual development work — compilers, runtimes, demos for the
COR24 toolchain — lives in separate sibling repos under the
[`sw-embed`](https://github.com/sw-embed) GitHub org. Each agent is
assigned exactly one primary repo from that org and operates in a
sandboxed clone of it.

## Current state

- 45+ d* / dw* agents provisioned, each with its own primary repo
  (one of `sw-cor24-<lang>` / `web-sw-cor24-<lang>`).
- Coordinator tooling stable: relay, release, list-pr, reap, mark-pr,
  new-feature, new-fix all in daily use.
- Shared toolchain installed under `work/bin/`: tc24r, cor24-asm,
  cor24-emu, cor24-dbg, link24, meta-gen, plus per-language wrapper
  scripts (`pl-sw`, `snobol4`, `fortran`).
- Shared loadable images under `work/lib/cor24/`: `plsw.lgo`,
  `snobol4.lgo`, `snobol4.bin`. Updated by the coordinator after each
  relevant release lands on `main`.
- Active architecture work: dcpls's chunk-allocator phases (see
  `sw-cor24-plsw/docs/shrink-lgo-size.md`) to bring PL/SW compiler
  SRAM use under control; dcxas's `.lgo` compaction flag pair
  (`--lgo-full` / `--lgo-compact`) and parallel work in
  `bin-to-lgo.sh` on the snobol4 side.

## Where to find more

- Architecture: [`overview.md`](overview.md).
- Setup / install: [`usage.md`](usage.md).
- Workflow: [`branching-pr-strategy.md`](branching-pr-strategy.md).
- The full briefs library: [`tools/briefs/`](../tools/briefs/).
- The repo's GitHub home: <https://github.com/softwarewrighter/devgroup>.
