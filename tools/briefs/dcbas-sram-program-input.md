# Brief: BASIC interpreter SRAM-based program input

**Owner:** dcbas
**Repo:** `sw-cor24-basic`
**Requested by:** dcstk (sw-cor24-smalltalk)

## Context

The Smalltalk compiler (`stc.bas`) + VM (`vm.bas`) together are ~21KB of
BASIC source. The emulator's UART RX buffer is ~512 bytes (hardware FIFO,
not a bulk-transfer mechanism). Programs larger than ~500 bytes cannot be
loaded via UART.

The COR24 architecture convention for large file loading is:
- Large files (program source) → loaded into SRAM via `--load-binary file@address`
- UART → small interactive input only (RUN, BYE, REPL keystrokes)

The Smalltalk repo already loads its `.st` source at 0x080000 and reads it
via `PEEK(524288+offset)`. The BASIC interpreter itself needs the same
treatment for its `.bas` program source at a different address.

## Required behavior

1. At startup, `read_line` reads characters from SRAM starting at address
   0x040000 (262144 decimal).
2. Characters are read one per word via `peek(addr)`, advancing a pointer.
3. When a NUL byte (0) is encountered, source is exhausted. `read_line`
   switches permanently to UART mode for all subsequent input (interactive
   RUN command, INPUT statements, REPL).
4. The switch is one-way: once UART mode is entered, it stays there.

## Emulator invocation (downstream usage by dcstk)

```
pvm24 basic.p24 \
  --load-binary "program.bas@0x040000" \
  -u $'RUN\nBYE\n\x04'
```

Or the full manual invocation:
```
cor24-run --load-binary pvm.bin@0 \
  --load-binary basic.p24@0x010000 \
  --load-binary "program.bas@0x040000" \
  --patch "code_ptr=0x010000" \
  --entry 0 --speed 0 \
  -u $'RUN\nBYE\n\x04'
```

The `.bas` text at 0x040000 is consumed during line-entry mode (before RUN).
The tiny UART payload triggers execution after the program is loaded.

## Minimal change (src/basic.pas)

Add two globals:
```pascal
var sp:integer; sm:integer;
{ sp = source pointer (SRAM address), sm = source mode (1=SRAM, 0=UART) }
```

Initialize in main:
```pascal
sp := 262144; { 0x040000 }
sm := 1;
```

In `read_line` (line ~211), replace the `readln(c)` calls:
```pascal
if sm=1 then begin c:=peek(sp); sp:=sp+1;
  if c=0 then begin sm:=0; c:=10 end
end else readln(c)
```

## Acceptance criteria

- `read_line` reads program text from PEEK(0x040000+) at startup
- When NUL is hit, switches to UART for interactive input
- Existing tests still pass (when SRAM region is zeros, sm switches
  immediately on first read → existing UART behavior preserved)
- A BASIC program loaded at 0x040000 via `--load-binary` executes
  correctly when followed by `RUN\nBYE\n` via UART
- Rebuild `basic.p24` and install to shared toolchain after merge

## Notes

- The source address (0x040000) can be hardcoded for now.
- For test compatibility: if PEEK(0x040000) returns 0 on first read,
  sm switches to UART immediately — zero behavioral change for existing
  test harness.
- This unblocks the Smalltalk BASIC-hosted compiler which replaces the
  awk-based toolchain entirely.
