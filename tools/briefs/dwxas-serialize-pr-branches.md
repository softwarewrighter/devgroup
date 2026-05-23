# Brief: serialize `pr/*` discipline — one in flight at a time

**Owner:** dwxas
**Branch:** `pr/serialize-pr-discipline`
**Repo:** `web-sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-18)

## Why this brief exists

On 2026-05-18 you had three parallel `pr/*` branches in flight
(`pr/hide-test-device-sliders`, `pr/rtc-auto-tick-and-run-shortcut`,
`pr/i2c-tmp101-oled-demo`), all branched from the same `origin/dev`
tip. Relaying produced two rounds of rebase-and-rebuild churn:

- Round 1: ship #1; #2 and #3 both conflicted in `src/main.rs`.
- Round 2: rebase #2 + #3 onto new dev; ship #2; #3 still conflicted
  in `pages/index.html` + the renamed `pages/*.wasm` / `*.js` artifacts.
- Round 3: rebase #3 again + rebuild `pages/`; finally shipped.

Three relay rounds to ship work that should have taken one each.

The root cause is structural to this repo: **`pages/` contains
per-build hash-named artifacts that ALL dwxas PRs touch**. Every
`pr/*` rewrites `pages/index.html` (referencing its own wasm hash)
and renames the .js/.wasm files. There is no merge-resolution for
two different wasm bundles — only "rebuild from current source." So
whichever PR ships second always conflicts on `pages/`, regardless
of source-level overlap.

Other agents in this devgroup (dcxas, dcemu) don't hit this because
their builds don't commit hash-named artifacts. This is a web-side
discipline issue.

## The rule

**Only one `pr/*` branch in flight at a time** in this repo.
Concretely:

1. Complete saga step N. Commit. Rename `feat/<slug>` → `pr/<slug>`
   via `dg-mark-pr`.
2. **STOP.** Do not start step N+1. Wait for the mike → relay →
   promote → next-session-fetch → `dg-reap` cycle.
3. After `dg-reap` deletes your local `pr/<slug>` and fast-forwards
   `dev`, *then* `git switch -c feat/<slug-of-step-N+1> origin/dev`
   and proceed.

Yes, this means your loop has idle time between sagas. That's fine —
agent loops are event-driven, not throughput-driven. The wait is
free; the rebase-and-rebuild cycle when you skip the wait is not.

## The exception: stacked branches

When step N+1's work logically depends on or extends step N
(e.g., "add Y feature on top of X infrastructure I'm landing now"),
branch step N+1 off **`pr/<slug-of-step-N>`**, not off `dev`. Then
after mike relays N, your step-N+1 branch is either:

- already a strict superset of dev (relay is a fast-forward), or
- has only its own changes vs new dev (clean merge, no `pages/`
  conflict because your rebuild was on top of N's pages/).

This is *not* the same as having two independent `pr/*` in flight —
the dependent branch's name is still `feat/*` until step N has
shipped and you've reaped. Only one `pr/*` exists in your clone at
any moment.

## What to update in your repo

`AGENTS.md` currently has a workflow section but doesn't call out
this discipline. Add a subsection — name it whatever fits, suggest
"## PR serialization" or "## One `pr/*` at a time" — under your
existing workflow guidance. Body roughly:

> Because `pages/` contains per-build hash-named artifacts that
> every PR rewrites, parallel `pr/*` branches in this repo always
> conflict on `pages/` and force a rebase-and-rebuild cycle. To
> avoid this churn:
>
> - Only one `pr/*` in flight at a time. After signaling `pr/<slug>`,
>   stop and wait for the relay → promote → reap cycle before
>   starting the next step's work.
> - For sequential work, branch the next step off `pr/<slug>`
>   (not off `dev`). After `pr/<slug>` relays, your dependent
>   branch's relay is a clean merge.
> - Reap (`dg-reap`) is the signal that you're free to start the
>   next step.

`CLAUDE.md` is a symlink to `AGENTS.md` per the existing convention,
so no separate edit there.

Optionally, also update `.agentrail/plan.md` (or wherever your saga
queue's "next step seed" guidance lives) so future-step prompts
include "this step starts after `dg-reap` of the prior step" rather
than just "branch from origin/dev."

## Acceptance

- One commit, one PR (`pr/serialize-pr-discipline`).
- `AGENTS.md` gains the discipline section.
- No code changes; pure docs.
- `pages/` MUST be rebuilt as part of the commit (because every
  dwxas PR rewrites `pages/` — yes, this brief is itself subject
  to the discipline it documents).
- Cross-link to this brief if you want a "see also" pointer; otherwise
  let the AGENTS.md section stand on its own.

## Out of scope

- **Don't move `pages/` out of source control.** That's the durable
  fix (have GH Actions build pages/ on push to main, standard Yew+Trunk
  pattern) but it's a much bigger change with deploy-workflow risk.
  Separate brief if and when you want to chase it.
- **Don't add tooling enforcement** (e.g., `dg-mark-pr` refusing to
  rename when another `pr/*` exists). That's a devgroup-level tooling
  change mike's territory. If/when mike updates `dg-mark-pr`, this
  brief becomes belt-and-suspenders to a hard check.
- **Don't audit prior sagas** to retroactively classify them as
  "should have been stacked." The discipline applies from now
  forward.

## Cross-repo coordination

This discipline is **web-repo specific**. dcxas and dcemu don't need
to adopt it — their parallel PRs don't conflict because they don't
commit hash-named build artifacts. If you ever start contributing
to a non-web repo from your sibling clones, the rule there is the
ordinary "respect what's in flight" without the strict
serialization.

## Workflow

```bash
cd /disk1/.../work/dwxas/github/sw-embed/web-sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/serialize-pr-discipline
$EDITOR AGENTS.md   # add the serialization section
./scripts/build-pages.sh
git add AGENTS.md pages/
git commit -m "docs(agents): document the one-pr/*-at-a-time discipline"
git branch -m feat/serialize-pr-discipline pr/serialize-pr-discipline
```

Then signal as usual. **This brief itself is the test case** —
after you signal `pr/serialize-pr-discipline`, the rule kicks in:
no new `pr/*` until I've relayed + promoted this one and you've
reaped.
