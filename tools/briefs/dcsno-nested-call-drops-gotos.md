# Brief: SNOBOL4 silently drops gotos when statement has a nested function-call argument

**Owner:** dcsno
**Branch:** `pr/nested-call-drops-gotos`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-1-fortran-compiler` saga (2026-05-08), in the wake of the `pr/combined-goto-parser` fix.
**Affects:** any SNOBOL4 statement of the shape

```
        PRED(F(...), Y)    :S(L) | :F(L) | :(L)
        PRED(X, F(...))    :S(L) | :F(L) | :(L)
        PRED(F(...), G(..)):S(L) | :F(L) | :(L)
```

i.e. any predicate (IDENT, DIFFER, EQ, GT, etc.) whose argument list contains a nested function call (SUBSTR, SIZE, CHAR, etc.). All goto types -- `:S`, `:F`, and unconditional `:(LBL)` -- are silently dropped from such statements; the statement falls through regardless of the goto specified.

This is a structural sibling of the combined-goto bug just fixed in `dcsno-combined-goto-parser.md`, but the manifestation is different and the repros need to be checked against this build.

## Symptom

When a SNOBOL4 statement evaluates a predicate (or function call) whose arguments include another function call, **the parser drops the goto on that statement**. The predicate evaluates and updates the success/failure flag correctly -- single-form gotos do still consult the flag in the dispatcher -- but no branch is ever taken because the goto target was never wired up.

The bug is silent: no warning, no error, no diagnostic. The statement just falls through into the textually-next statement, producing programs that "almost work" but mis-route on every line where they were supposed to branch.

## Repro (no-fallthrough regression)

All four cases below run against the canonical `snobol4` wrapper on the deployed build (`snobol4.lgo` timestamp 2026-05-08 19:09:06).

### Case 1: predicate that should SUCCEED, `:S` should fire

```
$ cat /tmp/c1.sno
        IDENT(SUBSTR('CAB', 1, 1), 'C')         :S(MATCH)
        OUTPUT = 'fall through -- :S was DROPPED'   :(EX)
MATCH   OUTPUT = ':S fired (correct)'               :(EX)
EX      END

$ snobol4 --load-binary /tmp/c1.sno@0x080000 --entry 0 \
          --quiet --speed 0 -n 10000000 -t 30
fall through -- :S was DROPPED
```

Expected `:S fired (correct)` because `SUBSTR('CAB',1,1) = 'C'` and `IDENT('C','C')` succeeds.

### Case 2: predicate that should FAIL, `:F` should fire

```
$ cat /tmp/c2.sno
        IDENT(SUBSTR('XYZ', 1, 1), 'C')         :F(FAIL)
        OUTPUT = 'fall through -- :F was DROPPED'   :(EX)
FAIL    OUTPUT = ':F fired (correct)'               :(EX)
EX      END

$ snobol4 --load-binary /tmp/c2.sno@0x080000 --entry 0 \
          --quiet --speed 0 -n 10000000 -t 30
fall through -- :F was DROPPED
```

Expected `:F fired (correct)` because `SUBSTR('XYZ',1,1) = 'X'` and `IDENT('X','C')` fails.

### Case 3: unconditional `:(LBL)` -- also dropped

```
$ cat /tmp/c3.sno
        IDENT(SUBSTR('CAB', 1, 1), 'C')         :(JUMP)
        OUTPUT = 'fall through -- :(LBL) was DROPPED' :(EX)
JUMP    OUTPUT = ':(LBL) fired (correct)'             :(EX)
EX      END

$ snobol4 --load-binary /tmp/c3.sno@0x080000 --entry 0 \
          --quiet --speed 0 -n 10000000 -t 30
fall through -- :(LBL) was DROPPED
```

Expected `:(LBL) fired (correct)` because unconditional gotos always transfer.

### Case 4: same-shape statement WITHOUT a nested call -- works correctly (control)

```
$ cat /tmp/c4.sno
        A = SUBSTR('CAB', 1, 1)
        IDENT(A, 'C')                           :S(MATCH)
        OUTPUT = 'fall through'                  :(EX)
MATCH   OUTPUT = ':S fired (correct)'            :(EX)
EX      END

$ snobol4 --load-binary /tmp/c4.sno@0x080000 --entry 0 \
          --quiet --speed 0 -n 10000000 -t 30
:S fired (correct)
```

Identical predicate, same goto, but the SUBSTR result is extracted into a variable first. With no nested call in the statement's arg list, the goto is properly attached and fires.

### Scope of the bug (smoke-tested against the same build)

| Statement | Goto | Expected | Observed |
|---|---|---|---|
| `IDENT(SUBSTR(s,p,l), Y)` | `:S(L)` | fire on `IDENT(F,Y)` success | dropped |
| `IDENT(X, SUBSTR(s,p,l))` | `:S(L)` | fire on success | dropped |
| `EQ(SIZE(s), n)`          | `:S(L)` | fire on success | dropped |
| `DIFFER(SUBSTR(s,p,l), Y)` | `:F(L)` | fire on failure | dropped |
| `IDENT(SUBSTR(s,p,l), Y)` | `:(L)` (unconditional) | always fire | dropped |
| `A = SUBSTR(s,p,l); IDENT(A, Y)` | `:S(L)` | fire on success | **fires (correct)** |

So the bug is gated on the presence of a nested function call in the statement's argument list. Extract the nested call to a local variable and the goto attaches normally.

## Hypothesised root cause

This looks like the inner function call's parsing or expression-stack handling is consuming the trailing colon and goto target tokens before the outer statement's TK_COLON handler runs. The lexer/parser may be over-greedy when parsing the inner call's argument list, walking past the closing `)` of the outer call and into the goto.

A second possibility: the codegen or AM lowering path for "predicate-with-nested-call" statements emits the predicate evaluation correctly but emits no branch dispatch. The success/failure flag is updated; nothing consults it because no branch op was emitted. (This would be analogous to the `S_GTYP(S)` single-slot bug fixed in `pr/combined-goto-parser` -- maybe nested-call statement types route through a code path that didn't get the same treatment.)

Worth grepping `src/sno_lex.plsw` and `src/sno_exec.plsw` for the statement-type routing of `ST_FN_CALL` (or whichever ST_* applies) and confirming the goto attachment / dispatch is wired up uniformly across statement types.

## Tests to add

1. **Case 1** above -- `:S` on nested-arg predicate, success path.
2. **Case 2** above -- `:F` on nested-arg predicate, failure path.
3. **Case 3** above -- `:(LBL)` on nested-arg statement.
4. **Combined `:S():F()` on nested-arg** -- compose with the just-shipped combined-goto fix.
5. **Pattern-match form with nested call** -- `STR pattern :S(L)` where `pattern` involves a function call, e.g. `'foo' BREAK(SUBSTR(',',1,1))`. Confirm gotos still attach.
6. **Control: variable subject** -- the extract-first form (Case 4) should remain green.

## Workaround (until the fix lands)

Extract every nested function-call result to a local variable first, then use the variable as the predicate argument. Verbose but works:

```
        X = SUBSTR(L, 1, 1)
        IDENT(X, 'C')                           :S(SKIP)
```

instead of

```
        IDENT(SUBSTR(L, 1, 1), 'C')             :S(SKIP)   ; goto dropped
```

dcftn's draft `snobol4/src/normalize.sno` was written in the inline-nested form throughout (col-1 char extraction, rstrip/lstrip loops, blank-line check, classify dispatch -- all use `IDENT(SUBSTR(...), ...)` or `EQ(SIZE(...), ...)`). The workaround would mean rewriting most of normalize.sno around explicit temp variables -- doable but defeats the dialect's natural idiom and is exactly the kind of friction that motivates the brief.

## What does NOT go in this PR

- No changes to single-form gotos with non-nested args -- they work.
- No changes to predicate evaluation (the success/failure flag is being computed correctly; just not consulted because the branch dispatch is missing).
- No changes to the combined-goto wiring just shipped in `pr/combined-goto-parser`.

## When done

Push `pr/nested-call-drops-gotos`. After mike relays + reinstalls the snobol4 binary:

- dcftn's `feat/m1-resume` saga can use the natural inline form `IDENT(SUBSTR(...), ...) :S(...)` throughout normalize / classify / expr / lower / emit_plsw, without preemptive extraction-to-local-variables.
- Any future SNOBOL4 program in any other repo using the documented nested-call form works as written.

## Why this surfaced now

The combined-goto fix (`pr/combined-goto-parser`, verified green at 19:09) re-enabled dcftn's testing pipeline for `normalize.sno`. With combined gotos working, the next layer of bug surfaced: even single gotos on nested-call statements are dropped. The combined-goto fix was necessary but not sufficient for the dialect to match its own documented surface.
