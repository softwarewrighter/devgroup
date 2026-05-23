# Brief: emit `.zero N` for `INIT(0)` static arrays

**Owner:** dcpls
**Branch:** `pr/emit-zero-fill`
**Repo:** `sw-cor24-plsw`
**Discovered by:** dcsno during step 2 of `snobol4-runtime-split`
(2026-05-09).
**Depends on:** `dcxas-zero-fill-directive.md` — adds the `.zero N`
directive to `cor24-asm`. **dcxas must land and be reinstalled to
PATH first.** This brief assumes the directive is available.

## Symptom

`cg_emit_static_var` (in `src/codegen.h`) emits one `.byte 0` per
byte of an `INIT(0)` static array. For a `DCL X(N) BYTE` (or `DCL
X(M) INT`) declared with `INIT(0)` that's N (or M*3) comma-separated
textual zeros, costing roughly 2 bytes of `.s` per byte of `.bin`.

## Direct blocker for dcsno (and the partner brief)

`EMIT_BUF_SIZE = 262144` and SNOBOL4's `sno_main.s` is 261,638
bytes, **97.7% of which is enumerated zero-fill text** for ~75
`INIT(0)` arrays in `snoglob.msw`. The single `_SB:` array alone is
131 KB of `.byte` source for a 64 KB buffer.

The `cor24-asm` partner brief
(`dcxas-zero-fill-directive.md`) adds a `.zero N` directive that
reserves N bytes of zero without enumerating them. After this
brief lands and consumes that directive, `sno_main.s` shrinks from
~261 KB to ~7 KB — and several downstream sagas
(`snobol4-runtime-split` step 2, future `snoglob.msw`-touching
features) immediately unblock.

## Root cause (per source read on 2026-05-09)

`src/codegen.h:1271` — `cg_emit_static_data` walks top-level DCLs,
delegates each non-LIBRARY-mode DCL to `cg_emit_static_var(idx,
init_node)`. `cg_emit_static_var` (further down) inspects the init
expression and emits one `.byte`/`.word` per element. Zero is
written exactly the same way as any other constant — there's no
"all-zero" fast path.

The fix is local: when emitting a static array whose init is
"all zero" (either an explicit `INIT(0)`, a missing INIT for an
implicitly-zero `BYTE`/`INT` array, or a string init of all NULs),
use the new directive instead of enumerating.

## Goal

When the init for a top-level static array would emit only zero
bytes, emit `.zero <total_byte_size>` instead. Identical bytes go
into the `.bin`; the `.s` shrinks proportionally.

## What changes

In `src/codegen.h`:

1. In `cg_emit_static_var` (or wherever the per-element emit loop
   lives), detect the "all init values are zero" case before
   entering the loop. For arrays: check that every element is
   `INIT(0)` (or that there's no INIT, which is also zero).
2. If detected, emit a single line:
   ```c
   emit_str(EMIT_INDENT);
   emit_str(".zero ");
   emit_int(total_bytes);
   emit_nl();
   ```
3. Otherwise, fall through to the existing per-element loop. Mixed
   non-zero arrays (`INIT(1, 2, 3, 0, 0)`) keep the spelled-out
   form. Strings with non-zero chars stay spelled out.

`total_bytes` = `array_count * element_width` (1 for BYTE/CHAR, 3
for INT/PTR).

## Output spec (byte-identical)

After the change, the generated `.bin` must be byte-identical to
the previous output for the same source. Smoke test:

```sh
# Build PL/SW with the change.
# Build sno_main.plsw with old PL/SW, capture .bin.
# Build sno_main.plsw with new PL/SW, capture .bin.
# diff .bin's must be empty.
```

The `.s` will differ (compact form vs spelled-out) but the `.lst`
listing addresses + the `.bin` bytes must match.

## Tests to add

1. **Byte-identical regression** — pick `examples/chain.plsw` (or
   any reg-rs case with an `INIT(0)` array) and verify the `.bin`
   matches before/after.
2. **All-zero array** — `DCL X(100) BYTE INIT(0);` emits
   `.zero 100`, not `.byte 0,0,...×100`.
3. **All-zero INT array** — `DCL X(50) INT INIT(0);` emits
   `.zero 150` (50 elements × 3 bytes). Confirm element-width
   handling.
4. **Mixed-init array** — `DCL X(5) INT INIT(0, 1, 2, 0, 0);`
   keeps the spelled-out form (any non-zero element disables the
   fast path).
5. **No-init array** — `DCL X(100) BYTE;` with no `INIT` clause:
   should emit `.zero 100` (implicit zero for static storage).
6. **String init** — `DCL S(8) CHAR INIT('hello');` keeps the
   spelled-out form (non-zero char data).
7. **Reg-rs** — bump the SNOBOL4 cross-build smoke test (if
   present) to confirm `sno_main.s` drops to <10 KB.

## What does NOT go in this PR

- No changes to `cor24-asm` — that's the partner brief.
- No changes to non-zero init emission — `.byte 1,2,3` stays as is.
- No new directives — this brief just consumes the one dcxas adds.
- No changes to LIBRARY-mode DCL suppression — that codepath stays
  the same; this fix only affects the non-LIBRARY (entry-module)
  emission of statics.

## When done

Push `pr/emit-zero-fill`. After mike relays + reinstalls the PL/SW
compiler:

- SNOBOL4's `sno_main.s` drops from 261 KB to ~7 KB. **40× shrink.**
- `EMIT_BUF` utilisation drops from 99.8% to ~3%.
- dcsno's `pr/sno-engine-consolidation` (currently parked) restarts;
  step 2 and downstream steps of `snobol4-runtime-split` complete in
  one bounded follow-up saga.
- Any other PL/SW consumer with large `INIT(0)` arrays (Prolog when
  it lands, future Fortran runtime, etc.) gets the same shrink for
  free.

This is the load-bearing half of the pair; the dcxas directive
brief is the prerequisite.
