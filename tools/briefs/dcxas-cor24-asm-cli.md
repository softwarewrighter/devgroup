# Brief: add a `cor24-asm` CLI to `sw-cor24-x-assembler`

**Owner:** dcxas
**Branch:** `pr/cor24-asm-cli`
**Repo:** `sw-cor24-x-assembler`

## Context

The existing crate is library-only (`crate-type = ["rlib"]`). It has no command-line entry point, so the `.s → .lgo` step of the COR24 toolchain pipeline cannot be invoked from a shell. Without it, downstream tooling (the new `devgroup/tools/` toolchain orchestrator and any agent that needs to assemble) has to either link the assembler as a Rust dep — which agents writing in non-Rust languages can't do — or shell out to a non-existent binary.

This is being refactored from the deprecated `cor24-rs` repo. That repo's `cli/` subcrate exposed only `cor24-dbg`; it had no `cor24-asm`. So this is a new CLI, not a port of an existing one — but use the deprecated `cor24-rs/cli/src/main.rs` and the current `sw-cor24-emulator/cli/` as structural references for argument parsing, exit codes, and version output.

**Where this fits in the assembler roadmap:**
1. **Now (this PR):** `cor24-asm` CLI for the existing Rust library `sw-cor24-x-assembler`.
2. **Later:** richer assembler in `sw-cor24-hlasm` (in C or assembler, dchla's repo) — that becomes the production-quality assembler.
3. **Much later:** a fully self-hosting assembler in self-hosted C, HLASM, or PL/SW.

Phases (2) and (3) are explicitly **out of scope** for this PR — don't try to anticipate them in the design.

## Goal

A native binary `cor24-asm` (one word, lowercase, dash) that takes COR24 assembly text and produces three optional output artifacts:

- **`.lgo`** — load-and-go text format consumed by `cor24-emu --lgo` (default output)
- **`.bin`** — raw machine-code bytes for `cor24-emu --load-binary <file>@<addr>` (used to load images at specific memory addresses, e.g. p-code VMs)
- **`.lst`** — human-readable listing for debugging (matches the format produced by today's `cor24-emu --assemble` so existing tooling parses it identically)

This consolidates "assemble" responsibility entirely in this binary. `cor24-emu`'s `--run` and `--assemble` modes will be removed in a follow-up dcemu saga that depends on this PR landing.

## CLI shape

Match existing tool conventions in `sw-cor24-emulator/cli/src/main.rs` for versioning, help, and exit codes.

```
cor24-asm <input.s>                                      # default: writes <stem>.lgo next to input
cor24-asm <input.s> -o <out.lgo>                         # explicit .lgo output
cor24-asm <input.s> --bin <out.bin>                      # raw bytes only (no .lgo)
cor24-asm <input.s> --bin <out.bin> --listing <out.lst>  # raw bytes + listing
cor24-asm <input.s> -o <out.lgo> --bin <out.bin> --listing <out.lst>   # all three
cor24-asm -                                              # stdin → stdout (.lgo)
cor24-asm -V | --version                                 # version (use vergen like cor24-dbg)
cor24-asm -h | --help                                    # usage
```

**Output rules:**
- If neither `-o` nor `--bin` is given, default to `<stem>.lgo` next to the input.
- If both are given, both are written.
- `--listing` is independent and can pair with either output mode.
- `-` as input means stdin (read all). When input is `-` and no `-o` is given, write `.lgo` to stdout.
- Refuse to write binary `.lgo`/`.bin` content to a TTY (require explicit redirect or `/dev/stdout`).

**Exit codes:** `0` on clean assembly, `1` on assembly errors, `2` on usage/IO errors.

**Errors:** one per line on stderr, prefixed `<input>:<line>:<col>: <message>` when the library exposes location info; bare message otherwise.

## Crate structure

Mirror the workspace pattern used by `sw-cor24-emulator`:

```
sw-cor24-x-assembler/
  Cargo.toml          # workspace root + lib (existing)
  cli/
    Cargo.toml        # name = "cor24-asm-cli", [[bin]] name = "cor24-asm"
    src/
      main.rs
```

Top-level `Cargo.toml` becomes a workspace:

```toml
[workspace]
members = [".", "cli"]
resolver = "2"
```

The library crate stays as it is (don't rename or re-export; just add the workspace).

## Library API to use

From the current README:

```rust
use cor24_assembler::Assembler;
let mut asm = Assembler::new();
let result = asm.assemble(source);
// result.errors : Vec<...>
// result.bytes  : Vec<u8>  // machine code
```

If the library doesn't currently expose source-location info on errors, that's a separate enhancement — the CLI should still work with whatever the library returns, even if errors are unlocated. Don't block this PR on improving error reporting; flag it as a follow-up if needed.

## The three writers — all in scope for this PR

### `.lgo` writer

dcemu investigated and confirmed: there is **no `.lgo` writer anywhere today**. The MakerLisp "load and go" text format (produced historically by the external `as24 | longlgo` pipeline) is *read* by `sw-cor24-emulator/src/loader.rs` but never written by any current Rust code.

**Boundary agreed with dcemu:**
- `.lgo` parser/loader → `sw-cor24-emulator` (stays put)
- `.lgo` writer/emitter → **`sw-cor24-x-assembler` (this repo)**
- Format spec → implicit in `sw-cor24-emulator/src/loader.rs` (`load_lgo`, `parse_lgo_load_line`, `parse_lgo_go_line`). Read these read-only to derive what to emit.

Add as a public library function (e.g., `cor24_assembler::lgo::write(bytes, entry, &mut writer) -> io::Result<()>`) in a new `src/lgo.rs` module. The CLI is a thin wrapper.

### `.bin` writer

Trivial: `result.bytes` from the library is already raw machine code. The `.bin` writer is `out.write_all(&result.bytes)`. Place a small helper in `src/bin_writer.rs` (or just inline in the CLI) — your call.

### `.lst` (listing) writer

Today the listing is generated by `sw-cor24-emulator/src/assembler.rs`'s internal listing emitter (used by `cor24-emu --assemble`). To preserve compatibility for existing tooling, **match that format byte-for-byte**.

Procedure:
1. Read `sw-cor24-emulator/src/assembler.rs` (read-only, in dcemu's repo) to understand the listing format.
2. Reproduce the equivalent in `sw-cor24-x-assembler` — likely as `cor24_assembler::listing::write(...)` in `src/listing.rs`.
3. Add a round-trip test that captures the listing for a fixture and compares against a known-good string.
4. **Do NOT depend on `sw-cor24-emulator` as a Cargo dep.** Re-implement the listing logic on top of this library's existing API surface.

If the existing listing is intertwined with the internal-emulator-assembler in a way that's hard to extract cleanly, document the gap in your PR description rather than blocking on a perfect port. A "good-enough listing that matches today's columns" is acceptable.

### Important: do NOT modify `sw-cor24-emulator`

You are reading its source as a *spec reference*, not as something to change. If you find clarifications that should land there (format docs, etc.), capture them in your PR description and mike will route them to dcemu via a separate brief.

**Possible follow-up (out of scope):** factor the `.lgo` format module into a shared `cor24-lgo` crate consumed by both the assembler (writer) and the emulator (reader). Mention this in the PR if you think it's worth doing later — don't do it now.

## Tests

Match the existing repo's test bar (`scripts/build.sh` runs `cargo build && cargo clippy -- -D warnings && cargo test`):

1. **Round-trip smoke tests** (one per writer):
   - `.lgo`: assemble a hand-written `.s`, write `.lgo`, parse it back through the spec implied by `sw-cor24-emulator/src/loader.rs`, compare bytes + entry to the original.
   - `.bin`: assemble `.s`, write `.bin`, compare to a fixture.
   - `.lst`: assemble `.s`, write `.lst`, compare against a known-good listing string.
   Cover at least: simple instruction emission, label resolution (forward + backward), one error case.
2. **CLI integration tests** (`tests/cli.rs` via `assert_cmd` or `Command::new`):
   - `cor24-asm <file>` writes `<stem>.lgo` and exits 0
   - `cor24-asm <file> -o <out>` writes to the explicit `.lgo` path
   - `cor24-asm <file> --bin <out>` writes raw bytes
   - `cor24-asm <file> --bin <bin> --listing <lst>` writes both
   - `cor24-asm <file> -o <lgo> --bin <bin> --listing <lst>` writes all three
   - stdin/stdout pipe mode round-trips for `.lgo`
   - syntactically broken input emits errors on stderr and exits 1
   - missing input file exits 2 with clear stderr
3. `cor24-asm -V` prints the version block; smoke-test it parses cleanly.

## Build/install

After this lands, `cargo build --release -p cor24-asm-cli` (or whatever the CLI crate is named) produces `target/release/cor24-asm`. Mike's forthcoming `devgroup/tools/build-all` script will pick it up and install into `devgroup/tools/bin/`. You don't need to write that orchestrator — just produce the binary and update the README.

## README

Update `sw-cor24-x-assembler/README.md`:

- Add a "CLI" section with usage examples
- Note that `cor24-asm` is the canonical command for `.s → .lgo`
- Keep the existing "Usage" Rust example (rename it to "Library Usage")
- The build step `./scripts/build.sh` should now also produce `target/<profile>/cor24-asm` — verify the existing script does `cargo build --workspace`, or update it

## What goes in this PR

1. Workspace conversion of the top-level `Cargo.toml`
2. New `cli/` subcrate with `Cargo.toml` + `src/main.rs`
3. `.lgo` writer (`src/lgo.rs`) — public library API
4. `.lst` (listing) writer (`src/listing.rs`) — public library API matching today's emulator listing format
5. `.bin` writing (trivial — raw bytes from the library; can live in the CLI)
6. CLI tests (above)
7. Updated README
8. Update `scripts/build.sh` if needed so `--workspace` is in scope

## What does NOT go in this PR

- No changes to `sw-cor24-emulator` (those would be a separate saga relayed via dcemu).
- No language-toolchain orchestration (mike is doing that under `devgroup/tools/`).
- No `sw-cor24-assembler` (non-`-x-`) work — that's wishlist.
- Don't add a bench/profile target unless the tests need it; keep this PR focused.

## When done

Push `pr/cor24-asm-cli` and notify mike via the usual channel. Mike will relay it via `dg-relay dcxas sw-cor24-x-assembler pr/cor24-asm-cli`, which merges into `dev`. Promotion to `main` is mike's call, separately.
