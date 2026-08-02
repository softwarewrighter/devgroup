# FT232RL uploader experiments

## Purpose

These experiments tested reliable COR24-TB load-and-go transfers through an
FTDI FT232RL USB-to-UART adapter at the board's fixed 921600-baud rate. They
also produced `te2`, a checksummed, retrying two-stage loader that can fall
back to the proven `te-rs` delay-based uploader when necessary.

## Hardware and device identity

The FT232RL enumerated as:

```text
USB VID:PID: 0403:6001
USB serial:  A50285BI
Linux driver: ftdi_sio
```

Its `/dev/ttyUSBN` number changed as adapters were unplugged and power-cycled.
Use the stable path instead of assuming `/dev/ttyUSB0` or `/dev/ttyUSB1`:

```text
/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A50285BI-if00-port0
```

The working wiring is crossed in the normal UART fashion:

```text
FT232RL TX   -> COR24-TB RX
FT232RL RX   <- COR24-TB TX
FT232RL RTS  -> COR24-TB CTS
FT232RL CTS  <- COR24-TB RTS
GND          <-> GND
```

## What was observed

- An FT232H loaded the test image correctly on two initial attempts using the
  C `te` uploader.
- The FT232RL loaded the medium-large `plsw-apps-and-snobol4.lgo` image on the
  second initial attempt. All five menu choices and the monitor's Ctrl-]
  return worked after the successful load.
- At one point the FT232RL entered a bad state in which resetting COR24-TB
  printed only `L`, rather than the complete `Load and Go` banner. Power
  cycling the board alone did not fix it; power cycling the USB/UART adapter
  restored normal behavior. Loader failures observed while it was in that
  state were therefore not valid measurements of the uploader alone.
- After recovery, the C `te` again loaded the larger PL/SW and SNOBOL4 suite,
  and all menu entries and Ctrl-] worked.

The practical recovery rule is: after a failed or malformed transfer, stop
the host uploader, power-cycle the USB/UART adapter, reset COR24-TB, verify the
full load-and-go banner, and only then retry. A board reset is required before
starting a fresh ROM-monitor load.

## `te` changes

The C `te` uploader was updated to:

- accept an explicit serial device;
- otherwise try `/dev/ttyUSB0` and `/dev/ttyUSB1`;
- configure 921600 baud, 8N1, and RTS/CTS flow control;
- explicitly assert host RTS;
- poll CTS before transmitting each byte; and
- retain separate reader/writer activity so received echo is drained while
  the sender is active.

The stable `/dev/serial/by-id/...` name should be supplied explicitly whenever
possible.

## `te-rs` fallback

`te-rs` provides strict echo synchronization and configurable pacing. It was
the reliable workaround for adapters that did not tolerate the original
unpaced transfer. A representative conservative invocation is:

```sh
te-rs --sync --byte-delay 100 --delay 10 --verbose \
  /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A50285BI-if00-port0
```

`te-rs` then prompts for the LGO filename. Delays can be reduced after a stable
baseline is established.

## `te2` verified-loader experiment

`te2` was built as a two-stage uploader:

1. The ROM monitor loads the small `te2.lgo` bootstrap at `0x0fc000` using
   exact echo checking.
2. The bootstrap announces `TE2 READY 1`.
3. The host sends idempotent data records containing a sequence number,
   absolute address, length, payload, and CRC-16-CCITT.
4. The target validates each record, writes it, reads RAM back, and ACKs it.
   A NACK, corrupt response, or timeout makes the host resend the same record.
5. A final stream CRC must match before the loader permits the entry-point
   jump.

Verification is intrinsic to this protocol, so `te2` has no `--sync` option.
Timeouts and retry counts are bounded so failure is reported instead of
hanging indefinitely.

The decisive FT232RL run loaded `plsw-apps-and-snobol4.lgo` successfully:

```text
bootstrap: loader ready after 56 records
record 612 at 0A140C: retry 1/5
record 2587 at 0B21B0: retry 1/5
verified: 2747 records, CRC16=E967
jump: 0D0000; terminal active (Ctrl-C exits)
```

The PL/SW and SNOBOL4 menu then appeared. This run demonstrated that the
second-stage checksum and retry path detected transient errors and recovered
without silently accepting a damaged image.

Example:

```sh
te2 -d /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A50285BI-if00-port0 \
  plsw-apps-and-snobol4.lgo
```

Useful configuration includes `-r` for retries, `-t` for response timeout,
`-n` for stage-two payload size, `-L` for an alternate bootstrap, and `-2` to
attach to an already-running second-stage loader.

## Baud-rate conclusion

Lower-speed negotiation was considered but deferred. The current COR24-TB
FPGA UART is fixed at 921600 baud: `uartio.h` names a baud register, but the
current `cor24_io.v` does not implement it and the UART RTL uses fixed timing.
A genuine phase-two baud switch therefore requires an FPGA change as well as
host and loader support.

## Current conclusions and next tests

- The crossed RTS/CTS wiring can work: both ordinary `te` and verified `te2`
  completed substantial transfers on the FT232RL.
- A successful unverified transfer does not prove every byte was correct;
  `te2` provides end-to-end detection and targeted retry.
- FT232RL adapter state is an important confounding variable. Always recover
  and verify the full ROM banner after a failure.
- Repeat the same image many times after clean resets to quantify reliability,
  recording retry counts and whether adapter power cycling was needed.
- Keep `te-rs` as the pacing-based fallback while `te2` is exercised further.
