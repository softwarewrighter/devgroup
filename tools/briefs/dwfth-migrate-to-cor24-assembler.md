# Brief: migrate web-sw-cor24-forth off the removed internal assembler

**Owner:** dwfth
**Branch:** `feat/migrate-to-cor24-assembler` → `pr/migrate-to-cor24-assembler`
**Repo:** `web-sw-cor24-forth`
**Depends on:** `sw-cor24-x-assembler` (crate `cor24-assembler`) on `dev` — already shipped.
**Drafted by:** mike (coordinator)

## Context

The COR24 transition split the old `cor24-rs` monolith into `cor24-emulator` +
`cor24-isa` + `cor24-assembler`. `dcemu-remove-internal-assembler.md` (shipped)
removed the in-tree assembler from `cor24-emulator`; the assembler now lives in
`sw-cor24-x-assembler` as the library crate **`cor24-assembler`**, with an
identical API (`Assembler::new()`, `assemble() -> AssemblyResult { bytes, errors,
labels }`, plus `AssembledLine` / `AssemblyResult`).

This repo was never migrated. It still imports `Assembler` from `cor24_emulator`,
which no longer exports it, so the crate fails to build. Forth is the **heaviest**
of the web migrations: it uses the assembler at **both build time and runtime**.

## Affected sites

- `build.rs:1` — `use cor24_emulator::{Assembler, EmulatorCore, StopReason};`
- `src/repl.rs:13` — `use cor24_emulator::{Assembler, EmulatorCore};`
- `src/debugger.rs:5` — `use cor24_emulator::{AssembledLine, Assembler, EmulatorCore};`

## What to change

1. `Cargo.toml`: add the assembler path-dep to **both** sections that use it
   (`[build-dependencies]` for `build.rs`, `[dependencies]` for `src/`):
   ```toml
   cor24-assembler = { path = "../sw-cor24-x-assembler", default-features = false }
   ```
   Leave `cor24-emulator` / `cor24-isa` as-is.

2. Split each import — take `Assembler` (and `AssembledLine`) from
   `cor24_assembler`, keep `EmulatorCore` / `StopReason` from `cor24_emulator`:
   - `build.rs:1` → `use cor24_assembler::Assembler;` + `use cor24_emulator::{EmulatorCore, StopReason};`
   - `src/repl.rs:13` → `use cor24_assembler::Assembler;` + `use cor24_emulator::EmulatorCore;`
   - `src/debugger.rs:5` → `use cor24_assembler::{AssembledLine, Assembler};` + `use cor24_emulator::EmulatorCore;`

The assembler API is unchanged — no call-site logic edits, only the crate path.

## Infra note

The srcroot must have `sw-cor24-emulator`, `sw-cor24-isa`, and
`sw-cor24-x-assembler` checked out as siblings. If they're missing, that's mike's
infra concern — surface it, don't paper over it with workarounds.

## Verification

- `cargo check`, `cargo clippy -- -D warnings`, and a `wasm32-unknown-unknown`
  build all green.
- Rebuild `pages/` via the repo's build script and confirm the REPL + debugger
  still load and run a demo.
- Update `CHANGES.md` (changelog discipline) and any `CLAUDE.md`/docs that still
  describe the assembler as living in `cor24-emulator`.

## Out of scope

- No changes to `sw-cor24-emulator` or `sw-cor24-x-assembler` (other agents' repos).
- No new REPL/debugger features. Migration-only.

## When done

Push `pr/migrate-to-cor24-assembler`, notify mike. Mike relays via
`dg-relay dwfth web-sw-cor24-forth pr/migrate-to-cor24-assembler`.
