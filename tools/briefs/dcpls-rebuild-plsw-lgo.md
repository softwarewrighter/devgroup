# Brief: rebuild and ship `plsw.lgo` with `.zero N` codegen

**Owner:** dcpls
**Branch:** `pr/rebuild-plsw-lgo` (or rolled into the same saga as a follow-up to `emit-zero-fill`)
**Repo:** `sw-cor24-plsw`
**Drafted by:** mike (2026-05-09).
**Depends on:** ✅ `dcpls-emit-zero-fill.md` — landed on `main` today (`c7e1262 feat(codegen): emit .zero N for all-zero static data`).

## Why this brief exists

The codegen change shipped today, but the installed compiler is stale:

```
work/lib/cor24/plsw.lgo   May  9 00:04   ← built before c7e1262 landed
work/bin/pl-sw            (shell wrapper, unaffected)
```

The wrapper just dispatches to `cor24-emu --lgo $TOOLROOT/lib/cor24/plsw.lgo`, so the active PL/SW compiler is whatever lgo lives there. Until that lgo is rebuilt, `pl-sw` still emits per-byte `.byte 0` for zero-init arrays — the very behavior `emit-zero-fill` was meant to remove.

## Direct downstream impact

dcsno's `snobol4` runtime work is gated by this. Per the original `dcpls-emit-zero-fill.md` analysis, `sno_main.s` is currently 261,638 bytes (97.7% enumerated zero-fill) against an `EMIT_BUF_SIZE` of 262,144 — a few more `INIT(0)` arrays in `snoglob.msw` and the buffer overflows. After the new compiler is on PATH, that file shrinks to roughly 7 KB and the overflow risk goes away.

## Goal

Replace `work/lib/cor24/plsw.lgo` with one built from `main` (post-`c7e1262`), so `pl-sw` invocations actually emit `.zero N`.

## What to do

1. Pull `main` in your sandbox: `cd $SRCROOT && git fetch origin && git switch main && git merge --ff-only origin/main`. Confirm `c7e1262` is in `git log`.
2. Build `plsw.lgo` via the existing `just`/scripts pipeline (the same path that produced the May 9 00:04 lgo). The build chain is `tc24r src/main.c → cor24-asm → plsw.lgo`; both tc24r and cor24-asm are on PATH.
3. Sanity-check the new compiler against a small `.plsw` source with an `INIT(0)` array — the produced `.s` should contain `.zero <N>` instead of long `.byte 0,0,0,...` runs.
4. Smoke-test self-bootstrap: use the new `plsw.lgo` to recompile `src/main.c` and confirm the result still works (drift sanity check). Not a hard gate, but worth doing.
5. Ship the artifact. Two acceptable shapes — pick whichever fits your existing build pipeline:
   - **Commit the rebuilt `build/plsw.lgo` in the repo** under your usual artifact path, signal a `pr/<slug>` whose diff is just that lgo plus any saga records. Mike picks it up on relay and copies to `work/lib/cor24/plsw.lgo`.
   - **Don't commit the lgo; instead document the rebuild command** clearly in CHANGES.md / saga records and signal `pr/<slug>` with just the docs. Mike then runs the same command from the relay clone and installs.

The first shape is preferred (matches the `dcpls-bootstrap-plsw-toolchain` precedent).

## What "mike installs" means concretely

There is no install script. After `dg-relay` + `dg-release` land the new lgo on `main`, mike runs (from the relay clone):

```bash
install -m 0640 \
  /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-plsw/build/plsw.lgo \
  /disk1/github/softwarewrighter/devgroup/work/lib/cor24/plsw.lgo
```

That single `install` is the entire deploy step — every lgo currently in `work/lib/cor24/` got there this way. After it runs, every d* user's `pl-sw` wrapper picks up the new compiler on the next invocation (the wrapper is a thin pass-through that resolves `lib/cor24/plsw.lgo` at run time).

## Verification mike will run after install

- `pl-sw < <(echo 'DCL X(64) BYTE INIT(0); ...')` produces a `.s` containing `.zero 64` (not 64 comma-separated zeros).
- `dcsno` can rebuild `snobol4.lgo` (separate brief) and the resulting `sno_main.s` is < 20 KB.

## Out of scope

- No source changes to `src/codegen.h` — `c7e1262` already shipped. This brief is purely the rebuild + redeliver step.
- No changes to the `pl-sw` wrapper script. It's correct as-is.
