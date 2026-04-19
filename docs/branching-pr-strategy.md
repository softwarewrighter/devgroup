# Branching and Pseudo-PR Strategy

How worker agents (`devXX`) signal "ready to merge" and how the coordinator
(`mike`) relays their work to GitHub, without giving workers push access
anywhere.

## Topology recap

```
worker clone (devXX-owned)  --fetch-->  bare repo (mike-owned)  <--push-->  GitHub
      origin = bare                          origin = GitHub
```

- Worker clones' `origin` remote = the local bare repo under `work/bare/`.
- Bare repos' `origin` remote = the real GitHub repo.
- Workers have **read-only** access to bare repos (mode `2750`, files `640`,
  ACL `g:devgroup:rX`). Workers cannot push to bare or to GitHub.
- Only `mike` has GitHub credentials and only `mike` can write to bare repos.

## Branch model

| Branch          | Who writes       | Purpose                                     |
|-----------------|------------------|---------------------------------------------|
| `main`          | `mike` only      | Release branch. Tagged.                     |
| `dev`           | `mike` (relay)   | Integration branch. Accumulates merged PRs. |
| `pr/<slug>`     | `devXX` (local)  | "Ready to relay" signal to the coordinator. |
| `feat/<slug>`   | `devXX` (local)  | Work-in-progress. Ignored by the relay.     |
| anything else   | `devXX` (local)  | Ignored.                                    |

The ref name is the contract. A worker renaming `feat/foo` to `pr/foo` means
"please merge." No JSON manifest, no drop box, no helper protocol.

Workers always base new branches on `origin/dev`, never `origin/main`, so they
see and integrate with prior merged work.

## Worker workflow (devXX)

### Start a task

```bash
cd ~/work/devXX/github/sw-embed/<repo>
git fetch origin --prune
git switch -c feat/<slug> origin/dev
```

### While working

Normal commits on the `feat/<slug>` branch. Worker has no push capability;
commits stay local until relayed.

### Signal ready

Rename the branch. That is the entire "open PR" action:

```bash
git branch -m feat/<slug> pr/<slug>
```

### Detect merge and clean up

After the relay has merged the branch into `dev` and pushed to GitHub, the
bare repo's `dev` ref advances. The worker learns this by fetching:

```bash
git fetch origin --prune
if git merge-base --is-ancestor pr/<slug> origin/dev; then
  git switch dev 2>/dev/null || git switch -c dev origin/dev
  git branch -D pr/<slug>
fi
```

The helper `work/bin/pr-reap` wraps this for convenience.

### Handling merge conflicts

If the relay reports a conflict (the branch didn't fast-forward cleanly on
`dev` because of other workers' merged changes), the worker rebases and
re-signals:

```bash
git fetch origin
git switch pr/<slug>
git branch -m pr/<slug> feat/<slug>         # back to WIP while rebasing
git rebase origin/dev
# resolve conflicts, commit
git branch -m feat/<slug> pr/<slug>         # re-signal
```

## Coordinator workflow (mike)

### Relay checkout

Bare repos cannot merge (no working tree), so `mike` keeps an r/w checkout
per repo at `work/relay/<repo>/`, owned `mike:mike` mode `700`, outside the
`devgroup`-readable surface.

### Periodic relay

A systemd user timer under `mike` runs `scripts/relay-pr-branches.sh` (to be
added). The script, for each worker clone under `work/dev*/github/*/*/`:

1. Scans `refs/heads/pr/*` in the worker clone.
2. For each `pr/<slug>`:
   - Fetches into the relay checkout under `refs/pr-queue/<worker>/pr/<slug>`.
   - Skips if already an ancestor of `dev` (idempotent re-run).
   - Merges with `--no-ff` into `dev` with message `Merge <worker>/pr/<slug>`.
   - On conflict: aborts the merge and logs. The worker must rebase.
3. After processing all workers for a repo, pushes `dev` to GitHub and to the
   bare. This makes `origin/dev` visible to workers on their next fetch.

Conflict policy: the relay never resolves conflicts. It reports and moves on.
The worker rebases on `origin/dev` and re-signals.

### Releases

Releases are manual and gate-reviewed. A separate `scripts/release.sh` (to be
added) run by `mike`:

1. In the relay checkout, verify `dev` is pushed and CI green on GitHub.
2. `git switch main && git merge --ff-only origin/main`
3. `git merge --no-ff dev -m "Release <version>"`
4. `git tag -a v<version> -m "..."`
5. Push `main` and tag to GitHub and to the bare.

The `dev → main` merge is the human review checkpoint. Anything on `dev`
that shouldn't ship gets reverted on `dev` before the release merge.

## Monitoring commands (mike)

### Does a worker have unrelayed commits?

```bash
WT=/disk1/github/softwarewrighter/devgroup/work/devXX/github/sw-embed/<repo>
git config --global --add safe.directory "$WT"     # one-time per worker repo

git -C "$WT" status --short                         # uncommitted
git -C "$WT" for-each-ref --format='%(refname:short)' refs/heads/pr/*
                                                    # branches signaling ready
git -C "$WT" log --oneline origin/dev..pr/<slug>    # commits pending relay
```

### Is the bare repo ahead of GitHub?

```bash
BARE=/disk1/github/softwarewrighter/devgroup/work/bare/sw-embed/<repo>.git
git -C "$BARE" fetch origin
git -C "$BARE" rev-list --left-right --count dev...origin/dev
# output: "<bare-ahead>\t<github-ahead>"
git -C "$BARE" log --oneline origin/dev..dev       # commits bare has, GitHub doesn't
```

### Stale `pr/*` branches

Branches that were relayed long ago but never reaped by the worker:

```bash
for wt in /disk1/github/softwarewrighter/devgroup/work/dev*/github/*/*/; do
  git -C "$wt" for-each-ref \
    --format='%(committerdate:iso) %(refname:short)' refs/heads/pr/*
done
```

## One-time bootstrap

Before this workflow is usable, each bare repo (and its GitHub origin) needs
a `dev` branch:

```bash
BARE=/disk1/github/softwarewrighter/devgroup/work/bare/sw-embed/<repo>.git
git -C "$BARE" update-ref refs/heads/dev refs/heads/main
git -C "$BARE" push origin dev
```

Workers then pick it up on their next `git fetch origin`.

`mike`'s global gitconfig should pre-declare `safe.directory` entries for all
worker repos so scripts don't trip on "dubious ownership":

```bash
for wt in /disk1/github/softwarewrighter/devgroup/work/dev*/github/*/*/; do
  git config --global --add safe.directory "${wt%/}"
done
```

## Why this design

- **Zero-protocol signalling.** The ref name is the entire API. No JSON, no
  helper binary required.
- **Idempotent relay.** Already-merged branches are skipped by the ancestor
  check, so the timer can run as often as useful.
- **Isolation preserved.** Workers remain read-only outside their own tree.
  The coordinator reaches into worker clones; worker processes never reach
  out.
- **Conflict responsibility lands on the worker.** The relay aborts cleanly;
  the worker rebases. `mike` never has to understand another agent's code.
- **Human review gate at `dev → main`.** Everything accumulates on `dev`
  automatically; nothing ships to `main` without `mike` deciding.

## Watch-outs

- Workers must base on `origin/dev`, not `origin/main`. Document this in each
  repo's `AGENTS.md`.
- Branch renames are local-only. Since workers never push, a `pr/*` ref only
  exists in the worker's clone. The relay scans worker clones directly.
- `safe.directory` must be set in `mike`'s global gitconfig for every worker
  repo path, or cross-user git operations fail.
- `work/relay/` must be `mike:mike` mode `700`. If it lands under a
  `devgroup`-readable parent, `mike`'s in-progress state leaks to workers.
- GitHub's `dev` branch needs creating once. See bootstrap above.
- Abandoned `pr/*` branches are a worker-side housekeeping issue. The relay
  doesn't delete them; workers do, via `pr-reap`.
- Force-pushes to `dev` are not part of this workflow. If the relay ever
  needs to rewrite `dev` history, workers will see divergence on next fetch
  and must reset their local `dev` manually.
