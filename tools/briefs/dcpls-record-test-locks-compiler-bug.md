# Brief: `plsw_record` regression test locks in a compiler bug

**Owner:** dcpls
**Branch:** start as `feat/fix-record-test`; `dg-mark-pr` when ready
**Repo:** `sw-cor24-plsw`
**Drafted by:** mike (regression-suite audit, 2026-05-09)

## What's wrong

The `examples/record.plsw` source file is described in its own header comment as a positive demo:

> `* record.plsw -- Record and Pointer Demo for PL/SW`
> `* Declares a multi-level record, fills fields, takes address,`
> `* accesses fields via pointer dereference.`
> `* Demonstrates: level-based DCL, record field access, ADDR(),`
> `* pointer dereference (P->field), arithmetic on fields. *`

But its golden trio in `reg-rs/` captures a *compilation failure* as the expected outcome:

```
$ cat reg-rs/plsw_record.rgt
command = "./tests/driver.sh record"
exit_code = 1

$ cat reg-rs/plsw_record.err
Compilation failed:
CODEGEN ERROR: ADDR requires a variable, field, or array element
--- compilation failed ---

$ cat reg-rs/plsw_record.out
(empty)
```

The test now asserts: *"the PL/SW compiler will continue to emit `CODEGEN ERROR: ADDR requires a variable, field, or array element` forever."* Any future fix to the codegen (which is what `record.plsw` is *demonstrating*) will fail this regression test, meaning the regression suite is actively *opposing* the stated goal of the demo.

This is the pattern the user flagged on 2026-05-09: agents capturing reg-rs goldens by blindly running the harness against the current repo state, including failures, instead of verifying the goldens reflect intended behavior.

## Two acceptable resolutions

### A. Fix the codegen (preferred)

`record.plsw` is supposed to work — the comment says so. The `ADDR requires a variable, field, or array element` error sounds like a codegen bug where `ADDR(record_field)` isn't being treated as one of the valid kinds. Investigate:

- `src/codegen.h` (or wherever the ADDR handler lives)
- The error string `"ADDR requires a variable, field, or array element"`
- Why a record field doesn't satisfy the predicate

Fix the codegen so `record.plsw` compiles and runs, then re-bootstrap the goldens with the working output.

### B. Mark as known-failure (acceptable as a stopgap)

If fixing the codegen is a separate, larger saga: rename the test to make the gap explicit, e.g.:

- Rename `examples/record.plsw` → `examples/record.plsw.broken` (or move under `examples/known-failures/`)
- Remove `reg-rs/plsw_record.{rgt,err,out}` so it isn't part of `just test`
- Add a `docs/known-failures.md` entry explaining: "record.plsw demonstrates ADDR on record fields; codegen currently rejects this with `ADDR requires a variable, field, or array element`. Fix is tracked in saga `pr/<followup>`."

This way `just test` doesn't lie about success/failure, the broken demo doesn't masquerade as a passing test, and the future codegen fix isn't fighting an opposing regression.

## What does NOT belong in this saga

- Bootstrapping more goldens (only the record case is suspicious; the storage_* tests are well-formed positive tests of error-detection paths and shouldn't be touched).
- Refactoring the test harness.
- Compiler features beyond what's needed to compile `record.plsw`.

## Verification

Whichever path you pick, after the saga lands:

- Either `just test` includes a green `plsw_record` that asserts correct output (path A), or
- `just test` no longer includes `plsw_record` and a clear known-failure doc references it (path B).

Don't ship a state where `just test` passes by asserting a known compiler bug as expected behavior.
