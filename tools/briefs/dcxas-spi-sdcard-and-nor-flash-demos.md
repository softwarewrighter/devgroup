# Brief: SPI SD Card + NOR Flash assembly demos (two-step saga)

**Owner:** dcxas
**Branch:** `pr/spi-sdcard-and-nor-flash-demos`
**Repo:** `sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-18)

## Cross-repo coordination

Middle agent in the thread:

- **Upstream blocker**: [`dcemu-spi-sdcard-and-nor-flash.md`](dcemu-spi-sdcard-and-nor-flash.md)
  — emulator must ship `SdCardDevice` and `W25q32Device` before
  these demos can run end-to-end. Don't start until
  `sw-cor24-emulator/main` has both.
- **Downstream**: [`dwxas-spi-sdcard-and-nor-flash-panels.md`](dwxas-spi-sdcard-and-nor-flash-panels.md)
  — web panels + dropdown entries. They `include_str!` your demos
  once shipped.

Refresh sibling clone before starting:

```bash
git -C ../sw-cor24-emulator fetch origin --prune
git -C ../sw-cor24-emulator switch main && git -C ../sw-cor24-emulator merge --ff-only origin/main
test -f ../sw-cor24-emulator/src/peripherals/spi/devices/sdcard.rs && echo "sd ok"
test -f ../sw-cor24-emulator/src/peripherals/spi/devices/w25q32.rs && echo "flash ok"
```

## Step 1: `spi_sdcard_read.s` — SD-mode boot + sector read

### What

Hand bit-bang the SD-SPI init sequence and read a known sector.
Print the first 16 bytes of the sector to UART as hex pairs +
newline. Halt.

Sequence:
1. CS high, send ≥80 SPI clocks (the "dummy clocks" — even though
   our emulator accepts CMD0 immediately, real cards need this).
2. CS low, send CMD0 (`0x40 00 00 00 00 95`). Read response byte
   until R1 = `0x01` (idle).
3. CMD8 (`0x48 00 00 01 AA 87`). Read 5-byte R7. Accept any
   reasonable echo.
4. CMD55 (`0x77 00 00 00 00 01`) then ACMD41 (`0x69 40 00 00 00 01`).
   Loop until R1 = `0x00` (ready). In our emulator, the second
   ACMD41 returns ready; in real hardware, this may loop many times.
5. CMD16 (`0x50 00 00 02 00 01`) to set block length 512.
6. CMD17 (`0x51 00 00 00 00 01`) to read sector 0. Wait for `0xFE`
   data token. Read 512 bytes. Discard the 2 CRC bytes.
7. Print first 16 bytes as `XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX\n`.
8. Halt.

The header comment should document the exact CLI invocation:

```
;   cor24-asm src/examples/assembler/spi_sdcard_read.s -o /tmp/sd.lgo
;   cor24-emu --lgo /tmp/sd.lgo --spi-device 'sdcard@cs=2?file=/path/to/disk.img'
```

Without `--spi-device`, the demo will hang in the CMD0-wait loop
(no slave responds). Document this fallback.

### `tests/integration_tests.rs`

```rust
("SPI SD Card Read", include_str!("../src/examples/assembler/spi_sdcard_read.s")),
```

The demo halts after printing 16 bytes; do NOT add to `non_halting`.

For the test fixture: create a tiny 512-byte file with known content
at `tests/programs/sdcard-test.img` (e.g., the bytes `0x00..0x1F`
repeated). The integration test runs the demo with that file and
asserts UART output starts with `00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n`.

### SPI primitive library

You already have `spi_xchg` from `i2c_ds1307_*.s` predecessors and
the OLED Hello demo. For SD card, the routines you need:

- `cs_low`, `cs_high` — toggle the SS/CS line for the SD card's
  CS pin.
- `spi_xchg_byte` — already exists.
- `sd_send_cmd` — push 6 bytes (opcode | 0x40, 4 arg bytes, CRC) +
  read the response byte (loop on 0xFF until top bit clears).
- `sd_wait_token` — loop reading bytes until `0xFE` appears.

Inline these at the bottom of the demo file, same as the i2c
demos. ~80 lines of helper routines plus the main flow.

## Step 2: `spi_nor_flash_demo.s` — W25Q32 program-and-read

### What

Demonstrate the full read-erase-program-read cycle on the NOR flash.

Sequence:
1. Read JEDEC ID (`0x9F`). Expect `0xEF 0x40 0x16`. Print as hex
   to UART: `JEDEC: EF 40 16\n`.
2. Read 4 bytes from `0x000000`. Print: `BEFORE: XX XX XX XX\n`.
3. Write Enable (`0x06`).
4. Sector Erase (`0x20 00 00 00`).
5. Poll status (`0x05`) until WIP bit (bit 0) clears.
6. Write Enable (`0x06`).
7. Page Program (`0x02 00 00 00`) followed by 4 known bytes
   (e.g., `0xDE 0xAD 0xBE 0xEF`).
8. Poll status until WIP clears.
9. Read 4 bytes from `0x000000`. Print: `AFTER: DE AD BE EF\n`.
10. Halt.

Header CLI:

```
;   cor24-emu --lgo /tmp/nor.lgo --spi-device 'w25q32@cs=3?file=/tmp/w25q32.bin'
```

Without `?file=`, the demo writes to an in-memory 4 MiB scratch and
the bytes are lost on exit (still demos the protocol). Document this.

### `tests/integration_tests.rs`

```rust
("SPI NOR Flash Program", include_str!("../src/examples/assembler/spi_nor_flash_demo.s")),
```

Halts. Integration test asserts UART contains
`JEDEC: EF 40 16` and `AFTER: DE AD BE EF`.

## Acceptance

- Two `.s` files at `src/examples/assembler/spi_sdcard_read.s` and
  `spi_nor_flash_demo.s`.
- Both registered in `examples()` at alphabetical slots:
  - "SPI NOR Flash Program" — after "SPI Echo Ping", before
    "SPI SD Card Read"
  - "SPI SD Card Read" — after "SPI NOR Flash Program", before
    "SPI TMP125 Read"
- One integration test per demo using a fixture file under
  `tests/programs/`.
- `cargo test` green.
- `cor24-asm` + `cor24-emu` round-trip both demos manually before
  signaling.
- TWO commits on `pr/spi-sdcard-and-nor-flash-demos`:
  - `feat(examples): SPI SD Card Read demo`
  - `feat(examples): SPI NOR Flash Program demo`

Saga: two-step.

## Out of scope

- **No FAT filesystem code.** Sector 0 is just bytes. The demo
  treats it as a hex blob, not as MBR/FAT. (A future demo could
  parse MBR / read a FAT16 file — separate brief.)
- **No multi-sector reads** (CMD18). One sector is enough to
  demonstrate the protocol.
- **No 4 KB page program demo.** A 4-byte program is enough to
  demonstrate the erase-before-write rule.
- **No write-protection or status register 2/3 demos.** Skip until
  needed.

## Workflow

```bash
cd /disk1/.../work/dcxas/github/sw-embed/sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/spi-sdcard-and-nor-flash-demos
# implement both demos (one commit each on the same branch)
cargo test
git branch -m feat/spi-sdcard-and-nor-flash-demos pr/spi-sdcard-and-nor-flash-demos
```

Signal as usual. Restore saga-complete-superset-of-feat discipline.
