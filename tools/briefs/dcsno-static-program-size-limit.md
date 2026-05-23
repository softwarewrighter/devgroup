# Brief: SNOBOL4 miscompiles programs above ~233 source statements

**Owner:** dcsno
**Branch:** `pr/static-program-size-limit`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-4-print-int` saga
(2026-05-12) while extending `snobol4/src/emit_asm.sno` to inline
the `_putint` runtime (decimal int-to-ASCII via repeated
subtraction; ~70 OUTPUT lines).
**Affects:** any SNOBOL4 program with more than ~233 source
statements, regardless of what those statements do. Hits programs
that emit large literal payloads via repeated `OUTPUT = '...'` or
build large strings via repeated `S = S '...'` concatenation.

## Symptom

When a SNOBOL4 source file exceeds ~233 non-comment, non-blank
statements, downstream control flow becomes corrupt:

- `:(LABEL)` branches stop landing on the intended label and
  appear to wrap back near the start of the program -- the
  prologue OUTPUTs re-execute.
- In some cases the program runs until the `-n` cycle budget is
  exhausted, emitting tens of thousands of lines of repeated
  prologue (looks like an infinite loop).
- In others it terminates but with truncated output (~256 lines)
  or missing label dispatches entirely.

The corruption is silent: no diagnostic, no error from the
interpreter. The dcftn user (m4-print-int saga) first saw it as
"`:(LOOP)` jumps to the top of the program after KPRTI emits its
output," manifesting as the assembly boilerplate getting emitted
twice.

## Minimal repro

A SNOBOL4 program that is just N `OUTPUT = 'padN'` lines followed
by a normal RAWINPUT-driven dispatch loop (LOOP / KPRG / KPRT /
KPRTI / KPRTS / KSTP / KEND / BAD / EOFL / END):

```
$ cat /tmp/repro-builder.sh
#!/bin/bash
N=$1
{
    for ((i=0; i<N; i++)); do
        echo "        OUTPUT = 'pad${i}'"
    done
    cat << 'SNOEND'
LOOP    LINE = RAWINPUT  :F(EOFL)
        LINE 'kind=' BREAK(' ') . KIND ' text=' REM . TXT  :F(BAD)
        IDENT(KIND, 'PROGRAM')   :S(KPRG)
        IDENT(KIND, 'PRINT')     :S(KPRT)
        IDENT(KIND, 'STOP')      :S(KSTP)
        IDENT(KIND, 'END')       :S(KEND)
        :(LOOP)
KPRG    OUTPUT = 'PROGRAM'                                                              :(LOOP)
KPRT    TXT BREAK("'") "'" BREAK("'") . LIT                                             :S(KPRTS)
        TXT 'PRINT *' SPAN(' ,') SPAN('0123456789') . NUM                               :S(KPRTI)
        :(BAD)
KPRTI   OUTPUT = 'INT=' NUM                                                             :(LOOP)
KPRTS   OUTPUT = 'STR=' LIT                                                             :(LOOP)
KSTP    OUTPUT = 'STOP'                                                                 :(LOOP)
KEND    OUTPUT = 'ENDREC'                                                               :(LOOP)
BAD     OUTPUT = 'BAD'                                                                  :(LOOP)
EOFL
END
SNOEND
}

$ cat > /tmp/r2-in.txt << 'IN'
stmt1 line=1 label= kind=PROGRAM text=PROGRAM X
stmt2 line=2 label= kind=PRINT text=PRINT *, 42
stmt3 line=3 label= kind=STOP text=STOP
stmt4 line=4 label= kind=END text=END
IN

$ for N in 200 230 233 234 240 250 300; do
    /tmp/repro-builder.sh $N > /tmp/r.sno
    lines=$(snobol4 --load-binary /tmp/r.sno@0x080000 \
                    --load-binary /tmp/r2-in.txt@0x090000 \
                    --entry 0 --quiet --speed 0 \
                    -n 100000000 -t 30 2>/dev/null | wc -l)
    echo "N=$N: lines=$lines  (expected=$((N+4)))"
done
N=200: lines=204  (expected=204)
N=230: lines=234  (expected=234)
N=233: lines=237  (expected=237)
N=234: lines=85024 (expected=238)
N=240: lines=84985 (expected=244)
N=250: lines=84962 (expected=254)
N=300: lines=256   (expected=304)
```

So the bug *first appears at N=234*. The exact threshold may
shift with statement length / label-table size; the underlying
issue is clearly a static program-size or statement-table cap
that the interpreter silently exceeds.

## Real-world hit (dcftn FTI-0 compiler)

`snobol4/src/emit_asm.sno` in `dcftn/sw-cor24-fortran` is the
FTI-0 → COR24 assembly emitter. When the m4-print-int saga
tried to inline a `_putint` runtime subroutine as ~70
`OUTPUT = '        ...'` lines (decimal int-to-ASCII via
repeated subtraction, since COR24 has no native div/mod), the
file crossed the threshold and the bug appeared as:

- Boilerplate prologue (`.text`, `_start`, `_putc`, `_puts`,
  `_putint`) emitted correctly.
- `_main:` emitted from the PROGRAM record.
- `_putint` call emitted from the PRINT record (the KPRTI block).
- THEN: the boilerplate prologue re-emitted from the top. Control
  fell back through statements 1..N of the program rather than
  branching to `LOOP`.

After the boilerplate re-emit, dispatch crawled through STOP / END
records eventually, but the final `.s` was unassemblable due to
duplicate symbols (`_start`, `_main`, etc.).

Workaround in dcftn: split the `_putint` runtime into
`snobol4/runtime/putint.s` (static file) and have
`scripts/fortran` `awk`-splice it into the SNOBOL4 emitter's
output at a marker line `; __RUNTIME_PUTINT__`. emit_asm.sno
shrinks back below the threshold; output is byte-equivalent.

This is a runtime-library pattern that's defensible on its own
(most real compilers split codegen from runtime), but it was
*forced* by the dcsno cap, not chosen for design reasons. If
the cap goes away the runtime can be inlined back into the
SNOBOL4 emitter, which is the dogfooded form.

## Hypothesis

The threshold value (N=234) and the *near-but-not-exact*
truncation at ~256 lines for very large programs suggest one of:

- A fixed statement-table allocation (likely a power-of-two near
  256 entries minus prologue / utility slots) that overflows into
  adjacent state (the label table? the goto-target table? the
  variable-name table?) -- corrupting the `:(LABEL)` dispatch.
- A label-index encoding that wraps at some bit width and aliases
  high-numbered labels onto early statement addresses.

The "control returns to the top of the program" symptom is
particularly suggestive of label resolution returning 0 / unset /
the start-of-program PC.

## What the dcsno owner needs to investigate

1. Look at static program / statement-table allocation in
   `src/sno_lex.plsw` (parser) -- is there a fixed-size array
   of statements?
2. Confirm whether the label-index field has a bit-width cap.
3. Check whether labels declared late in the program get
   index values that wrap into the statement-1 region.
4. Either grow the static caps or transition to dynamic
   allocation; emit a clear diagnostic when caps are exceeded
   rather than silently producing wrong code.

## Workaround posture on the dcftn side

`snobol4/runtime/putint.s` and the `awk` splice in
`scripts/fortran` will stay until this brief is resolved. They
are clearly marked as such (comments in both files reference
this brief). Once the cap lifts and is verified large enough for
near-future passes (lower / emit_plsw / etc.), I'll inline
`_putint` back into `emit_asm.sno` and delete the runtime split.

## Tests dcsno should add

- A scaling regression: build N from 100 to 500 in steps of 10;
  for each, assert that a program of N `OUTPUT = 'padN'` lines
  followed by a 4-record dispatch loop emits exactly N+4 lines.
- A label-resolution regression: program with 240+ statements
  where `:(LABEL)` near the end must branch to a label near the
  beginning; verify it lands on the right label, not on statement 1.

## Credit

Caught by dcftn during the FTI-0 m4-print-int saga. The bug
showed up after a clean build of the SNOBOL4-based emitter that
previously produced byte-identical output to hand-written
`examples/hello.s`. The dogfooding pressure (no Path A
short-circuits, no shell-level hacks beyond a clearly-marked
runtime-library splice) is what surfaced it.

## Verification 2026-05-12T17 -- partial fix landed

dcsno commit 07b7a21 (`fix: raise STMAX 256->1024 and emit
overflow diagnostic (dcftn brief)`) raised the statement-table
cap and replaced the silent miscompile with a real error message
("source too large, truncated at byte=<N>").

Verified against `snobol4.lgo` deployed at 2026-05-12 16:52:
- Statement-count cap: now passes well above the previous ~234
  threshold. dcftn's emit_asm.sno at 179 statements works.
- Error message: clearly visible when source overflows, no more
  silent infinite loops.

**Not yet fixed (filed as follow-on items below):**

1. **Source-byte cap** -- there's a hard ~12280-byte limit on
   SNOBOL4 source size. Independent of statement count.
   emit_asm.sno started failing at 13176 bytes; trimming comments
   to 9123 bytes makes it pass. The "truncated at byte=12280"
   number looks like a fixed source-buffer allocation. Lifting
   this lets us inline runtime/{prelude,putint}.s back into the
   SNOBOL4 emitter (the dogfooded form).

2. **ANY(class) pattern fails silently** -- even `ANY('X')`
   against subject `'X'` does not match. Worked around with
   `SPAN(class)` (matches one-or-more of class; behaves as a
   single-char match when the subject has exactly one char of
   class). Confirmed still broken in 2026-05-12 16:52 build.

3. **Concat-expression truncation at >~4 string operands** --
   `S = 'a' NL 'b' NL 'c' NL 'd' NL 'e'` yields a result of size
   8 instead of 9. Workaround: use 2-3 operands per
   concatenation, build incrementally with `S = S NL '...'`.
   Confirmed still broken in 2026-05-12 16:52 build.

4. **Pattern-result truncation at >~3 `.` captures per pattern**
   -- a single pattern with 4+ captures yields stale (default
   0) values for trailing captures. Workaround: split the
   record-parse / statement-parse into multiple smaller
   patterns. Confirmed still broken in 2026-05-12 16:52 build.

dcftn unblocked for milestones m8-m10 with the workarounds in
place. Will file separate briefs for (2), (3), (4) when there's
a quiet moment between sagas.
