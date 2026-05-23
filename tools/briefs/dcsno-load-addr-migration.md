# Brief: SNOBOL4 load-address migration -- IMMEDIATE unblock for dcftn

**Owner:** dcsno (this is dcsno explaining a breaking change we
shipped, with the migration path; not asking dcsno to do anything
new -- the patch is already on dev at commit TBD-of-this-saga).
**Target:** dcftn, dwftn, mike (any consumer of `snobol4.lgo`).
**Status:** This explains the 0-byte symptom in
`dcftn-classify-empty-on-new-snobol4.md` *before* dcftn tries to
bisect. The classify break is a real downstream-adapt issue, but
the 0-byte normalize/classify output mike + dcftn see is solely
due to a load-address change in the snobol4 binary.

## TL;DR

**OLD invocation** (worked against pre-2026-05-14 snobol4):

```
snobol4 --load-binary program.sno@0x080000 \
        --load-binary data.dat@0x090000 \
        --entry 0 ...
```

**NEW invocation** (required for snobol4 >= main `c675774`):

```
snobol4 --load-binary program.sno@0xE0000 \
        --load-binary data.dat@0xF0000 \
        --entry 0 ...
```

If you keep using `@0x080000` against the new binary, the source
file overwrites interpreter state at offset 0x80000-0x9D000 (the
binary is now 631 KB) and the program emits 0 bytes and halts
after ~408 instructions. That is *not* a regression in normalize
or classify -- it's the source never reaching the parser.

## Why the addresses changed

The cap-raise saga (`pr/cap-and-pattern-fixes`, four briefs from
the `dcsno-static-program-size-limit` follow-on set) bumped four
static caps:

- `SRC_SIZE` 12 KiB -> 64 KiB
- `STMAX` 256 -> 1024 (was already in the prior saga)
- `EPSLOTS` 8 -> 32 (over two steps)
- `PSTK_DEPTH` 16 -> 32

Cumulatively, the four `EPMAX`-sized INT arrays (EP_TYP, EP_VAL,
PP_TYP, PP_VAL) grew from 8 KiB each to 32 KiB each: +96 KiB.
Plus various per-statement arrays. The binary went 168 KB -> 631
KB.

The historical load addresses `0x80000` (524288) and `0x90000`
(589824) sat *inside* the new binary footprint. Loading source
or data at those addresses overwrote live interpreter state.
That caused the rawinput-eof-garbage and rawinput-mid-file-halt
regressions reported by dcftn on 2026-05-14, plus the 0-byte
classify symptom mike hit when installing.

`pr/more-fixes` (commit `63d20fb`) moved the addresses to
`0xE0000` / `0xF0000` and added a build-time assertion that
rejects binaries whose footprint reaches `SRC_LOAD_ADDR`. The
SAMA scripts in `sw-cor24-snobol4` itself
(`scripts/run-snobol4.sh`, `scripts/demo-hello.sh`, etc.) were
updated in lockstep. **The PATH wrapper
`work/bin/snobol4` does NOT hardcode an address** -- it just
passes args through to `cor24-emu`. So any external consumer
hardcoding `0x080000` in their own invocations needs to migrate.

## Migration

**Recommended (clean):** update your invocations to use
`@0xE0000` and `@0xF0000` directly. Example for dcftn:

```sh
# scripts/fortran (or wherever the snobol4 invocations live):
snobol4 --load-binary "$VENDOR/snobol4/src/normalize.sno@0xE0000" \
        --load-binary "$FORTRAN_SRC@0xF0000" \
        --entry 0 --quiet --speed 0 -n 100000000 -t 60
```

Per-phase pipeline (the form mike's brief uses for bisecting):

```sh
# Phase 1 -- normalize
snobol4 --load-binary "$VENDOR/snobol4/src/normalize.sno@0xE0000" \
        --load-binary "$HELLO@0xF0000" \
        --entry 0 --quiet --speed 0 -n 100000000 -t 60 \
        > $W/normalized.txt

# Phase 2 -- classify  (feed phase-1 output as data)
snobol4 --load-binary "$VENDOR/snobol4/src/classify.sno@0xE0000" \
        --load-binary "$W/normalized.txt@0xF0000" \
        --entry 0 --quiet --speed 0 -n 100000000 -t 60 \
        > $W/classified.txt
```

The migration is a literal sed: `0x080000` -> `0xE0000`,
`0x090000` -> `0xF0000` (or `524288` -> `917504`, `589824` ->
`983040` if you used decimal).

**Stopgap (if you have many invocations):** the dcsno repo also
ships `scripts/snobol4-compat.sh` which translates the legacy
addresses on the fly. Drop it in place of the PATH `snobol4`
wrapper and old scripts work unchanged. Mike can install:

```sh
install -m 0755 scripts/snobol4-compat.sh \
    /disk1/.../work/bin/snobol4
```

The compat wrapper is intended as a transition aid; the
recommendation is to migrate invocations to the canonical
addresses and retire the wrapper.

## Verifying the migration works

Round-trip on hello.f after re-installing the new snobol4.lgo
and using the new addresses (this was 0 bytes via the old
invocation, succeeds via the new one):

```sh
$ VENDOR=/disk1/.../work/lib/cor24/fortran
$ HELLO=/disk1/.../sw-cor24-fortran/examples/hello.f
$ snobol4 --load-binary "$VENDOR/snobol4/src/normalize.sno@0xE0000" \
          --load-binary "$HELLO@0xF0000" \
          --entry 0 --quiet --speed 0 -n 200000000 -t 120
stmt1 line=2 label= text=PROGRAM HELLO
stmt2 line=3 label= text=PRINT *, 'Hello, World!'
stmt3 line=4 label= text=STOP
stmt4 line=5 label= text=END
```

Verified against `sw-cor24-snobol4` dev `b52453d` (= main
`a1825d4` after `pr/more-fixes`).

## Once the migration lands

After dcftn re-runs their pipeline with the corrected addresses:

- The 0-byte normalize report goes away (normalize works fine
  under the new snobol4 with the new addresses).
- If classify still emits 0 bytes, *that* is the real
  pattern-semantics regression
  (`dcftn-classify-empty-on-new-snobol4`); bisect against
  `pr/cap-and-pattern-fixes` commits as described in that brief.
- mike re-installs the new snobol4.{lgo,bin} once dcftn's
  per-phase pipeline is green.

## Future-proofing (out of scope, but flagged)

The cleaner long-term fix is to either:

1. Move all dynamic state (the EPMAX/STMAX-sized arrays, SB,
   AM_CODE) into a heap-managed region above the binary, so the
   binary stays small and load addresses can stay fixed across
   cap changes; or
2. Have snobol4 emit a small "ABI header" listing its expected
   load addresses, and have the wrapper read that header.

Both are separate sagas. The migration above is the immediate fix.

## Credit

The miss is on dcsno's side -- the load-address change went out
in `pr/more-fixes` without a brief or doc update warning
consumers. This brief closes that gap.
