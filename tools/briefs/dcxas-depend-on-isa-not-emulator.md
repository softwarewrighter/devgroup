# Brief: drop the `cor24-emulator` dep from `sw-cor24-x-assembler`

**Owner:** dcxas
**Branch:** `pr/depend-on-isa-not-emulator`
**Repo:** `sw-cor24-x-assembler`
**Prerequisite:** dcemu's `pr/extract-isa` must be **relayed and merged into `dev`** first. Mike will signal you when that's done. The `sw-cor24-isa` repo will then be canonical (the copy that lived in `sw-cor24-emulator/isa/` is gone). You'll also need a sibling clone of `sw-cor24-isa` next to your assembler clone before you start.

## Context

`sw-cor24-x-assembler` currently depends on the **whole emulator** for what should be a small ISA-types dep. Today's `Cargo.toml`:

```toml
[dependencies]
cor24-emulator = { path = "../sw-cor24-emulator" }
serde = { ... }
```

And the assembler library imports four things from cor24-emulator across two files:

| File | Import | Belongs in |
|---|---|---|
| `src/lib.rs:22` | `pub use cor24_emulator;` (re-exports the entire emulator!) | nowhere — anti-pattern |
| `src/assembler.rs:6` | `use cor24_emulator::cpu::encode;` | `cor24_isa::encode` (already exists) |
| `src/assembler.rs:7` | `use cor24_emulator::cpu::instruction::Opcode;` | `cor24_isa::opcode::Opcode` (already exists) |
| `src/lgo.rs:45-46` | `use cor24_emulator::cpu::state::CpuState; use cor24_emulator::loader::load_lgo;` | the emulator (legitimately — these are runtime types used in tests for round-trip) |

So three of the four imports are actually ISA types and should come from `cor24-isa` directly. Only the test code in `lgo.rs` legitimately needs the emulator (for the `.lgo` round-trip parser).

This saga splits the dep cleanly: `cor24-isa` for the assembler's runtime needs (a small library), `cor24-emulator` only as a `dev-dependency` for tests that round-trip through `load_lgo`. End result: anyone building or linking `cor24-asm` no longer needs a clone of `sw-cor24-emulator` — only `sw-cor24-isa`.

## One-time setup before you start

Clone the new isa repo as a sibling of your assembler clone:

```
cd /disk1/github/softwarewrighter/devgroup/work/dcxas/github/sw-embed
git clone /disk1/github/softwarewrighter/devgroup/work/bare/sw-cor24-isa.git
```

You may also still need `sw-cor24-emulator` (for dev-deps in tests). If you don't already have it in your clones list, clone it the same way:

```
git clone /disk1/github/softwarewrighter/devgroup/work/bare/sw-cor24-emulator.git
```

(The emulator repo will be much smaller after dcemu's saga, since `isa/` got moved out.)

## What to change

### 1. Top-level `Cargo.toml`

Replace:
```toml
[dependencies]
cor24-emulator = { path = "../sw-cor24-emulator" }
serde = { ... }
```

with:
```toml
[dependencies]
cor24-isa = { path = "../sw-cor24-isa" }
serde = { ... }

[dev-dependencies]
cor24-emulator = { path = "../sw-cor24-emulator" }
```

The cor24-emulator move to dev-deps means: cargo build / cargo doc don't pull it in; only cargo test does. Production users of the assembler library don't need a sibling emulator clone.

### 2. `src/lib.rs`

Remove `pub use cor24_emulator;` (line 22). This was re-exporting the whole emulator from the assembler library — an anti-pattern that made callers transitively depend on the entire emulator. If any external code was relying on `cor24_assembler::cor24_emulator::...`, that code needs to switch to depending on `cor24-emulator` directly.

### 3. `src/assembler.rs`

Replace:
```rust
use cor24_emulator::cpu::encode;
use cor24_emulator::cpu::instruction::Opcode;
```

with:
```rust
use cor24_isa::encode;
use cor24_isa::opcode::Opcode;
```

(The exact module paths inside `cor24-isa` are `encode` and `opcode::Opcode`. Confirm against `sw-cor24-isa/src/lib.rs` — there's a `pub use opcode::Opcode;` re-export at the top of that file, so `cor24_isa::Opcode` may also be a valid shorter form.)

### 4. `src/lgo.rs` (test code)

The imports here:
```rust
use cor24_emulator::cpu::state::CpuState;
use cor24_emulator::loader::load_lgo;
```
are inside `#[cfg(test)]` (verify this — if not, they should be moved). They stay as-is *if* they're only in test code. With cor24-emulator demoted to `dev-dependencies`, they'll resolve at test time only.

If those imports are NOT in `#[cfg(test)]` today and are reachable from production code paths, that's a structural problem worth flagging — those types are emulator-runtime concerns, not assembler concerns.

### 5. Build artifacts

Delete `Cargo.lock` lines for cor24-emulator dependencies that are no longer pulled in. Just `cargo build` and let it regenerate.

### 6. CLI

`cli/Cargo.toml` doesn't currently depend on `cor24-emulator` directly (only on the parent `cor24-assembler`); it should remain unchanged. Verify after the refactor.

## What goes in this PR

1. Update root `Cargo.toml` (move emulator to dev-deps, add isa dep).
2. Remove `pub use cor24_emulator;` from `src/lib.rs`.
3. Update three import lines in `src/assembler.rs` to use `cor24_isa` paths.
4. Verify `src/lgo.rs` test imports are properly gated under `#[cfg(test)]`.
5. Run full check:
   ```bash
   cargo build --workspace --release
   cargo test --workspace
   cargo clippy --workspace --all-targets --all-features -- -D warnings
   target/release/cor24-asm -V
   ```
6. Update `README.md` if it mentions the cor24-emulator dep — replace with cor24-isa.
7. Commit message:
   ```
   refactor: depend on cor24-isa directly instead of cor24-emulator

   The assembler's runtime types (Opcode, encode) live in cor24-isa,
   not the emulator. Switching to a direct path-dep eliminates the
   emulator from compile-time deps; it remains a dev-dep for the
   .lgo round-trip tests in src/lgo.rs.

   Removes the pub use cor24_emulator anti-pattern from src/lib.rs.
   ```

## What does NOT go in this PR

- No `cor24-asm` CLI feature changes.
- No format/output changes (.lgo, .bin, .lst) — those stay byte-identical.
- No changes to `sw-cor24-emulator` or `sw-cor24-isa` (other repos).
- No drop of cor24-emulator from `dev-dependencies` — the round-trip tests need it. Future cleanup might inline a stub `.lgo` parser in the test crate to fully sever the dep, but that's a separate saga.

## When done

Workflow: `dg-new-feature depend-on-isa-not-emulator` (creates `feat/depend-on-isa-not-emulator` from `dev`) → implement and verify → `dg-mark-pr` to rename to `pr/depend-on-isa-not-emulator` when ready. Signal mike. After relay, mike rebuilds and reinstalls `cor24-asm`. Net effect across the toolchain:
- The assembler library no longer transitively pulls in the emulator at build time.
- Anyone consuming `cor24-asm` as a binary or library can do so with only a sibling clone of `sw-cor24-isa`.
- The emulator-as-runtime concern stays a dev-time consideration (tests round-trip through it).
