# COR24 PL/SW applications and SNOBOL4 suite

This suite combines two unchanged native PL/SW demonstrations with one shared
SNOBOL4 interpreter and three selectable SNOBOL programs:

1. PL/SW hello
2. PL/SW count from 1 through 10
3. SNOBOL4 ELIZA
4. SNOBOL4 interactive echo
5. SNOBOL4 palindrome

The PL/SW application sources are compiled without modification. The existing
SNOBOL4 native image is also retained except for its two live-UART input call
sites (`READ_INPUT` and `READ_RAW_INPUT`), which are redirected from the
standalone polling routine to the resident monitor's RX broker. SNOBOL source
programs, including ELIZA, are unchanged.

Ctrl-] is consumed by the monitor UART ISR. The ISR records attention and
returns normally through `ir`, which is required to clear COR24's
interrupt-in-service latch. SNOBOL4's centralized input broker observes the
attention flag and returns to the menu outside interrupt context. The two
finite output-only PL/SW demos have their generated standalone `_halt` changed
by the suite builder to return to the menu automatically; their `.plsw` source
files are unchanged.

Batch SNOBOL programs would otherwise finish in the shared interpreter's
standalone `_halt` loop, where no further input call could observe attention.
The builder therefore generates a suite-local copy with a short final
`INPUT` wait before `END`. The upstream `.sno` source remains unchanged, and
Ctrl-] is handled through the same centralized SNOBOL input patch.

The build omits complete zero-only records from the SNOBOL4 image. The
converter generates an assembly table describing the omitted half-open address
ranges, and the monitor clears those exact ranges before every SNOBOL launch.
Initialized constants in mixed/nonzero records remain in the LGO. This reduces
the current hardware upload from about 1.55 MiB (19,410 lines) to about 109 KiB
(1,377 lines), while also resetting the large SNOBOL working arenas on relaunch.

Build with:

```sh
./build.sh
```

The hardware-loadable result is
`build/plsw-apps-and-snobol4.lgo`. Large generated `.lgo` files should not be
committed.
