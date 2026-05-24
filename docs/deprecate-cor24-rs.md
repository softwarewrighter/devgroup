# Deprecate cor24-rs: migrate to sw-cor24-emulator

Status: **in progress**
Created: 2025-05-23
Host: large12

## Background

`cor24-rs` is the original monorepo for the COR24 emulator. It has been
superseded by the `sw-embed/sw-cor24-emulator` fork (and the extracted
`sw-embed/sw-cor24-isa` crate). The `cor24-run` binary that several repos
depend on is built from `cor24-rs/rust-to-cor24/` — a crate that also
bundles `wasm2cor24` and `msp430-to-cor24`, none of which belong in the
shared toolchain going forward.

The replacement binary is `cor24-emu`, built from
`sw-cor24-emulator/cli` (bin target `cor24-emu`).

## Current state of work/bin (large12)

| Binary      | Source repo            | Status       |
|-------------|------------------------|--------------|
| `cor24-run` | cor24-rs               | deprecated   |
| `cor24-emu` | sw-cor24-emulator      | installed    |
| `cor24-dbg` | sw-cor24-emulator      | installed    |
| `tc24r`     | sw-cor24-x-tinyc       | installed    |
| `tc24r-pp`  | sw-cor24-x-tinyc       | installed    |
| `pa24r`     | sw-cor24-pcode         | installed    |
| `pl24r`     | sw-cor24-pcode         | installed    |
| `p24-load`  | sw-cor24-pcode         | installed    |
| `p24-dump`  | sw-cor24-pcode         | installed    |
| `pv24t`     | sw-cor24-pcode         | installed    |
| `pv24d`     | sw-cor24-pcode         | installed    |
| `sw-as24.bin` | sw-cor24-assembler   | installed    |
| `cor24-asm` | sw-cor24-x-assembler   | installed    |
| `link24`    | sw-cor24-plsw (linker) | installed    |
| `meta-gen`  | sw-cor24-plsw (linker) | installed    |
| `pas24`     | wrapper script         | installed    |
| `pvm24`     | wrapper script         | installed    |
| `plsw`      | wrapper script         | installed    |
| `agentrail` | symlink → mike sw-install | installed |
| `sw-checklist` | symlink → mike sw-install | installed |
| `reg-rs`    | symlink → mike sw-install | installed |
| `pjmai-rs`  | symlink → mike sw-install | installed |

## Shared library artifacts (work/lib/)

Pre-built artifacts consumed by the wrapper scripts above:

| Path | Source | Description |
|------|--------|-------------|
| `lib/pcode/pvm.s` | sw-cor24-pcode | P-Code VM (assembly source) |
| `lib/pcode/pvmasm.s` | sw-cor24-pcode | Integrated assembler+VM |
| `lib/pascal/p24p.s` | sw-cor24-pascal | Pascal compiler (assembly source) |
| `lib/pascal/runtime.spc` | sw-cor24-pascal | Pascal runtime library |
| `lib/pascal/relocate_p24.py` | sw-cor24-pascal | P-code relocation helper |
| `lib/plsw/plsw.lgo` | sw-cor24-plsw | PL/SW compiler (.lgo artifact) |

## Repos that depend on cor24-run (deprecated)

### sw-cor24-assembler (dcasm)

- `vendor/sw-em24/v0.1.0/version.json` points to `cor24-rs` with
  `build_cmd: "cargo build --release --bin cor24-run"` and
  `build_cwd: "rust-to-cor24"`.
- `just vendor-fetch` resolves `cor24-run` via three strategies:
  1. `$SW_EM24_BIN` override (env var or `vendor/active.env`)
  2. Sibling `../cor24-rs` repo + cargo build
  3. `cor24-run` on system PATH
- `just build` and `just run` invoke vendored `cor24-run` to
  cross-assemble `.s` files and execute binaries.
- **Migration:** Update vendor manifest to source `cor24-emu` from
  `sw-cor24-emulator` instead. Verify CLI flag compatibility
  (`--run`, `--assemble`, `--load-binary`, `--dump`, etc.) or
  adapt the justfile invocations.

### sw-cor24-pcode (dcpvm)

- `vm/demo.sh` and VM tests invoke `cor24-run` to execute assembled
  pcode VM binaries.
- **Migration:** Replace `cor24-run` references with `cor24-emu` in
  VM scripts. Verify flag compatibility.

## Migration steps

1. [x] Build `cor24-emu` and `cor24-dbg` from sw-cor24-emulator on large12
2. [ ] Audit CLI flag compatibility between `cor24-run` and `cor24-emu`
       (`--run`, `--assemble`, `--load-binary`, `--dump`, `--terminal`,
       `--echo`, `--speed`, `--max-instructions`, `--uart-input`)
3. [ ] Update sw-cor24-assembler vendor manifest and justfile to use `cor24-emu`
4. [ ] Update sw-cor24-pcode VM scripts to use `cor24-emu`
5. [ ] Verify `just build && just test` in sw-cor24-assembler with `cor24-emu`
6. [ ] Verify `cargo test` and `vm/demo.sh` in sw-cor24-pcode with `cor24-emu`
7. [ ] Remove `cor24-run` from `work/bin/`
8. [ ] Remove `work/relay/cor24-rs/` checkout
9. [ ] Consider whether `cor24-rs.git` bare mirror should be archived

## Notes

- `cor24-run` was built from `cor24-rs` into `work/relay/cor24-rs/` on
  2025-05-23 as a stopgap while standing up large12.
- `sw-cor24-emulator` requires `sw-cor24-isa` as a sibling
  (`../sw-cor24-isa`). Cloned under `dcemu/github/sw-embed/` for the
  build.
- `vendor/active.env` in sw-cor24-assembler supports
  `SW_EM24_BIN=/disk1/github/softwarewrighter/devgroup/work/bin/cor24-run`
  as an override — this can point to `cor24-emu` once compatibility is
  confirmed.
