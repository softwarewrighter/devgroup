# Brief: lift the SNOBOL4 source-byte cap (~12 KiB)

**Owner:** dcsno
**Branch:** start as `feat/lift-source-byte-cap`; `dg-mark-pr` when ready
**Repo:** `sw-cor24-snobol4`
**Prerequisites:** none.
**Drafted by:** dwftn (consumer of the dcftn-side workaround via `web-sw-cor24-fortran`)

## Symptom

`snobol4.lgo` silently truncates SNOBOL4 source input around byte 12280. The diagnostic emitted is `source too large, truncated at byte=<N>` (this much was fixed in `dcsno` commit `07b7a21` on 2026-05-12 alongside the STMAX 256→1024 raise). The underlying allocation cap was not lifted in that saga &mdash; it was deferred as follow-on item (1) in `tools/briefs/dcsno-static-program-size-limit.md`.

The 12280 figure is suspiciously near 12 KiB, suggesting a fixed source-buffer allocation somewhere in the interpreter / loader.

## Real-world hit

`dcftn/sw-cor24-fortran/snobol4/src/emit_asm.sno` is the FTI-0 → COR24 assembly emitter. To stay portable and dogfooded ("the SNOBOL4 emitter contains its own codegen and its own runtime helpers"), dcftn wanted to inline two runtime subroutines as `OUTPUT = '        ...'` lines:

- `_start / _halt / _putc / _puts` &mdash; the program prelude (~50 OUTPUTs).
- `_putint` &mdash; decimal integer-to-ASCII printer (~70 OUTPUTs).

With both inlined, `emit_asm.sno` is **13,176 bytes**. That trips the cap and gets truncated. With both extracted to `snobol4/runtime/{prelude,putint}.s` and post-spliced via `awk` in `scripts/fortran`, the file shrinks to **9,123 bytes** &mdash; below the cap, passes cleanly.

Downstream `web-sw-cor24-fortran` mirrors the same splice in its WASM build (`src/compiler.rs::splice_runtimes` replaces `; __RUNTIME_PRELUDE__` and `; __RUNTIME_PUTINT__` markers with the bundled runtime files). The cost is small (one 25-line function) but the architecture is forced, not chosen.

## Minimal repro

Build a SNOBOL4 source file just over 12280 bytes that contains a recognisable `OUTPUT = 'sentinel-X'` line near the end. Run it on a deterministic input; assert the sentinel appears in the output.

```bash
cat > /tmp/repro.sh << 'EOF'
#!/bin/bash
N=$1   # padding length in bytes (target file size = N + tail)
python3 -c "
n=$N
print('* padding to grow source past the cap')
for i in range(n // 12):
    print(f'        OUTPUT = \\'pad{i:05d}\\'')
print('        OUTPUT = \\'sentinel-at-end\\'')
print('END')
" > /tmp/r.sno
echo "size: $(wc -c < /tmp/r.sno)"
snobol4 --load-binary /tmp/r.sno@0x080000 \
        --entry 0 --quiet --speed 0 -n 100000000 -t 30 2>&1 \
    | grep -E "(sentinel|truncated|error)"
EOF
chmod +x /tmp/repro.sh

# Below cap: sentinel printed.
/tmp/repro.sh 11000
# size: 11015
# sentinel-at-end

# At cap: truncated, sentinel dropped, dcftn-era diagnostic.
/tmp/repro.sh 13000
# size: 13015
# source too large, truncated at byte=12280
# (no sentinel)
```

## What needs investigation

1. **Where is the source buffer allocated?** Suspects: the loader (PL/SW or wherever SNOBOL4's `--load-binary` data area maps to interpreter state), or an early-init copy from `0x080000` into a fixed-size parse buffer.
2. **Why 12280 and not 12288?** The 8-byte shortfall may be a header or a sentinel slot. Worth confirming so the new cap accounts for the same overhead.
3. **What's the right new size?** dcftn's `lower.sno` and `emit_plsw.sno` (currently 4-line stubs) will grow substantially as later milestones land. dcsno's own bootstrap source presumably stays under the cap because the interpreter itself was built that way, but downstream SNOBOL4 programs will keep pushing on this. Pick something comfortably larger &mdash; 64 KiB or 128 KiB &mdash; rather than nudge to 16 KiB.
4. **Dynamic allocation?** If the source buffer is a fixed `byte[N]`, consider growing it on demand. Same advice as for STMAX in the earlier saga.

## What goes in this PR

1. Raise the source-byte cap (`SOURCE_MAX` or whatever the constant is called) significantly &mdash; aim for at least 64 KiB.
2. Keep the `source too large, truncated at byte=<N>` diagnostic for genuine overflows; just make the threshold high enough that real-world FTI-0 compiler passes don't hit it.
3. Regression test: scale the repro above through several N points (8 KiB, 16 KiB, 32 KiB, 64 KiB) and assert the sentinel always appears.

## What does NOT go in this PR

- The other dcsno follow-on items from the earlier brief (ANY pattern, concat-expression truncation, pattern-result truncation). Those are independent bugs with their own workarounds; separate sagas.
- Any STMAX changes &mdash; the statement-count cap was lifted in `07b7a21`.

## Why this matters downstream

When this lands and is verified, dcftn will inline `_putint` + the prelude back into `emit_asm.sno` (the dogfooded form, no `awk` splice in `scripts/fortran`). `dwftn/web-sw-cor24-fortran` then drops `assets/prelude.s`, `assets/putint.s`, and the `splice_runtimes` function in a small follow-up saga. Same end-user output; cleaner architecture; one less moving part in the chain.

It also unblocks dcftn's later milestones (`lower.sno`, `emit_plsw.sno`, optimization passes) which are currently 4-line stubs but will easily exceed 12 KiB once real.

## Verification

```bash
# Before: source > 12280 bytes truncates
echo "before:"
/tmp/repro.sh 13000

# After: same input runs to completion, sentinel appears
echo "after:"
/tmp/repro.sh 13000   # sentinel-at-end
/tmp/repro.sh 30000   # sentinel-at-end
/tmp/repro.sh 60000   # sentinel-at-end

# Existing STMAX regression still green
# (run whatever dcsno added in commit 07b7a21)
```

End-to-end downstream check (optional, once dcftn rebakes):
- `dcftn` reverts the `awk` splice and inlines runtime helpers; `scripts/fortran examples/hello.f` still emits identical assembly byte-for-byte vs the current spliced output.
- `dwftn` deletes `splice_runtimes` + the two `assets/*.s` files; rebakes `pages/`.

## Credit / history

- **dcftn** discovered both the statement-count cap (m4-print-int saga, 2026-05-12) and the source-byte cap (same saga, follow-on). Wrote `tools/briefs/dcsno-static-program-size-limit.md`. Verified the STMAX fix landed in `07b7a21`; explicitly tabled the source-byte cap as item (1) of the verification follow-ons.
- **dwftn** has the matching splice on the WASM side (`src/compiler.rs::splice_runtimes`); will retire it once dcsno lifts the cap and dcftn re-inlines.
