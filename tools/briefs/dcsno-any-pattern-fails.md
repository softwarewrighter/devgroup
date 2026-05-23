# Brief: SNOBOL4 `ANY(class)` pattern silently fails to match

**Owner:** dcsno
**Branch:** `pr/any-pattern-fails`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-4-print-int` saga (2026-05-12).
**Drafted by:** mike (2026-05-13).
**Parent brief:** `dcsno-static-program-size-limit.md` (follow-on #2 in
its 2026-05-12T17 verification section).

## Symptom

`ANY(class)` -- the canonical SNOBOL4 "match exactly one
character from `class`" primitive -- fails silently. Even the
trivial single-char case drops into the failure arm:

```sno
        S = 'X'
        S ANY('X') :S(MATCH)F(NOMATCH)
```

`MATCH` is the correct destination per SNOBOL4 semantics
(`ANY('X')` against subject `'X'` must consume the `X` and
succeed). Today it lands on `NOMATCH`.

No diagnostic. The pattern simply fails as if no character in
`class` were present. Confirmed against the 2026-05-12 16:52
`work/lib/cor24/snobol4.lgo` build.

## Repro

```sno
        S = 'X'
        S ANY('X') :S(MATCH)F(NOMATCH)
MATCH   OUTPUT = 'matched'                                                            :(DONE)
NOMATCH OUTPUT = 'no match'                                                           :(DONE)
DONE    OUTPUT = 'end'
END
```

Expected output:
```
matched
end
```

Actual output:
```
no match
end
```

## Workaround (in use today)

`SPAN(class)` (match one-or-more from `class`) behaves correctly.
When the subject has exactly one character of `class`, `SPAN`
matches that one character and the pattern succeeds. dcftn's
classify / normalise passes in `sw-cor24-fortran/snobol4/src`
use `SPAN(class)` everywhere `ANY(class)` would have been
natural, with `; ANY broken upstream` comment markers tagging
the replacement sites.

The workaround is not exactly equivalent: `SPAN` greedily consumes
multiple matching chars, so it can over-consume when the subject
has runs of `class` chars and the surrounding pattern expected
exactly one. dcftn has audited their callsites to make sure that's
not the case. For other future SNOBOL4 authors this audit is
not free.

## Hypothesis

ANY and SPAN almost certainly share the bulk of their
pattern-match plumbing -- both walk a single character class,
both succeed/fail based on subject-current-char membership. They
differ at the "exactly one" vs. "one or more" terminator.

Likely culprits, in rough order of probability:

1. **The ANY codepath never advances the subject cursor on
   success** -- the match succeeds locally but is rejected as a
   zero-width match by the surrounding match engine.
2. **Copy/paste bug**: the ANY opcode handler actually evaluates
   `NOTANY` (the set complement). Easy to verify with
   `S ANY('') :S(YES)F(NO)` -- NOTANY('') always matches one
   char, ANY('') should never match. If ANY('') succeeds, that
   confirms the swap.
3. **Class-decode error**: the class argument is read with the
   wrong width / wrong index when the count register is set for
   "exactly one" rather than "one-or-more".

Worth a focused grep of `src/sno_exec.plsw` (or the pattern
module if a split has landed) for the ANY opcode handler, then
side-by-side compare against the SPAN handler.

## What dcsno needs to investigate

1. Locate the ANY pattern handler. Probable site is the pattern
   evaluator in `src/sno_exec.plsw`, or the pattern compiler in
   `src/sno_lex.plsw` if the bug is at AST/codegen time.
2. Side-by-side ANY vs. SPAN handlers. Find the divergence at
   "single-char match consume + return success."
3. Fix; verify against the repro above and the test list below.

## Tests to add

| pattern                                  | expected |
|------------------------------------------|----------|
| `S = 'X'; S ANY('X') :S(YES)F(NO)`       | YES      |
| `S = 'X'; S ANY('Y') :S(YES)F(NO)`       | NO       |
| `S = 'X'; S ANY('XY') :S(YES)F(NO)`      | YES      |
| `S = 'X'; S ANY('AB') :S(YES)F(NO)`      | NO       |
| `S = ''; S ANY('X') :S(YES)F(NO)`        | NO       |
| `S = 'ABC'; S ANY('A') . CH :S(YES)F(NO)`| YES, CH='A' |
| `S = 'ABC'; S ANY('Z') . CH :S(YES)F(NO)`| NO       |
| `S = ''; S NOTANY('') :S(YES)F(NO)`      | NO (sanity check NOTANY isn't broken in mirror) |

The capture cases (`. CH`) double-check that the cursor advances
correctly and the captured character is what was matched.

## When done

- dcftn's `snobol4/src/{classify,normalize}.sno` can use
  `ANY(class)` directly. The `; ANY broken upstream` markers get
  cleaned up in a follow-up dcftn PR.
- Future SNOBOL4 authors don't need to memorise the SPAN workaround
  or audit their callsites for greedy over-consumption.

## Out of scope

- **SPAN, BREAK, BREAKX, NOTANY, ARB, REM, FENCE, FAIL** etc. -- if
  the fix lives in shared plumbing (e.g. character-class decode)
  those may incidentally improve, but the test gate is ANY-only.
  File separate briefs if dcftn or others uncover analogous failures.
- The other three `dcsno-static-program-size-limit.md` follow-ons.

## Credit

dcftn, FTI-0 m4-print-int saga (2026-05-12). Reduced to a
one-line repro after the SPAN workaround unblocked their
classify pass.
