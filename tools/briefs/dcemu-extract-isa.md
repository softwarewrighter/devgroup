# Brief: extract `isa/` workspace member out of `sw-cor24-emulator`

**Owner:** dcemu
**Branch:** `pr/extract-isa`
**Repo:** `sw-cor24-emulator`
**Prerequisite:** mike has already created `sw-cor24-isa` and populated it with the current `isa/` content. The new repo is at `git@github.com:sw-embed/sw-cor24-isa.git`, bare at `/disk1/github/softwarewrighter/devgroup/work/bare/sw-cor24-isa.git`. You'll need a sibling clone of it next to `sw-cor24-emulator` for the path-dep to resolve.

## Context

`isa/` was a workspace member of `sw-cor24-emulator` because back when the codebase lived in the deprecated `cor24-rs` monorepo, all of `isa`, `components` (assembler), `cli`, and the emulator were sibling crates of a single Cargo workspace. When that monorepo got split into separate repos, `isa` got stranded inside `sw-cor24-emulator` for convenience — but it's a foundational shared crate, not an emulator-internal detail. Today, six web frontends and `sw-cor24-x-tinyc` reach across into `../sw-cor24-emulator/isa` for type definitions, which forces every consumer to clone the whole emulator just for opcode/register/branch types.

mike has now created `sw-cor24-isa` as a small standalone repo with the same content. This saga finishes the split: remove `isa/` from `sw-cor24-emulator` and re-link the emulator to the new external crate.

After this lands, downstream consumer repos (assembler, x-tinyc, web-*) get migrated to `path = "../sw-cor24-isa"` in a separate mike-led batch. Don't touch their Cargo.toml from this saga.

## One-time setup before you start

Clone the new repo as a sibling of your emulator clone:

```
cd /disk1/github/softwarewrighter/devgroup/work/dcemu/github/sw-embed
git clone /disk1/github/softwarewrighter/devgroup/work/bare/sw-cor24-isa.git
```

Result: `dcemu/github/sw-embed/sw-cor24-isa/` exists. You don't push from this clone (only mike does); just need the source on disk so cargo's path-dep resolves.

## What to change in this PR

1. **Delete `isa/` directory** from sw-cor24-emulator. Five `.rs` files plus the `isa/Cargo.toml`.

2. **Update root `Cargo.toml`**:
   - Remove `"isa"` from the workspace `members` array.
   - Change the dependency line from
     ```toml
     cor24-isa = { path = "./isa", features = ["serde"] }
     ```
     to
     ```toml
     cor24-isa = { path = "../sw-cor24-isa", features = ["serde"] }
     ```
   - The crate name in the dep stays `cor24-isa` (unchanged), so all `use cor24_isa::...` imports in the emulator's source code keep working without edits.

3. **Update any other Cargo.toml in the workspace that depends on `isa`** — specifically:
   - `cli/Cargo.toml` if it has a direct path-dep on `./isa` or `../isa`. Adjust to the new path.
   - `tests/`, `benches/`, etc. similarly if any.

4. **Verify everything still builds, tests pass, and the binary works:**
   ```bash
   cargo build --workspace --release
   cargo test --workspace
   cargo clippy --workspace --all-targets --all-features -- -D warnings
   ./target/release/cor24-emu -V
   ./target/release/cor24-emu --demo --time 1   # or another smoke test
   ```

5. **`Cargo.lock`** will regenerate; commit the new lock if it changes.

## What does NOT go in this PR

- **No source-level changes** to the emulator's `.rs` files. Imports like `use cor24_isa::opcode::Opcode;` stay exactly as-is — the dep name didn't change, only the path resolved by Cargo.
- **No changes to other repos.** Don't try to fix downstream `path = "../sw-cor24-emulator/isa"` references in `web-*`, `x-assembler`, `x-tinyc`, etc. Those are mike-coordinated.
- **No isa-crate refactors** in `sw-cor24-isa` itself. The new repo's content is whatever mike copied over; treat it as immutable from this saga's perspective. If you find a bug in isa, that's a separate brief routed to whoever owns sw-cor24-isa (TBD — likely mike or a new agent).

## What goes in this PR

1. `git rm -r isa/`
2. Edit `Cargo.toml` (workspace member list + dep path).
3. Edit `cli/Cargo.toml` if it directly depends on the local `./isa`.
4. `cargo build --workspace --release && cargo test --workspace` — must be clean before push.
5. Commit message:
   ```
   refactor: extract cor24-isa workspace member into sw-cor24-isa repo

   The isa/ subdirectory has been moved to its own repo
   (sw-embed/sw-cor24-isa) so foundational ISA types can be consumed
   without forcing every downstream repo to clone the whole emulator.

   Source-level imports unchanged (use cor24_isa::...).
   ```

## When done

Push `pr/extract-isa` and signal. After mike relays:
- mike runs the synchronized Cargo.toml path update across the 6+ downstream consumer repos (mechanical: `s|../sw-cor24-emulator/isa|../sw-cor24-isa|`).
- dcxas's parallel saga (`pr/depend-on-isa-not-emulator`) cleans up the assembler's tangled emulator dep.
- The emulator stops being a transit point for ISA type definitions; it becomes a peer consumer of `sw-cor24-isa` like any other repo.
