# Overview

This page describes how the system is structured at the file-system
and process level. For why the system exists at all, see
[`purpose.md`](purpose.md); for a one-paragraph status, see
[`summary.md`](summary.md).

## The problem in one sentence

AI coding agents follow filesystem permissions reliably even when they
ignore polite prompts; so the isolation between them is built out of
Unix users, groups, and modes, not out of instruction-following.

## The actors

| Role | Unix user | Can write to | Can push to GitHub |
|---|---|---|---|
| Coordinator (admin) | `mike` | All bare repos and the coordinator's own clones, plus all `tools/`, `scripts/`, `docs/` | Yes |
| Worker agent (one per agent) | `dcXXX` or `dwXXX` | Only inside its own `work/<user>/` subtree | No |
| Shared group | `devgroup` (contains all of the above) | Read-only for cross-user reference; never write | — |

Each worker agent gets a Unix user account whose home is its sandbox.
The coordinator decides what work that agent does (via a brief),
the agent commits inside its sandbox, the coordinator relays its
output to GitHub.

## Repository layout

```
devgroup/
  README.md                — entry point
  LICENSE                  — MIT
  docs/                    — design, intro, overview, usage, format specs
  scripts/                 — coordinator-only tools (setup, relay, release, etc.)
  tools/briefs/            — saga briefs: one file per piece of work in flight
  work/                    — runtime state (gitignored); created by setup
    bare/<repo>.git/       — bare mirrors of every GitHub repo; coordinator-owned
    relay/<repo>/          — coordinator's working clones for merge / promote
    scripts/               — per-user helper bins on every worker's PATH
    bin/                   — shared compiled toolchain (cor24-asm, cor24-emu, etc.)
    lib/cor24/             — shared loadable artifacts (plsw.lgo, snobol4.lgo, etc.)
    log/                   — usage logs (e.g. cor24-run deprecation shim)
    <user>/                — one subtree per worker; only that worker can write here
      github/sw-embed/     — the worker's clones of repos it's assigned to
```

## Branch and PR workflow

Workers can't push anywhere. The flow is:

1. Worker creates a feature branch in its sandbox: `feat/<slug>`.
2. When the work is complete and tested, the worker renames it to
   `pr/<slug>`. The branch *shape* (the `pr/` prefix) is the
   "ready for relay" signal.
3. Coordinator (`dg-relay`) merges the worker's `pr/<slug>` into the
   shared `dev` branch on the local bare mirror, then pushes `dev`
   to GitHub.
4. Coordinator (`dg-release`) merges `dev` into `main` on demand,
   tags it if requested, and pushes `main` to GitHub.
5. Workers reap (`dg-reap`) their merged `pr/<slug>` branches when
   convenient.

Full detail in [`branching-pr-strategy.md`](branching-pr-strategy.md).

## Why this works

- Workers don't have GitHub credentials, SSH keys, or push access
  anywhere — not local, not remote.
- Workers can't write outside their own `work/<user>/` subtree
  because the filesystem won't let them; the coordinator's bare
  repos and other workers' homes are read-only to them.
- The coordinator (`mike`) is the only actor that talks to GitHub,
  so the audit trail is centralized.
- Every step is plain Unix and plain git. No containers, no daemons.
  The setup can be torn down and rebuilt with one script run.

## Reading order for the rest of the docs

If you want to actually understand the system from scratch, in order:

1. [`purpose.md`](purpose.md) — why this exists.
2. This file — what its shape is.
3. [`usage.md`](usage.md) — how to install and bring it up.
4. [`branching-pr-strategy.md`](branching-pr-strategy.md) — how to
   ship work through it.
5. [`agent-briefing.md`](agent-briefing.md) — the doc an agent reads
   before its first task.
6. [`update-rust.md`](update-rust.md) and other operational notes as
   you need them.
