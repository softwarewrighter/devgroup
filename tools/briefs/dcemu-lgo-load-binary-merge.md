# Brief: cor24-emu drops `--load-binary` when `--lgo` is set

**Owner:** dcemu
**Branch:** `pr/lgo-load-binary-merge`
**Repo:** `sw-cor24-emulator`
**Discovered by:** dcsno during `pr/bootstrap-toolchain` (2026-05-08).
**Affects:** every wrapper of the shape `exec cor24-emu --lgo <foo>.lgo "$@"`
where the wrapped program loads auxiliary data via `--load-binary`. SNOBOL4
is the first concrete victim; the planned Fortran path
(`cor24-emu --lgo snobol4.lgo --uart-file fortran-compiler.sno -u "<src>"`)
is also gated on this — see the critical-path callout in
`tools/briefs/README.md`.

## Symptom

When `--lgo <file>` is passed to `cor24-emu`, any `--load-binary` flags on
the same command line are silently parsed and then silently dropped. The
emulator runs with only the `.lgo` image loaded; the auxiliary data the
caller asked for never reaches memory.

The CLI documentation (both `cor24-emu --help` and the in-source examples)
explicitly advertises the combined form, so the current behaviour is a
silent contract violation, not a documented restriction:

```
$ cor24-emu --help
...
  cor24-emu --lgo pvm.lgo --load-binary hello.p24@0x010000 --terminal
  cor24-emu --load-binary pvm.bin@0 --load-binary hello.p24@0x010000 \
                  --entry 0 --terminal
```

## Repro (uses today's PATH-installed tooling, no SNOBOL4 dependency)

```bash
# 1. Take any pre-built .lgo and any auxiliary file.
LGO=/disk1/github/softwarewrighter/devgroup/work/lib/cor24/plsw.lgo
echo -n ABCD > /tmp/aux.bin

# 2. Run with both flags. Note the missing "Loaded ... at 0x080000" line.
cor24-emu --lgo "$LGO" --load-binary /tmp/aux.bin@0x080000 \
          -n 100 --speed 0 2>&1 | head -3
```

Observed:

```
Loaded 1036760 bytes from /disk1/.../plsw.lgo
[UART TX @ 100] 'P'  (0x50)
[UART TX @ 100] 'L'  (0x4C)
```

Expected (and the binary-only path actually does this):

```
Loaded 1036760 bytes from /disk1/.../plsw.lgo
Loaded 4 bytes from '/tmp/aux.bin' at 0x080000
[UART TX @ 100] 'P'  (0x50)
...
```

For a stronger demonstration: SNOBOL4's interpreter reads its source from
memory at `0x080000`. Loaded via `--load-binary` it runs to completion;
loaded via `--lgo` + `--load-binary` (same image, same auxiliary file) it
halts at PC 0x000005 after 397 instructions because the auxiliary data is
absent from memory.

```bash
cd /disk1/github/softwarewrighter/devgroup/work/dcsno/github/sw-embed/sw-cor24-snobol4
just build  # produces build/snobol4.bin
bash scripts/bin-to-lgo.sh build/snobol4.bin /tmp/snobol4.lgo

# Working: --load-binary path
cor24-emu --load-binary build/snobol4.bin@0 \
          --load-binary examples/hello.sno@0x080000 \
          --entry 0 -n 200000000 -t 60 --speed 0 -q
# -> "Hello, World!" ; ~11569 instructions

# Broken: --lgo path (auxiliary file silently dropped)
cor24-emu --lgo /tmp/snobol4.lgo \
          --load-binary examples/hello.sno@0x080000 \
          --entry 0 -n 200000000 -t 60 --speed 0 -q
# -> no output ; halts after 398 instructions
```

## Root cause (per source read on 2026-05-08)

`cli/src/run.rs` parses `--load-binary` into `cli.load_binaries` for every
mode, but only the `"binary"` dispatch arm consumes that vector
(`load_binaries_and_patches(&mut emu, &cli.load_binaries, &cli.patches)`
at ~line 1503). The `"lgo"` arm (~line 1415–1490) calls `emu.load_lgo`,
sets the entry point, attaches I2C devices, and runs — but never calls
`load_binaries_and_patches`. Same story for `--patch`.

So the bug is one missing call in the `lgo` arm.

## Fix shape

In the `"lgo"` dispatch arm, after `emu.load_lgo` succeeds and before
`run_with_timing`, call:

```rust
load_binaries_and_patches(&mut emu, &cli.load_binaries, &cli.patches);
```

Same precedence as the `"binary"` arm: load the .lgo first (so its bytes
sit at the addresses the linker chose), then layer `--load-binary` writes
on top (matches the help text "Load raw bytes into memory at address" and
matches what users testing the `pvm.lgo` example would expect).

If a `--load-binary` overlaps the `.lgo` image, the latter `--load-binary`
write wins (consistent with the "patches over base image" mental model).
A warning to stderr when overlap is detected would be nice but not
required.

## Tests to add

Add to `cli/tests/`:

1. **End-to-end load test** — assemble a tiny .s that reads a byte from
   `0x080000` and emits it on UART. Build a `.lgo`. Stage a 1-byte file
   with value `0x42`. Confirm:
   - `cor24-emu --lgo prog.lgo --load-binary aux.bin@0x080000 -q` outputs
     `B` (0x42).
   - Without the `--load-binary` (auxiliary missing), the same .lgo
     outputs whatever was at 0x080000 in the .lgo image (likely 0).
2. **Multiple `--load-binary` with `--lgo`** — confirm both files land,
   addresses don't clobber each other unless they overlap.
3. **`--patch` parity** — same fix should make `--patch` work alongside
   `--lgo`; add a smoke test for that too.

## What does NOT go in this PR

- No new flags (e.g. `--no-load-binary-with-lgo`); the bug is purely a
  missing call.
- No changes to `cor24-asm` or `link24`.
- No changes to the lgo loader's address bookkeeping; the existing logic
  is fine.

## When done

Push `pr/lgo-load-binary-merge`. After mike relays + reinstalls
`cor24-emu` to `work/bin/`:

- dcsno's `pr/bootstrap-toolchain` flips a documented workaround in the
  README + justfile and the planned `cor24-emu --lgo snobol4.lgo "$@"`
  wrapper starts working.
- dcftn's `feat/fortran-hello-world` (and follow-ons) can use the SNOBOL4
  wrapper as designed.
