# Brief: SNOBOL4 pattern with >~3 `.` captures drops trailing capture values

**Owner:** dcsno
**Branch:** `pr/pattern-captures-truncation`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-4-print-int` saga (2026-05-12).
**Drafted by:** mike (2026-05-13).
**Parent brief:** `dcsno-static-program-size-limit.md` (follow-on #4 in
its 2026-05-12T17 verification section).

## Symptom

A single SNOBOL4 pattern containing 4 or more `.` capture clauses
yields stale (default 0 / empty) values for the trailing capture
variables. The first ~3 captures bind to the matched substrings
correctly; captures 4+ get the default value of the destination
variable regardless of what the pattern actually consumed.

The pattern itself still *matches* (the surrounding `:S(...)`
fires). Only the side-effect of binding capture variables breaks
for slots beyond ~3.

Confirmed still broken in the 2026-05-12 16:52
`work/lib/cor24/snobol4.lgo` build.

## Repro

```sno
        L = 'a=1 b=2 c=3 d=4'
        L 'a=' BREAK(' ') . A ' b=' BREAK(' ') . B ' c=' BREAK(' ') . C ' d=' REM . D :F(BAD)
        OUTPUT = 'A=' A
        OUTPUT = 'B=' B
        OUTPUT = 'C=' C
        OUTPUT = 'D=' D                                                                :(DONE)
BAD     OUTPUT = 'no match'                                                            :(DONE)
DONE    OUTPUT = 'end'
END
```

Expected output:
```
A=1
B=2
C=3
D=4
end
```

Actual output:
```
A=1
B=2
C=3
D=0
end
```

(`D` retains its initial default value of `0` -- it is never
assigned -- and the surrounding pattern succeeds anyway.)

## Workaround (in use today)

Split the pattern into multiple smaller patterns of <=3 captures
each, with `:F(BAD)` after each:

```sno
        L = 'a=1 b=2 c=3 d=4'
        L 'a=' BREAK(' ') . A ' b=' BREAK(' ') . B :F(BAD)
        L 'c=' BREAK(' ') . C ' d=' REM . D        :F(BAD)
```

dcftn's classify / record-parse paths in
`sw-cor24-fortran/snobol4/src` use this two-stage shape throughout.
Cost: extra `:F(BAD)` plumbing and an extra pattern compile per
parse, but functionally equivalent for non-backtracking records.

## Hypothesis

The 3-capture threshold is suspiciously low (vs. the more typical
8-16 cap of fixed-size arrays elsewhere in this engine). That
shape suggests a *tuple of named registers* rather than an
array -- likely `CAP0` / `CAP1` / `CAP2` slots in the pattern-match
state. Captures beyond slot 2 either:

- Get silently dropped at pattern-compile time (the `.` binder is
  parsed but the capture index is clipped to a 0-2 range), or
- Get assigned a slot index that aliases onto runtime bookkeeping
  registers (so the binding "happens" but is overwritten before
  the variable assignment fires), or
- Skip the per-capture write loop after slot N because the loop
  is unrolled for slots 0..2 only.

The "default 0 stays, surrounding match succeeds" observation
rules out a pattern-compile *failure* -- the pattern compiles
cleanly and the match succeeds. It's specifically the variable
assignment side-effect that drops.

## What dcsno needs to investigate

1. Find the `.` (capture-bind) parser in the pattern compiler.
   Probable site: `src/sno_lex.plsw` (or the pattern module if a
   split has landed).
2. Find the capture-flush machinery in the matcher --
   wherever successful match writes capture results back to
   subject variables. Probable site: `src/sno_exec.plsw`.
3. Identify the slot count. If it's a 3-register tuple, switch
   to an array (size 8-16) with a clear diagnostic on overflow.
4. If it's already an array but with a fixed walk loop, generalise
   the walk.

## Tests to add

| pattern (subject `L`)                                       | expected captures           |
|-------------------------------------------------------------|-----------------------------|
| `L = 'x=1'; L 'x=' REM . X`                                 | X='1'                       |
| `L = 'a=1 b=2'; L 'a=' BREAK(' ') . A ' b=' REM . B`        | A='1', B='2'                |
| `L = 'a=1 b=2 c=3'; <3-capture pattern>`                    | A='1', B='2', C='3'         |
| `L = 'a=1 b=2 c=3 d=4'; <4-capture pattern>` (repro above)  | A='1', B='2', C='3', D='4'  |
| `L = 'a=1 b=2 c=3 d=4 e=5'; <5-capture pattern>`            | all five bound              |
| `L = 'a=1 b=2 c=3 d=4 e=5 f=6'; <6-capture pattern>`        | all six bound               |
| Stretch: 12-capture single pattern                          | all twelve bound            |

Plus a negative case at the new cap: a pattern with N+1 captures
(where N is the new slot count) must emit a diagnostic, not
silently drop. Don't reintroduce silent truncation at a higher
threshold.

## When done

- dcftn's classify / record-parse code in
  `sw-cor24-fortran/snobol4` can use single-pattern parses with
  4+ captures. That's the natural shape for `kind=X label=Y
  text=Z body=...` records, which already appear in dcftn's
  intermediate representation and which currently parse in
  two stages.
- The "split for 3-capture limit" comment markers in dcftn's
  sno files get removed in a follow-up dcftn PR.

## Out of scope

- **Other pattern features:** alternation `|`, repetition,
  recursion depth, fenced patterns, etc. This brief is
  specifically about the `.` capture count per pattern.
- **Indirect capture** (`. *VAR`) -- if it works in the 1-3 slot
  range today, treat as same fix; if it has its own bug, file
  separately.
- The other three `dcsno-static-program-size-limit.md` follow-ons.

## Credit

dcftn, FTI-0 m4-print-int saga (2026-05-12). Surfaced when
the record parser in `snobol4/src/classify.sno` started failing
silently on FORTRAN intermediate records with 4 fields.
