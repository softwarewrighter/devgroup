# Brief: add `--base-addr` flag to `cor24-asm`

**Owner:** dcxas
**Branch:** `pr/cor24-asm-base-addr`
**Repo:** `sw-cor24-x-assembler`
**Depends on:** `pr/cor24-asm-cli` (your previous saga, already merged)

## Context

The `cor24-asm` CLI you shipped doesn't expose a `--base-addr` flag, but the `cor24-assembler` library already supports it internally (`src/assembler.rs::Assembler.base_address`). The PL/SW linker (`link24` + `meta-gen`) needs `--base-addr` for **pass-2 reassembly**: each module gets re-emitted at its final position so internal `la rN, label` references resolve to absolute addresses.

dcpls left two `cor24-run --assemble ... --base-addr` callsites as TODOs in their migration saga (`components/linker/tests/demo-fixup.sh` and `demo-plsw-modular.sh`) precisely because `cor24-asm` couldn't replace them yet. After this PR lands, those callsites flip and the old `cor24-run` is no longer needed for the linker tests.

## Goal

Add `--base-addr <addr>` to the `cor24-asm` CLI. The library backing is already there; this is mostly CLI plumbing.

## Reference: the old behavior

The original implementation was in `sw-cor24-emulator/src/assembler.rs` and `cli/src/run.rs` before dcemu's `pr/remove-internal-assembler` saga deleted them. To see the exact semantics:

```
git -C /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-emulator \
    show ba96d75
```

That's the original "feat: add --base-addr for assembling at non-zero base addresses" commit. **Read it as a spec reference; do NOT modify the emulator repo.**

## CLI shape

Add `--base-addr <addr>` to the existing argument parser. Accept hex (`0x1000`), decimal (`4096`), or label-style numeric input — match how `cor24-emu --entry` does it for consistency.

```
cor24-asm <input.s> --base-addr 0x1000 -o out.lgo
cor24-asm <input.s> --base-addr 4096 --bin out.bin --listing out.lst
```

Default if not specified: `0` (current behavior, no regression).

## Library API to use

The library already has the field. Inspect `src/assembler.rs` to see if there's a public setter or constructor variant; if not, add one:

```rust
impl Assembler {
    pub fn with_base_address(base: u32) -> Self { ... }
    // or:
    pub fn set_base_address(&mut self, base: u32) { ... }
}
```

Whichever feels cleanest given the existing library style. Don't break the existing `Assembler::new()` signature unless the migration is trivial — additive is fine.

## Output semantics

`--base-addr N` should:

1. **Treat N as the address of the first instruction** in the output (i.e., shift the location counter by N before pass 1).
2. **Bake N into all internal label resolutions** so `la r0, foo` where `foo` is local resolves to `N + offset_of(foo)`, not just `offset_of(foo)`.
3. **Affect the `.lgo` `L` record addresses** (your `src/lgo.rs::write` already takes a `base_addr` parameter — wire `--base-addr` through to it).
4. **Affect the listing** (your `src/listing.rs` should show absolute addresses including the offset).

What `--base-addr` should NOT do:

- It should **not** affect FIXUP records that the linker emits for cross-module references — those stay relative to the module. Since the assembler library doesn't currently emit FIXUP records (those come from `link24`/`meta-gen`), this is mostly a documentation note.

## Byte-identical output to old `cor24-run --assemble --base-addr`

This is the regression invariant. After your PR:

```
cor24-asm input.s --base-addr 0x1000 --bin out.bin
```

should produce the **exact same bytes** in `out.bin` as the old:

```
cor24-run --assemble --base-addr 0x1000 input.s out.bin /dev/null
```

did. Same for `.lgo` and `.lst`. Test it explicitly with at least one fixture that the old `cor24-run --assemble` accepts (you can capture one before mike retires `cor24-run` from `work/bin/`).

## Tests

Add round-trip and CLI tests:

1. **Library round-trip**: assemble a source with labels at `base_address = 0x1000`; verify byte output and label-resolved instructions encode the absolute addresses.
2. **CLI smoke test** (in `cli/tests/cli.rs`):
   - `cor24-asm in.s --base-addr 0x1000 -o out.lgo` exits 0
   - `cor24-asm in.s --base-addr 0x1000 --bin out.bin` produces expected bytes
   - `cor24-asm in.s --base-addr abc` (invalid) exits 2 with a clear stderr message
3. **Regression fixture**: capture `cor24-run --assemble --base-addr 0x100 simple.s simple.bin /dev/null` output (where `simple.s` has at least one local `la` reference), commit `simple.bin` as a fixture, and verify the new CLI produces identical bytes. Once captured, the test is environment-independent.

## What goes in this PR

1. Add `--base-addr` arg parsing to `cli/src/main.rs`.
2. Plumb the value through the library API (add setter/constructor variant if needed).
3. Wire it through to `src/lgo.rs::write` (already takes `base_addr`) and the listing emitter.
4. Tests above.
5. Update `README.md` CLI section with the new flag.
6. Update help text.

## What does NOT go in this PR

- No `cor24-emu` changes.
- No new output format support (sticks to `.lgo`/`.bin`/`.lst`).
- No FIXUP record emission (that's `link24`'s job, and is dcpls's repo).
- No removing `Default::default()` or `Assembler::new()` semantics — additive only.

## When done

Push `pr/cor24-asm-base-addr`. After mike relays + reinstalls `cor24-asm` to `work/bin/`, dcpls does a small follow-up PR in `sw-cor24-plsw` to flip the two TODO'd `cor24-run --assemble --base-addr` lines to `cor24-asm --base-addr`.
