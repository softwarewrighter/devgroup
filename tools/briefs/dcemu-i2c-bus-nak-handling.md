# Brief: I2C bus state machine — honor master NAK so slave releases SDA

**Owner:** dcemu
**Branch:** `pr/i2c-bus-nak-handling`
**Repo:** `sw-cor24-emulator`
**Drafted by:** mike (2026-05-17), root cause diagnosed by dcxas
**Refs:** dcxas commit `275f17e fix(examples): i2c_ssd1306_rtc_clock — skip 9th-clock so i2cstop fires STOP`

## Why this brief exists

dcxas hit a bug in `i2c_ssd1306_rtc_clock.s` where a multi-byte
DS1307 read would never terminate — the canonical "NAK the last
byte" pattern didn't release the bus. The OLED rendered garbage
(typically `"00:40:40"` against runaway-read bytes). They shipped a
demo-side workaround (skip the explicit 9th clock; let `i2cstop`'s
SCL-up double as the ACK clock — see `src/examples/assembler/i2c_ssd1306_rtc_clock.s`
on `sw-cor24-x-assembler/main` and the commit body above for the
forensic walk).

The demo workaround is *correct hardware behavior* on real I2C
silicon and ships fine, but it papers over a bug in our bus state
machine: **on master NAK, the slave should release SDA, but our
state machine keeps pulling it low**. As a result, any third-party
demo or driver that uses canonical NAK-the-last-byte termination
will hang.

This brief asks dcemu to fix the bus state machine so the
canonical pattern works. Once it ships, dcxas can optionally
revert the demo workaround to the canonical shape (separate
follow-up; not in scope here).

## The bug (file:line precise)

In `src/cpu/i2c_bus.rs`, the `AckSlaveToMaster` arm of the SCL-rise
handler (around line 270–288):

```rust
I2cPhase::AckSlaveToMaster => {
    let master_acked = !sda;
    let target = self.current_target.unwrap_or(0);
    if let Some(dev) = self.addresses.lookup(target)
        && let Ok(mut d) = dev.lock()
    {
        if master_acked {
            d.on_master_ack();
            self.tx_byte = d.on_read_byte();
        } else {
            d.on_master_nak();
        }
    }
    // Continue with another byte slot — if the master
    // chose NAK, it will issue STOP/repeated START before
    // any further clocks shift this state.
    self.phase = I2cPhase::TxByte { bits: 0, n: 0 };
}
```

The transition to `TxByte { bits: 0, n: 0 }` happens regardless of
`master_acked`. Then on the *next* `on_scl_fall` (line 304–308):

```rust
I2cPhase::TxByte { .. } => {
    // Drive the current MSB of tx_byte.
    self.slave_sda_pull = (self.tx_byte >> 7) & 1 == 0;
}
```

After NAK, `tx_byte` was never refilled (we only refill in the
`master_acked` branch), so its value is whatever the byte was
shifted down to during the 8 bit-clocks: zero. `(0 >> 7) & 1 == 0`
→ `slave_sda_pull = true`.

The open-drain wired-AND `eff_sda = master_sda && !slave_sda_pull`
then suppresses every subsequent master SDA-up. `i2cstop`'s first
step (drive SDA low while SCL low) is fine, but the SDA *rise*
that should produce the STOP edge is masked by `slave_sda_pull`
still being true. No STOP edge is detected; the slave keeps
streaming bytes through the register file (and into RAM, in
DS1307's case) for every SCL pulse the master makes after.

## The fix

On NAK in `AckSlaveToMaster`, do **not** transition to `TxByte`.
Transition to a state where the slave releases SDA and the bus
waits for the master's STOP or REPEATED START.

Options, pick one:

### Option A (smallest diff): use `Idle`

```rust
self.phase = if master_acked {
    I2cPhase::TxByte { bits: 0, n: 0 }
} else {
    I2cPhase::Idle
};
```

`on_scl_fall`'s default arm (line 309–311) already sets
`slave_sda_pull = false` for any phase that isn't `AckMasterToSlave`
or `TxByte`. Idle is among those, so the slave releases SDA and
the master's STOP / REPEATED START can fire normally.

Tradeoff: `Idle` is semantically "no transaction" but the
transaction technically isn't fully terminated until STOP fires.
Functionally indistinguishable for the bus; just a minor naming
inaccuracy in state-machine reads. The Stop-edge detector in the
SCL handlers (or wherever your START/STOP logic lives) needs to
keep working from `Idle` — quick read-through to confirm.

### Option B (clearer intent): add `PostNak`

```rust
pub enum I2cPhase {
    Idle,
    // ... existing variants ...
    /// Master sent NAK — slave releases SDA, waiting for STOP
    /// or REPEATED START before any further bus activity.
    PostNak,
    Stopped,
}
```

And in `AckSlaveToMaster`:

```rust
self.phase = if master_acked {
    I2cPhase::TxByte { bits: 0, n: 0 }
} else {
    I2cPhase::PostNak
};
```

Default `on_scl_fall` arm already handles `PostNak`
(slave_sda_pull = false). The START/STOP detector probably
needs `PostNak` added to the "valid source state" list — quick
audit.

**Recommendation: Option B.** Slightly more code, but makes the
state machine self-documenting and avoids the "is Idle really
correct here?" question for the next reader.

## Tests

In `src/cpu/i2c_bus.rs::tests` (or wherever the bus tests live):

- `nak_releases_slave_sda` — drive a single-byte read; master sends
  NAK on the ACK clock; on the next SCL fall, assert
  `bus.slave_sda_pull == false` (was `true` pre-fix).
- `canonical_multi_byte_read_terminates` — set up a slave that
  streams an auto-incrementing sequence; master reads 3 bytes,
  NAKs the third, issues STOP. Assert: the slave does not stream
  any more bytes after STOP; the bus reaches `Stopped` then
  `Idle`; total byte count is exactly 3 (not 4+).
- `nak_then_repeated_start` — master sends NAK then a REPEATED
  START (without STOP). Assert: the new transaction's address
  byte is parsed correctly; previous slave was released; new
  device is addressed.

Plus an integration test in `tests/i2c.rs` that runs an actual
.lgo program performing canonical multi-byte NAK termination
(can borrow the pattern from dcxas's
`i2c_ds1307_read.s` once dcxas migrates it to canonical NAK, or
write a tight hand-bit-bang fixture here).

## Acceptance

- `cargo test --workspace` passes including the new tests.
- After this lands and mike rebuilds `work/bin/cor24-emu`, running
  the existing `i2c_ssd1306_rtc_clock.s` demo continues to work
  (dcxas's workaround isn't affected by the fix — it doesn't
  depend on the bug, just avoids it).
- A test or example using canonical NAK termination
  (`d.on_master_nak()` then i2cstop) reaches STOP cleanly without
  the slave streaming runaway bytes.

## Out of scope

- **Don't change `dcxas`'s demo.** dcxas can clean up
  `i2c_ssd1307_rtc_clock.s` to use canonical NAK in a separate
  follow-up brief once this lands. That's their call.
- **Don't audit every other state-machine arm for similar bugs.**
  This brief is narrow. If you spot related issues during the fix
  (e.g., `AckMasterToSlave`'s NAK handling has a sibling bug),
  surface them in a new brief; don't bundle.
- **No new public API.** This is a state-machine correctness fix.
  Existing `Ds1307HandleExt`, `Ssd1306HandleExt`, etc. unchanged.

## Workflow

```bash
cd /disk1/.../work/dcemu/github/sw-embed/sw-cor24-emulator
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/i2c-bus-nak-handling
# implement: Option B is recommended
cargo test --workspace
git commit -am "fix(i2c): honor master NAK so slave releases SDA before STOP"
git branch -m feat/i2c-bus-nak-handling pr/i2c-bus-nak-handling
```

Then `dg-mark-pr` (or just leave the `pr/` name). Standard
two-pr-branch pattern: optional `pr/...-saga-complete` for saga
bookkeeping. **Keep saga-complete a strict superset of feat at
signal time.**

## Cross-repo coordination

- After this lands, mike rebuilds `work/bin/cor24-emu` (binary
  install needed since `i2c_bus.rs` is in the library).
- dcxas may want a follow-up brief to clean up
  `i2c_ssd1306_rtc_clock.s` to use canonical NAK now that it
  works. Not blocking; their call when they're touching that
  file next.
- dwxas's wasm bundle embeds the emulator via Cargo path-dep; on
  their next sibling-refresh + rebuild, the canonical-NAK fix
  applies to in-browser execution too.
