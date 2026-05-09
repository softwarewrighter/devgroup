# Brief: SNOBOL4 parser drops second goto in combined `:S(...):F(...)` form

**Owner:** dcsno
**Branch:** `pr/combined-goto-parser`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-1-fortran-compiler` saga (2026-05-08).
**Affects:** any SNOBOL4 program that uses combined success/failure
transfer on a single statement, e.g.

```
        SUBJECT pattern    :S(MATCH):F(NOMATCH)
        IDENT(X, Y)        :S(SAME):F(DIFF)
```

This is part of the documented dialect surface
(`examples/pattern-tutorial.sno` documents the form
`SUBJECT  pattern... :S(label) :F(label)`) and is part of standard
SNOBOL4 (Bell Labs Macro Implementation, SPITBOL, Catspaw).
Programs that rely on it must be rewritten to use single-direction
gotos until the fix lands -- bug-prone and clutters the source.

## Symptom

When a statement specifies both `:S(...)` and `:F(...)` (with or
without a separating space, with or without a colon prefix on the
second goto), the parser attaches **only the first goto** and
silently drops the second.

The bug is parser-only -- predicate evaluation, the success/failure
flag, and goto dispatch all work correctly when given a properly-
attached goto. With single-direction gotos (`:S(LBL)` alone,
`:F(LBL)` alone), control flow is correct.

The bug is silent: no warning, no error, just lost branches.
Programs appear to "work" until they exercise the dropped goto, at
which point they fall through to the next statement (often into a
wrong-label block) and produce nonsense.

## Repro (no-fallthrough regression -- isolates parser from flow artifacts)

Earlier diagnosis attempts used label sequences that allowed silent
fallthrough into the success-branch's body, which masked parser-
bug behavior as "always-S-fires." The disambiguating form below
puts an explicit `:(EX)` between the predicate and the labelled
branches so neither label can be entered by fallthrough:

```
$ cat /tmp/goto.sno
        IDENT('A', 'B')                         :S(SOK):F(SBAD)
        OUTPUT = 'fallthrough -- no goto fired'  :(EX)
SOK     OUTPUT = 'S branch fired'                :(EX)
SBAD    OUTPUT = 'F branch fired'                :(EX)
EX      END

$ snobol4 --load-binary /tmp/goto.sno@0x080000 --entry 0 \
          --quiet --speed 0 -n 10000000 -t 30
fallthrough -- no goto fired
```

Standard SNOBOL4 says: `IDENT('A','B')` fails, `:F(SBAD)` fires,
output `F branch fired`. Observed: fallthrough -- neither goto
fired, so `:F(SBAD)` was never attached to the statement.

Inverse with a succeeding predicate works correctly:

```
$ cat /tmp/goto2.sno
        IDENT('A', 'A')                         :S(SOK):F(SBAD)
        OUTPUT = 'fallthrough -- no goto fired'  :(EX)
SOK     OUTPUT = 'S branch fired'                :(EX)
SBAD    OUTPUT = 'F branch fired'                :(EX)
EX      END

$ snobol4 --load-binary /tmp/goto2.sno@0x080000 --entry 0 \
          --quiet --speed 0 -n 10000000 -t 30
S branch fired
```

This proves `:S(SOK)` IS attached and works. Only the trailing
`:F(SBAD)` is dropped on the failing-predicate path.

## Root cause hint (per source read on 2026-05-08)

`src/sno_lex.plsw`:

- `S_GTYP(S)` is a single-valued field initialised at line 372:
  `S_GTYP(S) = GT_NONE;`. Type values seen in source:
  `GT_NONE`, `GT_SUCC`, `GT_FAIL`, `GT_UNCOND`, `GT_RET`, `GT_FRET`.
  There is no enum value for "both S and F." The per-statement
  representation carries one goto, not two.
- Two TK_COLON handlers (`sno_lex.plsw:1069..` and `:1113..`).
  Each sets `S_GTYP(S) = GT_SUCC | GT_FAIL | GT_UNCOND | ...` once.
  Neither loops over multiple `:S/:F/:` colons after the first.
  Whichever colon is parsed first wins; subsequent colons (even
  followed by a valid goto) are silently consumed but not stored.

So the bug is structural: the parsed-statement representation
cannot carry both an S-branch and an F-branch.

## Fix shape

Two-line summary:

1. Extend the per-statement state to carry both an S-target and
   an F-target (separate label slots, or a small per-goto table).
2. Update the TK_COLON handler to loop and consume `:S(...) :F(...)`
   variants (with/without space, with/without second-colon).

Concrete: rather than a single `S_GTYP / S_GLBL` pair, hold up to
two structured goto records:

- `S_GLBL_S(S)` -- label index, or `-1` for "none".
- `S_GLBL_F(S)` -- label index, or `-1` for "none".
- Optionally keep `S_GTYP(S)` for `GT_UNCOND / GT_RET / GT_FRET`
  (which are mutually exclusive with S/F).

The dispatcher (`src/sno_exec.plsw`) currently jumps based on
`S_GTYP(S)`. Update it to:

- If predicate succeeded and `S_GLBL_S(S) >= 0`, jump there.
- If predicate failed and `S_GLBL_F(S) >= 0`, jump there.
- Otherwise fall through.

(Plus existing UNCOND / RET / FRET handling, unchanged.)

## Accepted goto-syntax variants (per standard SNOBOL4)

All four of these forms must parse equivalently and produce the
same wired-up `S_GLBL_S` and `S_GLBL_F`:

```
        STMT    :S(L1):F(L2)
        STMT    :S(L1) :F(L2)
        STMT    :S(L1)F(L2)        ; second goto without a colon prefix
        STMT    :F(L2):S(L1)        ; reversed order
```

Single-direction forms (`:S(L1)` alone, `:F(L2)` alone, `:(L1)`,
`:RETURN`, `:FRETURN`) keep their current correct behaviour.

## Tests to add

Add to `examples/` or a dedicated test fixture set:

1. **Combined goto, predicate FAILS, both branches present** --
   expect F to fire (the no-fallthrough form above).
2. **Combined goto, predicate SUCCEEDS, both branches present** --
   expect S to fire.
3. **No-colon-on-second-goto variant** -- `:S(L1) F(L2)` should
   behave identically to `:S(L1):F(L2)`.
4. **Reverse order** -- `:F(L1):S(L2)` should also work.
5. **Pattern-match form** -- `STR pattern :S(L1):F(L2)` (the form
   documented in `examples/pattern-tutorial.sno`).
6. **Pathological** -- `:S(L1):S(L2)` (two S targets) should
   either reject or document precedence; do not silently drop one.

## What does NOT go in this PR

- No new SNOBOL4 surface features.
- No changes to the success/failure flag mechanics -- those work.
- No changes to single-direction goto handling -- that works.

## When done

Push `pr/combined-goto-parser`. After mike relays + reinstalls
the snobol4 binary:

- dcftn's `feat/m1-resume` saga can use combined gotos (more
  readable in normalize / classify / expr / lower / emit_plsw)
  instead of refactoring around single-direction-only gotos.
- Any other future SNOBOL4 program relying on the documented
  combined-goto form works as written.

## Credit

Bug class identified by an external reviewer who pushed back on a
sloppy first diagnosis from dcftn (which had assumed "always-S-
fires" based on a fallthrough artifact). The reviewer suggested
the no-fallthrough regression form which cleanly disambiguates
parser-bug from flag-bug from fallthrough; that form is what's
used above. Captured here so the next agent walking the same path
goes straight to the right diagnosis.

## Verification

### 2026-05-08T18:46 build -- partial fix

dcsno's first attempt only made the reversed-order form
`:F(L1):S(L2)` work; the canonical S-first form
`:S(L1):F(L2)` (and its space/no-colon variants) still
silently dropped the second goto. Symptom: whichever goto was
parsed first stuck; the other was lost. Reported back to dcsno.

### 2026-05-08T19:09 build -- fully fixed

snobol4.lgo timestamp 2026-05-08 19:09:06. All six variants in
the matrix below now route both predicate paths to the correct
branch (12/12 cells green):

| Form | predicate FAILS | predicate SUCCEEDS |
|------|------------------|---------------------|
| `:S(SOK):F(SBAD)`         | F branch | S branch |
| `:S(SOK) :F(SBAD)`        | F branch | S branch |
| `:S(SOK)F(SBAD)`          | F branch | S branch |
| `:F(SBAD):S(SOK)`         | F branch | S branch |
| `:F(SBAD) :S(SOK)`        | F branch | S branch |
| `:F(SBAD)S(SOK)`          | F branch | S branch |

dcftn unblocked. Combined gotos can be used freely in
normalize / classify / expr / lower / emit_plsw.
