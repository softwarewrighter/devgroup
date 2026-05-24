# Moving dwxtc work from queenbee to large12

This doc captures the migration playbook used 2026-05-23 when dwxtc work was
moved off queenbee (the night-only rack server) and onto large12 (the 24/7
climate-controlled workstation).

The general pattern applies to any d* user move; substitute the user and repo
list accordingly.

## Why this can't be a simple rsync

`d*` accounts are unprivileged: they have no GitHub credentials and cannot
push. The only push path is via mike running `dg-relay` and `dg-release` from
the local bare mirrors. So "moving in-flight work" is *not* a filesystem
operation — it has to land on GitHub first, then be re-cloned on the target
host. The mechanism is the same `pr/<topic>` workflow used for normal PRs.

## Phase 1 — wrap up WIP on queenbee (as the agent in the d* sandbox)

For each clone with uncommitted changes, the agent commits and renames to a
`pr/` branch:

```bash
# inside the d* user's sandbox
cd "$SRCROOT/<repo>"
git switch -c feat/<topic>     # if not already on feat/ or pr/
git add -A
git commit -m "feat: ..."      # one or more commits as appropriate
dg-mark-pr                     # renames feat/<topic> -> pr/<topic>
```

If the WIP isn't ready to land on `dev`, commit it anyway as a snapshot — the
relay will land it on `dev` (since `dg-relay` always merges pr/* into dev). If
that's not acceptable, *don't* relay; instead, the work has to be pushed
manually to a non-`dev` branch on GitHub by mike (see "escape hatch" below).

After all sandboxes have their `pr/<topic>` branches, mike sees them with
`dg-list-pr`.

## Phase 2 — relay and release (as mike on queenbee)

For each pending `pr/<topic>`:

```bash
dg-relay <user> <repo> pr/<topic>      # merges into dev, pushes to GitHub
dg-release <repo>                       # optional: dev -> main
```

After relay, the d* sandbox should be re-synced (otherwise the next agent run
will see a stale `pr/<topic>` and possibly the same SHA on `dev`):

```bash
sudo -u <user> bash -lc 'cd $SRCROOT && dg-reap'
```

`dg-reap` fetches, fast-forwards `dev`, and deletes any local `pr/*` that are
ancestors of `origin/dev`.

## Phase 3 — bring up large12

Run `setup-devgroup-accounts.sh` on large12 after installing prereqs (see
`docs/usage.md`). This:

- creates the `devgroup` group and every d* user from `dev-users.tsv`
- builds the sandbox tree under `/disk1/github/softwarewrighter/devgroup/work/`
- clones each user's `primary_repo` from the local bare mirror, which now
  contains the work that was just relayed

There's a chicken-and-egg: `sync-bare-repos.sh` needs `work/bare/` to exist
(created by setup) and refuses to run under sudo (it requires `mike`).
`install-shared-rust.sh` hard-codes `-g devgroup` so it needs the group to
exist first. So the bring-up runs setup twice with sync in between:

```bash
# on large12, as mike — first pass creates devgroup, users, sandboxes;
# warns "no bare mirror for ..." for every repo (expected on fresh box).
cd /disk1/github/softwarewrighter/devgroup    # cwd dev users can traverse
sudo /disk1/github/softwarewrighter/devgroup/scripts/setup-devgroup-accounts.sh \
  /disk1/github/softwarewrighter/devgroup/scripts/dev-users.tsv

# shared Rust toolchain — devgroup now exists so this works
sudo /disk1/github/softwarewrighter/devgroup/scripts/install-shared-rust.sh

# mirror every non-archived repo in the org (runs as mike, NOT sudo)
/disk1/github/softwarewrighter/devgroup/scripts/sync-bare-repos.sh

# second pass clones each user's primary repo from the now-populated mirrors
cd /disk1/github/softwarewrighter/devgroup
sudo /disk1/github/softwarewrighter/devgroup/scripts/setup-devgroup-accounts.sh \
  /disk1/github/softwarewrighter/devgroup/scripts/dev-users.tsv
```

For d* users like dwxtc that have *sibling* clones beyond the primary repo
(Cargo `path = "../..."` deps), reproduce those manually:

```bash
# on large12, as mike — for dwxtc
for r in sw-cor24-emulator sw-cor24-isa sw-cor24-x-assembler sw-cor24-x-tinyc; do
  sudo -u dwxtc git clone \
    /disk1/github/softwarewrighter/devgroup/work/bare/${r}.git \
    /disk1/github/softwarewrighter/devgroup/work/dwxtc/github/sw-embed/${r}
done
```

## Phase 3b — build shared toolchain

After the accounts and bare mirrors are in place, build all shared binaries
and library artifacts that d* users need on their PATH:

```bash
# as mike (NOT sudo)
./scripts/build-shared-toolchain.sh
```

This builds cor24-emu, cor24-asm, tc24r, pa24r, pvm24, pas24, plsw, and all
other tools into `work/bin/` and `work/lib/`. See the script for the full
list and prerequisites (just, python3 via pacman). Wrapper scripts (pas24,
pvm24, plsw) are tracked in git and already present after clone; the script
only rebuilds the compiled artifacts.

Prereqs (pacman): `just python base-devel`

The symlinked tools (agentrail, reg-rs, pjmai-rs, sw-checklist) require mike
to have them installed via `sw-install` first.

## Phase 4 — resume work on large12 (as the d* user)

```bash
ssh dwxtc@large12          # drops into tmux 'main' at $SRCROOT
git switch dev             # dev now has the i2c-ssd1306-oled work
dg-new-feature <next-topic>
```

## Verify both hosts match

Run on each host and diff the snapshot directories:

```bash
/disk1/github/softwarewrighter/devgroup/scripts/verify-devgroup.sh \
  --snapshot /tmp/dgsnap-$(hostname)
```

Then on either host:

```bash
diff -r /tmp/dgsnap-queenbee /tmp/dgsnap-large12
```

Expected differences after a clean migration: none in any snapshot file
except possibly the manifest-shape and bare-list ordering (deterministic),
and the rustc-version if toolchains drift. UIDs/GIDs are *allowed* to differ
across hosts (the script doesn't compare them — ACLs are name-based).

## Escape hatch — work that must not land on dev

If WIP truly cannot be merged into `dev` but must still cross hosts, the only
mechanism is mike pushing a non-`dev` branch directly from the bare to
GitHub. Cost: it bypasses the relay, so it doesn't go through merge review
and the d* sandbox isn't auto-cleaned by `dg-reap`. Use sparingly.

```bash
# as mike, after the agent commits WIP on wip/<topic> in their sandbox
BARE=/disk1/github/softwarewrighter/devgroup/work/bare/<repo>.git
WORKER=/disk1/github/softwarewrighter/devgroup/work/<user>/github/sw-embed/<repo>
git -C "$BARE" fetch "$WORKER" wip/<topic>:refs/heads/wip/<user>/<topic>
git -C "$BARE" -c remote.origin.mirror=false push origin \
  refs/heads/wip/<user>/<topic>:refs/heads/wip/<user>/<topic>
```

Then on large12 the d* user fetches and checks out `origin/wip/<user>/<topic>`.
