# TE2 verified COR24 loader

`te2` is a two-stage, checksummed uploader for COR24-TB. Verification is always
enabled; there is no `--sync` option.

## Usage

Reset COR24-TB so that the ROM load-and-go monitor is running, then use:

```sh
./te2 image.lgo
```

For initial development, bootstrap `te2.lgo` with the proven C `te`, exit C
`te`, and attach directly to the running loader:

```sh
./te2 -2 image.lgo
```

Attach mode sends `H` and requires a fresh `TE2 READY 1` response, so it does
not depend on having captured the loader's original startup message.

The default serial search order is `/dev/ttyUSB0`, then `/dev/ttyUSB1`. Select
an explicit device or change the retry limit with:

```sh
./te2 -d /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A50285BI-if00-port0 \
  -r 8 image.lgo
```

Additional configuration:

```text
-L loader.lgo   Override the second-stage bootstrap image
-t milliseconds Set ACK/response timeout (50 through 10000; default 1000)
-n bytes        Set stage-2 record payload (1 through 32; default 32)
-2              Skip bootstrap and attach to an existing stage-2 loader
```

After verification, `te2` jumps to the image entry address and becomes an
interactive terminal. Ctrl-] is passed to COR24 applications; Ctrl-C exits the
host program. SIGINT/SIGTERM cleanup terminates the receiver process, restores
terminal and serial settings, and releases the adapter.

## Stages

1. `te2` loads `te2.lgo` through the ROM monitor at address `0x0fc000`.
   A dedicated receiver process continuously drains UART input, matching the
   proven C `te` reader/writer structure. The sender checks CTS and uses
   one-byte writes while the receiver independently collects the echoed line.
   Adapter RTS is explicitly asserted and kernel `CRTSCTS` remains enabled.
   Each complete bootstrap line must echo exactly before the next is sent.

Bootstrap CTS/write waits are 250 ms. Stage-2 responses use the configured
timeout. Failures are retried rather than allowed to stall indefinitely. The
complete bootstrap has a hard 25-second deadline, including nonblocking serial
writes, so it succeeds or fails in under 30 seconds.
   Absolute load records are safely resent after a timeout. The jump record is
   never resent blindly.
2. The second-stage loader prints `TE2 READY 1`.
3. The requested LGO image is parsed by the host and divided into data records
   of no more than 32 bytes.
4. Every record includes an absolute address, sequence number, byte count, and
   CRC-16-CCITT. The loader validates it before writing, reads RAM back, and
   acknowledges the sequence. A NACK, corrupted reply, or timeout causes the
   host to resend the same idempotent record.
5. A final CRC-16 covers the ordered sequence numbers, addresses, lengths, and
   read-back data. The loader refuses to jump until it matches.

The loader reserves addresses `0x0fc000` and above. A target image that overlaps
this region is rejected. Current COR24 suite images occupy substantially lower
addresses.

## Protocol

Data record:

```text
DssssaaaaaaLL<data>CCCC
```

- `ssss`: 16-bit sequence number.
- `aaaaaa`: 24-bit destination address.
- `LL`: byte count, at most 32.
- `data`: hexadecimal data bytes.
- `CCCC`: CRC-16-CCITT, initial value `0xffff`, over the binary header and data.

Successful data acknowledgement:

```text
Assss
```

A retry of the most recently accepted sequence receives the same ACK without
writing or adding it to the stream checksum again.

Failure response:

```text
NssssR
```

The final character identifies format, hexadecimal, CRC, sequence, range,
memory-readback, final-checksum, or jump-state failure.

End record and acknowledgement:

```text
EssssCCCC
Fssss
```

Verified jump and acknowledgement:

```text
Gaaaaaa
Jaaaaaa
```

Loader presence handshake:

```text
H
TE2 READY 1
```

## Baud-rate limitation

The current COR24-TB FPGA UART is fixed at 921600 baud in `uart_rx.v` and
`uart_tx.v`. Although `uartio.h` defines `UARTBAUD` at offset 2, `cor24_io.v`
does not implement that register. Host/loader baud negotiation therefore
requires a revised FPGA bitstream with a writable divisor or baud selector.

## Build

Install the COR24 tools (`cc24`, `ld24`, and `longlgo`) on `PATH`, then run
from this directory:

```sh
make
```

`te2.lgo` is deliberately small (about 4 KiB) so the unverified bootstrap is
short. Its exact echo and `READY` response are mandatory before the target
transfer begins.
