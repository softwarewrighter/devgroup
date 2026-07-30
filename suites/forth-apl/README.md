# COR24 Forth/APL interrupt-monitor suite

This build produces one load-and-go image containing:

- a resident menu at `0x000000`;
- the COR24 APL interpreter at `0x001000`;
- the COR24 Forth interpreter at `0x020000`; and
- a monitor-owned UART RX interrupt handler and 256-byte RX ring.

The menu is:

```text
1: Forth
2: APL
h: help
q: quit
mon>
```

Ctrl-] (`0x1D`) is monitor attention. The UART ISR consumes it and does not
place it in the application's RX ring. Because the COR24 `ir` register cannot
be assigned by software, the ISR returns once through `jmp (ir)` to clear the
interrupt-in-service latch. The application's blocked monitor-input broker
then abandons the old stack and jumps to monitor address zero. This provides
the required REPL escape while preserving working interrupts on later
launches. It is not arbitrary-instruction preemption of a compute-bound app;
attention completes when that app next requests console input.

## Build

```sh
bash build.sh
```

The script assembles each component at its assigned base, checks component
overlap, preserves each component's zero-initialized records, merges only
their `L` records, and appends the sole `G000000`.

Generated files are under `build/` and are intentionally ignored by Git.
Upload `build/forth-apl-suite.lgo` with the paced `te-rs` setup used for the
other COR24 hardware images.

## Memory map

```text
0x000000-0x000251  resident monitor and UART ISR
0x000800           RX head
0x000803           RX tail
0x000806           monitor-attention flag
0x000810-0x00090F  RX ring
0x001000-0x01E896  APL
0x020000-0x020F45  initial Forth dictionary
0x020F46-0x0EFFFF  Forth dictionary growth
0x0F0000           Forth return-stack base
0x100000           end of external SRAM
```

## Emulator verification

The final merged image was tested with the hardware-matching 3 KiB EBR stack.
The following sequence worked without reset:

1. Forth: `2 3 + .` produced `5`.
2. Ctrl-] returned to the monitor.
3. APL: `2+3` produced `5`.
4. Ctrl-] returned to the monitor.
5. Forth: `6 7 * .` produced `42`.
6. Ctrl-] returned again, proving the interrupt latch was cleared.
7. `q` entered the stable halt loop.

The validated artifact contains 3,487 `L` records, one `G000000`, and no
overlapping records. Its SHA-256 is:

```text
f14ee98ee388e6ec5cbf9d8f121db0664e796502b5330e42a6e63e988420979c
```

## Source provenance

`src/forth.s` is based on the installed `sw-cor24-forth` interpreter, with
UART RX routed through the monitor broker. `src/apl.s` is the generated
assembly from `sw-cor24-apl`, with its central `_getchar` routed through the
same broker. UART output remains direct in both applications.
