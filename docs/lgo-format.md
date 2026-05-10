# The `.lgo` load-image format

This document describes what `.lgo` files are, how they're consumed,
and what compatibility constraints we operate under given that the
real COR24 hardware is from makerlisp.com (not a part of this repo).
It also lays out the safe path to making `.lgo` files much smaller
without breaking format compatibility — a practical concern because
production `plsw.lgo` is currently 1.66 MB, of which ~92.6% is
literal zeros.

> **No action proposed in this document.** It exists to capture the
> analysis so any future brief / saga / decision starts from shared
> ground.

## ELI5

A `.lgo` file is a text file. Each line is one tiny instruction to a
loader: *"starting at memory address X, write these 12 words of data
into memory."* The loader walks the file top to bottom, writes each
block at its address, and is done.

Per-line format (one record per line):

```
L 000048 4D093800C815164D0938007D29070000250C034D097D
^   ^      ^
|   |      `-- 72 hex chars = 36 bytes = 12 24-bit words of data
|   `--------- 6 hex chars = 24-bit address
`------------- literal "L" tag, marks "load this record"
```

Each `L` line writes exactly 12 24-bit words (= 36 bytes) starting at
the given address. Consecutive lines normally have addresses that
differ by `0x24` (= 36 decimal), so a typical file describes a
contiguous memory region 36 bytes at a time.

This format is in the same family as Intel HEX and Motorola S-records
— text-based load images that ROM programmers, serial bootloaders,
and FPGA configuration tools have used for decades. Simple,
ASCII-safe, easy to ship over a serial port, easy to inspect by eye.

## How `.lgo` files get loaded

Two kinds of consumer:

1. **`cor24-emu`** (our emulator, in `sw-cor24-emulator`).
   Invoked as `cor24-emu --lgo prog.lgo [...]`. It allocates simulated
   COR24 SRAM (zero-initialized), reads the `.lgo` line by line, and
   writes each `L`-record's data at the named address. Then it begins
   execution.

2. **The COR24 FPGA board** (hardware, from makerlisp.com).
   The board's bootloader (typically `as24` / `cc24`-flavor tooling
   on the host side, plus on-board firmware) reads the `.lgo`,
   transmits each `L` record to the board (e.g. over serial), and the
   board's loader writes each block into SRAM. Then it kicks off
   execution.

Both consumers see the **same file format**. We control consumer #1.
We do **not** control consumer #2 — the makerlisp toolchain and FPGA
bitstream are upstream artifacts we consume. **Anything we do to the
`.lgo` format must remain loadable by makerlisp's tooling**, or we
fork the format and our outputs only run in our emulator.

## Why we care about size

Empirical: production `build/plsw.lgo` (the PL/SW compiler running on
COR24) is 1,657,430 bytes today.

Counting the lines:

| Metric | Value |
|---|---|
| Total `L` lines | 20,718 |
| Pure-zero lines (entire 12-word block is `0000…00`) | **19,178 (92.6%)** |
| Bytes in zero-only lines | ~1,612,000 |

The file is almost entirely zeros. They come from PL/SW's
pre-allocated static buffers (AST node pools, `chunk_storage`, etc.)
that are declared but unused at compile time and so emit as
`.zero N` in the assembly source — and `cor24-asm` faithfully
materializes them as literal zero bytes in the `.lgo`.

Concrete sample of a zero-only line:

```
L00B58C000000000000000000000000000000000000000000000000000000000000000000000000
```

There are 19,178 of those, contiguous, accounting for the bulk of
the file.

## Compatibility analysis: three approaches to shrinking `.lgo`

| Approach | Format change? | Hardware risk | File-size win |
|---|---|---|---|
| **Add a new record type** (e.g. `Z<addr><N>` "fill N zero bytes") | **YES** | **High** — breaks any loader that doesn't recognize the new tag | ~13× |
| **Omit zero-only `L` lines** (no new syntax; loader simply sees a gap in addresses) | NO | **Low-but-not-zero** — depends on the loader's behavior for addresses *not* mentioned in the file | ~13× |
| **Status quo** (emit every word, including all-zero blocks) | NO | None | None |

### Why "add a new record type" is the wrong path

Any loader that doesn't understand the new tag will either:

- Hard-error on the unrecognized line (bricked load).
- Silently skip it (the zero-init region is left as whatever was in
  SRAM at boot — undefined behavior).
- Treat it as ASCII data of some other kind (corrupted load).

makerlisp's bootloader is the unknown. Until we have FPGA hardware
in hand and have read or experimented with their loader, we have to
assume any unrecognized line type is fatal. **A `.lgo` with a
`Z<addr><N>` line stops being a `.lgo` — it's a different format that
happens to look similar.**

### Why "omit zero-only lines" is the candidate

A compacted `.lgo` produced by stripping zero-only lines is still
**a strict subset** of a full `.lgo`. Every line in it is a
syntactically and semantically valid `L<addr><data>` record under
the original format. Nothing new gets introduced.

The only behavioral change: the loader receives fewer records, and
for some address ranges, no record at all. Whether that's safe
depends on **the loader's contract for un-named addresses**:

- If the loader (or the hardware it loads onto) **assumes SRAM is
  zero before loading** — typical of FPGA BRAM, which is
  zero-initialized at FPGA configuration time — then omitting
  zero-only lines is safe and equivalent in effect.
- If the loader assumes nothing about pre-state, or some addresses
  may have stale data from a previous session — omitting lines could
  leave undefined values in those regions.

For `cor24-emu`: the emulator allocates SRAM as zero-init in fresh
process memory, so omitted lines effectively *are* zero. Almost
certainly safe; can be confirmed quickly with a round-trip test.

For the FPGA board: virtually always safe in practice (BRAM
zero-init at config), but **not formally verified for makerlisp's
loader**. Needs hardware-in-hand testing.

### Why status quo is OK in the meantime

The current 1.66 MB `.lgo` works. It loads on the emulator, would
load on hardware. Disk space and load-time cost is annoying but not
blocking. Nothing forces a near-term decision.

## Recommended path (when we choose to act)

Stated for the record; not a brief, not a commitment:

1. **Do not modify `cor24-asm`'s default output.** Its emitted `.lgo`
   stays the makerlisp-compatible full form. No regression risk for
   hardware.
2. **Add a separate post-processor** — e.g. `work/bin/lgo-compact` or
   a small Rust binary alongside `cor24-asm` — that takes a full
   `.lgo` and produces a compact one with pure-zero lines removed.
   Pure text filtering. No format change.
3. **Use compact `.lgo`s with `cor24-emu`** by default (we control
   the emulator and can verify gap-handling behavior).
4. **Continue using full `.lgo`s for FPGA hardware** until physical
   testing on the makerlisp board confirms the bootloader treats
   unspecified addresses as zero. After verification, flip the
   default; before then, don't risk it.
5. **Round-trip verify on `cor24-emu` first**: take a known-good
   full `.lgo`, run `lgo-compact`, load the compact form, check
   program behavior matches the full form. That's strong evidence
   for the emulator path; weak evidence for the hardware path
   (because the two loaders may behave differently on gap addresses).
6. **Once FPGA hardware is in hand**, run the same round-trip on
   real hardware before switching the default for hardware-targeted
   builds.

This way:

- File-size win (~13×) is realized immediately for emulator runs.
- Hardware loading is untouched.
- The format stays singular — compact `.lgo` is just a *subset* of
  full `.lgo`, never a divergent dialect.
- No flag day, no incompatibility window, no upstream surprise.

## Open questions

- **Does `cor24-emu` actually treat un-named addresses as zero
  today?** Highly likely (process memory is zero-init), but worth
  confirming by reading `sw-cor24-emulator/src/loader.rs` (or
  wherever the `--lgo` consumer lives). If it instead errors on
  gaps, the post-processor approach needs the emulator side updated
  in lockstep — still no format change, just loader-tolerance work.
- **What does makerlisp's hardware bootloader do for un-named
  addresses?** Unknown until we have hardware to test on.
  Conservatively, assume "undefined" and don't ship compact `.lgo`
  to hardware without verification.
- **Is there a `cor24-asm` flag or post-processor convention that
  upstream already supports for this?** Worth a quick scan of
  upstream docs (makerlisp.com) before we invent our own
  convention. If they already have a "compact" mode, we should
  reuse their tag/syntax rather than inventing parallel tooling.

## Format reference (for anyone implementing a tool that touches
`.lgo`)

- One record per line.
- Each record begins with literal `L`.
- Followed by a 6-character hexadecimal address (uppercase),
  zero-padded.
- Followed by hexadecimal data, 2 hex chars per byte. Typical line
  carries 12 24-bit words = 36 bytes = 72 hex chars of data.
- No leading whitespace, no trailing whitespace, terminated by `\n`.
- Address increments between consecutive lines are typically
  `0x24` (36 decimal) when describing contiguous memory; gaps in
  the address sequence are syntactically permitted but
  *semantically* depend on loader behavior (see above).
- Last record carries the entry point's data area; cor24-emu's
  default entry address is implementation-defined and not encoded
  in the file itself (passed via `--entry` or defaults).

This document was prepared 2026-05-10 from an inspection of
`sw-cor24-plsw`'s `build/plsw.lgo` and `sw-cor24-fortran`'s
`examples/hello.lgo`. It is descriptive of observed behavior, not
authoritative spec — for the canonical format definition, refer to
makerlisp.com upstream.
