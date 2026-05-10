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

> **Status:** Format constraints are now verified against the
> upstream makerlisp loader source at `cc24/demo/loadngo/loadngo.c:166`.
> Sections previously marked "open questions" or "low-but-not-zero
> risk" have been replaced with definite findings.

## ELI5

A `.lgo` file is a text file. Each line is one record. The loader
walks the file top to bottom and acts on each record. The most
common record is `L` ("load these bytes at this address"); there
are two others (`G` for jump/call, `;` for comment) and **no
others**.

Per-line format for the load record:

```
L 000048 4D093800C815164D0938007D29070000250C034D097D
^   ^      ^
|   |      `-- 72 hex chars = 36 bytes = 12 24-bit words of data
|   `--------- 6 hex chars = 24-bit address (uppercase only)
`------------- literal "L" tag, marks "load this record"
```

It's in the same family as Intel HEX and Motorola S-records — but
much simpler than either. No checksum, no byte count, no extended
addressing, no end-of-file marker.

## How `.lgo` files get loaded

Two consumers:

1. **`cor24-emu`** (our emulator, in `sw-cor24-emulator`).
   Invoked as `cor24-emu --lgo prog.lgo [...]`. It allocates simulated
   COR24 SRAM in a fresh OS process (zero-initialized), reads the
   `.lgo` line by line, and writes each `L`-record's data at the
   named address. Then it begins execution.

2. **The COR24 FPGA board** (hardware, makerlisp.com).
   The board's `loadngo` program at `cc24/demo/loadngo/loadngo.c:166`
   reads the `.lgo`, dispatches each line by its first character, and
   directly writes bytes from `L` records to memory via
   `*(char *)lodadr = ...; lodadr++;`.

Both consumers see the **same file format**. We control consumer #1.
We do **not** control consumer #2 — the makerlisp tooling is upstream
we consume. **Anything we do to the `.lgo` format must remain
loadable by makerlisp's `loadngo`**, or we fork the format and our
outputs only run in our emulator.

## Verified format spec (from `loadngo.c`)

- **Three record types**, distinguished by the first character of
  the line:
    - `L<addr><data>` — load bytes at the 6-hex-digit address
    - `G<addr>` — jump/call to the 6-hex-digit address
    - `;...` — comment line, skipped
- **Any other first character** triggers `"? Unknown command:"` —
  unknown record types are not silently ignored. **We cannot invent
  new record types.**
- Hex digits are **uppercase only** (`0-9A-F`). Lowercase is
  rejected by `hexnum()`.
- **Address is exactly 6 hex chars**, zero-padded. Any byte address
  is accepted (no alignment requirement).
- **Line length cap: 80 characters total including newline** (`LINSIZ
  = 81` minus the terminator). For `L` records that gives:
  `L` (1) + addr (6) + data ≤ 72 hex chars = 36 bytes per record.
  Existing `cor24-asm` output uses this exact maximum.
- **Minimum `L` record:** at least one full byte (2 hex chars) of
  data. `L00000000` is the smallest legal load.
- **No checksum.** No byte count field. No EOF marker. No extended
  addressing.
- **No address ordering or contiguity requirement.** Records can be
  sparse or out of order. Later writes overwrite earlier ones at
  the same address.
- **Malformed records are partially destructive.** The loader writes
  the high nybble of a byte before validating the low nybble. An
  odd-nybble line will write half a byte before reporting the error.
  → Don't ever produce malformed lines.

## Where `.lgo` is *produced* in our codebase

Effectively one place:

- **`sw-cor24-x-assembler/src/lgo.rs`** — the canonical emitter.
  `cor24-asm` invokes it to write the `.lgo` alongside the `.bin`
  output. Every compiler in the toolchain (PL/SW, SNOBOL4 runtime,
  Fortran chain, etc.) routes through `cor24-asm`, so this single
  module determines the .lgo shape for the whole ecosystem.

- **`sw-cor24-snobol4/scripts/bin-to-lgo.sh`** — a 30-line shell
  helper that converts a raw `.bin` to `.lgo` via `printf
  "L%06X%s\n"`. Auxiliary path; same record format.

That's it. Nothing else in the workspace emits `.lgo` records.
This means any .lgo-shaping work — adding flags, post-processors,
or a compact-output mode — has a single load-bearing change site.

## Why we care about size

Empirical: production `build/plsw.lgo` (the PL/SW compiler running
on COR24) is 1,657,430 bytes today. Of its 20,718 `L` records,
**19,178 (92.6%) are pure-zero blocks**: `L<addr>0000…00` with no
non-zero bytes. They come from PL/SW's pre-allocated static buffers
(AST node pools, `chunk_storage`, etc.) which the C source declares
zero-init, and the compiler-asm-loader chain materializes those
zeros literally in the file because the loader will not provide
them otherwise (see next section).

Concrete example:
```
L00B58C000000000000000000000000000000000000000000000000000000000000000000000000
```
There are 19,178 lines like that, contiguous, accounting for the
bulk of the file.

## Why pure-zero `L` records exist (and can't just be deleted)

`loadngo.c` is **passive**: it writes only the bytes named in `L`
records and never touches any other address. Specifically, **it does
not pre-zero SRAM**. So if a C `static int x[N]` array (zero-init by
language spec) is to actually contain zeros at program start, *some*
mechanism has to put zeros at those addresses. Today's chain puts
that responsibility on the `.lgo`: cor24-asm emits explicit `L`
records full of zeros, and the loader writes them.

Stripping zero-only `L` records moves the zero-init responsibility
from "the .lgo file" to "whatever-came-before-the-load." That's
**only safe in environments that independently zero SRAM before the
load runs**:

| Loading context | Memory state at load time | Compact .lgo safe? |
|---|---|---|
| `cor24-emu` (fresh OS process) | Zero (Linux page allocation) | ✅ Always safe |
| FPGA cold boot, post-bitstream-config, pre-anything-running | Zero (BRAM init from FPGA bitstream) | ✅ Safe |
| FPGA warm reboot, hot reload, second `loadngo` invocation | Whatever previous program left in SRAM | ❌ **Not safe** |
| Loading on top of a paused-but-resident program | That program's data | ❌ Not safe |

The third and fourth rows are the gotchas. A user who power-cycles
the FPGA before each load is fine with compact .lgo; a user who
reuses a running loader to swap programs is not.

## Three approaches to shrinking `.lgo`, ranked

| Approach | Format change? | Hardware risk | File-size win | Verdict |
|---|---|---|---|---|
| **Add a new record type** (e.g. `Z<addr><N>` "fill N zero bytes") | YES | **Categorical** — `loadngo` rejects unknown commands as `"? Unknown command:"` | Up to ~13× | **Off the table.** Format divergence; would only run on our emulator. |
| **Omit zero-only `L` lines** (no syntax change; loader sees gaps) | NO | **Conditional** — safe on cor24-emu always; safe on hardware only at cold boot | ~13× | **Conditionally safe.** Recommended path; environment-dependent. |
| **Status quo** (emit every word, including all-zero blocks) | NO | None | None | Always works. Default until we deliberately compact. |

## Recommended path (when we choose to act)

Stated for the record; not a brief, not a commitment. Now grounded
by the verified loader contract.

1. **Don't modify `cor24-asm`'s default output.** The default `.lgo`
   stays makerlisp-compatible (full form) so it loads under any
   condition. No regression risk for existing or new hardware
   workflows.
2. **Add a `--compact` flag** to `cor24-asm` (in
   `sw-cor24-x-assembler/src/lgo.rs`), or alternatively a separate
   post-processor (`work/bin/lgo-compact`) that takes a full `.lgo`
   and emits one with pure-zero `L` lines stripped. Either path is
   pure text filtering — no new record types, no syntax change.
   Single-repo change either way.
3. **Use compact `.lgo` with `cor24-emu`**. The emulator runs in a
   fresh OS process; SRAM is zero before any `L` writes. Compact is
   unconditionally safe in this environment, and `cor24-emu` is
   where most current usage lives anyway (hardware not yet in hand).
4. **Use full `.lgo` for FPGA hardware until cold-boot semantics
   are explicitly confirmed in the deploy workflow.** If your
   hardware workflow always power-cycles before loading, compact is
   safe; if it ever hot-reloads, full is required. Document the
   policy explicitly so it doesn't drift.
5. **Round-trip verify** before any default flip: take a known-good
   full `.lgo`, run the compactor, load the compact form in
   cor24-emu, compare program behavior. (Already gives strong
   evidence for emulator path; weak evidence for hardware path
   because the two loaders may differ in pre-state assumptions.)
6. **No need to touch `bin-to-lgo.sh`** unless you want a compact
   shell path too. It's auxiliary; if it stays as a full-form
   producer that's fine.

Format constraints any compactor must respect (these are firm, from
`loadngo.c`):

- Only emit `L`, `G`, or `;` lines. No new tags.
- Hex uppercase only.
- Lines ≤ 80 chars including newline.
- Preserve existing line lengths and structure of non-zero records;
  the simplest correct compactor is a *line filter* (drops zero-only
  lines verbatim, leaves all others untouched).
- Don't reorder records (legal per the loader, but pointless and
  reduces human readability).
- Preserve `G` records and their positions exactly.
- Preserve `;` comments exactly (or strip them as a separate
  optional pass — they're already small).

## What this means for our plsw.lgo size goal

The 92.6% zero share is real and recoverable in cor24-emu (our
primary near-term consumer) without any format change. A line-filter
compactor would shrink plsw.lgo from 1.66 MB to ~125 KB for emulator
use. That's a 13× win, available with one repo change in
`sw-cor24-x-assembler/src/lgo.rs`.

But for hardware deployment, **the .lgo file size win is a workflow
choice, not a free architectural improvement.** Every byte we strip
is a byte the *environment* (FPGA cold-boot zero) has to provide.
That's a contract about deployment, not a contract about file
format.

For genuine SRAM-footprint reduction at runtime (i.e. less actual
memory used by the running program, not just smaller files on
disk), the path remains **dcpls's chunk-allocator architecture
plan** in `sw-cor24-plsw/docs/shrink-lgo-size.md`. Those phases
shrink the *amount* of zero-init storage actually needed, not just
its file-encoding size. A program with a smaller AST pool and
demand-allocated buffers fits in less SRAM regardless of `.lgo`
shape.

The two efforts are complementary and independent:
- `.lgo` compaction → smaller files on disk, faster loads,
  emulator-default win, hardware win at cold boot only.
- chunk-allocator phases → smaller runtime SRAM footprint,
  compatible with both full and compact .lgo, makes the program
  itself smaller regardless of how it's encoded.

## Format reference (for anyone implementing a tool that touches `.lgo`)

- One record per line.
- Each record's first character is `L`, `G`, or `;`. **No others.**
- For `L` records:
  - 6 uppercase hex address chars (24-bit address).
  - Followed by hex data bytes, 2 hex chars per byte. Typical line
    carries 12 24-bit words = 36 bytes = 72 hex chars of data.
  - At least 1 byte of data required.
  - Line ≤ 80 chars including newline.
- For `G` records:
  - 6 uppercase hex address chars.
  - No payload after the address.
  - Loader jumps/calls to the named address.
- For `;` records:
  - Free-form text after the `;`. Ignored by the loader.
- No leading whitespace, no trailing whitespace, terminated by `\n`.
- Address increments between consecutive `L` lines are typically
  `0x24` (36 decimal) when describing contiguous memory; gaps in
  the address sequence are syntactically permitted but
  *semantically* mean those addresses retain their pre-load state
  (see compaction safety table above).
- The entry point is conveyed by a `G` record at the end of the
  file (or wherever execution should begin); cor24-emu's `--entry`
  flag overrides if present.

This document was prepared 2026-05-10 from inspection of
`sw-cor24-plsw/build/plsw.lgo`, `sw-cor24-fortran/examples/hello.lgo`,
and the canonical loader source at
`~/tools/cor24io/cc24/demo/loadngo/loadngo.c:166`. Producer-side
findings come from a workspace-wide search; the only `.lgo`-emitting
modules are `sw-cor24-x-assembler/src/lgo.rs` and
`sw-cor24-snobol4/scripts/bin-to-lgo.sh`.
