# Brief: capture historical briefs into `docs/briefs/`

**Owner:** dcxtc
**Branch:** `pr/briefs`
**Repo:** `sw-cor24-x-tinyc`
**Drafted by:** mike (2026-05-17)

## What's happening

The coordinator-side `tools/briefs/` in the devgroup workstation has
accumulated 8 brief(s) addressed to you. Until now they've lived
only in mike's tree, which means they vanish from your repo's history
and are invisible to anyone reading just the repo on GitHub.

Going forward, the relay cycle will mirror each new brief into
`docs/briefs/` automatically. This brief captures the existing
backlog so the in-repo record starts complete.

## What you'll find

8 new untracked file(s) under `docs/briefs/`, named
`<N>-dcxtc-<slug>.md`:

- `N` is the chronological order (by file mtime) the brief was drafted.
- The original filename is preserved (with the `dcxtc-` prefix kept so the file is self-identifying when read out of context).
- Content is a verbatim copy of the brief mike sent you; no
  re-editing.

```bash
ls docs/briefs/
```

## What to do

```bash
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/briefs
git add docs/briefs/                  # ONLY this path — don't -A
git status                            # confirm only docs/briefs/ is staged
git commit -m "docs(briefs): capture historical briefs (8 files)"
git branch -m feat/briefs pr/briefs
```

Then `dg-mark-pr` (or just leave the `pr/` name); mike relays as usual.

## Acceptance

- All 8 files committed under `docs/briefs/`.
- No other paths touched in the commit (your working-tree state for
  unrelated WIP stays untracked; don't fold it in).
- No content changes to the brief files (this is a verbatim capture,
  not a re-edit). If you find errors in the historical record, file
  a separate follow-up brief — don't silently revise the captures.

## Out of scope

- **Don't reorder, rename, or split files.** The N prefix encodes
  chronology; mike's tooling assumes it's stable across mirrors.
- **Don't add new briefs to this PR.** Going-forward briefs land via
  the relay-cycle mirror, not this capture PR. Keep this one clean.
- **Don't edit content.** If a historical brief is wrong, file a
  correction brief separately; preserve the original capture as a
  historical record.
