# Brief: web "No battery / With battery" toggle for the DS1307 RTC demo

**Owner:** dwxas
**Branch:** `pr/battery-backed-rtc`
**Repo:** `web-sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-16)

## Cross-repo coordination

You're the third agent in this thread. Order of upstream landings:

1. [`dcemu-ds1307-initial-time-and-system-preset.md`](dcemu-ds1307-initial-time-and-system-preset.md)
   — emulator gains `Ds1307Device::with_initial_registers(...)`
   constructor + registry param parsing (`?hour=&minute=&...` and
   `?preset=system`). **Wait for this on `sw-cor24-emulator/main`
   before starting this brief.** Your wasm build embeds the
   emulator via Cargo path-dep, so the constructor must exist in
   your sibling clone.
2. [`dcxas-finish-ds1307-set-and-document-cli-preset.md`](dcxas-finish-ds1307-set-and-document-cli-preset.md)
   — refreshes demo headers to show the CLI one-liners. Not a
   blocker for you; runs in parallel with this brief.

When you start, refresh your sibling `sw-cor24-emulator` clone:

```bash
git -C ../sw-cor24-emulator fetch origin --prune
git -C ../sw-cor24-emulator switch main
git -C ../sw-cor24-emulator merge --ff-only origin/main
# confirm the constructor exists:
grep -l with_initial_registers ../sw-cor24-emulator/src/peripherals/i2c/devices/ds1307.rs
```

## Naming separation (load-bearing)

- **Emulator side: device-shaped.** `Ds1307Device::with_initial_registers(...)`
  takes seven BCD register values. The CLI's `?hour=&minute=&...`
  params are also device-shaped. No "battery" metaphor leaks into
  the Rust types or CLI surface.
- **Web side: feature-shaped.** "No battery / With battery" toggle,
  the `ds1307.battery` localStorage key, the UI prose. The web
  layer owns the metaphor.

Same separation as `tmp101@0x4A?temp=25.0` (device-shaped temp,
not a thermistor metaphor).

## What changes

### 1. Toggle UI in the I2C RTC panel

A single radio pair (or a checkbox if you prefer): `Battery (off)`
| `Battery (on)`. Default: off. Persisted in localStorage under
`ds1307.battery_enabled` (independent of `ds1307.battery` below).

### 2. localStorage schema

Key: `ds1307.battery`

```json
{
  "set_value": {"h": 12, "m": 34, "s": 56},
  "set_at_ms": 1731715200000
}
```

- `set_value` is the time the user (or a `i2c_ds1307_set.s` run)
  wrote into the device.
- `set_at_ms` is `Date.now()` at the moment of that write.

Effective initial registers at attach time:

```javascript
const persisted = JSON.parse(localStorage.getItem("ds1307.battery"));
const elapsed_s = Math.floor((Date.now() - persisted.set_at_ms) / 1000);
const total_s = (persisted.set_value.h * 3600
              + persisted.set_value.m * 60
              + persisted.set_value.s
              + elapsed_s) % 86400;
const effective = {
  h: Math.floor(total_s / 3600),
  m: Math.floor((total_s % 3600) / 60),
  s: total_s % 60,
};
```

Note `% 86400` — battery only persists **time-of-day**, not date.
Date arithmetic across days is intentionally out of scope (matches
the brief's no-date-on-emulator-side stance).

### 3. Attach path on Run

| Toggle | What you do at Assemble & Run |
|---|---|
| Off (default) | Attach `Ds1307Device::new()` (all-zero defaults). Demo prints 00:00:00. |
| On + no persisted state | Attach `Ds1307Device::new()`. After the first user write, the persisted state populates. |
| On + persisted state | Compute `effective` from localStorage; attach `Ds1307Device::with_initial_registers(effective.h, effective.m, effective.s, dow, date, month, year, 0)`. Use `0, 1, 1, 0` for the date fields (epoch defaults; date is out of scope). |

### 4. **THE SYNCHRONIZATION TRAP** (do not ship without this)

When the user runs `I2C RTC Set` (or any future demo that writes
the DS1307 registers via i2c), the web layer **must update
localStorage synchronously with the i2c-write transaction
completion**. Otherwise on the next page reload, the persisted
state is stale by the duration of the current run — the
"battery" loses everything written this session.

Concretely:

- Subscribe to your existing `I2cBusState`/`Ds1307HandleExt`
  observation in the run-loop tick.
- On each tick, if the DS1307 register file changed since the
  last tick (compare `Ds1307HandleExt::hour() / minute() / second()`
  tuples), write the new tuple to localStorage with
  `set_at_ms = Date.now()`.
- Coalesce updates inside a single tick — no need to fire one
  `setItem` per `r0=...` instruction.

Implementation hint: put a `Option<(u8,u8,u8)>` in your run-state
holding the last-seen RTC reading; compare on each tick and only
`setItem` when it changes. That covers both UART-driven sets and
any future slider-driven writes.

### 5. Demo dropdown — no rename

Keep `I2C RTC Read` and `I2C RTC Set`. The toggle is a panel-side
state, not a demo selector. Both demos work with both toggle
positions (read shows whatever the registers are; set writes to
them and triggers the localStorage update).

## Acceptance

- New toggle in the RTC panel, default off, persisted across reloads.
- With toggle off: identical to today's behaviour.
- With toggle on + no persisted state: identical to today's
  behaviour on first run.
- With toggle on + persisted state: device boots at
  `(last_set + elapsed_real_time) % 86400`.
- Running `I2C RTC Set` to write 14:00:00, waiting 30 seconds,
  reloading the page, running `I2C RTC Read` shows ~14:00:30.
- `cargo clippy -D warnings` green; `pages/` rebuilt.

## Out of scope

- **Date / month / year persistence.** `% 86400` is the contract.
- **Cross-tab sync.** localStorage events between tabs are not
  handled; the assumption is one tab at a time.
- **Encryption / signing.** localStorage is plaintext; users can
  hand-edit it. Acceptable for a demo.
- **A separate "wipe battery" button.** Users can clear via
  browser devtools or by toggling battery off → on (which
  preserves persisted state but ignores it; toggling on again
  picks it up).

## Open question (push back if you disagree)

The toggle could either:
- (A) Live in the panel only — invisible until the user opens the
  I2C RTC panel.
- (B) Live in the demo dropdown as a "(battery on)" suffix when
  enabled, so users see the persisted-state nature at a glance.

I'd default to (A) — keeps the dropdown semantics clean (one
entry per demo, not per state). But if you find users confused by
"why does it not show 00:00:00 anymore?", consider (B) as a
follow-up.
