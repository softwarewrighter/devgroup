# Brief: remove the internal assembler from `cor24-emu`

**Owner:** dcemu
**Branch:** `pr/remove-internal-assembler`
**Repo:** `sw-cor24-emulator`
**Depends on:** dcxas's `pr/cor24-asm-cli` relayed to `dev` first. **Do not start until `cor24-asm` is on PATH and produces `.lgo`, `.bin`, and `.lst` outputs.**

## Context

Today `cor24-emu` has two redundant assembler entry points backed by the in-tree `cor24_emulator::assembler` library (`src/assembler.rs`):

- `cor24-emu --run <file.s>` — internally assembles, then runs
- `cor24-emu --assemble <in.s> <out.bin> <out.lst>` — assembles only

With dcxas's standalone `cor24-asm` CLI now available, both modes are redundant — and a divergence risk (two Rust assemblers that can drift apart). This saga removes them and turns the emulator into a pure runtime consumer.

## Architectural boundary (project-wide convention)

- **`.lgo` reader/loader** — stays here. Already correct.
- **Assembler / writers (`.lgo`, `.bin`, `.lst`)** — live in `sw-cor24-x-assembler` (the `cor24-asm` CLI).
- **Runtime** — `cor24-emu` consumes `.lgo` (via `--lgo`) and arbitrary raw bytes at addresses (via `--load-binary`). It does not know how those artifacts were built.

## What to remove

1. `cor24-emu --run <file.s>` mode — argument parsing, dispatch, help text.
2. `cor24-emu --assemble <in.s> <out.bin> <out.lst>` mode — same.
3. `src/assembler.rs` (the internal assembler library code).
4. The `pub use` / re-exports of the assembler in `src/lib.rs` (and any `cor24_emulator::assembler::*` surface).
5. The `cor24_emulator::assembler` import in `cli/src/run.rs` and any other internal callers that referenced it solely for `--run` / `--assemble`.
6. Help-text examples that demonstrate the removed flags.
7. Tests that exist solely to test `cor24_emulator::assembler` (the library is gone).

## What to keep

- Everything else. Specifically:
  - `--demo`, `--lgo <file.lgo>`, `--load-binary <file>@<addr>` (repeatable), `--patch`, `--entry`
  - All UART / terminal / observability flags (`--uart-input`, `--uart-file`, `--terminal`, `--echo`, `--quiet`, `--dump`, `--dump-uart`, `--dump-i2c`, `--trace`, `--step`, etc.)
  - All execution-bound flags (`--speed`, `--time`, `--max-instructions`, `--guard-jumps`, `--canary`, `--watch-range`, `--switch`, `--uart-never-ready`, `--stack-kilobytes`, `--code-end`, `--base-addr`, `--i2c-device`)
  - The `cor24-dbg` binary (debugger CLI) — it never used the internal assembler. Untouched.
  - `src/loader.rs` (`.lgo` parser) — stays exactly as-is.

## Migration of every callsite using `--run` or `--assemble`

Find them first:
```bash
git grep -nE -- '--run|--assemble' -- 'scripts/' 'tests/' 'docs/' '*.md'
```

Migrate each:

- `cor24-emu --run prog.s [opts]` → `cor24-asm prog.s -o /tmp/prog.lgo && cor24-emu --lgo /tmp/prog.lgo [opts]`
  - Use a stable per-test build dir, not `/tmp`, when running in CI.
- `cor24-emu --assemble in.s out.bin out.lst` → `cor24-asm in.s --bin out.bin --listing out.lst`

Targets to inspect:
- `scripts/demo-cli-*.sh` (count_down, hello_world, led_blink, sieve)
- `scripts/rebuild-i2c-fixtures.sh`
- Any `tests/integration*.rs` or `tests/*.rs` invoking the binary
- `README.md`, `CLAUDE.md`, anything in `docs/` referencing the flags
- The example invocations in `cli/src/run.rs::print_short_help`

If `cor24-asm` isn't on PATH in your build environment, that's mike's infra concern — don't paper over it with `cargo build && target/release/cor24-asm` workarounds in tests. Surface it.

## Tests

The existing test suite should pass after the removal. Specifically:

- Tests for `cor24_emulator::assembler` will be deleted along with the module.
- Tests for `cor24-emu` invocations using `--run` / `--assemble` should be updated to call `cor24-asm` first, then `cor24-emu --lgo` (or `--load-binary`).
- Add a smoke test verifying neither `--run` nor `--assemble` appears in `--help` and that passing them prints a clear error and exits non-zero.

## What goes in this PR

1. Delete `src/assembler.rs` and remove its declarations from `src/lib.rs`.
2. Remove `--run` and `--assemble` arg handling and help-text entries in `cli/src/run.rs`.
3. Migrate every callsite to the new pipeline.
4. Update `README.md`, `CLAUDE.md`, and any docs that describe the now-removed flags.
5. Verify `cargo build`, `cargo clippy -- -D warnings`, and `cargo test` (workspace) still pass.

## What does NOT go in this PR

- No changes to `src/loader.rs` (the `.lgo` parser). It stays untouched.
- No changes to `sw-cor24-x-assembler` (that's dcxas's repo).
- No new `cor24-emu` features. This is removal-only.
- No format-spec doc rewrites. If you find docs about the assembler that reference the now-removed library, simply remove them — don't redesign the docs structure as part of this saga.

## When done

Push `pr/remove-internal-assembler` and notify mike. Mike will relay it via `dg-relay dcemu sw-cor24-emulator pr/remove-internal-assembler`. Promotion to `main` is mike's call, separately.
