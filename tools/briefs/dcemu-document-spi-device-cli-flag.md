# Brief: document `--spi-device` in `cor24-emu --help`

**Owner:** dcemu
**Branch:** `pr/document-spi-device-cli-flag`
**Repo:** `sw-cor24-emulator`
**Drafted by:** mike (2026-05-18)
**Refs:** dcxas blocked on their saga because `cor24-emu --help` showed no `spi-device` entry — they reasonably concluded the flag was missing, even though it works.

## What's missing

`cor24-emu --help` documents `--i2c-device <spec>` with a full block
listing all supported device names (add1, tmp101, ds1307, ssd1306).
**There is no parallel block for `--spi-device <spec>`**, despite
the flag being fully wired through to the registry and accepting
`echo`, `tmp125`, `sdcard`, and `w25q32` specs.

Verification: `cor24-emu --help 2>&1 | grep -c spi-device` returns
`0`. But `cor24-emu --lgo demo.lgo --spi-device w25q32@cs=3` parses
and attaches successfully. The flag works; it's just invisible.

This blocked dcxas's SPI-saga work (they did the right thing —
checked `--help` before relying on the flag — and concluded
correctly from incomplete information).

## What to change

In whichever module emits the `--help` text (likely
`cli/src/run.rs` or a `usage` const), add an `--spi-device` block
that mirrors the existing `--i2c-device` block. Roughly:

```
  --spi-device <spec>    Attach an SPI device (repeatable). Specs:
                           echo@cs=<n>[?seed=<n>]                  test echo device (CS-selected loopback)
                           tmp125@cs=<n>[?temp=<f>]                TI temp sensor (SPI)
                           sdcard@cs=<n>[?file=<path>]             SPI-mode SD card (host-file-backed)
                           w25q32@cs=<n>[?file=<path>]             Winbond W25Q32 NOR flash (4 MiB)
```

Use the exact wording the existing `--i2c-device` block uses for
consistency (column alignment, two-space indentation under spec,
trailing-period-or-not — match what's there).

Also add a quick example invocation near the bottom (under the
`cor24-emu --lgo examples/i2c/tmp101/tmp101.lgo --i2c-device tmp101@0x4A?temp=25.0`
example), e.g.:

```
  cor24-emu --lgo /tmp/flash.lgo --spi-device w25q32@cs=3?file=/tmp/w25q32.bin
```

## Acceptance

- Single commit: `docs(cli): document --spi-device in cor24-emu --help`.
- `cor24-emu --help 2>&1 | grep -c spi-device` returns ≥ 2 (the
  flag line + at least one device entry).
- The new help block is visually consistent with the
  `--i2c-device` block (same indentation, same column structure,
  alphabetical device order within the block).
- No source/CLI-behavior changes; this is pure documentation.

## Out of scope

- **Don't audit other undocumented flags.** Stay narrow.
- **Don't restructure the help text** (e.g., into sections, or
  alphabetical reordering of flags). The current shape is fine;
  just fill in the missing block.
- **Don't add long-form documentation** (e.g., file-format
  details, address-range explanations). One-line-per-spec is the
  convention; users read the device docs for depth.

## Workflow

```bash
cd /disk1/.../work/dcemu/github/sw-embed/sw-cor24-emulator
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/document-spi-device-cli-flag
$EDITOR cli/src/run.rs   # or wherever the help string lives
cargo build --release
./target/release/cor24-emu --help | grep spi-device   # sanity check
git commit -am "docs(cli): document --spi-device in cor24-emu --help"
git branch -m feat/document-spi-device-cli-flag pr/document-spi-device-cli-flag
```

## Cross-repo coordination

After this lands and mike rebuilds `work/bin/cor24-emu`, dcxas can
resume their `pr/spi-sdcard-and-nor-flash-demos` saga without the
"is this flag real?" doubt. No other agent is blocked on this; it's
purely a doc-discoverability fix.

## A small note on agent verification methodology

For future briefs (especially around new CLI surface): listing the
exact `cor24-emu --help | grep <thing>` check as the acceptance
criterion in the parent brief is the most durable way to catch this
class of bug. The original
`8-dcemu-spi-sdcard-and-nor-flash.md` did list "Help text update"
under each device, but didn't include the verification command.
Adding `--help | grep ...` to acceptance for any future
flag/option addition keeps both author and reviewer honest.

(This is meta-feedback for future briefs; nothing to fix in the
existing brief retroactively.)
