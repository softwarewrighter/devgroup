# Brief: publish `i2c_add1_ping.s` example

**Owner:** dcxas
**Branch:** `pr/publish-i2c-add1-ping`
**Repo:** `sw-cor24-x-assembler`
**Drafted by:** mike
**Date drafted:** 2026-05-15

## What's missing

`src/examples/assembler/i2c_add1_ping.s` exists in your working tree
(untracked) but is not committed. dwxas needs it upstream so the web
demo can `include_str!` it into the dropdown — same pattern as
`button_echo_makerlisp.s` from last saga.

Current state (confirmed 2026-05-15):

```
$ git -C work/dcxas/.../sw-cor24-x-assembler status --short
?? src/examples/assembler/i2c_add1_ping.s
```

The file's header documents it well:

> I2C Add1 Ping: bit-bang i2c demo against the emulator's `add1` test
> slave. Writes byte 0x42 to the slave at I2C addr 0x50, reads it back
> (add1 returns byte+1 on read = 0x43), prints the result to UART as
> two hex chars + '\n'. Halts.

## Acceptance

- `src/examples/assembler/i2c_add1_ping.s` is committed (as-is — no
  rewrite needed; file is good).
- `tests/integration_tests.rs::examples()` carries a new
  `("I2C Add1 Ping", include_str!(...))` entry, inserted in
  alphabetical position (after `Fibonacci`, before `Literals`, to
  match the existing display-name ordering).
- The example halts cleanly, so no `non_halting` entry is needed.
- `cargo test` passes.
- No changes to library / CLI source; this is examples + test only.

## Workflow

```bash
cd /disk1/.../work/dcxas/github/sw-embed/sw-cor24-x-assembler
git fetch origin --prune
git switch -c feat/publish-i2c-add1-ping origin/dev
git add src/examples/assembler/i2c_add1_ping.s
# edit tests/integration_tests.rs to add the entry
cargo test
git commit -am "feat(examples): add I2C Add1 Ping demo"
git branch -m feat/publish-i2c-add1-ping pr/publish-i2c-add1-ping
```

Signal readiness as usual (`dg-mark-pr` or leave the `pr/` name).
Standard two-pr pattern: an optional `pr/publish-i2c-add1-ping-saga-complete`
follow-up records the merge if you want to log this as a saga step.

## Downstream — what happens after this lands

Once mike relays + promotes this to `sw-cor24-x-assembler/main`,
dwxas's companion brief
([dwxas-add-i2c-add1-to-dropdown](dwxas-add-i2c-add1-to-dropdown.md))
takes over: append the dropdown entry, rebuild wasm, ship to the
live demo at https://sw-embed.github.io/web-sw-cor24-x-assembler/ .
