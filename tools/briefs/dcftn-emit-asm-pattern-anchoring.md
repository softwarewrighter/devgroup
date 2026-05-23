# Brief: anchor KPRT / KASN patterns in `emit_asm.sno`

**Owner:** dcftn
**Branch:** start as `feat/emit-asm-pattern-anchoring`; `dg-mark-pr` when ready
**Repo:** `sw-cor24-fortran`
**Prerequisites:** none &mdash; orthogonal to in-flight `feat/m4-print-int` / m5 follow-ons. Can land before or after.
**Drafted by:** dwftn (diagnosed while wiring m4 + m5 into `web-sw-cor24-fortran`)

## Symptom

For three of the four canonical `examples/*.f` demos, the chain produces *syntactically-looking* COR24 assembly that contains illegal labels rather than a `; WARN:` line. The downstream assembler then fails with the unhelpful `Line N: label must be on its own line`. End-users see this as "this demo doesn't work" with no clue *why*.

Concretely:

| Input statement | `emit_asm.sno` currently emits | Why this is wrong |
|---|---|---|
| `I = I + 1` (from `goto1.f`) | `la r0,1` / `la r1,_V_+` / `sw r0,0(r1)` | KASN matched with `VN=+`, `NUM=1` |
| `A(I) = I * 10` (from `array1.f`) | similar with `_V_*` (or `_V_(`) | KASN matched mid-string |
| `PRINT *, A(3)` (from `array1.f`) | `la r0,_V_A(3)` / `lw r0,0(r0)` / ... | KPRTV matched with `VN=A(3)` &mdash; including parens |

Expected behaviour: each of these should emit a `; WARN: malformed input: ...` line (so dwftn can surface a clean compile-stage error to the user) rather than silently producing bogus labels.

## Root cause

The `KPRT` (variable-name fallback) and `KASN` patterns aren't anchored to the start of `TXT`, and they don't validate that the captured `VN` is actually a Fortran identifier. SNOBOL4's default unanchored matching lets the pattern engine search the whole `TXT` for *some* substring that satisfies the pattern. Concrete trace for `I = I + 1`:

```
TXT = 'I = I + 1'
KASN pattern: BREAK(' =') . VN  SPAN(' =')  SPAN('0123456789') . NUM  :F(BAD)

Match attempt at pos 0 ('I'):
  BREAK(' =') -> 'I',  SPAN(' =') -> ' = ',  SPAN(digits) needs digit at 'I' -> FAIL

Engine searches further. Match attempt at pos 6 ('+'):
  BREAK(' =') -> '+',  SPAN(' =') -> ' ',    SPAN(digits) -> '1' -> SUCCESS
  -> VN = '+', NUM = '1'
```

The same pathology hits `KPRTV` for `PRINT *, A(3)`: `BREAK(' ')` is happy to capture `A(3)` because there's no space inside.

## Fix

Two equivalent options &mdash; pick whichever feels cleanest:

### Option A: anchor the patterns

Add `POS(0)` at the start of each fallback pattern so it must match at the beginning of `TXT`:

```sno
KASN    POS(0) BREAK(' =') . VN SPAN(' =') SPAN('0123456789') . NUM   :F(BAD)
...
KPRTV-line:   TXT 'PRINT *' SPAN(' ,') POS(N) BREAK(' ') . VN  :S(KPRTV)
```

(For KPRT's fallback, `TXT 'PRINT *' SPAN(' ,')` already anchors via the literal prefix; the remaining issue is just that `BREAK(' ')` captures `A(3)`. Adding an identifier validation on `VN` covers that.)

### Option B: validate `VN` is an identifier

After capture, assert `VN` matches `[A-Z][A-Z0-9]*` (FORTRAN identifier shape). If not, fall through to BAD:

```sno
        VN POS(0) ANY('ABCDEFGHIJKLMNOPQRSTUVWXYZ') SPAN('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789') RPOS(0)  :F(BAD)
```

I'd lean toward **A + the VN identifier check**, but either alone is sufficient to surface a `; WARN:` for the three failing demos.

## Verification

Add fixtures to `snobol4/tests/emit_asm/` (or wherever you already gate emit_asm regressions):

```
input:  PRINT *, A(3)         expect: ; WARN: malformed input: ... text=PRINT *, A(3)
input:  A(I) = I * 10         expect: ; WARN: malformed input: ... text=A(I) = I * 10
input:  I = I + 1             expect: ; WARN: malformed input: ... text=I = I + 1
input:  PRINT *, X            expect: la r0,_V_X / lw r0,0(r0) / ...   (still works)
input:  X = 42                expect: la r0,42 / la r1,_V_X / sw       (still works)
input:  PRINT *, 42           expect: la r0,42 / push / _putint / ...  (still works)
```

End-to-end: `scripts/fortran examples/array1.f` and `scripts/fortran examples/goto1.f` should emit *some* `; WARN:` lines for the unsupported statements; the rest of the program (boilerplate, PROGRAM/STOP/END) should still assemble cleanly. The programs themselves still won't *run correctly* until `DO` / `IF_GOTO` / array indexing / integer-expression `ASSIGN` are implemented, but they'll fail honestly instead of with `label must be on its own line`.

`scripts/fortran examples/hello.f`, `print-int.f`, `print-var.f` should be unaffected.

## What goes in this PR

1. The pattern-anchoring or VN-validation change in `snobol4/src/emit_asm.sno`.
2. Six regression fixtures (three "should WARN", three "should still compile") under `snobol4/tests/emit_asm/`.
3. README / docs note if you've been tracking known-limitations.

## What does NOT go in this PR

- Actually emitting code for `DO`, `IF_GOTO`, `ASSIGN`-with-expression, array indexing, or `DIMENSION`. Those are their own milestone sagas.
- Any classify.sno or normalize.sno changes (the bug is purely in emit_asm.sno).

## Why this matters

Without this, `array1.f` / `goto1.f` / `sum10.f` look broken at the *assembler* layer rather than the *compiler* layer. dwftn's web demo currently surfaces "Line N: label must be on its own line" from cor24-asm, which is confusing &mdash; users think the assembler is broken when really emit_asm.sno just doesn't yet handle the statement kind. With this fix, each unsupported statement turns into a clean `; WARN: malformed input: ... text=...` line that dwftn surfaces as a friendly compile-stage error naming the specific statement.

## When done

`dg-mark-pr` &rarr; signal mike. After relay, dwftn refreshes `assets/emit_asm.sno` and rebakes `pages/` (small `pr/refresh-emit-asm-anchoring` saga in `web-sw-cor24-fortran`).
