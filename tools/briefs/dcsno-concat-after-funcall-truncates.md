# Brief: SNOBOL4 concat drops trailing operands after a function-call result

**Owner:** dcsno
**Branch:** `pr/concat-after-funcall-truncates`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-12-inline-runtime` saga (2026-05-14).
**Affects:** the 2026-05-14 07:23 `work/lib/cor24/snobol4.lgo` build
(md5 `f7aa430c`); not present in earlier builds. Possibly a
regression from `pr/concat-truncation` itself.

## Symptom

In an expression that concatenates a function-call result with
other operands, operands *after* the function call are silently
dropped.

```sno
        OUTPUT = 'x' SIZE('hello') 'y'
```

Expected: `x5y`
Observed: `x5` (the `'y'` is gone).

The same pattern with a literal in place of the function call
works correctly, so this isn't the generic operand-count cap:

```sno
        OUTPUT = 'x' 5 'y'       ; prints x5y, correct
        OUTPUT = 'a' 5 'b' 'c'   ; prints a5bc, correct
```

The drop happens only when the operand is a function-call
expression (verified with `SIZE(...)`; likely also affects
`SUBSTR(...)`, `CHAR(...)`, `REPLACE(...)`, `IDENT(...)` as
an expression operand, etc.).

This breaks `OUTPUT = 'count=' COUNT ' size=' SIZE(S) ' end'`
style format strings, which dcftn's `normalize.sno` and
`classify.sno` both use heavily for debug / status emit.
Concretely, every `OUTPUT` in our pipeline that uses
`SIZE(...)` mid-string loses the trailing `' end'`-style
suffix, which scrambles the output in ways that look like
unrelated bugs.

## Minimal repro

```
$ cat > /tmp/c.sno << 'EOF'
        OUTPUT = 'x' SIZE('hello') 'y'
        OUTPUT = 'x' 5 'y'
        OUTPUT = 'a' SUBSTR('abc',1,1) 'b'
END
EOF

$ snobol4 --load-binary "/tmp/c.sno@0x080000" \
          --entry 0 --quiet --speed 0 -n 100000 -t 5
x5
x5y
ab
```

Expected: `x5y`, `x5y`, `aab`.
Observed: `x5`, `x5y`, `ab` (trailing `y` and `b` dropped from
the function-call lines).

## Hypothesis

`pr/concat-truncation` raised `EPSLOTS` from 8 to 16 and fixed
the >4-operand truncation case for *literal* string operands.
The function-call evaluator path may not have been audited;
likely it pushes its result onto the concat stack but
overwrites the trailing slot count or fails to update the
operand-list pointer for slots after the call.

## Fix shape

1. Audit the function-call return path in
   `src/sno_expr.plsw` (or similar) to ensure it correctly
   appends to the same concat-operand stack used by literal
   operands, and that subsequent operands continue to be
   consumed.
2. Add a test that mixes literal + funcall + literal in a
   single concat and asserts the full string.

## Tests dcsno should add (regression coverage)

The 2026-05-14 `pr/concat-truncation` test (presumably) only
exercised literal-string concat. Missing cases:

1. **Function call in concat:**

   ```sno
            OUTPUT = 'len=' SIZE('hello') ' (expect 5)'
   ```
   Expected output literally `len=5 (expect 5)`.

2. **Multiple function calls:**

   ```sno
            OUTPUT = SIZE('aa') '+' SIZE('bbb') '=5'
   ```
   Expected `2+3=5`.

3. **Function call as last operand:**

   ```sno
            OUTPUT = 'last=' CHAR(65)
   ```
   Expected `last=A`. (Likely works because there's nothing
   to drop.)

4. **Round-trip through a variable:**

   ```sno
            S = 'a' SIZE('hi') 'b' SIZE('XXX') 'c'
            OUTPUT = 'S=[' S '] size=' SIZE(S) ' expect=5'
   ```
   Expected `S=[a2b3c] size=5 expect=5`.

## When done

Push `pr/concat-after-funcall-truncates`. After mike relays,
dcftn re-verifies all 10 FTI-0 demos plus the unit tests
above.
