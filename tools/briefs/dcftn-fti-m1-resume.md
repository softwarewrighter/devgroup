# Brief: resume FTI-0 milestone-1 with the new SNOBOL4 runtime

**Owner:** dcftn
**Branch:** continue on `feat/m1-resume` (already exists in your sandbox); `dg-mark-pr` when ready
**Repo:** `sw-cor24-fortran`
**Drafted by:** mike (forwarding dcsno's heads-up after `pr/nested-call-drops-gotos` shipped)

## What just changed in your toolchain

The SNOBOL4 nested-call bug you flagged — where
`IDENT(SUBSTR(X, 1, 1), 'X')` and similar inline forms dropped the
`:S(...) :F(...)` gotos — is **fixed**. dcsno shipped `pr/nested-call-drops-gotos`
(`fix: nested SUBSTR/SIZE/CHAR in builtin predicate args`,
`src/sno_lex.plsw` +193, `src/sno_exec.plsw` +38), and mike has
rebuilt and reinstalled the runtime:

- `work/lib/cor24/snobol4.lgo` is now 373,290 bytes (was 368,640 — built from `dev = main = 0890b5b`)
- `snobol4` wrapper on `$PATH` invokes the new image automatically
- Smoke test: `just hello` from the SNOBOL4 repo prints
  `Hello, World!` cleanly under the rebuilt image

## What this unblocks

You were carrying a workaround in `src/normalize.sno` (and possibly
elsewhere) where you split nested builtin predicate calls into
intermediate variables to avoid the dropped-goto bug, e.g.

```snobol4
*  ====  workaround we no longer need  ====
        FIRSTC = SUBSTR(LINE, 1, 1)
        IDENT(FIRSTC, 'C')                         :F(NEXT)
```

You can now write the natural inline form throughout `normalize.sno`
(and any other file in the saga touched by the workaround):

```snobol4
*  ====  natural form, now works  ====
        IDENT(SUBSTR(LINE, 1, 1), 'C')             :F(NEXT)
```

## What this saga is

Resume the work-in-progress on `feat/m1-resume` and finish FTI-0
milestone-1 per `docs/plan.md` (or whatever your saga plan calls it
internally). Concretely:

1. **Audit `feat/m1-resume` for the workaround pattern** — every place
   you broke a nested builtin call into intermediate variables only
   because of the bug. Inline them. Run `git grep -n 'FIRSTC\|TMP_\|WK_'`
   (or whatever naming convention you used) to spot the candidates;
   compare against `git log -p` on dcsno's `pr/nested-call-drops-gotos`
   to confirm which exact patterns the fix covers.

2. **Run the existing m1 test fixtures.** Whatever tests you had in
   place before the snobol4 block should now pass without the
   workaround. If any still fail, narrow the smallest reproducer and
   either (a) inline-fix it if it's a normalize.sno issue, or (b) draft
   a precise follow-up brief back to dcsno (or dcemu, or dcpls
   depending on root cause).

3. **Continue the original m1 scope** — any FTI-0 statements beyond
   PRINT that you'd intended for milestone-1. Stay within m1 scope per
   your plan; if scope creeps, split into a separate saga.

4. **Bump `examples/hello.lgo` if your compiler output changes.** Path A
   (hand-written `hello.s`) is what's on `main` today; if your m1 work
   produces a real compiler-emitted `hello.lgo`, swap it in and update
   `scripts/fortran` accordingly. dwftn's web demo embeds whatever
   `hello.lgo` is committed in your `examples/` dir, so a swap there
   propagates after promotion.

## What does NOT belong in this saga

- m2 / m3 work (later milestones).
- Refactoring `scripts/fortran` beyond what the m1 scope demands.
- Changes to `snobol4.lgo` itself or the SNOBOL4 sources (that's dcsno).
- Web frontend work (that's dwftn).
- Re-baselining `examples/hello.s` unless your m1 work legitimately
  produces a different (still-correct) output for hello.f.

## Verification

After the saga lands:

```bash
# the canonical end-to-end pipeline still works:
fortran examples/hello.f > /tmp/hello.s
cor24-asm /tmp/hello.s -o /tmp/hello.lgo
cor24-emu --lgo /tmp/hello.lgo --terminal --speed 0
# expect: "Hello, World!" + newline
```

Plus your m1 test fixtures all green. If you run a regression suite,
include it in the saga summary so mike can confirm the claim.

## When done

Workflow: continue on `feat/m1-resume` → finish edits + tests →
`dg-mark-pr` (rename to `pr/m1-resume`) → signal mike. Mike relays
into `dev`; promotion to `main` is a separate, on-demand step.

If your m1 work produces compiler artifacts that should ship to
`work/lib/cor24/` (e.g. a real `fortran-compiler.sno` or bundled
`.lgo`), call that out in the saga summary so mike knows to install
post-relay.

## After this saga

Subsequent FTI-0 milestones (m2 control flow, m3 I/O, etc.) continue
on fresh feat branches. The demo stays live throughout; if your
compiler outpaces hand-written `hello.s`, dwftn can swap in dynamic
compilation in a future saga, but that's out of scope here.
