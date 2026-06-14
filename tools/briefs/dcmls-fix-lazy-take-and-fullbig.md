# Brief: fix lazy-take hang and fullbig early-halt in macrolisp

**Owner:** dcmls
**Branch:** `feat/fix-lazy-take-fullbig` → `pr/fix-lazy-take-fullbig`
**Repo:** `sw-cor24-macrolisp`
**Drafted by:** mike (coordinator)
**Not part of** the cor24-rs retirement epic — these reproduce under **both** the
old and new toolchains, so they are pre-existing language/prelude bugs, not
migration fallout. Surfaced by dcmls during `dcmls-migrate-toolchain` and kept out
of that PR (correctly).

## Symptoms

1. **`demos/lazy.l24` (lazy-take) hangs** — runs without terminating. Reproduces
   on old (`cor24-run`) and new (`cor24-asm` + `cor24-emu`) toolchains, so it is
   not a codegen/assembler artifact.
2. **`build-fullbig` / `eval-fullbig` halts after 2 instructions** — the program
   stops almost immediately instead of running. Likely a load/entry/prelude-init
   problem specific to the "fullbig" configuration.

dcmls has additional detail saved in its agent memory — start there.

## Prime suspect (bisect hint)

The recent **tail-recursive prelude rewrites** are the likely cause of the
lazy-take hang (a rewritten `take`/lazy interaction). Candidate commits in
`prelude-full`, newest first:

- `f6459e4` chore(main.c): sync test-scaffold prelude with tail-recursive forms
- `c994a15` perf(prelude-full): tail-recursive flatten
- `f3005ee` perf(prelude-full): tail-recursive zip
- `44379dc` perf(prelude-full): tail-recursive **take** ← most likely for lazy-take
- `cd2851c` perf(prelude-full): tail-recursive repeat
- `ef50c70` perf(prelude-full): tail-recursive range

`git bisect` `demos/lazy.l24` across this range (run it under `lisp24 -u` or
`cor24-emu` with a bounded `-n` so a hang shows as "no output before limit").
The `fullbig` early-halt is probably a separate root cause — triage independently.

## What to do

1. Bisect / root-cause each symptom (they may be unrelated).
2. Fix in the prelude / REPL C source, keeping the tail-recursive perf win if
   possible (don't just revert the optimization unless that's the only safe fix —
   raise with mike if so).
3. Add a regression test: `demos/lazy.l24` must terminate with correct output
   under a bounded instruction budget; `fullbig` must run past init. Wire into
   `scripts/run-tests.sh` so it can't silently regress again.

## Verification

- `demos/lazy.l24` terminates with expected output; `fullbig` runs.
- `scripts/run-tests.sh` green (using the migrated `cor24-asm`/`cor24-emu` path).

## When done

Push `pr/fix-lazy-take-fullbig`, notify mike. Mike relays via
`dg-relay dcmls sw-cor24-macrolisp pr/fix-lazy-take-fullbig`.

## Downstream note

The macrolisp prelude feeds the web REPL (dwmls). A prelude fix here may shift the
generated asm / snapshot, so sequence any dwmls re-baseline **after** this lands
to avoid re-baselining onto a buggy prelude.
