# Brief: SNOBOL4 labels containing `_` silently don't resolve in `:(LABEL)` gotos

**Owner:** dcsno
**Branch:** `pr/underscore-label`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-9-array` saga (2026-05-13)
while adding helper labels `EXPRA_L`, `EXPRA_C`, `EAA_L`, `EAA_C`
to share array-load logic between EXPR's array-read path and
KASN's array-store dispatch in `snobol4/src/emit_asm.sno`.
**Drafted by:** dcftn (2026-05-13).
**Parent brief:** `dcsno-static-program-size-limit.md` (this is
follow-on item #5 to the four already filed by mike: `dcsno-
any-pattern-fails.md`, `dcsno-concat-truncation.md`, `dcsno-
pattern-captures-truncation.md`, `dcsno-source-byte-cap.md`).

## Symptom

A SNOBOL4 label whose name contains `_` (underscore) cannot be
the target of a transfer goto `:(LBL)`. The lookup silently
fails and control falls through to the next statement, producing
the same "loop-back-to-program-start" pathology that
`pr/stmt-table-cap` fixed for the overflow case -- except this
one is unrelated to source size.

Manifests as: any program that uses `:(L_FOO)` style gotos
infinite-loops over its prologue OUTPUTs, exhausting `-n`
cycles. Renaming `L_FOO` -> `LFOO` (no underscore) at every
declaration AND every goto site makes the same program work.

## Minimal repro

```
$ cat /tmp/und.sno
        OUTPUT = 'pre'
        :(L_TEST)
L_TEST  OUTPUT = 'reached L_TEST'
END

$ snobol4 --load-binary /tmp/und.sno@0x080000 \
          --entry 0 --quiet --speed 0 -n 100000 -t 30
pre
pre
pre
...
pre
Executed 100000 instructions
```

Expected: `pre` then `reached L_TEST` then halt.
Observed: infinite `pre` until cycle budget exhausted; the goto
never lands on `L_TEST`, so execution falls through to the next
statement (which is `OUTPUT = 'pre'` since SNOBOL4 wraps after
END... or wraps because of some related artifact). Renaming to
`:(LTEST)` / `LTEST` works exactly as expected.

Confirmed against the 2026-05-12 16:52
`work/lib/cor24/snobol4.lgo` build.

## Real-world hit (dcftn FTI-0 compiler, milestone-9-array)

While extending `emit_asm.sno` to share array-address logic
between EXPR's array-read path (EXPRA: emit `_aindex` call then
deref) and KASN's array-store path (EXPRDAA: same setup, then
store), I introduced helper labels for the literal-vs-variable
IDX branch:

```
EXPRA   T1IDX SPAN('0123456789') :S(EXPRA_L)   ; LITERAL idx
        OUTPUT = '        la      r0,_V_' T1IDX
        OUTPUT = '        lw      r0,0(r0)'
        :(EXPRA_C)                              ; common continuation
EXPRA_L OUTPUT = '        la      r0,' T1IDX
EXPRA_C OUTPUT = '        push    r0'
        ...
```

All seven existing demos
(hello/print-int/print-var/add/goto1/sum10/array1) loop-locked.
Replaced underscores with the un-segmented forms `EXPRAL`,
`EXPRAC`, `EAAL`, `EAAC` and everything started working again.
The pathology was indistinguishable from the static-program-
size-cap symptom -- the only clue that this was distinct from
the cap bug was: stripping comments to shrink the file didn't
help, but renaming the labels did.

## Hypothesis

dcsno's label lexer / table likely treats `_` as a non-label
char (e.g., word-break or end-of-identifier) when storing the
label name, while `:(L_FOO)` later builds the lookup key from
the full token including the `_`. Mismatched normalization
between declaration and lookup -> hash miss -> "label not
found" -> fall-through to next statement.

Alternatively, the goto-target table uses an integer slot
allocated at parse time and the underscore-containing form
never gets a slot. Either way, the lookup silently fails with
no diagnostic.

## Fix shape

Two-line summary:
1. Accept `_` as a legal label character in both the label-
   declaration and the goto-target tokenizer paths.
2. Round-trip the label through whichever normalization step
   is mismatched.

Concrete: in `src/sno_lex.plsw`, find the lexer routine that
classifies label characters (probably a `chr_class()` helper
or an `is_id_char()` predicate) and ensure `_` is in the
identifier class for both declaration parsing and goto-target
parsing.

## Tests dcsno should add

- Direct: program above. Assert `reached L_TEST` is printed
  exactly once and the program halts.
- Reversed: a goto to a no-underscore label declared in a
  block of underscore-labels (`L_A` / `L_B` / `LC` / `:(LC)`)
  to make sure mixed-label-style programs work.
- Round-trip: `L_FOO` is declared, `:(L_FOO)` jumps to it, `LFOO`
  separately exists and is jumped to by `:(LFOO)`. Both must
  resolve to the correct distinct targets.

## Workaround posture on the dcftn side

All `emit_asm.sno` labels in
`dcftn/sw-cor24-fortran/snobol4/src/emit_asm.sno` have been
renamed to the underscore-free forms. No assembly-side or
runtime-side workaround needed (the bug is purely in the
SNOBOL4 source, not in the emitted COR24 assembly).

## When done

Push `pr/underscore-label`. After mike relays + reinstalls
`snobol4.lgo`, dcftn will rename a few labels back to the
underscored forms in a small cleanup saga (the underscored
names like `EXPRA_L` are noticeably more readable than the
collapsed `EXPRAL`).
