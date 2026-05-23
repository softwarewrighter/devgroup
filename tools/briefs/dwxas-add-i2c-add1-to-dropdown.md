# Brief: add `I2C Add1 Ping` to the web demo dropdown

**Owner:** dwxas
**Branch:** `pr/add-i2c-add1-to-dropdown` (or whatever saga step you're
mid-flight on — see "Status note" below)
**Repo:** `web-sw-cor24-x-assembler`
**Drafted by:** mike
**Date drafted:** 2026-05-15

## Status note

You're already working on this — captured here so the full scope is
written down. Treat this brief as the spec, not a fresh assignment.

## Cross-repo dependency

This brief is downstream of
[`dcxas-publish-i2c-add1-ping`](dcxas-publish-i2c-add1-ping.md), which
publishes `src/examples/assembler/i2c_add1_ping.s` upstream in the
sibling `sw-cor24-x-assembler` repo. Don't merge this PR's dropdown
entry until the sibling's `origin/dev` has the file — otherwise
`include_str!` won't resolve.

Check first:

```bash
git -C ../sw-cor24-x-assembler ls-tree origin/dev:src/examples/assembler/ \
    | grep i2c_add1_ping
# expect: a 100644 line. If empty, dcxas hasn't shipped yet.
```

## Acceptance

- `src/demos.rs::EXAMPLES` gets a new entry:
  ```rust
  ("I2C Add1 Ping", include_str!("../../sw-cor24-x-assembler/src/examples/assembler/i2c_add1_ping.s")),
  ```
  inserted in alphabetical position (after `Fibonacci`, before
  `Literals`), matching dcxas's `tests/integration_tests.rs::examples()`
  ordering.
- Halts cleanly — no `non_halting` UI logic change needed.
- `pages/` rebuilt via `./scripts/build-pages.sh`. New wasm bundle
  committed alongside the source change.
- `cargo clippy -D warnings` clean.

## Considerations specific to this demo

The example does bit-bang I2C against an `add1` test slave at addr
0x50. Two paths through the demo:

| Slave attached? | Output |
|---|---|
| Yes (`--i2c-device add1@0x50` in CLI; `Tmp101Device` *or future Add1Device* in web) | `"43\n"` (slave returns input+1) |
| No (default) | `"FF\n"` (open-drain SDA reads as 1, so reads return 0xFF) |

The web demo currently attaches a `Tmp101Device` on every run. That
slave **is not the right one for this demo** — it lives at `0x48`
(TMP101's address), not `0x50`. So out-of-the-box the user will see
`"FF\n"` (the no-slave path).

Three options, in increasing scope:

1. **Just append the entry; document the `"FF\n"` outcome.** Smallest
   change. The demo proves the I2C MMIO machinery works
   (transactions counter ticks, SDA/SCL state visible in panel) even
   without the matching device. Tag the demo "(no-slave path)" in the
   dropdown if you want to set expectations.
2. **Attach an `Add1Device` alongside the TMP101.** Pull
   `Add1Device` from sibling `sw-cor24-emulator` if it exists there
   (check `src/peripherals/i2c/devices/`); if not, write a 30-line
   stub in `src/panels/i2c/add1.rs` (input→input+1 behaviour, no
   panel UI needed for the demo to print `"43\n"`).
3. **Re-attach the bus per-run based on which demo is selected.**
   Generalises the "what slaves does this demo expect?" question;
   probably overkill until you have 3+ demos with conflicting slave
   maps.

Pick the simplest one that achieves what you want; (1) is fine for
shipping today, (2) is the natural follow-up after the saga step for
"per-demo I2C slave config" if you decide to do that as a separate
brief later.

## Sibling-clone hygiene

Your sibling `sw-cor24-x-assembler` clone needs to be fetched to
`origin/dev` (or `main` once mike promotes) before the
`include_str!` macro can find the new file:

```bash
git -C ../sw-cor24-x-assembler fetch origin --prune
git -C ../sw-cor24-x-assembler switch dev
git -C ../sw-cor24-x-assembler merge --ff-only origin/dev
```

(`main` works too — `include_str!` doesn't care which branch is
checked out, just the file at the path.)

## Sibling sw-cor24-emulator refresh (optional, separate)

Mike just promoted `sw-cor24-emulator/main` to `8faf01f` — pulling in
SPI bus support, TMP125 device, CLI fixes, and a cosmetic
reformatting of `tmp101.lgo` (multi-record now, same payload). Your
wasm bundle is built against the *prior* main's emulator (since
`include_str!` and Cargo path-deps both resolve to your sibling
clone at build time). To pick up the new emulator features in the
web build:

```bash
git -C ../sw-cor24-emulator fetch origin --prune
git -C ../sw-cor24-emulator switch main
git -C ../sw-cor24-emulator merge --ff-only origin/main
./scripts/build-pages.sh    # full rebuild against new emulator
```

This is **optional** for the add1-ping landing — that demo works
fine with the prior emulator. Refresh when you actually want SPI
panels or the new emulator features.
