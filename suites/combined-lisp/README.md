# COR24 combined Lisp suite

This suite contains one shared Tiny Macro Lisp interpreter and two embedded
preludes behind a resident UART monitor:

- `1`: Scheme REPL
- `2`: Full Macro Lisp REPL
- Ctrl-] from either REPL: return to the monitor
- `q`: halt

Every REPL selection resets the interpreter, heap, GC, symbols, strings, and
global environment before evaluating the selected prelude.

Build with:

```sh
./build.sh
```

Use `build/combined-suite.lgo` on hardware. It is the warm-reload-safe full
image and ends with `G000000`. The compact image is generated only for size
comparison and cold-boot/emulator use; it omits all-zero records and is unsafe
if external RAM may contain an older image.
