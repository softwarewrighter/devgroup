# Brief: SSD1306 demos — "OLED Hello" and "RTC Clock on OLED"

**Owner:** dcxas
**Branch:** `pr/i2c-ssd1306-demos`
**Repo:** `sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-17)

## Cross-repo coordination

You're the middle agent in this thread:

- **Upstream blocker**: [`dcemu-i2c-ssd1306-device.md`](dcemu-i2c-ssd1306-device.md)
  — emulator must ship `Ssd1306Device` before these demos can run
  end-to-end. Don't start until `sw-cor24-emulator/main` has the
  device.
- **Downstream**: [`dwxas-i2c-ssd1306-panel.md`](dwxas-i2c-ssd1306-panel.md)
  — web panel + dropdown entries. Wait for your demos to land on
  `sw-cor24-x-assembler/main` before they can `include_str!` them.

Before starting: refresh your sibling clone of the emulator —

```bash
git -C ../sw-cor24-emulator fetch origin --prune
git -C ../sw-cor24-emulator switch main && git merge --ff-only origin/main
# verify:
grep -l Ssd1306Device ../sw-cor24-emulator/src/peripherals/i2c/devices/ssd1306.rs
```

## What changes

Add two demos under `src/examples/assembler/`:

### `i2c_ssd1306_hello.s`

A minimal "init then write text" demo. Hand-tight loop:
init the display (24-ish command bytes per the standard SSD1306
init sequence), set page+column to (0, 0), write "HELLO" pixels
via a 5×8 font lookup, halt.

Header comment requirements:
- Document the I2C MMIO addresses (`0xFF0020 = SCL`, `0xFF0021 = SDA`).
- Document the device address (`0x3C` default; brief mentions `0x3D`
  as an alternate strap).
- Document the control-byte protocol (`0x00` = command stream,
  `0x40` = data stream).
- CLI one-liner: `cor24-emu --lgo hello.lgo --i2c-device ssd1306@0x3C`.
- Note: without `--i2c-device`, the open-drain SDA returns 1, the
  init writes silently succeed nowhere, and the demo halts cleanly
  (no slave-attached path is the no-display fallback).

### `i2c_ssd1306_rtc_clock.s`

Reads DS1307 every ~1 emulated second, renders `HH:MM:SS` at
page 0 starting at column 0. Loops forever. Combines:

- Bit-bang I2C library from `i2c_ds1307_read.s` (the routines for
  i2cstart, i2cstop, i2cwrite, i2cread are already proven).
- DS1307 read pattern from `i2c_ds1307_read.s`.
- SSD1306 init + write pattern from the Hello demo.
- A "draw digit" routine that maps 0..9 to font lookup → 5 column
  bytes, plus ':' separator.

Header requirements:
- Document the two-device CLI:
  `cor24-emu --lgo clock.lgo --i2c-device 'ds1307@0x68?preset=system' --i2c-device ssd1306@0x3C`
- Note that omitting either device still produces a halting program
  (SSD1306 init writes go nowhere if SSD1306 is absent; DS1307
  reads return 0xFF if DS1307 absent → displays 99:99:99 or
  similar; both modes are diagnostic).
- Document the tick cadence (e.g., "redraws every ~10k I2C
  transactions" — pick a value that's responsive in the web demo
  at the current 100k instructions/tick budget).

### 5×8 font table

Inline the font glyphs at the bottom of each demo (under `.data`).
Minimum coverage for these two demos:

- Hello demo: `H`, `E`, `L`, `O` (uppercase). 4 glyphs × 5 bytes = 20 bytes.
- Clock demo: `0`-`9`, `:`. 11 glyphs × 5 bytes = 55 bytes.

If you'd rather ship a single fuller font (e.g., uppercase A-Z plus
digits and a few punctuation chars) as one shared table, that's fine
— in that case, put it in `src/examples/assembler/lib/font5x8.s` and
have both demos include it via a comment-instructed copy-paste step
in their headers. (cor24-asm doesn't have `.include`, so "library"
files are header-documented copy-paste, not a real include.)

Suggested font format: each glyph is 5 bytes, one byte per column,
LSB at top (matches SSD1306 GDDRAM convention so the bytes flow
directly to the wire). Empty rows (top/bottom of a 5×7 glyph in an
8-tall page) are just zero bits.

For "HELLO":
- `H` = `0x7F, 0x08, 0x08, 0x08, 0x7F`
- `E` = `0x7F, 0x49, 0x49, 0x49, 0x41`
- `L` = `0x7F, 0x40, 0x40, 0x40, 0x40`
- `O` = `0x3E, 0x41, 0x41, 0x41, 0x3E`

(Standard "Picaxe" / "Adafruit micro" 5×7 glyph encoding, MSB
unused for 7-row in 8-bit page.)

### `tests/integration_tests.rs::examples()`

Insert two entries, alphabetical position:

```rust
("I2C OLED Hello", include_str!("../src/examples/assembler/i2c_ssd1306_hello.s")),
("I2C OLED RTC Clock", include_str!("../src/examples/assembler/i2c_ssd1306_rtc_clock.s")),
```

Slot: between `I2C Add1 Ping` (which is "I2C Test Device Ping" in
the web dropdown but still "I2C Add1 Ping" in this repo's `examples()`,
matching the original name) and `I2C RTC Read`.

Wait — verify the current ordering with `grep -n 'I2C' tests/integration_tests.rs`
before inserting. Match the slot that gives:
```
I2C OLED Hello
I2C OLED RTC Clock
I2C RTC Read
I2C RTC Set
```

The clock demo's `non_halting` status: it loops forever, so add it
to the `non_halting` list inside `test_all_examples_halt`. The
Hello demo halts; don't add it.

## Acceptance

- Two `.s` files at `src/examples/assembler/i2c_ssd1306_{hello,rtc_clock}.s`.
- Both registered in `examples()` at the slots above.
- Clock demo registered in `non_halting`.
- `cargo test` passes.
- Header comments document both demos' CLI invocations including
  the `--i2c-device` flag.
- `cor24-asm` + `cor24-emu` round-trip both demos against the new
  `Ssd1306Device` (manual smoke test before signaling: assemble,
  run with `--i2c-device ssd1306@0x3C`, observe at minimum a clean
  halt for Hello and continuous looping for Clock).
- No changes to `src/` library or CLI (this is examples + tests only).

## Out of scope

- **No SSD1306 driver crate.** These demos hand-bit-bang at the
  i2c layer; abstracting later is fine, not part of this brief.
- **No scrolling or animation.** First text demo is static.
- **No multi-line text or fancy rendering.** Single row, single
  font, fixed position.
- **No font file in `src/examples/assembler/lib/`** unless you
  decide to make it shared (see "5×8 font table" above). If you go
  shared, document the copy-paste convention in the header.

## Workflow

```bash
cd /disk1/.../work/dcxas/github/sw-embed/sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/i2c-ssd1306-demos
# implement
cargo test
git commit -am "feat(examples): I2C OLED Hello + RTC Clock demos"
git branch -m feat/i2c-ssd1306-demos pr/i2c-ssd1306-demos
```

Then signal as usual. **Restore saga-complete-superset-of-feat
discipline**: if you do a `saga-complete` companion branch, ensure
it includes any follow-up commits on feat.
