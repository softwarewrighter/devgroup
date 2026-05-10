# Brief: shrink `snobol4.lgo` by stripping zero-only `L` records in `bin-to-lgo.sh`

**Owner:** dcsno
**Branch:** `pr/snobol4-lgo-compact`
**Repo:** `sw-cor24-snobol4`
**Drafted by:** mike (2026-05-10).

## Context

After today's `pr/builtin-arg-expressions` rebuild, the installed
`snobol4.lgo` is 374,456 bytes / 4,681 records. Of those records,
**3,524 (75.3%) are pure-zero `L` blocks** — `L<addr>0000…00` lines
that materialize zero-init data regions byte-by-byte. Stripping
those lines would shrink `snobol4.lgo` from ~374 KB to ~92 KB
(roughly 4× smaller).

The format constraint analysis is in `devgroup/docs/lgo-format.md`
(verified against `cc24/demo/loadngo/loadngo.c:166`). Short
summary for context:

- The `.lgo` format has three record types only: `L`, `G`, `;`.
  No new record types may be added.
- The makerlisp loader (and `cor24-emu`) writes only what `L`
  records name; never pre-zeros SRAM.
- Stripping pure-zero `L` lines is **safe** when the loader's
  destination memory is independently zero-initialized:
  - `cor24-emu` (fresh OS process) — always safe.
  - FPGA cold boot (BRAM zero from bitstream) — safe.
  - Warm reload / hot replacement — **not safe**.

For `snobol4.lgo`'s consumer (the `snobol4` wrapper script invoking
`cor24-emu --lgo`), the destination is always a fresh emulator
process. Compact form is unconditionally safe in current usage.

## Why this brief exists

A parallel brief `dcxas-lgo-compact-flag.md` adds `--lgo-compact`
to `cor24-asm`. **That flag does not help here** because
`snobol4.lgo` is not produced by `cor24-asm` directly — it's
produced by `scripts/bin-to-lgo.sh`, a shell helper that converts
the post-`link24` raw `.bin` to `.lgo` line by line. Per the script
comment header, the long-term plan is to retire it once `link24`
emits `.lgo` natively. That's a larger refactor; this brief is the
near-term win.

The build chain today:

```
src/sno_*.plsw  ─pl-sw─→  build/mod/sno_*.s
                          ↓ cor24-asm (per module)
                          build/mod/sno_*.bin + .meta
                          ↓ link24 (multi-module)
                          build/snobol4.bin   ← raw 24-bit binary
                          ↓ scripts/bin-to-lgo.sh  ← THIS SCRIPT
                          build/snobol4.lgo   ← what gets installed
```

`bin-to-lgo.sh` is a 30-line shell script. The change is small.

## What to change

`scripts/bin-to-lgo.sh` currently emits every `L` record:

```bash
xxd -p -c 36 "$IN" | awk '
{
    upper = toupper($0)
    printf "L%06X%s\n", addr, upper
    addr += length($0) / 2
}
' > "$OUT"
```

Add a flag (or a default mode change) that skips pure-zero lines.
Suggested approach — match the `cor24-asm` flag pair semantically:

```bash
# Default --lgo-compact (safe for cor24-emu).
# Pass --lgo-full for hardware-targeted builds when cold-boot
# semantics aren't guaranteed.

MODE="${MODE:-compact}"   # or accept --lgo-full / --lgo-compact CLI flags

xxd -p -c 36 "$IN" | awk -v mode="$MODE" '
{
    upper = toupper($0)
    if (mode == "compact" && upper ~ /^0+$/) {
        # skip pure-zero block
    } else {
        printf "L%06X%s\n", addr, upper
    }
    addr += length($0) / 2
}
' > "$OUT"
```

**Default-mode question for dcsno to decide:** mirror cor24-asm's
choice (`--lgo-full` default, `--lgo-compact` opt-in) or default to
compact since the only consumer is cor24-emu. Either works; the
recommendation is **default to `--lgo-full`** for the same
conservative reasons as in `dcxas-lgo-compact-flag.md`:

- Matches today's behavior bit-for-bit (no install regression).
- Hardware-safe by default once the FPGA arrives.
- Build pipelines that want compact opt in explicitly via
  `MODE=compact` env var or `--lgo-compact` flag, documenting the
  choice in scripts.

The `justfile`'s `build-lgo` recipe (or wherever this script is
invoked) can then pass `--lgo-compact` once a downstream consumer
specifically wants it. For now, the build keeps producing full
`.lgo` and someone (mike) opts into compact at install time if
they want.

## Format constraints to preserve

From `docs/lgo-format.md`:

- Output must contain only `L`, `G`, or `;` records. (This script
  only emits `L`; that doesn't change.)
- Hex must be uppercase. The current `toupper($0)` handles this
  correctly; preserve.
- Lines ≤ 80 chars total. Current output uses
  `L + 6 addr + 72 hex data = 79` chars exactly; preserve. A line
  filter doesn't change line widths.
- Don't introduce new syntax. A compact `.lgo` is a strict subset
  of a full `.lgo` — every line in compact form is a syntactically
  identical valid `L` record.

## Tests

In `tests/` (or wherever the existing build tests live):

1. **`test_full_default`** — invoke `bin-to-lgo.sh` (no flag) on
   a known fixture; assert output matches today's bit-for-bit.

2. **`test_compact_strips_zero_lines`** — invoke with
   `--lgo-compact` (or `MODE=compact`); assert no line in output
   matches `^L[0-9A-F]{6}0+$`.

3. **`test_compact_preserves_nonzero`** — every non-zero line
   present in full output is also present byte-identically in
   compact output.

4. **`test_compact_round_trip`** — assemble `snobol4.bin` →
   produce both `snobol4-full.lgo` and `snobol4-compact.lgo` →
   load each via `cor24-emu --lgo` → run a known program (e.g.
   `examples/hello.sno`) → assert UART output and exit are
   identical. This is the semantic safety check for compact mode.

5. **`test_size_reduction`** — assert compact form is at least 50%
   smaller than full form for the canonical `build/snobol4.bin`.
   (Empirically it should be ~75% smaller; 50% is a conservative
   regression gate.)

## What "mike installs" means

The script change is internal to `sw-cor24-snobol4`. After
`dg-relay` + `dg-release`:

1. mike pulls `main` in the relay clone.
2. Runs `just rebuild` then `just build-lgo` (full mode by default).
3. Optionally re-runs `bin-to-lgo.sh build/snobol4.bin
   build/snobol4.lgo --lgo-compact` if the install target is
   cor24-emu-only.
4. `install -m 0640 build/snobol4.{lgo,bin}
   /disk1/.../work/lib/cor24/`.

Whether mike installs the full or compact form is a per-deploy
decision. For the current ecosystem (no FPGA hardware yet), compact
is fine and immediately gives ~75% smaller `snobol4.lgo` on disk.

## Out of scope

- **No changes to `cor24-asm`** — that's the parallel
  `dcxas-lgo-compact-flag.md` brief's territory.
- **No changes to `link24`** — it stays raw-binary-only for now.
  The longer-term plan to make link24 emit `.lgo` natively (per
  the `bin-to-lgo.sh` header comment) would retire this script
  entirely; that's a separate larger brief.
- **No changes to `snobol4.bin` size or contents.** The `.bin`
  is the runtime image; that doesn't shrink. Only the `.lgo`
  text-encoded delivery format shrinks. SRAM footprint is
  unchanged either way.
- **No new record types.** Format-incompatible changes are
  categorically off the table (loader rejects unknown commands).
- **No automatic flip of build defaults.** `just build-lgo` can
  keep producing full-form `.lgo`; the build pipeline opts into
  compact at the call site or at install time.

## When done

- `bin-to-lgo.sh` accepts an opt-in compact mode (flag or env var).
- Tests gate the compact / full modes.
- `snobol4.lgo` can be produced in full or compact form on demand.
- For cor24-emu-only deployments, mike can install compact and
  enjoy ~4× smaller `snobol4.lgo` on disk with identical execution
  behavior.
- No regression for full-form callers.
- No format-compatibility regression — compact `.lgo` is still a
  valid `.lgo` per the `loadngo.c` contract.
