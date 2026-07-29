# Tiny C UART counter

`count.c` is a small physical-board smoke test. It prints increasing decimal
numbers to the polled UART as CRLF-terminated lines and runs forever. It avoids
the Tiny C `stdio.h` implementation and integer division so the `.lgo` stays
small.

## Build and run

From the devgroup repository root:

```bash
reference/tinyc-count/build-and-run.sh
```

The script uses the pinned workspace tools:

```text
work/bin/tc24r
work/bin/cor24-asm
work/bin/cor24-emu
```

It performs the pipeline explicitly:

```bash
work/bin/tc24r reference/tinyc-count/count.c \
  -o reference/tinyc-count/build/count.s

work/bin/cor24-asm reference/tinyc-count/build/count.s \
  -o reference/tinyc-count/build/count.lgo \
  --lgo-full \
  --bin reference/tinyc-count/build/count.bin \
  --listing reference/tinyc-count/build/count.lst

work/bin/cor24-emu \
  --lgo reference/tinyc-count/build/count.lgo \
  --speed 0 \
  --max-instructions 5000000 \
  --quiet
```

`--max-instructions` is essential for a batch emulator run because the
application intentionally never halts. Increase it to see more numbers. On
hardware, reset the board to stop the application and return to the built-in
load-and-go monitor.

## Hardware artifact

Upload this exact full image:

```text
reference/tinyc-count/build/count.lgo
```

Do not use an `.lgo` made with `--lgo-compact` for this validation. Full output
also writes zero-filled regions and is safe after a warm reload.

With `te-rs`, start with the most conservative upload:

```bash
te-rs --sync --verbose --delay 10 /dev/serial/by-id/YOUR_ADAPTER
```

After the monitor banner appears, press Ctrl-R and enter the absolute path to
`reference/tinyc-count/build/count.lgo`. A successful upload executes the
trailing `G` record automatically. Expected UART output begins:

```text
0
1
2
3
```

The software delay is intentionally approximate; its purpose is to make the
numbers readable on the physical board, not to provide a calibrated clock.

## About the blinky `--switch off` run

The supplied blinky application is an infinite button-follow loop and produces
no UART text. `--switch off` means S2 is released, so D2 remains off. A run
without a termination option therefore looks hung.

Use an instruction bound and `--dump`:

```bash
work/bin/cor24-emu \
  --lgo reference/blinky.lgo \
  --switch off \
  --speed 0 \
  --max-instructions 1000 \
  --dump
```

Expected final state includes `Halted: false`, `LED D2: OFF`, and
`BTN S2: RELEASED`. `Halted: false` is expected because the instruction limit,
not the application, stopped execution.
