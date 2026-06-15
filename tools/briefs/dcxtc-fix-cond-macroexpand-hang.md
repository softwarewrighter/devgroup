# Brief: fix tc24r codegen hang on deep recursion (cond/macroexpand)

**Owner:** dcxtc
**Branch:** `feat/fix-deep-recursion-hang` → `pr/...`
**Repo:** `sw-cor24-x-tinyc` (`tc24r`)
**Drafted by:** mike (coordinator)

## The bug (real, confirmed in current `tc24r`)

`tc24r`-compiled macrolisp infinite-loops when a macro recurses a few levels deep.
Minimal repro at the Lisp level:

```
(macroexpand (quote (cond ((= n 0) (quote zero)) ((= n 1) (quote one)) (t (quote other)))))
```

- **2-clause `cond` returns; 3-clause hangs** (runs to the instruction cap, never
  returns). `cond` is `(defmacro cond clauses (cond-expand clauses))` — `cond-expand`
  recurses per clause, so depth ≥3 trips it.
- Confirmed by **building `tc24r` from HEAD** (`96090c8`) — not a stale binary.
- **Long-standing, not a recent regression:** also hangs at `565a66c` (pre
  `codegen-dce`). The recent codegen commits are not the cause.
- The *working* shipping asm was produced by the **old `tml24c` compiler** (verbose,
  ~115K lines) — `tc24r` (compact, ~12K) has never compiled this pattern correctly.
- Corroboration: macrolisp already carries `b20931b "work around tc24r stack
  corruption"`, and the `format` demo (one of ~8 that hang) is in that lineage —
  i.e. this is a class of deep-call/stack codegen bugs in `tc24r`.

## Repro harness (compiler-isolated)

```bash
TC=<tc24r>; ASM=cor24-asm; EMU=cor24-emu
SRC=<sw-cor24-macrolisp@1a2d777>
$TC $SRC/src/repl-full.c -I $SRC/src -o /tmp/r.s && $ASM /tmp/r.s -o /tmp/r.lgo
echo '(macroexpand (quote (cond ((= n 0) (quote zero)) ((= n 1) (quote one)) (t (quote other)))))\n(+ 3 4)\n' \
  | $EMU --lgo /tmp/r.lgo -u "$(cat)" --speed 0 -n 100000000 --quiet
#   correct: prints the expanded `if` chain, then 7.  bug: prints `>` then nothing.
```

## What to do

1. Root-cause the codegen defect — the symptom (infinite loop only past a recursion
   depth) points at how `tc24r` emits recursive/non-leaf calls (stack frame / return
   handling / a too-aggressive branch or tail optimization). Reproduce with the
   smallest C program you can (deeply-recursive function) so it's not macrolisp-specific.
2. Fix it in `tc24r`; remove the need for macrolisp's `b20931b` workaround if the
   same root cause covers it.
3. Add the minimal C repro to the `tc24r` test suite so it can't regress.

## Impact / why it matters

This **blocks re-baselining macrolisp's asm onto `tc24r`** (`dw-rebaseline-asm-tc24r`
PR2) — and likely any language with deep recursion. It is the gating bug for moving
generated artifacts onto the new compiler. (It is **not** a cor24-rs issue — the asm
is compiler output, not a cor24-rs artifact.)

## When done

Push `pr/...`, notify mike to relay. Then dwmls can re-baseline macrolisp onto the
fixed `tc24r`.
