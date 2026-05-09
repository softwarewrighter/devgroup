# Brief: 4 OCaml lex tests lock in a Pascal-compilation failure

**Owner:** dcoca
**Branch:** start as `feat/fix-lex-tests`; `dg-mark-pr` when ready
**Repo:** `sw-cor24-ocaml`
**Drafted by:** mike (regression-suite audit, 2026-05-09)

## What's wrong

Four lexer tests in `work/reg-rs/` have the same shape: each `.out` is empty, each `.err` is a 14,960-byte block starting with `Pascal compilation failed:`, and each `.rgt` declares `exit_code = 1`. Affected:

| Test | exit_code | err first line | out |
|---|---|---|---|
| `lex_all` | 1 | `Pascal compilation failed:` | empty |
| `lex_arith` | 1 | `Pascal compilation failed:` | empty |
| `lex_fun` | 1 | `Pascal compilation failed:` | empty |
| `lex_let` | 1 | `Pascal compilation failed:` | empty |

All four assert: *"the Pascal compilation step continues to fail in this specific way."* They aren't testing the OCaml lexer at all — they're locking in the current state of an upstream Pascal-compilation breakage. Any future improvement to the upstream Pascal toolchain that fixes these compilation failures will *break these tests*, meaning the regression suite is preventing the upstream fix from being adopted.

This is the pattern the user flagged on 2026-05-09: agents capturing reg-rs goldens by blindly running the harness against the current repo state, including failures, instead of verifying the goldens reflect intended behavior.

## Investigation steps

1. **Read the four 14,960-byte `.err` files**. They're suspiciously identical-sized — likely they share the same prefix (the Pascal compiler dumping its error state). What's the actual error?
2. **Determine whether the lexer tests *should* be reachable**. The naming (`lex_all`, `lex_arith`, etc.) suggests these test the OCaml lexer in isolation, NOT the full compile pipeline. If yes, they shouldn't be invoking Pascal at all — the test setup is wrong.
3. **Or, if the tests legitimately exercise the full pipeline**, the upstream Pascal failure is a real blocker for the OCaml interpreter. Track the failure to its source (likely in `dcpas`/`dcpvm` territory) and either fix it or wait for a fix.

## Two acceptable resolutions

### A. Repoint the lex tests at a lexer-only harness

If the tests are about the OCaml lexer specifically, they shouldn't run through Pascal. Set up a smaller harness that exercises just `lex_*` paths and produces meaningful `.out` files. Capture those as the new goldens, with `exit_code = 0`.

### B. Mark as known-failure pending upstream fix

If the tests legitimately need the full pipeline:

- Rename or move under `work/reg-rs/known-failures/` (or similar)
- Add `docs/known-failures.md` entry explaining the upstream Pascal blocker
- Reference the saga in `dcpas` or `dcpvm` that's tracking the fix
- Remove the four `.rgt` entries from the active `just test` matrix until the upstream is fixed

Don't ship a state where `just test` passes by asserting a known upstream bug as expected behavior.

## What does NOT belong in this saga

- Fixing the upstream Pascal toolchain (that's `dcpas`/`dcpvm`'s territory; you'd file a precise saga back to them with the captured `.err` content as the bug repro).
- Adding new lexer tests beyond the current four.
- Refactoring the `work/reg-rs/` harness shape.

## Verification

After the saga lands, `just test` (or whatever your test driver is) should not be asserting `Pascal compilation failed:` as expected output. Either the lex tests genuinely test the lexer and produce meaningful output, or they're moved out of the active suite with a documented reason.
