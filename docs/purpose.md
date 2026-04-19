# Purpose

This repo provides portable scripts and documentation for managing isolated
AI-agent developer workspaces on Linux (Arch).

## Problem

AI coding agents repeatedly modify repos they were not asked to work on, even
when explicitly told not to. Prompt-level instructions are insufficient; the
agents admit the violation but keep doing it.

## Solution

Enforce write isolation at the Unix permission level:

- One Unix user per agent worker (e.g. `devh1`, `devr1`, `devo1`, `devp1`).
- Each worker can write **only** inside its own `work/<user>/` subtree.
- All workers share a `devgroup` Unix group for cross-user **read** access.
- Bare git repos under `work/bare/` are owned by the admin (`mike`) and
  read-only to workers. Workers clone from these instead of GitHub directly.
- Workers have **no** GitHub credentials, SSH keys, or push capability.
- A coordinator agent (running as `mike`) is the only actor that pushes to
  GitHub, creates PRs, and merges reviewed changes.

## Design Principles

1. **Capability-based control** -- the agent cannot write outside its lane
   because the filesystem denies it, not because a prompt asks nicely.
2. **Minimal machinery** -- standard Unix users, groups, permissions, and
   setgid directories. No containers required (though they can be added later).
3. **Portable** -- this repo can be cloned to any Linux system and the setup
   scripts re-run to recreate the same workspace layout.
4. **Auditable** -- validation scripts confirm the permission model is intact.

## Repo Layout

```
devgroup/
  docs/           # purpose, usage, design notes
  scripts/        # setup, validation, bare-repo management
  work/           # gitignored -- created by scripts at runtime
    bare/         #   bare repos (mike-owned, group-readable)
    devh1/        #   worker subtree (devh1-owned)
    devr1/        #   worker subtree (devr1-owned)
    devo1/        #   worker subtree (devo1-owned)
    devp1/        #   worker subtree (devp1-owned)
```

## Actors

| Role        | User    | Can write              | Can push to GitHub |
|-------------|---------|------------------------|--------------------|
| Coordinator | `mike`  | bare repos, own clones | Yes                |
| Worker      | `devXX` | `work/devXX/` only     | No                 |
