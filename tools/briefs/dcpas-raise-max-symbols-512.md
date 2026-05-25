# Brief: Raise MAX_SYMBOLS from 256 to 512

**Owner:** dcpas
**Repo:** `sw-cor24-pascal`

## Context

The Pascal compiler's symbol table is hard-coded at `MAX_SYMBOLS=256`
(`compiler/src/parser.h` line 22). The OCaml interpreter (`sw-cor24-ocaml`)
has grown to 246+ identifiers and overflows the table at line 160 of
`ocaml.pas` during compilation, producing:

```
error line 160: too many symbols (got IDENT)
; COMPILE ERROR
```

This blocks building the `ocaml24` shared toolchain wrapper. Other large
Pascal programs (BASIC is currently ~190 symbols) will hit this limit as
they grow.

## Required change

In `compiler/src/parser.h`, change:

```c
#define MAX_SYMBOLS 256
```

to:

```c
#define MAX_SYMBOLS 512
```

Then rebuild `p24p.s`:

```bash
just build    # or: tc24r compiler/src/main.c -o compiler/p24p.s
```

## Acceptance criteria

- `MAX_SYMBOLS` is 512
- `p24p.s` is regenerated and committed
- Existing Pascal compilation tests still pass
- OCaml interpreter (`ocaml.pas`, ~2700 lines, 246+ symbols) compiles
  successfully with the updated p24p

## Migration note: cor24-run is deprecated

The `cor24-run` binary is from the deprecated `cor24-rs` repo. It is
being replaced by two separate tools already available on PATH:

- `cor24-asm` — cross-assembler (from `sw-cor24-x-assembler`)
- `cor24-emu` — emulator/runtime (from `sw-cor24-emulator`)

Migration commands:

```
cor24-run --assemble input.s output.bin listing.lst
  → cor24-asm input.s --bin output.bin --listing listing.lst

cor24-run --load-binary file@addr --entry 0
  → cor24-emu --load-binary file@addr --entry 0

cor24-run --run input.s [opts]
  → cor24-asm input.s --bin /tmp/prog.bin && cor24-emu --load-binary /tmp/prog.bin@0 --entry 0 [opts]
```

Any scripts in this repo that reference `cor24-run` should be updated
to use `cor24-asm` + `cor24-emu` instead. `cor24-run` will be removed
from the shared toolchain once all repos have migrated.

See: `/disk1/github/softwarewrighter/devgroup/docs/deprecate-cor24-rs.md`
