# Brief: SNOBOL4 string concatenation truncates with >~4 operands

**Owner:** dcsno
**Branch:** `pr/concat-truncation`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-4-print-int` saga (2026-05-12).
**Drafted by:** mike (2026-05-13).
**Parent brief:** `dcsno-static-program-size-limit.md` (follow-on #3 in
its 2026-05-12T17 verification section).

## Symptom

A SNOBOL4 string-concatenation expression with 5 or more
operands silently produces a shorter result than the sum of its
parts. The trailing byte(s) get dropped.

Reduced example from dcftn:

```sno
        NL = '\n'
        S = 'a' NL 'b' NL 'c' NL 'd' NL 'e'
        OUTPUT = SIZE(S)
```

emits `8` instead of the expected `9` (five 1-char strings + four
1-char NLs = 9). Confirmed against the 2026-05-12 16:52
`work/lib/cor24/snobol4.lgo` build.

## Repro

```sno
        NL = '\n'
        S = 'a' NL 'b' NL 'c' NL 'd' NL 'e'
        OUTPUT = 'size=' SIZE(S)
        OUTPUT = '[' S ']'
END
```

Expected:
```
size=9
[a
b
c
d
e]
```

Actual (paraphrased -- exact tail byte may vary):
```
size=8
[a
b
c
d
]
```

The trailing `e` is absent and `SIZE` agrees that the string is
one byte short.

## Workaround (in use today)

Build the concatenation incrementally with <= 3 operands per
expression:

```sno
        S = 'a' NL 'b' NL 'c'
        S = S NL 'd' NL 'e'
```

dcftn uses this shape in `sw-cor24-fortran/snobol4/src/emit_asm.sno`
across the KPRG (assembly header), KPRT, and runtime-stub emit
blocks. Adds ~20% line count to those blocks compared to a
natural single-expression concat.

## Hypothesis

The 4-5 operand threshold is suspiciously low and consistent
across runs, which points at a fixed-size data structure rather
than a heap-allocation failure. Two candidates:

1. **N-ary concat AST node with a fixed-size operand array.**
   The parser likely flattens `'a' NL 'b' NL 'c' NL 'd' NL 'e'`
   into a single N-operand concat node whose operand-array has a
   hardcoded size of ~4. The 5th operand overflows -- silently
   either dropped or written into adjacent state.
2. **Evaluator-side fixed scratch buffer.** The expression
   evaluator may walk operands into a fixed-width "concat in
   progress" descriptor (e.g. a 4-entry stack of pointers + a
   running-size accumulator) and stop walking after slot 4.

Both shapes are consistent with the observed "result is exactly
one byte short of correct" -- the 5th operand of length 1 simply
never makes it into the output.

A simple discriminator: try `S = 'aa' NL 'bb' NL 'cc' NL 'dd' NL 'ee'`
(operands 2 bytes each). If the result is 13 bytes (= 14 - 1, one
byte from the last operand) the parser flattens to N-ary and
truncates the operand list to 4; if it's 12 bytes (= 14 - 2, the
entire 5th operand dropped) the parser keeps the full list but the
evaluator stops walking. Either way the fix is local.

## What dcsno needs to investigate

1. Look at how the parser handles consecutive concat operators in
   `src/sno_lex.plsw` (or the consolidated reader). Is concat
   built as N-ary or left-leaning binary?
2. Look at the concat evaluator in `src/sno_exec.plsw`. Find any
   fixed-size operand array or accumulator.
3. Either:
   - Grow the cap to a comfortable size (~16+ operands; covers
     all realistic single-expression concats) with a diagnostic
     on overflow, or
   - Restructure parse to emit left-leaning binary concat
     (`((((a . b) . c) . d) . e)`), making the cap a non-issue.
4. Make sure `SIZE` reports the actual emitted size (not the
   computed-but-truncated size). Today the two agree on the
   wrong answer, which is at least self-consistent.

## Tests to add

| expression                                          | expected SIZE |
|-----------------------------------------------------|---------------|
| `'a' 'b'`                                           | 2             |
| `'a' 'b' 'c'`                                       | 3             |
| `'a' 'b' 'c' 'd'`                                   | 4             |
| `'a' 'b' 'c' 'd' 'e'`                               | 5             |
| `'a' 'b' 'c' 'd' 'e' 'f'`                           | 6             |
| `'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h'`                   | 8             |
| `'a' NL 'b' NL 'c' NL 'd' NL 'e'` (NL = `'\n'`)     | 9             |
| `'aa' NL 'bb' NL 'cc' NL 'dd' NL 'ee'`              | 14            |
| `A B C D E F` (each a 1-char string variable)       | 6             |
| 16-operand concat of 1-char literals                | 16            |

The varied-length and variable-mixed cases differentiate the two
hypotheses above and verify the fix works for all operand kinds.

## When done

- dcftn's `emit_asm.sno` OUTPUT blocks (especially the multi-line
  KPRG assembly-header block) can be written as natural
  single-expression concats. Lines saved: ~30 across emit_asm.sno;
  comprehension win is larger than the line savings.
- Future SNOBOL4 authors don't need to fragment string-building
  into S = S ... chains to dodge a silent-truncation bug.

## Out of scope

- **Pattern-side concat** (PATTERN sequence, `|`, `+`, etc.) --
  this brief is specifically about string-value concat in
  expression context. If patterns have an analogous truncation,
  file separately.
- **Result-size limits.** This is about operand-count truncation,
  not byte-size truncation. A 5-operand concat of two 100KB
  strings is a separate (larger) memory question.
- The other three `dcsno-static-program-size-limit.md` follow-ons.

## Credit

dcftn, FTI-0 m4-print-int saga (2026-05-12). Surfaced when the
KPRG block in `emit_asm.sno` started silently dropping the last
line of the assembly header.
