# Brief: add `--lgo-compact` / `--lgo-full` flags to `cor24-asm`

**Owner:** dcxas
**Branch:** `pr/lgo-compact-flag`
**Repo:** `sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-10, after `docs/lgo-format.md` analysis verified the makerlisp loader contract).

## Context

Today's `cor24-asm` always emits a "full" `.lgo`: every byte of every
`.zero` / zero-init-data region is materialized as literal zero
hex in the output stream. For the production PL/SW compiler image
this means **92.6% of `plsw.lgo`'s 20,718 lines are pure-zero `L`
records** (`L<addr>0000…00`), totalling ~1.6 MB of the 1.66 MB file.

The makerlisp `loadngo.c` loader (`cc24/demo/loadngo/loadngo.c:166`)
is **passive** — it writes only what `L` records name and never
pre-zeros SRAM. So those zero-only `L` records exist because the
zero-init contract demands them at runtime, not because they're a
missed compiler optimization.

That gives us a clean dichotomy:

- A `.lgo` that includes all zeros (today's output) loads correctly
  in **any** environment.
- A `.lgo` that omits pure-zero `L` records loads correctly only in
  environments where SRAM is independently zeroed before the load
  runs: `cor24-emu` (always — fresh OS process), FPGA cold boot
  (BRAM zero from bitstream config). Not safe on warm reload.

We want both options available to the build pipeline. **This brief
proposes a CLI flag pair that selects between the two**, with the
conservative shape as default.

See `docs/lgo-format.md` in this repo for the full format/safety
analysis. This brief is the implementation companion.

## What changes

Add a flag pair to `cor24-asm`:

- **`--lgo-full`** — emit every `L` record, including pure-zero
  blocks. Output is loadable anywhere `loadngo` runs. **This is
  the default.**
- **`--lgo-compact`** — omit `L` records whose entire data payload
  is `00` bytes. Output is loadable in `cor24-emu` always and on
  FPGA cold boot; not safe on warm reload.

`--lgo-full` and `--lgo-compact` are **mutually exclusive**. If
both are passed, exit with a clear error.

If neither is passed, `--lgo-full` is the implicit choice. No
existing caller (anywhere in the workspace) breaks.

## Why default to `--lgo-full`

- **Conservative.** Matches today's behavior bit-for-bit. No caller
  in any repo regresses.
- **Hardware-safe by default.** Once the FPGA arrives, anyone
  running `cor24-asm` for hardware target gets a full `.lgo`
  without needing to remember a flag.
- **Easy to flip later.** Once the hardware deploy workflow is
  formally cold-boot-only, project policy can change the default
  with a one-line edit. Until then, the conservative choice carries
  no risk.
- **Opt-in compaction matches cost.** `--lgo-compact` is a
  build-pipeline-level decision (the pipeline knows what consumer
  it's targeting); making it explicit at the call site documents
  the choice in scripts.

## Where to make the change

Single load-bearing module: **`src/lgo.rs`** — the canonical `.lgo`
emitter. Per a workspace-wide search, this is the only place
`L<addr><data>` records are produced (the small auxiliary
`sw-cor24-snobol4/scripts/bin-to-lgo.sh` is unaffected and stays
full-form).

CLI surface: **`cli/src/main.rs`** — add the two flags using
whatever clap/argparse pattern the existing CLI uses, wire through
to a struct field passed to the emitter.

The actual emission logic in `lgo.rs` is a one-conditional change.
At each `L`-record emission, if `mode == Compact` and the data
payload is all `0x00`, skip the write. Otherwise emit normally.

Pseudocode shape (adapt to existing emitter structure):

```rust
pub enum LgoMode { Full, Compact }

fn emit_l_record(out: &mut impl Write, addr: u32, data: &[u8], mode: LgoMode)
    -> io::Result<()>
{
    if mode == LgoMode::Compact && data.iter().all(|&b| b == 0) {
        return Ok(());           // skip pure-zero L record
    }
    write!(out, "L{:06X}", addr)?;
    for byte in data {
        write!(out, "{:02X}", byte)?;
    }
    writeln!(out)?;
    Ok(())
}
```

`G` records and `;` comments are unaffected — always emitted in
both modes.

## Format constraints to preserve

From `docs/lgo-format.md` (verified against the loader source):

- Hex must be **uppercase** (`0-9A-F`). Use `{:02X}` / `{:06X}`.
- Lines ≤ 80 chars total including newline. Today's `L` records hit
  72 hex chars of data exactly; **don't change that** — a compactor
  that filters whole lines preserves it trivially.
- Don't reorder records. Don't merge them. Don't introduce new
  record types.
- Pure-zero detection is on the **data payload only**, not the
  address. A line with non-zero data but address `0x000000` still
  emits.

## Tests

Add to `tests/integration_tests.rs` (or wherever the existing CLI
integration tests live):

1. **`lgo_full_default`** — invoke `cor24-asm` on a fixture with
   known zero-init data; assert output matches today's bit-for-bit.
   Catches accidental default changes.

2. **`lgo_compact_strips_zero_lines`** — invoke `cor24-asm
   --lgo-compact` on the same fixture; assert no `L` line in the
   output has all-zero data. Use a regex like
   `^L[0-9A-F]{6}0+$` should match zero lines in the file.

3. **`lgo_compact_preserves_nonzero`** — assert every non-zero `L`
   record present in `--lgo-full` output is also present
   (byte-identical line) in `--lgo-compact` output.

4. **`lgo_compact_preserves_g_records`** — fixture with at least
   one `G` record; both modes emit it identically.

5. **`lgo_full_and_compact_mutex`** — `--lgo-full --lgo-compact`
   together → non-zero exit, error message mentioning both flags.

6. **`lgo_compact_round_trip`** — take a known program; assemble
   with `--lgo-full` and `--lgo-compact`; load each in `cor24-emu`
   (which has fresh-zero SRAM) and run; assert observable behavior
   (UART output, return code, etc.) is identical. This is the
   semantic safety check.

## Smoke test mike will run after install

```
# Same source, two outputs
cor24-asm fixture.s -o full.lgo
cor24-asm fixture.s --lgo-compact -o compact.lgo

# Compact should be substantially smaller; full should match today
ls -la full.lgo compact.lgo

# Both should produce identical execution under cor24-emu
cor24-emu --lgo full.lgo --quiet > full.out
cor24-emu --lgo compact.lgo --quiet > compact.out
diff full.out compact.out  # expect empty diff
```

For the realistic test case, use plsw.lgo's source: assemble with
both modes from `build/plsw.s`, expect compact to be roughly 1/13
the size of full, and expect cor24-emu execution to match.

## What "mike installs" means

After `dg-relay` + `dg-release` land this PR on
`sw-cor24-x-assembler/main`, mike rebuilds and reinstalls
`cor24-asm` from the relay clone:

```bash
cd /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-x-assembler
cargo build --release --manifest-path cli/Cargo.toml
install -m 0755 cli/target/release/cor24-asm \
  /disk1/github/softwarewrighter/devgroup/work/bin/cor24-asm
```

(Adapt the `--manifest-path` to whatever the actual CLI crate path
is in this repo; it's not the same layout as
`sw-cor24-x-tinyc/components/cli/crates/tc24r/Cargo.toml`.)

After install, no immediate behavior change for any caller —
defaults are unchanged. Compactor-using callers opt in by passing
`--lgo-compact` in their build scripts.

## Out of scope

- **No changes to the .lgo format.** No new record types. The
  compact `.lgo` is a strict subset of the full `.lgo`; both are
  valid format-wise.
- **No changes to `cor24-emu`** — it already handles sparse address
  ranges (writes only what's named, leaves the rest at OS-zeroed
  default).
- **No changes to `bin-to-lgo.sh`** in sw-cor24-snobol4. It stays
  full-form; if anyone wants compact-form output from that path,
  the right approach is to pipe through `cor24-asm --lgo-compact`
  rather than complicate the shell helper.
- **No automatic flip of build-pipeline defaults.** Whether
  `dcpls`'s `just build-lgo`, `dwftn`'s pages build, etc. start
  passing `--lgo-compact` is a per-repo decision tracked by the
  consuming agent's brief, not this one.
- **No new docs file beyond what `docs/lgo-format.md` already
  covers.** This brief implements; the doc explains.

## When done

- `cor24-asm --lgo-compact` available, defaults unchanged.
- Production callers can opt in to compact form when their target
  is `cor24-emu` or guaranteed-cold-boot hardware.
- File-size win realized for `cor24-emu`-bound builds (likely
  `plsw.lgo`-rebuild from PL/SW pipeline first, opt-in via the
  pipeline's just recipe — separate brief).
- Hardware-targeted builds keep using full output by default; no
  change in their behavior.
- Once the FPGA arrives and cold-boot-only deploy is verified, a
  follow-up one-line PR can change the project default to compact
  if desired. That decision is *out of scope here*.
