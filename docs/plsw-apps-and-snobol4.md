# PL/SW applications and SNOBOL4 hardware suite

## Result

`plsw-apps-and-snobol4.lgo` is a composite COR24-TB hardware image with a
resident UART monitor, two native PL/SW demonstrations, and one shared
SNOBOL4 interpreter serving three SNOBOL programs:

1. PL/SW Hello
2. PL/SW Count 1 through 10
3. SNOBOL4 ELIZA
4. SNOBOL4 interactive echo
5. SNOBOL4 palindrome

The generated LGO is intentionally ignored by Git. Reproducible sources and
the builder live in `suites/plsw-snobol4/`.

## Memory layout

| Address | Contents |
|---:|---|
| `0x000000` | Shared native SNOBOL4 interpreter |
| `0x0B0000` | Embedded ELIZA source |
| `0x0B1000` | Embedded interactive echo source |
| `0x0B2000` | Embedded palindrome source with generated menu epilogue |
| `0x0D0000` | Resident monitor and UART ISR |
| `0x0D3E00` | Monitor RX state and 256-byte ring |
| `0x0D4000` | Native PL/SW Hello |
| `0x0D8000` | Native PL/SW Count |
| `0x0E0000` | Selected SNOBOL source copied before launch |
| `0x0F0000` | SNOBOL input area; zero selects live UART input |

## Centralized UART handling

The monitor owns the UART receive interrupt vector while an application runs.
Ordinary characters are consumed by the ISR and placed in a monitor-owned
ring. Ctrl-] (`0x1D`) is consumed as monitor attention.

The COR24 UART data register cannot be inspected without consuming its byte,
so an interrupt handler cannot peek for Ctrl-] and leave ordinary input for an
unmodified polling application. Only SNOBOL4's two live-input call sites,
`READ_INPUT` and `READ_RAW_INPUT`, are redirected to the monitor input broker.
All `.sno` programs inherit that behavior without UART changes of their own.

COR24's `ir` register also constrains non-local interrupt escape:

- `jmp (ir)` is the only operation that clears the processor's
  interrupt-in-service latch.
- `la ir,address` is an absolute jump, not a write to `ir`.
- Normal instructions cannot read or rewrite `ir`.

An earlier monitor used `la ir,monitor_abort` inside the ISR. It appeared to
work once because it jumped to the menu, but it bypassed `jmp (ir)` and left
the interrupt-in-service latch set. Every later UART interrupt was suppressed.

The corrected ISR records an attention flag and returns normally through the
original `ir`. SNOBOL's monitor input broker checks the flag and jumps to the
monitor outside interrupt context.

## Finite PL/SW applications

The standalone PL/SW runtime finishes `MAIN` in:

```asm
_halt:
        bra _halt
```

The two menu applications are finite and use no UART input. The suite builder
keeps their `.plsw` sources unchanged and rewrites only their generated
standalone halt:

```asm
_halt:
        la ir,0x0D0000
```

On COR24 this is an absolute jump to the monitor. Hello and Count therefore
return automatically after printing instead of waiting in a halt loop.

## Batch SNOBOL completion

ELIZA and Echo repeatedly call `INPUT`, so Ctrl-] attention is observed by the
centralized broker. Palindrome is a batch program: after its last result it
would reach SNOBOL `END`, return from the interpreter, and enter the shared
interpreter's standalone halt loop.

`add_snobol_menu_wait.py` creates a suite-local copy of the batch program. It
routes explicit `:(END)` exits through a short final `INPUT` wait and prints:

```text
Press Ctrl-] to return to the menu.
```

The upstream `palindrome.sno` remains unchanged.

## Sparse zero initialization

The installed SNOBOL4 binary is 692,212 bytes, but approximately 650 KiB is
large statically allocated zero-filled storage. Transmitting those zeros made
the original LGO about 1.55 MiB and 19,410 lines, and exposed UART upload
timeouts.

`bin_to_lgo.py --skip-zero` omits complete zero-only load records and emits an
assembly table of the omitted half-open address ranges. The monitor clears
exactly those generated ranges before every SNOBOL launch. Mixed records
containing initialized constants remain in the LGO.

The final validated image is:

```text
1,377 lines
109,856 bytes
SHA-256 f86352a3543ca1132811a5ac445d94fc39e605669bad0060694cc1b357202f85
```

This is about fourteen times smaller on the UART wire and also resets the
large SNOBOL working arenas between menu launches.

## Build

From the devgroup repository:

```sh
cd suites/plsw-snobol4
./build.sh
```

The result is:

```text
suites/plsw-snobol4/build/plsw-apps-and-snobol4.lgo
```

Copy it to the devgroup root for hardware testing if desired. Root-level
generated composite LGO files are ignored by `.gitignore`.

## Hardware validation

The transcript in `plsw-apps-and-snobol4.log` records a complete
921600-baud COR24-TB session using the 1,377-line sparse image:

- Hello printed and returned automatically.
- Count printed 1 through 10 and returned automatically.
- ELIZA accepted several lines and Ctrl-] returned to the menu.
- Echo repeated typed input and Ctrl-] returned to the menu.
- Palindrome printed all three results, entered its generated wait, and
  Ctrl-] returned to the menu.

This validates repeated application launches, repeated interrupt returns,
SNOBOL arena initialization, centralized interactive input, and both automatic
and attention-driven menu return paths on physical hardware.
