# Agent briefing

You are working inside a worker clone of this repo. This file is the
onboarding you read before doing anything. It's short on purpose.

## What you can and cannot do

- You have **read-only** access to shared resources outside this sandbox.
- You have **no push** capability — not to the local bare mirror, not to
  GitHub. Commits you make stay in this worktree until a human (the
  coordinator, `mike`) merges and pushes them upstream.
- You **may** read peer repos under `$ORGROOT` (one dir up from here) for
  reference, but do not write to them.

## Branch model (memorize this)

| Branch        | Who writes       | Purpose                                     |
| ------------- | ---------------- | ------------------------------------------- |
| `main`        | coordinator only | Release branch, tagged.                     |
| `dev`         | coordinator only | Integration branch, accumulates merged PRs. |
| `feat/<slug>` | you (local)      | Work in progress. Base from `origin/dev`.   |
| `fix/<slug>`  | you (local)      | Bug-fix variant of `feat/`.                 |
| `pr/<slug>`   | you (local)      | "Ready for coordinator to merge." Rename    |
|               |                  | `feat/<slug>` → `pr/<slug>` to signal this. |

**The ref name is the contract.** There is no PR API, no JSON manifest, no
ticketing. Renaming a branch is the entire "open PR" action.

## First run in this repo

Do this once before you touch anything else:

```bash
git fetch origin --prune

# If origin has a dev branch yet, use it as your base:
if git rev-parse --verify --quiet origin/dev >/dev/null; then
  git switch dev 2>/dev/null || git switch -c dev --track origin/dev
else
  # dev doesn't exist upstream yet. Create it locally from main so you
  # still base your work on the right branch. The coordinator will
  # establish origin/dev when ready; your local dev will converge on the
  # next fetch.
  git switch dev 2>/dev/null || git switch -c dev origin/main
fi
```

## Starting a task

```bash
git fetch origin --prune
git switch -c feat/<short-slug> origin/dev   # or: fix/<slug>
```

Commit as you work. Do not force-push, do not rebase once you have shared
anything. Your commits live only in this clone until signalled.

## Signaling ready

```bash
git branch -m feat/<slug> pr/<slug>
# or, from any non-shared branch:
dg-mark-pr                                   # helper does the rename
```

The coordinator's relay process scans worker clones for `refs/heads/pr/*`
and merges them into `dev`, then pushes upstream.

## After the coordinator merges

```bash
git fetch origin --prune
if git merge-base --is-ancestor pr/<slug> origin/dev; then
  git switch dev
  git merge --ff-only origin/dev
  git branch -D pr/<slug>
fi
```

## If there's a conflict on the relay

The coordinator will not resolve conflicts for you. Rebase and re-signal:

```bash
git fetch origin
git switch pr/<slug>
git branch -m pr/<slug> feat/<slug>          # back to WIP while rebasing
git rebase origin/dev
# resolve, commit
git branch -m feat/<slug> pr/<slug>          # re-signal
```

## Hard rules

- Never push. You lack the capability anyway, but don't script around it.
- Never rewrite history on `dev` or `main` (you don't own them).
- Never touch files outside `$SRCROOT` without explicit instruction.
- If something seems wrong with the shared infrastructure (bare repo,
  `dev` missing for a long time, permissions), stop and report — don't
  attempt repair.

## Handy environment variables

- `$SRCROOT` — this repo (your primary r/w area).
- `$ORGROOT` — parent dir holding peer repos (all read-only).
- `$REPOROOT` — the bare mirrors (read-only).
- `$PRIMARY_REPO` — the repo name (basename of `$SRCROOT`).

## When in doubt

Re-read this file. If it still doesn't answer the question, the canonical
long-form policy lives at
`/disk1/github/softwarewrighter/devgroup/docs/branching-pr-strategy.md` —
but prefer this brief for operational decisions.
