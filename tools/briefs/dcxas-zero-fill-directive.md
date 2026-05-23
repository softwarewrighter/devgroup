# Brief: add a zero-fill directive to `cor24-asm`

**Owner:** dcxas
**Branch:** `pr/zero-fill-directive`
**Repo:** `sw-cor24-x-assembler`
**Discovered by:** dcsno during step 2 of `snobol4-runtime-split`
(2026-05-09).
**Pairs with:** `dcpls-emit-zero-fill.md` — the PL/SW codegen change
that consumes this directive. dcxas lands first; dcpls follows.

## Symptom

`cor24-asm` today knows exactly five directives — `.byte`, `.word`,
`.text`, `.data`, `.globl`. There's no way to express "reserve N
bytes of zero" without enumerating each byte:

```asm
_buf:
    .byte 0,0,0,0, ... ,0,0,0    ; N comma-separated zeros
```

This text representation costs roughly **2 bytes of source per byte
of output** (`0,` per byte). For SNOBOL4's 64 KB string buffer
that's 131 KB of `.byte` text producing 64 KB of `.bin`; a 2x cost
in `.s` and an 8700x cost when measured against a hypothetical
`.zero 65536` directive.

## Direct blocker for dcsno

PL/SW's `EMIT_BUF_SIZE = 262144` (256 KB). SNOBOL4's `sno_main.s` is
261,638 bytes — 99.8% buffer utilisation, of which **97.7% is
`.byte 0,0,...` zero-fill text**. `sno_exec.s` is at 99.6% under the
same pressure. Any new feature that touches `snoglob.msw` (and most
do, transitively) overflows. Step 2 of `snobol4-runtime-split` (and
all subsequent steps) cannot land until the `.s` text shrinks.

I almost asked dcpls to enlarge `EMIT_BUF` to 2 MB to paper over
this. That would have moved the lid up; it would not have addressed
that `.s` files are an order of magnitude larger than they should
be. The right fix is the directive proposed here.

The shape of the bloat (one canonical example):

```
$ awk '{print length}' sno_main.s | sort -rn | head -3
131087    ← _SB:    .byte 0,0,0,...×65536
24591     ← _SRC:   .byte 0,0,0,...×12288
12303     ← one of the EPMAX-sized INT arrays (.byte ×6144)
```

## Goal

Add a directive that reserves N bytes of zero at the current
location counter without enumerating them. The exact spelling is
yours to pick; `.zero N` matches GNU as and is the spelling the
PL/SW change in `dcpls-emit-zero-fill.md` will assume by default.

## CLI / source shape

After this lands the following must assemble byte-identically to
the current spelled-out form:

```asm
_buf:
    .zero 1024              ; reserves 1024 zero bytes

; equivalent to today's:
_buf2:
    .byte 0,0,0, ... 1024 zeros ... ,0,0,0
```

The directive should be valid wherever `.byte` is valid (post
`.text` / `.data` / unspecified). N is a positive integer literal;
no expression evaluation required for v1 (constant-only is fine).

If a different spelling is preferred (`.skip N`, `.bss N`,
`.space N`), pick one and document it. The PL/SW change can adapt
in a one-line follow-up. **Whichever spelling lands, it must be
non-emit-side: it produces N bytes of output, not a hint to the
loader that N bytes will be zero at runtime.**

## Why a directive rather than a separate `.bss` segment

Two reasons:

1. The COR24 `.lgo` format is one contiguous image; there's no
   loader contract today that "this region is zero at runtime, no
   bytes in the image." Adding that is its own saga (loader change,
   image format change, link24 change). Out of scope.
2. The `.zero N` form keeps the assembled bytes identical to
   today's enumerated form. It's purely a source-density fix; the
   `.bin` and `.lgo` outputs don't change, only the `.s` shrinks.

If/when a real `.bss` segment lands later, `.zero N` could become a
hint to that loader machinery. For v1 it's just shorthand for the
spelled-out `.byte` form.

## Tests to add

1. **Byte-identical to spelled-out form** — assemble two `.s` files
   that reserve the same N zero bytes, one with `.zero N` and one
   with `.byte 0,0,...,0`, and confirm the `.bin` outputs match
   exactly.
2. **N=0** — `.zero 0` is a no-op; assembles cleanly, contributes
   no bytes.
3. **Inside `.data`** — works after `.data`, before `.byte`/`.word`.
4. **Inside `.text`** — works (or rejects with a clear error if you
   want to restrict to `.data`); document the choice.
5. **CLI smoke test** — round-trip a fixture with mixed `.zero` and
   non-zero data, verify `.lgo` and `--listing` outputs.

## What does NOT go in this PR

- No new segment / `.bss` machinery. The directive emits bytes;
  loader semantics are unchanged.
- No expression evaluation for N (constant only is fine for v1).
- No changes to `.byte`, `.word`, or any other existing directive.
- No PL/SW changes — that's the partner brief.

## When done

Push `pr/zero-fill-directive`. After mike relays + reinstalls
`cor24-asm` to `work/bin/`:

- dcpls's `pr/emit-zero-fill` (the partner brief) flips PL/SW's
  static-zero emission to use the new directive. SNOBOL4's
  `sno_main.s` drops from ~261 KB to ~7 KB.
- dcsno's `pr/sno-engine-consolidation` (currently parked
  `--reward=-1 --failure-mode=blocked-external`) restarts; the
  saga's steps 2-5 are now mechanically possible inside the
  unchanged 256 KB `EMIT_BUF`.
- Future `snoglob.msw`-touching features stop being one DCL away
  from overflow.

The pair of fixes is ~20 lines of code that unblocks several
multi-step downstream sagas. Cheap.
