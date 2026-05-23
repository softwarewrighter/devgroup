# Brief: SPI SD Card + W25Q32 NOR Flash devices (two-step saga)

**Owner:** dcemu
**Branch:** `pr/spi-sdcard-and-nor-flash` (single saga, two steps; each step its own commit)
**Repo:** `sw-cor24-emulator`
**Drafted by:** mike (2026-05-18)

## Why this brief exists

dcxas and dwxas are queued to add two new SPI demos: an **SD card
reader** (read sectors from a host-file-backed disk image) and a
**NOR flash demo** (read JEDEC ID, erase a sector, program a known
pattern, read it back). Both need new SPI device models in the
emulator. This brief is the upstream blocker.

The hardware target is the user's actual setup: 4 MiB W25Q32 NOR
flash modules (NOT the 16 MiB W25Q128) — match that exact size and
JEDEC ID. SD cards use the SPI-mode protocol (not native SDIO).

## Cross-repo coordination

Downstream of this brief (both block until `sw-cor24-emulator/main`
ships):

- [`dcxas-spi-sdcard-and-nor-flash-demos.md`](dcxas-spi-sdcard-and-nor-flash-demos.md)
  — assembler-side demos.
- [`dwxas-spi-sdcard-and-nor-flash-panels.md`](dwxas-spi-sdcard-and-nor-flash-panels.md)
  — web panels + dropdown entries (including a file-upload widget
  for the SD card image).

Reference patterns to mirror:
- `src/peripherals/spi/devices/tmp125.rs` — existing SPI device shape.
- `src/peripherals/spi/devices/echo.rs` — simplest device example.
- `src/peripherals/i2c/devices/ds1307.rs` — host-side persistence
  via the registry params (the DS1307's `?preset=system` pattern is
  the closest analogue for "user-controlled initial state").
- `src/cpu/spi_bus.rs` — bus state machine; you know it best.

## Step 1: SDCardDevice (SPI mode)

### What

Add `src/peripherals/spi/devices/sdcard.rs` (and re-export from
`devices/mod.rs`, `peripherals/spi/mod.rs`). Implement the minimal
SD-SPI command set needed for boot + read + write:

| Command | Hex | Purpose | Response |
|---|---|---|---|
| CMD0 | `0x40 00 00 00 00 95` | GO_IDLE_STATE | R1 = `0x01` (idle) |
| CMD8 | `0x48 00 00 01 AA 87` | SEND_IF_COND (SDHC check) | R7 = `0x01 00 00 01 AA` |
| ACMD41 | `0x77` then `0x69 40...` | SD_SEND_OP_COND | R1 = `0x00` (ready) after first call |
| CMD16 | `0x50` + 4-byte arg | SET_BLOCKLEN | R1 = `0x00` (accept; we hard-code 512 anyway) |
| CMD17 | `0x51` + 4-byte sector | READ_SINGLE_BLOCK | R1=`0x00`, then `0xFE` data token, 512 bytes, 2 CRC bytes |
| CMD24 | `0x58` + 4-byte sector | WRITE_BLOCK | R1=`0x00`, await `0xFE` from master, 512 bytes, 2 CRC bytes, then "data accepted" response `0xE5` |

Skip-on-this-pass:
- Dummy-clock count before CMD0 (real cards need ≥74 clocks; we
  accept immediately). Document this.
- CRC validation on commands (accept any CRC byte from master).
- CRC computation on responses (emit two `0xFF` bytes as the
  trailing CRC; matches real-world software-only drivers that
  don't check).
- Multi-block read/write (`CMD18`, `CMD25`) — defer to a future brief.

### State

```rust
pub struct SdCardDevice {
    cs: u8,                       // chip-select pin
    image_path: Option<PathBuf>,  // backing file; None = in-memory ephemeral
    image: Vec<u8>,               // loaded contents (or initial-zeros if no file)
    state: SdState,               // Idle | Ready | Reading{...} | Writing{...}
    expecting_bytes: usize,       // remaining argument or data bytes for current command
    rx_buf: [u8; 6],              // command receive buffer (6 bytes: opcode + 4 arg + CRC)
    rx_n: usize,
    tx_buf: VecDeque<u8>,         // bytes the slave will send out on next clocks
    acmd_pending: bool,           // CMD55 was just received → next command is ACMD
}
```

Block-level mapping: when CMD17 with argument N arrives, seek to
byte offset `N * 512` in `image`, queue up:
1. Several `0xFF` bytes (simulate hardware busy — 8 is fine).
2. The `0xFE` data token.
3. 512 bytes from the image.
4. Two `0xFF` (dummy CRC).

CMD24 writes the same way in reverse: master will eventually send
`0xFE` followed by 512 bytes. Buffer them, write back to the host
file on `0xFE + 512` complete, then emit the data-accepted byte.

### `SdCardHandleExt`

```rust
pub trait SdCardHandleExt {
    /// Bytes-eye view of the backing image (web panel uses this).
    fn image(&self) -> Vec<u8>;
    /// Image size in bytes (0 if no file mounted).
    fn size(&self) -> usize;
    /// Most-recently-accessed sector (for the web panel's "currently reading X" indicator).
    fn last_accessed_sector(&self) -> Option<u32>;
    /// Replace the entire image (web file-upload calls this).
    fn replace_image(&mut self, bytes: Vec<u8>);
}
```

### Registry

```
--spi-device sdcard@cs=<n>[?file=<path>]
```

- `file`: optional. If present, `mmap`/`read` the file as the initial
  image; subsequent writes propagate back. If absent, in-memory
  scratch (1 MiB blank zeros — enough to demo reads of sector 0 even
  without a file).
- Default CS: 2 (TMP125 already occupies whatever the existing
  pattern uses; pick the next available — confirm in your existing
  `spi_bus` allocation).

Help text:
```
sdcard@cs=<n>[?file=<path>]               SPI SD card (SD-mode)
```

### Tests

In `src/peripherals/spi/devices/sdcard.rs::tests`:

- `cmd0_returns_idle` — empty device responds `0x01` to CMD0.
- `cmd8_echo` — CMD8 returns the voltage-echo R7.
- `acmd41_transitions_to_ready` — after CMD55 + ACMD41, R1 returns `0x00`.
- `cmd17_reads_known_sector` — preload image with known bytes;
  send CMD17 sector=0; consume responses; assert the 512-byte data
  payload matches.
- `cmd24_writes_persists` — send CMD24 sector=5; feed 512 bytes;
  read back via CMD17; assert match.
- `cmd24_writes_to_file` — with `file=<tmpfile>`, write a sector,
  assert the host file on disk now contains those bytes at the
  right offset.

Plus an integration test in `tests/spi.rs` exercising
`--spi-device sdcard@cs=2?file=<tmpfile>` end-to-end.

## Step 2: W25q32Device (NOR Flash)

### What

Add `src/peripherals/spi/devices/w25q32.rs`. Implement the Winbond
W25Q32 instruction subset needed by a flash-management demo. The
chip is **4 MiB exactly** (4,194,304 bytes / 0x40_0000).

| Opcode | Name | Args | Behavior |
|---|---|---|---|
| `0x9F` | JEDEC ID | none | Return `0xEF 0x40 0x16` (Winbond, type, capacity — W25Q32 signature) |
| `0x05` | Read Status Register 1 | none | Return current status (bit 0 = WIP busy, bit 1 = WEL enable) |
| `0x06` | Write Enable | none | Set WEL bit = 1 |
| `0x04` | Write Disable | none | Clear WEL bit |
| `0x03` | Read Data | 3-byte addr | Stream bytes from `addr` forward (auto-incrementing) until master deselects CS |
| `0x02` | Page Program | 3-byte addr + ≤256 data | Write into the current 256-byte page; requires WEL=1; sets WIP busy for ~3 ms simulated; chip-erase-1-bits rule applies (only 1→0 transitions allowed unless erased) |
| `0x20` | Sector Erase | 3-byte addr | Set 4 KB (0x1000) aligned sector to `0xFF`; requires WEL=1; WIP busy ~40 ms |
| `0xD8` | Block Erase | 3-byte addr | Set 64 KB (0x10000) aligned block to `0xFF`; requires WEL=1; WIP busy ~200 ms |
| `0xC7` or `0x60` | Chip Erase | none | Whole image to `0xFF`; requires WEL=1; WIP busy ~2 s (skip the WIP delay if you want; document) |

Reject (return WIP-busy or no-op):
- Writes without `0x06` first — clear WEL after each program, so each
  write needs its own enable.
- Writes to a page that still has any 0→1 transition that isn't erased.

For WIP/WEL timing: don't actually `thread::sleep`. Simulate by
counting "clocks until WIP clears" — e.g., 1024 SPI byte-clocks for
Page Program, 4096 for Sector Erase. The CLI integration test can
poll `0x05` and observe the bit flip after enough byte-clocks.

### State

```rust
pub struct W25q32Device {
    cs: u8,
    image: Box<[u8; 4 * 1024 * 1024]>,
    wel: bool,
    wip_clocks_remaining: u32,
    current_op: Option<W25q32Op>,
    rx_buf: Vec<u8>,
    tx_buf: VecDeque<u8>,
}
```

Backing file: like SD card, optional `?file=<path>`. If present,
loaded on attach and synced on program/erase. If absent, in-memory
4 MiB scratch starting all-`0xFF` (default flash state).

### `W25q32HandleExt`

```rust
pub trait W25q32HandleExt {
    fn image(&self) -> Box<[u8; 4 * 1024 * 1024]>;   // or &[u8] if Box<arr> is awkward
    fn jedec_id(&self) -> [u8; 3];                   // always [0xEF, 0x40, 0x16]
    fn wip(&self) -> bool;
    fn wel(&self) -> bool;
    fn last_accessed_address(&self) -> Option<u32>;
    fn replace_image(&mut self, bytes: Vec<u8>);     // web panel "load file" path
    fn erase_chip(&mut self);                         // panel reset button shortcut
}
```

### Registry

```
--spi-device w25q32@cs=<n>[?file=<path>]
```

Help text:
```
w25q32@cs=<n>[?file=<path>]               Winbond W25Q32 NOR flash (4 MiB)
```

### Tests

In `src/peripherals/spi/devices/w25q32.rs::tests`:

- `jedec_id_signature` — 0x9F returns `[0xEF, 0x40, 0x16]`.
- `read_uninitialized_returns_ff` — fresh device, 0x03 from any
  address returns 0xFF.
- `page_program_requires_wel` — 0x02 without preceding 0x06 is
  rejected (WEL=0).
- `page_program_after_erase` — erase chip, write enable, program
  256 bytes, read back, assert match.
- `page_program_one_to_zero_rule` — write 0xAA over 0xFF — works.
  Write 0xAA over 0x55 (without erase) — fails (or silently does
  AND, depending on faithful behavior; document choice).
- `sector_erase_clears_4kb` — fill image with 0x00, erase sector
  at 0x1000, assert bytes 0x1000..0x1FFF are 0xFF and others are 0x00.
- `block_erase_clears_64kb` — same shape, 0xD000 boundary.
- `wip_clears_after_clocks` — write a page, poll 0x05, assert WIP=1
  initially, WIP=0 after sufficient byte-clocks.
- `image_persists_to_file` — `?file=<tmpfile>`, write a page, drop
  the device, reopen, read same address, assert match.

Plus an integration test in `tests/spi.rs` with the CLI flag.

## Acceptance

- Both devices live in `src/peripherals/spi/devices/`.
- `sdcard.rs` + `w25q32.rs` each ~400-600 lines (sdcard is simpler
  state-machine wise; w25q32 has more commands).
- Registry accepts both forms; help text updated.
- `cargo test --workspace` green including all new tests.
- `cargo clippy --workspace -- -D warnings` green.
- TWO commits on `pr/spi-sdcard-and-nor-flash`:
  - `feat(spi): SdCardDevice — SD-mode card reader`
  - `feat(spi): W25q32Device — Winbond W25Q32 NOR flash (4 MiB)`

Saga: `agentrail init` a two-step saga (step 1 = sd card, step 2 =
nor flash); each step gets its own commit recorded in `commits`.

## Out of scope

- **SDHC vs SDSC** distinction beyond the CMD8 echo — we present
  as SDHC always.
- **Multi-block read/write** (CMD18, CMD25). Defer.
- **Chip-erase WIP delay** is optional; if implementing, document
  the chosen byte-clock duration.
- **Dual/Quad SPI modes** on the W25Q32. Standard single-SPI only.
- **4-byte addressing mode** on the flash. W25Q32 fits in 3 bytes;
  no need for 0xB7 / 0xE9 / 0xC5 commands.
- **CRC validation** on either device. Accept any CRC the master
  sends; emit 0xFF as the trailing CRC.

## Workflow

```bash
cd /disk1/.../work/dcemu/github/sw-embed/sw-cor24-emulator
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
agentrail init --name spi-sdcard-and-nor-flash --plan docs/briefs/8-dcemu-spi-sdcard-and-nor-flash.md
# (or use your existing saga setup pattern)

# step 1
git switch -c feat/spi-sdcard-and-nor-flash
# implement sd card, write tests
cargo test --workspace
cargo clippy --workspace -- -D warnings
git commit -am "feat(spi): SdCardDevice — SD-mode card reader"

# step 2 (same branch)
# implement w25q32, write tests
cargo test --workspace
cargo clippy --workspace -- -D warnings
git commit -am "feat(spi): W25q32Device — Winbond W25Q32 NOR flash (4 MiB)"

git branch -m feat/spi-sdcard-and-nor-flash pr/spi-sdcard-and-nor-flash
```

Optional `pr/...-saga-complete` per the established pattern.
**Keep saga-complete a strict superset of feat at signal time.**
