# Brief: DS1307 initial-time CLI params + system-clock preset

**Owner:** dcemu
**Branch:** `pr/ds1307-initial-time-and-system-preset`
**Repo:** `sw-cor24-x-assembler`'s emulator path-dep — `sw-cor24-emulator`
**Drafted by:** dcxas (2026-05-16)

## Why this brief exists

dcxas just shipped two RTC demos in the assembler repo:
`i2c_ds1307_read.s` (passive observer, prints `HH:MM:SS\n`) and
`i2c_ds1307_set.s` (UART-driven setter that writes the registers
via i2c then reads them back). Both work end-to-end against
`cor24-emu --i2c-device ds1307@0x68` — but **the CLI spec accepts
no params**, so out-of-the-box the read demo prints `00:00:00`
forever. The only way to get a non-zero starting time today is
the in-Rust `Ds1307HandleExt::set_*` API (which the web panel
will use, but is unavailable to CLI users).

This brief proposes the missing CLI surface so:

- dcxas's read demo can show meaningful output from a one-liner
  without the web panel (`--i2c-device 'ds1307@0x68?preset=system'`
  shows the host clock).
- dwxas's `web-sw-cor24-x-assembler` battery-backed RTC demo
  (currently in design — see "Battery story" below) has a clean
  emulator-side hook to pass an effective initial time without
  routing through `handle.set_time`.
- The "no battery" vs "with battery" web toggle becomes a one-arg
  CLI-side difference rather than two separate Rust code paths.

## What changes

Add three things to `sw-cor24-emulator`:

1. **`Ds1307Device::with_initial_registers(...)`** — a public
   constructor that takes the 8 BCD register values (S/M/H/DoW/
   Date/Month/Year/Control) and pre-loads them. Defaults match
   `new()` (all-zero, control = 0). Use it in `new()` itself for
   the existing "all-zero" path, so there's one source of truth.
2. **Registry parser support for `ds1307@<addr>?<params>`** — in
   `src/peripherals/i2c/registry.rs::build_i2c_device`'s `"ds1307"`
   arm, accept these param keys (all optional):

   | Key      | Value         | Behavior                                  |
   |----------|---------------|-------------------------------------------|
   | `hour`   | 0-23 decimal  | Sets the Hours register (BCD-encoded; 24-hour). Conflicts with `preset`. |
   | `minute` | 0-59 decimal  | Sets Minutes. Conflicts with `preset`.     |
   | `second` | 0-59 decimal  | Sets Seconds. Conflicts with `preset`.     |
   | `date`   | 1-31 decimal  | Sets Date register. Conflicts with `preset`. |
   | `month`  | 1-12 decimal  | Sets Month. Conflicts with `preset`.       |
   | `year`   | 0-99 decimal  | Sets Year register (2-digit). Conflicts with `preset`. |
   | `dow`    | 1-7 decimal   | Sets DayOfWeek register. Conflicts with `preset`. |
   | `preset` | `system`      | Reads host clock at attach time and sets all 7 time registers from it. Conflicts with any of the above keys. |

   Reject unknown keys (current pattern). Reject out-of-range
   values with a clear error message naming both the key and the
   value (`"ds1307 'hour' out of range: 24 (valid: 0-23)"`).
   Reject `preset` with `?hour=` etc. set
   (`"ds1307 'preset' and explicit register values are mutually exclusive"`).

3. **Help text update** in the CLI:

   ```
   --i2c-device <spec>    Attach an I2C device (repeatable). Specs:
                            add1@<addr>[?wrap=<n>]                universal +1 test slave
                            tmp101@<addr>[?temp=<f>][?config=<n>] TI temp sensor
                            ds1307@<addr>[?hour=<n>][?minute=<n>]
                                  [?second=<n>][?date=<n>][?month=<n>]
                                  [?year=<n>][?dow=<n>]
                                  [?preset=system]                Dallas/Maxim RTC
   ```

That's the entire surface change. The existing
`Ds1307HandleExt::set_time / set_date / tick_second / ...` runtime
API stays as-is (it's what the web panel's slider will use for
mid-session updates).

## Tests

Add to `src/peripherals/i2c/registry.rs` test module:

- `build_ds1307_with_time_params` — `ds1307@0x68?hour=12&minute=34&second=56`
  → device address 0x68, register 0x00 = 0x56 (BCD), 0x01 = 0x34,
  0x02 = 0x12.
- `build_ds1307_with_full_date` — all 7 keys set; assert each
  register encodes correctly.
- `build_ds1307_preset_system` — `ds1307@0x68?preset=system`
  reads `std::time::SystemTime::now()` (or `chrono::Local::now()`),
  asserts the registers fall within a small window of "now" at
  test-run time (allow a few seconds of slack for test scheduling).
- `build_ds1307_preset_conflicts_with_hour` — `?preset=system&hour=12`
  rejected with the named error.
- `build_ds1307_out_of_range_rejected` — `?hour=24`, `?minute=60`,
  `?dow=0`, `?year=100` all rejected with named errors.
- `build_ds1307_unknown_param_still_rejected` — the existing
  `?temp=25.0` test passes unchanged (regression).

Plus an integration test in `tests/i2c.rs` or wherever existing
ds1307 end-to-end tests live: assemble the existing
`examples/i2c/tmp101/tmp101.lgo`-style fixture against
`ds1307@0x68?hour=12&minute=34&second=56`, run it through the
emulator, observe that an i2c-read of register 0x02 returns 0x12.

## Out of scope

- **No persistence in the emulator.** Battery-backed survival
  across runs is purely a web/UI concern — see "Battery story"
  below. The emulator stays stateless across CLI invocations;
  each `--i2c-device ds1307@...` call constructs a fresh device.
- **No 12-hour mode** on the Hours register. The device already
  stores 24-hour values per its doc; `?hour=` takes 0-23
  decimal and BCD-encodes it. Bit 6 (12/24 mode flag) stays 0.
- **No SQW/OUT control bit support.** Register 0x07 stays
  default-zero. Future brief if anyone needs the squarewave
  output simulated.
- **No `?control=<n>` param.** Skip until something actually
  reads register 0x07 meaningfully.
- **No `Ds1307Device::with_system_clock()` Rust API.** Use the
  registry path (`?preset=system`) for that. Library users who
  want it can call `with_initial_registers(...)` with a chrono-
  computed argument.

## Battery story (context for the param design — not work for this brief)

The web side will offer two toggles for the RTC live demo:

- **"No battery"** — emulator gets `ds1307@0x68` with no params;
  registers boot at 0:00:00. Each page reload starts fresh.
- **"With battery"** — emulator gets `ds1307@0x68?hour=X&minute=Y&second=Z`
  where X/Y/Z come from web localStorage:

  ```javascript
  // localStorage key: "ds1307.battery"
  // value: { set_value: { h, m, s, ... }, set_at_ms: <Date.now() at set> }
  const persisted = JSON.parse(localStorage.getItem("ds1307.battery"));
  const elapsed_s = (Date.now() - persisted.set_at_ms) / 1000;
  const effective_total_s = (persisted.set_value.h*3600 + persisted.set_value.m*60
                             + persisted.set_value.s) + elapsed_s;
  const effective = secondsToHms(effective_total_s % 86400);
  // build the --i2c-device spec from effective.{h,m,s} and reload the wasm bundle
  ```

This gives "battery survives page reload AND clock continues
ticking while the page was closed" with **zero emulator-side
persistence machinery** — the emulator just sees a constructed
device with the right initial values. dcemu does not need to
think about localStorage, chrono in the web, or page-reload
lifecycle. dwxas owns all of that.

The discipline the web side must enforce: whenever the assembler-
side set demo writes to the registers via i2c, the web must
update localStorage synchronously with the i2c-write transaction
completion. Otherwise next reload's "battery state" is stale.
That's a web-side concern, not an emulator concern.

## Naming rationale

Keep the param names device-shaped (`hour`, `minute`, `second`)
rather than feature-shaped (`battery`, `time_now`). Matches the
pattern already in use (`tmp101@0x4A?temp=25.0` doesn't pretend
to be a thermistor; `add1@0x50?wrap=10` doesn't pretend to be a
counter modulus). The web layer owns the battery metaphor.

## Downstream

After this lands and mike promotes `sw-cor24-emulator/main`:

- dcxas's `i2c_ds1307_read.s` demo gains a meaningful one-liner:
  `cor24-emu --lgo ds1307_read.lgo --i2c-device 'ds1307@0x68?preset=system'`
  prints the actual host time. Document in the demo's header
  comment (separate one-line follow-up PR in `sw-cor24-x-assembler`).
- dwxas can wire the battery toggle into the web demo without
  needing any new Rust API beyond what `Ds1307HandleExt` already
  provides for slider-driven mid-session updates.

## When done

`dg-mark-pr` to rename `feat/ds1307-initial-time-and-system-preset`
→ `pr/ds1307-initial-time-and-system-preset`. Mike relays via
`dg-relay dcemu sw-cor24-emulator pr/ds1307-initial-time-and-system-preset`.
Standard two-pr saga pattern (optional `pr/...-saga-complete`
bookkeeping branch).

## Cross-repo coordination

This brief is the upstream of two follow-ups:

- [`dcxas-finish-ds1307-set-and-document-cli-preset.md`](dcxas-finish-ds1307-set-and-document-cli-preset.md)
  — once `?preset=system` lands on `sw-cor24-emulator/main`,
  dcxas refreshes the `i2c_ds1307_read.s` header to show the new
  one-liner. dcxas's blocked `pr/i2c-ds1307-set` chain also needs
  rebasing onto current dev — covered there.
- [`dwxas-battery-backed-rtc.md`](dwxas-battery-backed-rtc.md) —
  dwxas wires the web "No battery / With battery" toggle on top of
  the `with_initial_registers(...)` constructor. The brief's
  "Battery story" section sketches the design; the dwxas brief
  pins the exact API contract + the localStorage synchronization
  trap.

## Coordinator guidance (mike, 2026-05-16)

Three points of direction added in response to dcemu's pre-implementation
questions:

1. **`set_from_unix_seconds`: demote, don't keep public.** This brief's
   philosophy is "registry params are the user surface; runtime Rust
   API exists only for the web panel's slider." A standalone
   Unix-seconds setter is off-pattern for both audiences. Demote to
   `pub(crate)` and use it internally to implement `?preset=system`.
   Library users who want time-from-epoch compute the seven BCD
   fields themselves and pass to `with_initial_registers(...)`.

2. **Reset framing was wrong: `5a92afe` is upstream.** It's already on
   `bare/main` and GitHub (mike shipped it 2026-05-16 ~14:00 GMT).
   Local `git reset --hard 5a92afe^` would not undo it; it'd just
   produce a branch that doesn't include it, which is confusing.
   Real options are (a) **single fresh commit on top of `origin/dev`**
   (recommended; inline-removes/demotes the obsoleted bits of
   `5a92afe`'s surface so the diff is the "new shape + what's gone"
   record), or (b) stack refinement commits (acceptable if distinct,
   but for this brief the changes are tightly coupled). Frame the
   commit message as a deliberate supersedure: "supersedes the
   `5a92afe` Rust-API surface; that path is now `?preset=system`."

3. **Branch rename: start fresh, don't in-place rename a stale branch.**
   The old `pr/i2c-ds1307-system-time` is merged; just delete locally:

   ```bash
   git fetch origin --prune
   git switch dev && git merge --ff-only origin/dev
   git branch -D pr/i2c-ds1307-system-time
   git switch -c feat/ds1307-initial-time-and-system-preset
   # commit, then dg-mark-pr
   ```

Two non-blocking notes on the brief itself:

- The `?preset=system` test will be time-dependent; the brief calls
  for slack tolerance — make sure it's generous enough for slow CI
  (a few seconds is enough for local but tight for hosted runners).
- The "preset conflicts with any other key" rule rejects
  `?preset=system&second=0` (host clock with zeroed seconds for
  deterministic UART output). That's strict but matches the brief.
  Don't soften it unilaterally; if you hit annoyance in your own
  demos, push back to dcxas for a refinement brief.
