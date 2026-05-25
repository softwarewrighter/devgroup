# Brief: Support compiling ocaml.pas (~120KB source)

**Owner:** dcpas
**Repo:** `sw-cor24-pascal`
**Requested by:** devgroup toolchain (ocaml24 wrapper)
**Depends on:** `pr/raise-max-symbols-512` (already merged)

## Context

After raising MAX_SYMBOLS to 512, the Pascal compiler (`p24p`) can now
parse the OCaml interpreter's symbol table. However, compilation still
fails: the compiler runs for ~1.26 billion instructions, outputs one
garbled source line to UART, then halts without producing `.spc` output
or a "; OK" marker.

The OCaml source (`sw-cor24-ocaml/src/ocaml.pas`) is 120KB / 2736 lines.
It uses advanced Pascal features that p24p already supports: pointers
(`^`), records, forward type declarations, `new()`, `nil`, and `exit`.

## Root cause (suspected)

The 120KB source is passed via `cor24-run --run p24p.s -u "$(cat ocaml.pas)"`.
This works for smaller programs (BASIC is ~35KB) but the emulator's UART
RX handling may have issues with 120KB payloads:

1. The emulator's internal UART RX buffer may be smaller than 120KB
2. The `-u` flag processes escape sequences (`\n`, `\x04`) across the
   full 120KB string — possibly corrupting binary data
3. p24p's `INPUT_BUF_SIZE` is 131072 (128KB) so it should fit, but the
   characters may not all arrive before the compiler starts reading

## Proposed fix path

### Option A: Use --uart-file (preferred)

`cor24-emu` supports `--uart-file <path>` which reads a file directly
into the UART RX buffer (appending EOF). The compilation pipeline would
become:

```bash
# Pre-assemble p24p.s to p24p.bin (one-time, cacheable)
cor24-asm p24p.s --bin p24p.bin

# Compile ocaml.pas using the binary + uart-file
cor24-emu --load-binary p24p.bin@0 --entry 0 --stack-kilobytes 8 \
  --uart-file ocaml.pas \
  --speed 0 -n 3000000000
```

This bypasses the shell command-line, escape processing, and UART `-u`
buffer entirely.

**Required changes:**
- Update `pas24` wrapper script to use pre-assembled p24p.bin +
  `cor24-emu --uart-file` for large sources (>64KB)
- Pre-assemble `p24p.s` → `p24p.bin` in `work/lib/pascal/` (the
  `build-shared-toolchain.sh` script already caches this)

### Option B: Investigate UART RX buffer in cor24-run

If the issue is in the emulator's UART implementation rather than shell/
arg limits, the fix belongs in `sw-cor24-emulator`:
- Ensure UART RX buffer dynamically sizes to fit the full `-u` payload
- Or document the limit and require `--uart-file` for large inputs

## Testing

Acceptance test:
```bash
# Should produce .spc output with "; OK" at the end
cor24-asm work/lib/pascal/p24p.s --bin /tmp/p24p.bin
cor24-emu --load-binary /tmp/p24p.bin@0 --entry 0 --stack-kilobytes 8 \
  --uart-file sw-cor24-ocaml/src/ocaml.pas \
  --speed 0 -n 3000000000
```

## OCaml features used (for reference)

The source uses these Pascal features (all should already work in p24p):
- `type` block with pointer types: `PPat = ^Pat`
- Record types with multiple fields
- Forward type references (recursive data structures)
- `new(ptr)` / pointer dereference `p^.field`
- `nil` assignment and comparison
- `exit` from nested procedures/functions
- `uses Hardware` (unit import)
- String constants
- Nested function calls returning pointer types

## Notes

- BASIC (35KB) compiles fine with the current `-u` approach
- The `--uart-file` path also eliminates the cor24-run dependency for
  compilation, completing the migration to cor24-asm + cor24-emu
- If Option A works, update `build-shared-toolchain.sh` to pre-build
  `p24p.bin` and use it for all Pascal compilation
