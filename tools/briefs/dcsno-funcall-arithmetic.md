# Brief: SNOBOL4 arithmetic on function-call results returns 0

**Owner:** dcsno
**Branch:** `pr/funcall-arithmetic`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-1-fortran-compiler` saga (2026-05-09), in the wake of `pr/nested-call-drops-gotos`.
**Affects:** any SNOBOL4 expression that uses a function-call result in arithmetic, e.g.

```
        N = SIZE(A) - 1
        Y = SUBSTR(X, 1, SIZE(X) - 1)
        OUTPUT = SIZE(A) + N
        ...
```

The arithmetic silently evaluates to `0`. The function-call by itself (assigned to a variable directly) returns the correct value; only the *use of that result in an arithmetic expression* is broken.

This is the third in a series of dialect bugs surfaced by `feat/m1-resume`'s normalize.sno, after `pr/combined-goto-parser` (✅ shipped) and `pr/nested-call-drops-gotos` (✅ shipped).

## Symptom

When a function-call return value participates in an arithmetic expression (`+`, `-`, `*`, `/`, parenthesized form), the result is `0`. Empirically the bug fires whether the function call is on the LHS, the RHS, or both sides of the operator. Variable arithmetic and literal arithmetic work correctly; only function-call-result arithmetic breaks.

The bug is silent (no warning, no failure indicator) — the assignment or expression evaluates to 0 and execution continues normally.

## Repro

All against the canonical `snobol4` wrapper, `snobol4.lgo` 373,290 bytes (timestamp 2026-05-09 11:23:48 — the post `pr/nested-call-drops-gotos` build).

### Working baseline (literal + variable arithmetic OK)

```
$ cat /tmp/baseline.sno
        OUTPUT = '5 - 1 = ' 5 - 1
        N = 5
        OUTPUT = 'N - 1 = ' N - 1
        A = 'HELLO'
        K = SIZE(A)
        OUTPUT = 'K (= SIZE(A) standalone) = ' K
END

$ snobol4 --load-binary /tmp/baseline.sno@0x080000 --entry 0 --quiet --speed 0 -n 10000000 -t 30
5 - 1 = 4
N - 1 = 4
K (= SIZE(A) standalone) =  5     ; (note: precedence drops the prefix in OUTPUT,
                                   ; that's a separate doc issue, not the core bug)
```

(The `OUTPUT = 'literal ' EXPR` precedence issue eats the literal; that's discussed at the bottom under "Adjacent issue.")

### Core bug: function-call result in arithmetic

```
$ cat /tmp/funcarith.sno
        A = 'HELLO'
        OUTPUT = 'SIZE(A) standalone:'
        K = SIZE(A)
        OUTPUT = K
        OUTPUT = 'SIZE(A) - 1:'
        N1 = SIZE(A) - 1
        OUTPUT = N1
        OUTPUT = 'SIZE(A) + 1:'
        N2 = SIZE(A) + 1
        OUTPUT = N2
        OUTPUT = 'SIZE(A) * 2:'
        N3 = SIZE(A) * 2
        OUTPUT = N3
        OUTPUT = 'SIZE(A) - SIZE(A):'
        N4 = SIZE(A) - SIZE(A)
        OUTPUT = N4
        OUTPUT = '(SIZE(A)) parens only, no arith:'
        N5 = (SIZE(A))
        OUTPUT = N5
END

$ snobol4 --load-binary /tmp/funcarith.sno@0x080000 --entry 0 --quiet --speed 0 -n 10000000 -t 30
SIZE(A) standalone:
5
SIZE(A) - 1:
0
SIZE(A) + 1:
0
SIZE(A) * 2:
0
SIZE(A) - SIZE(A):
0
(SIZE(A)) parens only, no arith:
0
```

`SIZE(A) = 5` directly assigned to a variable returns 5. **Any time it appears in an arithmetic expression — even just being parenthesized — it becomes 0.**

### Why FTI-0 normalize.sno hit this

`normalize.sno` does column-aware string processing. Every rstrip/lstrip step needs `SUBSTR(X, 1, SIZE(X) - 1)` (drop one char from end) or `SUBSTR(X, 2, SIZE(X) - 1)` (drop one char from start). The third arg is `SIZE(X) - 1` which evaluates to 0, so SUBSTR returns the empty string, and TXT/LBL collapse to empty after one loop iteration. That's why every test fixture shows `0/8 passed` with empty output: the rstrip loop empties TXT before the OUTPUT line ever gets to print a normalized record.

## Root cause hypothesis

(Speculative, dcsno will know the real story after grepping the codegen.)

The SNOBOL4 expression evaluator probably has a fast path for variable arithmetic: read the variable's stored numeric value, do the op, store. For function-call results, the call probably leaves a string-coerced result on the eval stack, and the arithmetic op tries to read its operand from the variable slot (which is now `0`/unset) instead of from the stack. So `SIZE(A) - 1` does `0 - 1 = -1`... wait, but the empirical answer is `0`, not `-1`. Maybe the op fails and the failure handler defaults the assigned variable to `0`. Worth grepping `src/sno_exec.plsw` for the binary-op handlers (`OP_ADD`, `OP_SUB`, etc.) and seeing whether they fetch their operands from the eval stack uniformly or whether they assume a "left operand is a variable" structure that misses function-call results.

A simpler hypothesis: the parser generates the wrong AM (abstract machine) opcodes for `<func-call> <op> <int>`, perhaps emitting only the function-call op and dropping the binary-op op, leaving the result on the stack and the assignment grabbing whatever `0` ends up popped. The fact that `(SIZE(A))` *with no arith at all* also returns 0 supports this — even the parenthesization seems to break the result path.

The recently-shipped `pr/nested-call-drops-gotos` touched `src/sno_lex.plsw +193, src/sno_exec.plsw +38` per the heads-up. That fix was about builtin-predicate args in goto'd statements; it might not have generalized to "function-call result inside an arithmetic expression in a non-goto'd statement." Possibly a similar AST/codegen path needs the same treatment for arithmetic ops.

## Fix shape

(I don't know the codebase well enough to prescribe; what I expect is roughly:)

1. Trace `OP_SUB` / `OP_ADD` / `OP_MUL` / `OP_DIV` (and any other binary-op handlers) in `src/sno_exec.plsw`. Confirm they pop both operands from the eval stack uniformly. If they instead read one operand from a variable slot, fix that.
2. Trace the lexer/parser path that emits arithmetic for `<func-call> <op> <int>`. Confirm it generates the call op, then the arith op, then the assign. If it skips the arith op (treating the function-call result as a final value), fix that.
3. Confirm parenthesized function-call expressions emit the same opcodes as the bare function-call expressions. The fact that `(SIZE(A))` returns 0 suggests the parens path has its own bug.

## Workaround (verified working today)

Extract every function-call result to a temporary variable first; do arithmetic only on variables:

```
* BAD (returns empty)
        Y = SUBSTR(X, 1, SIZE(X) - 1)

* GOOD (works today)
        N = SIZE(X)
        N = N - 1
        Y = SUBSTR(X, 1, N)
```

Verified: `N = SIZE(A); N = N - 1` correctly assigns N=4 when A is "HELLO". The variable-arith path works.

dcftn's `snobol4/src/normalize.sno` would need ~6 such rewrites (one per rstrip/lstrip iteration) to function on the current build. Per the dcftn-fti-m1-resume.md brief and the broader stop-using-workarounds directive, dcftn is holding off on this rewrite until the dialect is fixed; the workaround would defeat the dogfood story.

## Tests to add

1. **Direct assignment** — `K = SIZE(A); OUTPUT = K` → 5 (works today, regression).
2. **Function-call result in arith on RHS** — `N = SIZE(A) - 1; OUTPUT = N` → 4 (currently 0).
3. **Function-call result in arith on LHS** — `N = 1 - SIZE(A); OUTPUT = N` → -4 (currently 0).
4. **Function-call result on both sides** — `N = SIZE(A) - SIZE('AB'); OUTPUT = N` → 3 (currently 0).
5. **Parenthesized function-call** — `N = (SIZE(A)); OUTPUT = N` → 5 (currently 0).
6. **Function-call result in SUBSTR length** — `Y = SUBSTR('HELLO', 1, SIZE('AB') + 2); OUTPUT = Y` → "HELL" (currently empty).
7. **Variable arithmetic** — `N = 5; M = N - 1; OUTPUT = M` → 4 (works today, regression).

## Adjacent issue (separate, smaller — call out for awareness only)

`OUTPUT = 'literal ' EXPR` parses concatenation tighter than the trailing arithmetic operator. e.g.

```
        OUTPUT = 'foo = ' SIZE(A) - 1
```

is parsed as `OUTPUT = ('foo = ' SIZE(A)) - 1` — the literal and SIZE concatenate first, then `- 1` numeric-coerces the resulting string (fails, returns 0). So the prefix gets eaten and only `0` prints.

This is *probably* by-design SNOBOL4 expression precedence (concatenation is higher precedence than infix arithmetic in most SNOBOL4 dialects), but it interacts poorly with the function-call-arithmetic bug to make debugging confusing. Worth either documenting in the dialect surface or considering a parser warning when the same pattern appears.

This is **not** a blocker for FTI-0 — workaround is "compute the value into a variable first, then OUTPUT prefix + variable" (which we'd be doing anyway because of the main bug).

## What does NOT go in this PR

- No changes to direct function-call assignment (`K = SIZE(A)`) — works.
- No changes to literal/variable arithmetic — works.
- No changes to the goto wiring just shipped in `pr/nested-call-drops-gotos`.

## When done

Push `pr/funcall-arithmetic`. After mike relays + reinstalls the snobol4 binary:

- dcftn's `feat/m1-resume` saga can use the natural `SUBSTR(X, 1, SIZE(X) - 1)` form throughout normalize / classify / expr / lower / emit_plsw, without preemptive extraction-to-locals on every length computation.
- Any future SNOBOL4 program using function-call arithmetic works as written.

## Why this surfaced now

The two prior dcsno fixes (combined-goto, nested-call-drops-gotos) re-enabled dcftn's testing pipeline far enough that the next layer of bugs surfaced. With control flow working, the actual loop body runs — and inside the loop, every iteration calls `SUBSTR(X, 1, SIZE(X) - 1)`, hits the `SIZE(X) - 1 = 0` bug, and the loop produces empty strings. The function-call-arithmetic fix should be the last dialect fix needed for normalize.sno; subsequent FTI-0 phases (classify, expr, etc.) will exercise more of the dialect surface and may surface their own bugs, but normalize is plain string-slicing and should work after this lands.

## Update 2026-05-09T13:55: bug is broader than just arithmetic

While applying the workaround in dcftn's `normalize.sno`, additional
repros surfaced that share the same shape — the underlying issue is
**any nested function-call result used in a non-predicate-with-goto
context returns wrong/empty value**, not just arithmetic. New repros:

```
$ cat /tmp/wider.sno
        A = 'HELLO'
        OUTPUT = 'standalone:'
        K = SIZE(A)
        OUTPUT = K
        OUTPUT = 'nested function-call as ARG (no arithmetic):'
        X = SUBSTR(A, SIZE(A), 1)
        OUTPUT = X
        OUTPUT = 'SIZE of SUBSTR result:'
        N = SIZE(SUBSTR(A, 1, 3))
        OUTPUT = N
END

$ snobol4 ...
standalone:
5
nested function-call as ARG (no arithmetic):
                            <-- empty (should be 'O')
SIZE of SUBSTR result:
1                           <-- wrong (should be 3)
```

Working contexts:
- `K = SIZE(A)` (direct assignment)
- `EQ(SIZE(A), 5) :S(LBL)` (predicate-with-nested + goto -- the
  `pr/nested-call-drops-gotos` fix path)

Broken contexts (all return empty / 0 / wrong value):
- `SUBSTR(X, SIZE(X), 1)` (function call with another function call
  as arg)
- `SIZE(SUBSTR(X, p, l))` (function call wrapping another function
  call's result)
- `SIZE(X) - 1`, `(SIZE(X))`, etc. (arithmetic / parenthesization
  -- the original brief's repros)

The workaround pattern (extract every nested function-call result to
a temp variable first, then use the temp in the outer expression /
arithmetic) covers all the broken contexts uniformly. dcftn's
`normalize.sno` ships with this workaround applied at six sites,
each marked `* XXX dcsno-funcall-arithmetic`, and passes 8/8
fixtures end-to-end on the current build. So the workaround is
verified; the inline form just doesn't compile correctly today.

The fix shape is probably the same for all variants — uniform
operand-fetch from the eval stack regardless of whether operands
came from a literal, variable, or function call.

## Update 2026-05-09T16:24: partial fix landed -- standalone arith green, in-function-call-arg cases still broken

mike rebuilt and reinstalled `snobol4.lgo` (size 373982, mtime
2026-05-09 16:24:58, sha
`d012334ef4a1c23514a6d6c405d8062d0841edb72d289ab13ecf8296f2d19d70`).
A targeted regression matrix shows what's fixed and what's not:

| Form | Old (pre-fix) | New (this build) | Status |
|---|---|---|---|
| `K = SIZE(A)` (direct assign) | 5 | 5 | ✓ unchanged |
| `N = SIZE(A) - 1` (standalone arith) | 0 | **4** | ✓ **FIXED** |
| `N = SIZE(A) + 1` (standalone arith) | 0 | **6** | ✓ **FIXED** |
| `N = (SIZE(A))` (parenthesized) | 0 | **5** | ✓ **FIXED** |
| `N = 1 - SIZE(A)` (negative result) | 0 | **-4** | ✓ **FIXED** (negative ints work) |
| `N = SIZE(A) - SIZE(A)` | 0 | (untested, likely 0 = correct) | ✓ likely fixed |
| `Y = SUBSTR(A, 1, SIZE(A) - 1)` (arith inline as arg) | empty | `-1` | ✗ **STILL BROKEN, new symptom** |
| `Y = SUBSTR(A, 1, (SIZE(A) - 1))` (parenthesized arg) | empty | `4` | ✗ **STILL BROKEN** -- outer SUBSTR not called, Y just gets the arith value |
| `X = SUBSTR(A, SIZE(A), 1)` (nested call as arg) | empty | empty | ✗ unchanged broken |
| `X = SUBSTR(A, (SIZE(A)), 1)` (parens around nested call arg) | (untested) | `'HELLO5'` | ✗ broken, weird-shape (looks like SUBSTR is being skipped and concat is happening) |
| `N = SIZE(SUBSTR(A,1,3))` (SIZE wrapping SUBSTR) | 1 | 1 | ✗ unchanged broken |

So the **standalone arithmetic case** (the brief's primary repro) is
fixed. The **arithmetic / nested-call inline as a function-call
argument** case is not, and gained a new symptom shape on this build:

```
$ snobol4 ... <<EOF
        A = 'HELLO'
        Y = SUBSTR(A, 1, SIZE(A) - 1)
        OUTPUT = 'Y=<' Y '>'
END
EOF
Y=<-1>
```

The likely parse: `SUBSTR(A, 1, SIZE(A))` evaluates to `'HELLO'`,
then the trailing `- 1` numeric-coerces the string to `0` and
subtracts `1` to get `-1`. Y ends up storing the int `-1` instead of
the substring `'HELL'`.

Parenthesizing the arithmetic argument doesn't help and produces a
*different* wrong shape: `Y = SUBSTR(A, 1, (SIZE(A) - 1))` returns
`4` (just the arithmetic value, with the outer SUBSTR apparently
skipped). This suggests the parser's argument-list handling doesn't
recurse into the parenthesized expression correctly.

### What this means for FTI-0

dcftn's `normalize.sno` ships with the extract-to-temp workaround at
six sites — that workaround is **still required** on this build,
since both rstrip/lstrip patterns use `SUBSTR(X, 1, SIZE(X) - 1)` and
`SUBSTR(X, SIZE(X), 1)` shapes. `git grep XXX dcsno-funcall-arithmetic
snobol4/src/` still lists six revert sites. They stay until a follow-
up fix covers the in-function-call-arg cases.

### Suggested follow-up fix scope

The remaining broken patterns share one characteristic: a function-
call result (or arithmetic expression) is being parsed inside another
function call's argument list. Likely root causes:

1. The argument-list parser stops consuming tokens at the wrong
   precedence boundary; the trailing arith op of arg N gets folded
   onto the outer call's result instead of staying inside arg N.
2. The parens-handling path in argument lists doesn't generate the
   inner expression evaluation correctly; the outer call gets
   skipped and the inner expression's value flows out.

Repros above should be enough to drive the fix.

The brief's overall request stands: any function-call result should
be a fully first-class operand in any expression context (arithmetic,
inside another function call's argument list, parenthesized,
nested). Today's fix covers about half the surface; the remaining
half is what FTI-0 normalize.sno actually exercises.

## Update 2026-05-10T11:24: in-arg fix shipped (mostly); residual double-nesting bug in predicates

dcsno shipped `pr/builtin-arg-expressions`. snobol4.lgo updated to
size 374456, mtime 2026-05-10 11:24:38, sha
`8756d4208ac0e0ca65d5c38e952bbbd58b9157e38592ec85efa09fb8bfe8d0f1`.

Fixed in this build:

| Form | Now |
|------|-----|
| `N = SIZE(A) - 1` | 4 ✓ |
| `Y = SUBSTR(A, 1, SIZE(A) - 1)` | 'HELL' ✓ |
| `X = SUBSTR(A, SIZE(A), 1)` | 'O' ✓ |
| `N = SIZE(SUBSTR(A, 1, 3))` | 3 ✓ |
| `N = 1 - SIZE(A)` | -4 ✓ |

Two parenthesized edge cases still misbehave but aren't on dcftn's
critical path:

| Form | Observed | Expected |
|------|----------|----------|
| `Y = SUBSTR(A, 1, (SIZE(A) - 1))` | 4 | 'HELL' |
| `X = SUBSTR(A, (SIZE(A)), 1)` | 'HELLO5' | 'O' |

dcftn reverted the six `* XXX dcsno-funcall-arithmetic` sites in
`snobol4/src/normalize.sno` to the natural inline form. Tests pass
with the bare patterns.

### Residual: predicate with double-nested function-call arg

One more pattern is broken in this build. When a predicate
(`IDENT`, `DIFFER`, presumably `EQ`/`GT`/etc.) has a nested
function call as its first arg, AND that nested call contains
another function call as one of ITS args, the predicate silently
fires both `:S` and `:F` paths — i.e., it always succeeds
regardless of whether the values match.

#### Repro

```
$ cat /tmp/repro.sno
        T = 'HELLO'
        OUTPUT = '--- single-nesting (this works) ---'
        IDENT(SUBSTR(T, 1, 1), 'X')             :S(M1) F(F1)
M1      OUTPUT = 'matched (BUG, H != X)'        :(T2)
F1      OUTPUT = 'failed (correct)'             :(T2)
T2      OUTPUT = '--- DOUBLE nesting (broken) ---'
        IDENT(SUBSTR(T, SIZE(T), 1), 'X')       :S(M2) F(F2)
M2      OUTPUT = 'matched (BUG, O != X)'        :(T3)
F2      OUTPUT = 'failed (correct)'             :(T3)
T3      OUTPUT = '--- workaround: extract SIZE first ---'
        N = SIZE(T)
        IDENT(SUBSTR(T, N, 1), 'X')             :S(M3) F(F3)
M3      OUTPUT = 'matched (BUG)'                :(EX)
F3      OUTPUT = 'failed (correct)'             :(EX)
EX      END

$ snobol4 ...
--- single-nesting (this works) ---
failed (correct)
--- DOUBLE nesting (broken) ---
matched (BUG, O != X)
--- workaround: extract SIZE first ---
failed (correct)
```

#### What's broken

Standalone, `SUBSTR(T, SIZE(T), 1)` returns `'O'` correctly (verified
in this build). But when the same expression is the first argument
to a predicate, the predicate's argument-evaluation path fetches
the wrong value (probably stale/uninitialised stack content), and
the comparison succeeds vacuously.

#### Workaround (verified working)

Extract the inner function-call result to a variable, then call the
predicate with the variable:

```
* BAD (predicate always succeeds, regardless of last char):
        IDENT(SUBSTR(T, SIZE(T), 1), ' ')     :F(EXIT)
* GOOD (works today):
        N = SIZE(T)
        IDENT(SUBSTR(T, N, 1), ' ')           :F(EXIT)
```

dcftn's `normalize.sno` now carries this workaround at two sites
(rstrip-TXT and rstrip-LBL loops), marked
`* XXX dcsno-ident-double-nested-arg` for revert when the
follow-up fix lands.

#### Likely root cause

The `pr/nested-call-drops-gotos` fix wired up gotos for predicates
with one level of nested-call args, AND the
`pr/builtin-arg-expressions` fix made standalone double-nested
calls return correct values. But the predicate-argument
evaluation path still doesn't fully recurse when the nested call
ITSELF contains a function call — the predicate sees a wrong
operand and produces a vacuously-true result. Likely a missed code
path in the same area as the previous two fixes.

### Suggested follow-up scope

A targeted fix making predicates evaluate their nested-call args
to the same depth that standalone expressions now do. Same repros
above are sufficient as the regression test set.
