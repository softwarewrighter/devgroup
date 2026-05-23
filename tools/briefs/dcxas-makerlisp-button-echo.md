# Brief: add Button Echo (MakerLisp) example to `sw-cor24-x-assembler`

**Owner:** dcxas
**Branch:** `pr/makerlisp-button-echo` (in dcxas's primary clone)
**Repo:** `sw-cor24-x-assembler`
**Drafted by:** dwxas
**Date drafted:** 2026-05-15

## Why this brief exists (process note)

dwxas (web-assembler agent) wrote and tested this change in their
**sibling** clone of `sw-cor24-x-assembler` while working on their
own saga (`extract-live-demo`, step 002 `makerlisp-button-echo`). Per
the [primary-repo-only convention](../../docs/branching-pr-strategy.md),
sibling clones are read-only context — `dg-list-pr` skips them and the
coordinator does not relay from non-primary clones. dwxas therefore
hands the change off to dcxas (the actual code-owner) via this brief.
The existing dwxas-side commit is preserved as a `format-patch` for
authorship continuity; dcxas applies it from their primary clone and
ships through the normal feat/→pr/ flow.

## The change

Add a second Button Echo demo, `src/examples/assembler/button_echo_makerlisp.s`,
that reproduces MakerLisp's `blinky_s2.lgo` byte-for-byte, plus the
matching integration-test wiring.

Source author: dwxas (commit `8d219ee` in their sibling clone:
`/disk1/github/softwarewrighter/devgroup/work/dwxas/github/sw-embed/sw-cor24-x-assembler`,
branch `pr/makerlisp-button-echo`).

Diffstat (vs. `origin/dev` at the time dwxas committed):
```
 src/examples/assembler/button_echo_makerlisp.s | 35 +++++++++++++++++++++++++++
 tests/integration_tests.rs                     | 12 ++++++++-
 2 files changed, 46 insertions(+), 1 deletion(-)
```

## How to apply

Format-patch already lives next to this brief:

```
tools/briefs/0001-feat-examples-add-Button-Echo-MakerLisp-variant.patch
```

dcxas: in your own primary clone, apply and ship:

```bash
cd /disk1/.../work/dcxas/github/sw-embed/sw-cor24-x-assembler
git fetch origin --prune
git switch -c feat/makerlisp-button-echo origin/dev
git am /disk1/github/softwarewrighter/devgroup/tools/briefs/0001-feat-examples-add-Button-Echo-MakerLisp-variant.patch
cargo test                          # exercises the new tests/integration_tests.rs lines
git branch -m feat/makerlisp-button-echo pr/makerlisp-button-echo
```

Then signal readiness as normal (`dg-mark-pr` or just leave the `pr/`
name in place).

## Background — what the demo demonstrates

The MakerLisp variant illustrates the canonical 27-byte `blinky_s2.lgo`
program from the COR24-TB world: hold `S2` and LED `D2` lights; release
`S2` and `D2` goes dark. Same observable behaviour as the existing
`button_echo.s`, but a *different shape* worth showing learners
side-by-side:

- **Loop body at offset `0x0000`** with a prologue (`push fp / push r2
  / push r1 / mov fp,sp`) and `la r2,0xFF0000`. The frame is set up but
  never torn down — the loop is infinite, so the burned slots are
  intentional.
- **Three-instruction spin** at `0x0008`: `lb r0,(r2)` reads bit 0 of
  the shared `IO_LEDSWDAT` byte (= `S2`), `sb r0,(r2)` writes the same
  byte back (= drives `D2`), `bra` loops. Both ends of the MMIO byte
  are active-low so a direct echo lights `D2` while `S2` is held — no
  inversion needed (where `button_echo.s` is written more naively).
- **Startup at `0x000E`** sets `sp = 0xFEEC00` (top of EBR), parks
  `r1 = 0xFEE000` (bottom of EBR — unused bookkeeping), and absolute-
  jumps into the loop via `la ir,loop` (the `(7,7)` form of `la`,
  opcode `0xC7`, which writes the immediate into `pc`).

Source order matters: the startup block must land at exactly byte
offset `0x000E`, which is only true if the loop body assembles to
exactly 14 bytes. Reordering the directives would break the layout
even though the labels still resolve.

Decompiled-listing reference: `sw-cor24-emulator/docs/makerlisp-blinky_s2.md`
(the disassembly with byte/address listing this source faithfully
reproduces).

Verified the assembled L record matches upstream `blinky_s2.lgo`
byte-for-byte:

```
L000000807F7E652B0000FF2E00820013F82900ECFE662A00E0FEC7000000
```

(Upstream also carries a `G00000E` start-PC record, which this
assembler's CLI does not yet emit; the byte payload is identical.)

## Acceptance

- `button_echo_makerlisp.s` lands at `src/examples/assembler/`.
- `tests/integration_tests.rs::examples()` carries a
  `("Button Echo (MakerLisp)", include_str!(...))` entry, and the
  name is in the `non_halting` list inside `test_all_examples_halt`
  (the variant intentionally spins forever, same as `button_echo.s`).
- `cargo test` passes (the dwxas-side run shows 21/21 green in
  `integration_tests` plus 72/72 in unit tests; clippy with
  `-D warnings` clean).
- No changes to public API or `Cargo.toml`.

## Cleanup after this lands

dwxas: once dcxas's pr/ relays to origin/dev on sw-cor24-x-assembler,
delete the stale `pr/makerlisp-button-echo` from your sibling clone:

```bash
cd /disk1/.../work/dwxas/github/sw-embed/sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git branch -D pr/makerlisp-button-echo
```

That keeps the sibling clone in its intended "read-only context" shape
and prevents the coordinator from seeing a phantom pr/ on next
`dg-list-pr` (which would still skip it, but it's tidier).
