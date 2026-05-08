# Brief: bootstrap PL/SW regression goldens + small build polish

**Owner:** dcpls
**Branch:** `pr/bootstrap-goldens`
**Repo:** `sw-cor24-plsw`

## Context

The toolchain chain is now complete and verified:
- `tc24r` (May 7) — array-size-expressions + string-literal-concatenation fixes shipped
- `cor24-asm`, `cor24-emu`, `cor24-dbg`, `link24`, `meta-gen` — all on PATH
- `pl-sw` wrapper at `/disk1/.../work/bin/pl-sw`
- `/disk1/.../work/lib/cor24/plsw.lgo` (2.3 MB) — compiled compiler image installed

You verified end-to-end on your side: `pl-sw -V` resolves, `tc24r src/main.c` parses cleanly (the two old blockers are gone). The harness you shipped in `pr/plsw-test-harness` was scaffolding-only because the compiler couldn't build; that's now resolved.

## Goal

`just test` becomes a **real regression gate** — it runs all 10 reg-rs cases and they all pass against committed goldens. From this saga forward, any drift in compiler output will show up as a test failure.

## What to do

1. **Trivial build polish** — fold in the `mkdir -p build` fix you noted: the `build:` justfile recipe should `mkdir -p build` before invoking tc24r, so a fresh clone doesn't fail with "cannot write build/plsw.s: No such file or directory".

2. **Build cleanly end-to-end:**
   ```
   git fetch origin && git switch dev && git merge --ff-only origin/dev
   just clean
   just build           # → build/plsw.s
   just build-lgo       # → build/plsw.lgo
   ```
   Confirm both files exist and the .lgo is non-trivial (>1MB).

3. **Bootstrap goldens for all 10 cases:**
   ```
   just test-bootstrap-goldens
   ```
   Should produce `reg-rs/plsw_<case>.rgt` and `reg-rs/plsw_<case>.out` for: hello, led, loop, record, define, select_demo, select_nested, macro, hello_macro, chain.

4. **Verify the gate works:**
   ```
   just test
   ```
   All 10 cases should pass — no longer "no tests matched". Confirm green output.

5. **Optional but valuable:** also run `just test-linker` (the `demo-fixup.sh` and `demo-plsw-modular.sh` linker tests). If `demo-fixup.sh` still produces garbled UART output (you noted this as a pre-existing blocker), capture the symptom precisely in `docs/testing.md` for follow-up. `demo-plsw-modular.sh` should now run end-to-end since `build/plsw.s` exists.

6. **Commit:**
   ```
   git add reg-rs/ justfile docs/testing.md
   git commit -m "test: bootstrap golden outputs for plsw regression suite + build polish"
   ```

## What goes in this PR

1. The `mkdir -p build` fix in justfile (or wherever fits cleanly).
2. 20 new committed files: `reg-rs/plsw_*.rgt` (test recipes) + `reg-rs/plsw_*.out` (expected outputs) — 10 of each.
3. Optional: docs/testing.md update if you capture linker-test status.
4. The single saga commit + saga closeout.

## What does NOT go in this PR

- No compiler logic changes — goldens are captured *as-is* from the current `plsw.lgo`. If you find bugs in the generated assembly, file a separate saga; don't fix mid-bootstrap.
- No fixes to the pre-existing `demo-fixup.sh` garbled-UART issue (out of scope; document only).
- No new test cases beyond the existing 10. Adding cases is a future saga.

## When done

Workflow: `dg-new-feature bootstrap-goldens` (creates `feat/bootstrap-goldens` from `dev`) → implement and verify → `dg-mark-pr` to rename to `pr/bootstrap-goldens` when ready. Signal mike. After relay:
- mike does NOT need to reinstall anything (no binary changes; goldens are data).
- `just test` is now a real CI-style gate going forward.
- dcsno can stop being gated on "is the PL/SW compiler stable?" — yes, it is, and there's now a regression suite proving it.

Promotion to main is mike's call, separately.
