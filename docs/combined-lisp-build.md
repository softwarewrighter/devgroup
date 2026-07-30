# COR24 combined Scheme and Full Macro Lisp build

The `combined-suite` source builds one shared Tiny Macro Lisp interpreter with
two selectable preludes:

```text
1: Scheme REPL
2: Full Macro Lisp REPL
h: help
q: quit
```

Each selection resets the interpreter heap, garbage collector, symbol table,
string state, evaluator, and global environment before loading the selected
prelude. Ctrl-] returns to the menu while the REPL is reading input.

## Build

```sh
bash combined-suite/build.sh
```

The hardware artifact is:

```text
combined-suite/build/combined-suite.lgo
```

The build also makes `combined-suite-compact.lgo` for comparison. Do not use
the compact image for a warm replacement: it omits all-zero records and can
therefore retain stale external SRAM. The full image is deterministic.

## Size and memory

```text
binary extent:  348,990 bytes
address range:  0x000000-0x05533D
full LGO:       775,548 bytes, 9,696 lines
entry:          G000000
```

SHA-256 of the verified full LGO:

```text
64bea6c06b9a0a9340880bc10b7935c575a2cd68f47176a3092a4d63144789db
```

The full image passed emulator testing with the 3 KiB EBR stack, including
Scheme arithmetic and `reduce`, Full Macro Lisp threading and lazy sequences,
Ctrl-] return from both REPLs, and fresh re-entry.

## Hardware

Upload the full LGO through the RTS/CTS-paced `te-rs` configuration. After its
single `G000000` is accepted, choose `1` or `2` and wait for the selected
prelude to finish loading before typing an expression.

Generated `.lgo`, `.bin`, and listing files are build products and should not
be committed.
