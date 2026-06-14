# Brief: migrate web-sw-cor24-pascal off the removed internal assembler

**Owner:** dwpas
**Branch:** `feat/migrate-to-cor24-assembler` → `pr/migrate-to-cor24-assembler`
**Repo:** `web-sw-cor24-pascal`
**Depends on:** `sw-cor24-x-assembler` (crate `cor24-assembler`) on `dev` — already shipped.
**Drafted by:** mike (coordinator)

## Context

The COR24 transition split the old `cor24-rs` monolith into `cor24-emulator` +
`cor24-isa` + `cor24-assembler`. `dcemu-remove-internal-assembler.md` (shipped)
removed the in-tree assembler from `cor24-emulator`; the assembler now lives in
`sw-cor24-x-assembler` as the library crate **`cor24-assembler`**, same API
(`Assembler::new()`, `assemble() -> AssemblyResult { bytes, errors, labels }`).

This repo was never migrated. It calls `cor24_emulator::Assembler` in `build.rs`,
which no longer exists, so the build fails. Pascal uses the assembler **only at
build time**, at **two** sites (the prelude assembler and a `p24p_assembler`).

## Affected sites

- `build.rs:10` — `let mut asm = cor24_emulator::Assembler::new();`
- `build.rs:73` — `let mut p24p_assembler = cor24_emulator::Assembler::new();`

## What to change

1. `Cargo.toml`: add the assembler path-dep to `[build-dependencies]` (where
   `build.rs` uses it):
   ```toml
   cor24-assembler = { path = "../sw-cor24-x-assembler", default-features = false }
   ```
   Leave `cor24-emulator` as-is.

2. `build.rs`: change **both** `cor24_emulator::Assembler::new()` calls (lines 10
   and 73) → `cor24_assembler::Assembler::new()`. (Fully-qualified, one-token
   change each; no `use` statement to touch.)

The assembler API is unchanged — only the crate path moves.

## Infra note

The srcroot must have `sw-cor24-emulator`, `sw-cor24-isa`, and
`sw-cor24-x-assembler` checked out as siblings. If they're missing, that's mike's
infra concern — surface it, don't work around it.

## Verification

- `cargo check`, `cargo clippy -- -D warnings`, and a `wasm32-unknown-unknown`
  build all green.
- Rebuild `pages/` via the repo's build script and confirm the demo still loads.
- Update `CHANGES.md` (changelog discipline) and any `CLAUDE.md`/docs that still
  describe the assembler as living in `cor24-emulator`.

## Out of scope

- No changes to `sw-cor24-emulator` or `sw-cor24-x-assembler` (other agents' repos).
- No new features. Migration-only.

## When done

Push `pr/migrate-to-cor24-assembler`, notify mike. Mike relays via
`dg-relay dwpas web-sw-cor24-pascal pr/migrate-to-cor24-assembler`.
