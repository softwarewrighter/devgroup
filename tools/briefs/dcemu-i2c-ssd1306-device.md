# Brief: Add SSD1306 OLED display as an I2C device

**Owner:** dcemu
**Branch:** `pr/i2c-ssd1306-device`
**Repo:** `sw-cor24-emulator`
**Drafted by:** mike (2026-05-17)

## Why this brief exists

The web demo has I2C TMP101 (temp), I2C Test Device (ping), I2C RTC
(time), SPI Echo, and SPI TMP125 (temp). The next obvious device
is **output**: an OLED display that programs can write pixels to.
SSD1306 is the canonical small-OLED controller (128×64 or 128×32,
monochrome) and ships on every Arduino starter kit, so demos
targeting it are immediately recognizable. dcxas's downstream brief
adds two demos that need this device:

- `i2c_ssd1306_hello.s` — write "HELLO" pixels and halt.
- `i2c_ssd1306_rtc_clock.s` — combine DS1307 read with display
  update, rendering `HH:MM:SS` updated each second.

dwxas's downstream brief adds the panel that renders the framebuffer
back to the user.

## Cross-repo coordination

Downstream of this brief (both wait for `sw-cor24-emulator/main` to
ship before they can start):

- [`dcxas-i2c-ssd1306-demos.md`](dcxas-i2c-ssd1306-demos.md) — assembler demos.
- [`dwxas-i2c-ssd1306-panel.md`](dwxas-i2c-ssd1306-panel.md) — web panel + dropdown entries.

Reference patterns to mirror (look at these before starting):
- `src/peripherals/i2c/devices/tmp101.rs` — write-pointer-then-data shape.
- `src/peripherals/i2c/devices/ds1307.rs` — register-state + `HandleExt`
  observation API. Closest analogue.
- `src/peripherals/i2c/registry.rs` — CLI param parsing pattern.

## What changes

Add `src/peripherals/i2c/devices/ssd1306.rs` plus registry entry.

### Framebuffer state

```rust
const PAGES: usize = 8;        // 64 rows / 8 per page
const COLS: usize = 128;
const FRAMEBUFFER_LEN: usize = PAGES * COLS;  // 1024 bytes

pub struct Ssd1306Device {
    address: u8,
    framebuffer: [u8; FRAMEBUFFER_LEN],
    display_on: bool,
    addressing_mode: AddressingMode,  // Horizontal | Vertical | Page
    column: u8,                       // 0..COLS, current write pointer
    page: u8,                         // 0..PAGES
    column_start: u8,                 // for Horizontal/Vertical bounds
    column_end: u8,
    page_start: u8,
    page_end: u8,
    expecting_data_bytes: usize,      // remainder of multi-byte command
    command_buf: [u8; 3],             // most commands are 1-3 bytes
    command_pending: Option<u8>,      // command opcode awaiting params
}
```

GDDRAM byte convention: byte at `framebuffer[page * COLS + col]`
encodes 8 vertical pixels of that page's column, LSB at top. This
matches the chip's wire format — no transformation needed.

### Wire protocol (control byte + payload)

The SSD1306 I2C protocol: after the address-write, the first byte
of every "burst" is a **control byte**:

| Control byte | Meaning |
|---|---|
| `0x00` | Co=0, D/C#=0 — *only* command bytes follow until STOP |
| `0x40` | Co=0, D/C#=1 — *only* GDDRAM data bytes follow until STOP |
| `0x80` | Co=1, D/C#=0 — one command byte, then another control byte |
| `0xC0` | Co=1, D/C#=1 — one data byte, then another control byte |

(Real drivers usually use `0x00` for an init block and `0x40` for
a pixel block; the Co=1 modes are uncommon but should at least not
panic if they appear.)

Plumbing: extend the existing `I2cDevice::handle_write(buf: &[u8])`
state machine to consume the first byte as control, then dispatch
the rest as commands or data.

### Commands to implement semantically

Implement the framebuffer-affecting commands; lenient-consume the
rest (advance past their parameter bytes without state change so the
init sequence doesn't fault).

| Opcode | Bytes | Action |
|---|---|---|
| `0xAE` | 1 | display_on = false |
| `0xAF` | 1 | display_on = true |
| `0x20` | 2 | addressing_mode = arg & 0x03 (0=Horiz, 1=Vert, 2=Page) |
| `0x21` | 3 | column_start = arg1; column_end = arg2 (horizontal/vertical mode) |
| `0x22` | 3 | page_start = arg1; page_end = arg2 (horizontal/vertical mode) |
| `0xB0..=0xB7` | 1 | page = opcode & 0x07 (page-addressing mode only) |
| `0x00..=0x0F` | 1 | column = (column & 0xF0) \| (opcode & 0x0F) (page mode, low nibble) |
| `0x10..=0x1F` | 1 | column = (column & 0x0F) \| ((opcode & 0x0F) << 4) (page mode, high nibble) |

Lenient-consume these (correct param count, no state effect):

| Opcode | Bytes |
|---|---|
| `0x81` | 2 (contrast) |
| `0x40..=0x7F` | 1 (set display start line — `0x40 \| line`) |
| `0xA0`, `0xA1` | 1 (segment remap) |
| `0xA4`, `0xA5` | 1 (entire display on/normal) |
| `0xA6`, `0xA7` | 1 (normal/inverse) |
| `0xA8` | 2 (multiplex ratio) |
| `0xC0`, `0xC8` | 1 (COM scan direction) |
| `0xD3` | 2 (display offset) |
| `0xD5` | 2 (clock divide) |
| `0xD9` | 2 (precharge) |
| `0xDA` | 2 (COM pins config) |
| `0xDB` | 2 (vcomh deselect) |
| `0x8D` | 2 (charge pump) |

For unknown opcodes: log via `tracing::warn!` and consume just the
opcode byte. Don't panic — drivers often send things we don't model
and we want demos to keep running.

### Data writes (after `0x40` control byte)

Every byte goes to `framebuffer[page * COLS + col]`, then advance
the pointer:

- **Page mode**: `column = (column + 1) % COLS`; page does NOT advance.
- **Horizontal mode**: `column += 1`; if `column > column_end`,
  reset `column = column_start` and `page += 1`; if `page > page_end`,
  reset `page = page_start`.
- **Vertical mode**: `page += 1`; on overflow, reset and advance column
  (mirror of horizontal).

### Reads

The SSD1306 supports a status-register read (busy bit), but most
demos don't use it. Return `0x00` for any read until a real demo
needs more.

### Public Rust API (HandleExt)

```rust
pub trait Ssd1306HandleExt {
    /// Snapshot the framebuffer (web panel pulls this each tick).
    fn framebuffer(&self) -> [u8; 1024];
    /// Display-on state (panel renders blank when off).
    fn display_on(&self) -> bool;
    /// Width/height for the panel to size itself.
    fn width(&self) -> u16;
    fn height(&self) -> u16;
}
```

No `set_*` methods — programs write via I2C, not via the Rust API.
(Slider/button interactions for displays don't make sense the way
they do for sensors.)

### Registry / CLI

Add a `"ssd1306"` arm to `build_i2c_device`:

```
--i2c-device ssd1306@<addr>[?width=<n>&height=<n>]
```

- `width`: defaults to 128. Accept 128 only for now (other widths
  defer to a future brief).
- `height`: defaults to 64. Accept 32 or 64.
- Reject unknown params (existing pattern).
- Default address per datasheet: 0x3C (some boards strap to 0x3D).

Help text update (mirroring the ds1307 entry):
```
ssd1306@<addr>[?width=<n>][?height=<n>]   SSD1306 OLED display
```

### Tests

In `src/peripherals/i2c/devices/ssd1306.rs::tests` (mirror the ds1307 test shape):

- `framebuffer_starts_blank` — fresh device's framebuffer is all zeros.
- `display_on_toggles` — 0xAE/0xAF flip `display_on()`.
- `page_addressing_write_advances_column` — write 3 bytes in page mode,
  framebuffer[0..3] populated, column advanced to 3.
- `page_addressing_column_wraps` — write 130 bytes, last 2 are at
  cols 0,1 of the same page.
- `horizontal_addressing_full_row` — set range (col 0..127, page 0..0),
  write 128 bytes, then write 1 more — that one lands at (page 0, col 0).
- `horizontal_addressing_advances_page` — set range (col 0..127, page 0..1),
  write 256 bytes — second half lands on page 1.
- `init_sequence_consumed` — feed the standard 25-byte Arduino init
  sequence as commands; no panic; final state has `display_on = true`.
- `unknown_opcode_warns_but_does_not_panic` — 0x99 + GDDRAM write
  still succeeds.

Plus an integration test in `tests/i2c.rs`:

- `cli_ssd1306_attach` — `--i2c-device ssd1306@0x3C` attaches a default
  128×64 device.
- `cli_ssd1306_with_size` — `--i2c-device 'ssd1306@0x3C?width=128&height=32'`
  sizes correctly.

## Out of scope

- **No actual rendering on the emulator side.** The framebuffer is
  raw bytes; the web panel handles pixel painting.
- **No scrolling commands** (0x26..0x2F). Defer to a future brief if
  a demo needs scrolling.
- **No status-register read with busy bit.** Reads return 0x00.
- **No fade/blink (0x23) or zoom (0xD6).** Lenient-consume only if a
  driver sends them; otherwise no plumbing.
- **No 128×32 alternate layout in the Rust state.** The `height` param
  just narrows what the panel renders; framebuffer storage stays
  128×8 internally. The bottom 4 pages are simply ignored by 32-px
  displays.

## Workflow

```bash
cd /disk1/.../work/dcemu/github/sw-embed/sw-cor24-emulator
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/i2c-ssd1306-device
# implement + tests
cargo clippy -D warnings
cargo test
git commit -am "feat(i2c): Ssd1306Device — SSD1306 OLED display"
git branch -m feat/i2c-ssd1306-device pr/i2c-ssd1306-device
```

Then `dg-mark-pr` if you want, or just leave the `pr/` name in place.
Standard two-pr-branch pattern (optional `pr/...-saga-complete` for
saga bookkeeping). Per the recent feedback: **keep saga-complete a
strict superset of feat at signal time** — if you add a follow-up
fix commit to feat after signaling, rebase saga-complete on top of
the new feat tip before re-signaling.
