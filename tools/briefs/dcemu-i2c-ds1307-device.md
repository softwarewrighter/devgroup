# Brief: add `Ds1307Device` to `sw-cor24-emulator`

**Owner:** dcemu
**Branch:** `pr/i2c-ds1307-device` (in dcemu's primary clone)
**Repo:** `sw-cor24-emulator`
**Drafted by:** dwxas
**Date drafted:** 2026-05-16

## Why this brief exists

The web demo at `web-sw-cor24-x-assembler` is adding an I2C RTC card
+ demo. The web side can't ship its panel until the device
implementation lands in the emulator. The DS1307 is the simplest
popular I2C RTC — recommended in
[`sw-cor24-x-assembler/docs/i2c-rt2-research.txt`](../../../dwxas/github/sw-embed/sw-cor24-x-assembler/docs/i2c-rt2-research.txt)
— so this is the chip to add first; later RTCs (PCF8563, DS3231)
can grow as new device files.

## The change

Add a new I2C slave at
`sw-cor24-emulator/src/peripherals/i2c/devices/ds1307.rs` that
mirrors the DS1307 protocol exactly, so the same guest-side bit-bang
that talks to real hardware works against the emulated chip. Closest
existing template: `tmp101.rs` (also pointer-then-data, also a small
register array).

## Register map (BCD, 8 bytes total)

7-bit address `0x68` (write address 0xD0, read 0xD1).

| Pointer | Field | Range | Notes |
|---|---|---|---|
| 0x00 | Seconds | 00–59 BCD | Bit 7 = CH (Clock Halt); mask out on write to keep ticking |
| 0x01 | Minutes | 00–59 BCD | |
| 0x02 | Hours   | 00–23 BCD | Bit 6 = 12/24 mode; the demo uses 24-hr, store cleanly |
| 0x03 | Day of Week | 1–7 BCD | |
| 0x04 | Date    | 01–31 BCD | |
| 0x05 | Month   | 01–12 BCD | |
| 0x06 | Year    | 00–99 BCD | |
| 0x07 | Control | — | RAM / SQW / OUT bits; for v1, store-verbatim is fine |

Auto-incrementing pointer: each read/write byte advances the
pointer by 1 mod 8. After 0x07 it wraps to 0x00 (matches the
datasheet's RAM-overflow behaviour).

## Suggested struct shape

```rust
pub const DEFAULT_ADDRESS: u8 = 0x68;

pub struct Ds1307Device {
    address: u8,
    pointer: u8,
    write_idx: u8,
    /// Eight bytes: [sec, min, hr, dow, date, month, year, ctrl].
    regs: [u8; 8],
}

impl Ds1307Device {
    pub fn new(address: u8) -> Self { /* zeroed, CH bit clear */ }

    /// Set the full clock from binary integers; the impl handles
    /// the BCD conversion. Used by the web UI's "set to system
    /// time" button.
    pub fn set_time(&mut self, hour: u8, min: u8, sec: u8) { ... }
    pub fn set_date(&mut self, year: u8, month: u8, date: u8, dow: u8) { ... }

    /// Read back as binary integers, for the web panel snapshot.
    pub fn hour(&self) -> u8 { /* bcd_to_int(regs[2] & 0x3F) */ }
    pub fn minute(&self) -> u8 { ... }
    pub fn second(&self) -> u8 { ... }
    /* etc. */

    /// Advance the clock by one second; cascades into minutes,
    /// hours, day-of-week, date, month, year as needed. The web
    /// run-loop can call this each emulated wall-clock second so
    /// the displayed time actually ticks while a demo runs.
    pub fn tick_second(&mut self) { ... }
}
```

BCD helpers (per the research doc):

```rust
fn int_to_bcd(n: u8) -> u8 { ((n / 10) << 4) | (n % 10) }
fn bcd_to_int(b: u8) -> u8 { ((b >> 4) * 10) + (b & 0x0F) }
```

`I2cDevice` impl mirrors TMP101's:
- `on_start` resets `write_idx = 0`.
- `on_write_byte`: idx 0 = pointer load (mask to low 3 bits, 0..=7);
  idx 1.. = data byte at `regs[pointer]`, then `pointer = (pointer + 1) & 7`.
  On writes to register 0x00, mask CH (bit 7) so the clock keeps
  running regardless of what the master tries.
- `on_read_byte`: return `regs[pointer]`, then advance pointer.
- ACK every byte (DS1307 ACKs liberally).

## Tests

Co-locate with the device, like `tmp101.rs`'s tests:
- BCD round-trip helpers
- Read at pointer 0x00 returns 7 bytes in order, pointer wraps after 0x07
- Write with pointer-load then 7 data bytes lands them in `regs`
- CH bit is masked out on write
- `tick_second()` cascades correctly across minute / hour / day
  boundaries (including end-of-month — 31 → next month, 30 → next
  on April etc.; leap-year handling is OK to defer to a follow-up)

## Acceptance

- `src/peripherals/i2c/devices/ds1307.rs` lands.
- Re-exported from `src/peripherals/i2c/devices/mod.rs` and the
  parent `src/peripherals/i2c/mod.rs` matches the TMP101 / Add1
  re-export shape.
- `cargo test` green (per-device unit tests + integration if any).
- `cargo clippy -- -D warnings` clean.

## Sibling-clone hygiene for dwxas

Once dcemu's `pr/i2c-ds1307-device` relays into
`sw-cor24-emulator/origin/dev`, dwxas refreshes their sibling clone
and consumes the device in the web-side step 012 (panel + demo).

## Reference reading

- Research/spec: [`sw-cor24-x-assembler/docs/i2c-rt2-research.txt`](../../../dwxas/github/sw-embed/sw-cor24-x-assembler/docs/i2c-rt2-research.txt)
- Closest template: `sw-cor24-emulator/src/peripherals/i2c/devices/tmp101.rs`
- Test slave precedent: `sw-cor24-emulator/src/peripherals/i2c/devices/add1.rs`
