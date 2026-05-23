# Brief: SNOBOL4 silently halts dcftn's emit_asm.sno at ~364 statements / 17,460 bytes

**Owner:** dcsno
**Branch:** `pr/emit-asm-halt-near-364-stmts`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-13-inline-runtime` saga (2026-05-14)
while attempting to inline `runtime/putint.s` (the
`_putint` decimal-print runtime) back into `emit_asm.sno`.
**Affects:** the 2026-05-14 12:10 dcsno build with all four
cap-raise fixes landed (`pr/cap-and-pattern-fixes` +
`pr/more-fixes`), md5 `837b217e`.

## Symptom

A precise threshold in dcftn's `emit_asm.sno`:

| stmts | bytes  | result                                                |
|-------|--------|-------------------------------------------------------|
| 313   | 16,048 | all 10 FTI-0 demos pass                               |
| 363   | 17,417 | passes                                                |
| **364** | **17,459** | **halts after running ~4-5M instructions; only the prologue + part of `_main` emits; the rest of the dispatch (KSTP / KEND) never fires** |
| 373+  | 17,800+ | also halts                                            |

This is well below both documented caps from
`pr/cap-and-pattern-fixes`:
- `STMAX = 1024` statements
- `SRC_SIZE = 64K` bytes

The halt is silent: no diagnostic, no error, no `:F` taken on
any pattern, the program just stops running before reaching
the LOOP-end / `END` statement. Cycle limit (`-n 100000000`)
is not the cause -- the program halts at ~4-5M cycles.

## Minimal repro

```sh
# Setup -- start from the dcftn dev tip's emit_asm.sno (the
# version that splices both prelude.s and putint.s in via awk).
cd /disk1/.../sw-cor24-fortran
git checkout dev -- snobol4/src/emit_asm.sno  # 233 stmts, 11K bytes

# Inline the prelude (97 OUTPUTs) -- still passes:
# (...replace the `__RUNTIME_PRELUDE__` marker OUTPUT with the
# full set of OUTPUT lines from runtime/prelude.s...)
# After this: 313 stmts, 16K bytes -- all 10 demos pass.

# Try to ALSO inline putint (~70 OUTPUTs):
# After this: 380+ stmts, 18K bytes -- all 10 demos break.

# Run the broken version:
$ snobol4 --load-binary snobol4/src/emit_asm.sno@0xE0000 \
          --load-binary /tmp/h.cls@0xF0000 \
          --quiet --speed 0 -n 100000000 -t 60
.text
...
        .globl  _main
_main:
        push    fp
        push    r2
        push    r1
        mov     fp,sp
        la      r0,_S0
        push    r0
        la      r0,_puts
        jal     r1,(r0)
        add     sp,3
-131072       <-- raw '-131072' at the end (no newline)

Executed 5787358 instructions   <-- halted early
```

The trailing `-131072` byte sequence is suspicious; it's
`-0x20000` and looks like maybe a register value or pointer
being printed. Below the dispatch logic finishes processing
`stmt2` (the PRINT-with-string-literal case), but `stmt3`
(STOP) and `stmt4` (END) never trigger their handlers.

## Bisection

Worked in increments of 5 putint OUTPUTs from N=0 (no putint
inlined, splice still in place) up to N=70 (all of putint
inlined). Threshold is right at N=51 statements added beyond
the prelude-only baseline (313):

```
N=50 stmts=363 size=17417 -- emits .byte line, passes
N=51 stmts=364 size=17459 -- empty (.byte not emitted)
N=53 stmts=366 size=17546 -- empty
...
```

Same input file (`/tmp/h.cls`), same data load address
(`0xF0000`), same flags. Only the number of OUTPUT statements
in the prologue of emit_asm.sno differs.

## Hypothesis

This is a different cap than the four already-raised ones --
likely something in:

- The bytecode-compilation buffer for the parsed SNOBOL4
  program (a per-program intermediate that the interpreter
  emits before execution). 364 statements * ~K-bytes-per-stmt
  could overflow some 64K-ish intermediate.
- A program-counter or jump-table width that limits dispatch
  to ~365 statements regardless of the source byte cap.
- A label-resolution table sized to a power-of-two that's
  bigger than 256 but smaller than 1024 (256+something).

The "-131072 = -0x20000" trailing artifact is the strongest
hint -- it's a clean power-of-two negative, suggesting either
a sign-extended buffer pointer overflow or a 24-bit truncation
artifact.

## Workaround posture

`runtime/putint.s` + the `awk` splice in `scripts/fortran`
will stay until this brief is resolved. The prelude
(`_start / _halt / _putc / _aindex / _puts`) is now inlined
directly in `emit_asm.sno`; only `_putint` is still spliced.

Once this cap is identified and raised, m14 in dcftn can
inline `_putint` and delete `runtime/putint.s` + the splice
entirely.

## Tests dcsno should add (regression coverage)

1. **Big-program smoke test:** generate a SNOBOL4 program
   with N `OUTPUT = 'pad<i>'` lines for N in
   {100, 200, 300, 400, 500, 750, 1000}, run with simple
   3-record input. Assert the program emits N records before
   processing input.

2. **Mixed-content cap test:** start from dcftn's
   `emit_asm.sno` at dev tip, inline more and more of
   `runtime/putint.s`'s OUTPUT lines, run the standard 10-demo
   regression. Assert every step works.

3. **Compile-time vs run-time diagnostic:** when the
   interpreter exceeds an internal cap, it should emit a
   visible error (like `pr/stmt-table-cap` did for STMAX)
   rather than silently halt. Audit every internal table /
   buffer in the interpreter source and add a `bounds-check
   with diagnostic` to each.

## When done

Push `pr/emit-asm-halt-near-364-stmts`. After mike relays +
reinstalls `snobol4`, dcftn re-runs the inlining for the full
`_putint` runtime (`m14-inline-putint`) and deletes
`runtime/putint.s` and the last awk splice in
`scripts/fortran`.
