# Finishing the cor24-rs migration

**Status:** in progress · **Owner:** mike (coordinator) · **Tracking epic:** `tools/briefs/retire-cor24-rs.md`

## Goal

Retire the early **`cor24-rs` monolith** across the fleet: every repo builds and
runs on the **split toolchain**, and nothing depends on `cor24-rs` code, the
`cor24-emulator` *internal assembler*, or the `cor24-run` binary.

## What `cor24-rs` is (and isn't)

The monolith bundled the **emulator + ISA + an internal assembler**, and shipped
the combined **`cor24-run`** binary. It was split into:

| Old (cor24-rs) | New (split) | Repo / agent | Binary / crate |
|---|---|---|---|
| emulator | `sw-cor24-emulator` | dcemu | `cor24-emu`, `cor24_emulator` crate |
| ISA | `sw-cor24-isa` | dcisa | `cor24-isa` crate |
| internal assembler | `sw-cor24-x-assembler` | dcxas | `cor24-asm`, `cor24_assembler` crate |
| `cor24-run --run` | `cor24-emu --lgo` | dcemu | (run a `.lgo`) |
| `cor24-run --assemble` | `cor24-asm` | dcxas | (assemble `.s`) |

**Not cor24-rs** (do not conflate — these have their own tracks, see Appendix):
- **`tc24r`** (the C cross-compiler, `sw-cor24-x-tinyc`/dcxtc) — never part of
  cor24-rs. The committed `asm/*.s` are *its* output, not cor24-rs artifacts.
- The macrolisp **asm re-baseline** — a `tc24r`-output concern.

## Migration model

Three kinds of cor24-rs dependency exist in the fleet:
- **Axis 1 — Rust API:** a `dw*` web crate `use`s `Assembler` from the
  `cor24_emulator` crate, which **no longer exports it** (removed in
  `dcemu-remove-internal-assembler`, shipped). These repos **don't build against
  the current emulator** → highest priority.
- **Axis 2 — scripts/binaries:** `dc*`/`dw*` repos call `cor24-run --run/--assemble`.
  `cor24-run` is now a logging **deprecation shim** → `cor24-run.legacy`; callers
  still work but break when the shim is deleted.
- **Axis 3 — the monolith itself:** `relay/cor24-rs` and its `rust-to-cor24`
  subproject.

Every worker change follows the standard saga workflow:
`dg-new-feature <slug>` → edit + verify → `dg-mark-pr` → signal mike →
`dg-relay <user> <repo> pr/<slug>` → (later) `dg-release <repo>`.

---

# Prioritized plan

## P0 — Fix the broken `dw*` builds (Axis 1: assembler-split)

These five web crates import `Assembler` from `cor24_emulator`, which the current
emulator crate no longer provides. **The fix is identical in shape:** add the new
assembler crate and import `Assembler` from it.

**Common dependency edit** — add to `Cargo.toml`:
```toml
cor24-assembler = { path = "../sw-cor24-x-assembler", default-features = false }
```
**Common import edit** — `Assembler` (and `AssembledLine`/`AssemblyResult` if used)
comes from `cor24_assembler`; keep `EmulatorCore`/`StopReason` from `cor24_emulator`.
The API is identical (`Assembler::new()`, `assemble() -> AssemblyResult { bytes,
errors, labels }`) — **no call-site logic changes.**

**Prerequisite for all five (infra):** each agent's `srcroot`
(`work/<user>/github/sw-embed`) needs `sw-cor24-x-assembler`, `sw-cor24-emulator`,
`sw-cor24-isa` checked out as siblings (clone from `work/bare/`). `cor24-asm`,
`cor24-emu`, `tc24r` are already on PATH. Without the siblings the build can't
resolve the path-deps — surface to mike, don't work around it.

**Per-repo verification (all five):** `cargo check`, `cargo clippy -- -D warnings`,
a `wasm32-unknown-unknown` build, then rebuild `pages/` via the repo's script.
Brief: `tools/briefs/<user>-migrate-to-cor24-assembler.md`. Slug: `migrate-to-cor24-assembler`.

### P0.1 — dwfth · `web-sw-cor24-forth` *(heaviest — build-time AND runtime)*
- Add `cor24-assembler` to **both** `[dependencies]` and `[build-dependencies]`.
- Split the import in **three** files:
  - `build.rs:1` → `use cor24_assembler::Assembler;` + `use cor24_emulator::{EmulatorCore, StopReason};`
  - `src/repl.rs:13` → `use cor24_assembler::Assembler;` + `use cor24_emulator::EmulatorCore;`
  - `src/debugger.rs:5` → `use cor24_assembler::{AssembledLine, Assembler};` + `use cor24_emulator::EmulatorCore;`

### P0.2 — dwapl · `web-sw-cor24-apl`
- Add `cor24-assembler` to `[build-dependencies]`.
- `build.rs:7`: `cor24_emulator::Assembler::new()` → `cor24_assembler::Assembler::new()`.

### P0.3 — dwoca · `web-sw-cor24-ocaml`
- Add `cor24-assembler` to `[build-dependencies]`.
- `build.rs:11`: same one-token change.

### P0.4 — dwpas · `web-sw-cor24-pascal`
- Add `cor24-assembler` to `[build-dependencies]`.
- `build.rs:10` **and** `build.rs:73`: both `cor24_emulator::Assembler::new()` → `cor24_assembler::Assembler::new()`.

### P0.5 — dwpvm · `web-sw-cor24-pcode`
- Add `cor24-assembler` to `[build-dependencies]`.
- `build.rs:10`: one-token change.
- **Also has an Axis-2 script** (`scripts/run-pascal.sh`) — fold the P2 edit into
  the same PR or a follow-up (see P2.10).

> **Done:** dwmls · `web-sw-cor24-macrolisp` — migrated (relayed + on `main`).

## P1 — Migrate the shared launcher `cor24-interpret` (Axis 2, highest leverage)

`work/bin/cor24-interpret` (coordinator-owned) is the launcher behind **every
`native-s` language wrapper** (`lisp24`, forth, …). Its `native-s` branch runs
`cor24-run --run "$INTERP"`. Migrating this one script gets all native-s languages
off `cor24-run` at runtime in a single change.
- Brief: `tools/briefs/cor24-interpret-migrate-off-cor24-run.md`.
- Edit the `native-s` branch → `cor24-asm "$INTERP" -o "$LGO"` then
  `cor24-emu --lgo "$LGO" …`, preserving all flags.
- Put the script under version control (`devgroup/scripts/cor24-interpret.sh`) and
  install from there.
- Verify: `lisp24 -u '(+ 1 2)'` → `3`; each native-s wrapper still runs; no new
  `cor24-run` entries in `work/log/cor24-run-usage.log`.
- Coordinator-applied (no relay).

## P2 — Migrate `cor24-run` callers in worker repos (Axis 2)

Brief: the generic `tools/briefs/dc-migrate-toolchain.md` (full mapping table).
Slug: `migrate-toolchain`. **Mapping:**
- `cor24-run --run X.s [opts]` → `cor24-asm X.s -o X.lgo && cor24-emu --lgo X.lgo [opts]` (use a stable build dir, not `/tmp`, in CI)
- `cor24-run --assemble in.s out.bin out.lst` → `cor24-asm in.s --bin out.bin --listing out.lst`
- `cor24-run --terminal/--load-binary/…` (no `--run`/`--assemble`) → `cor24-emu` with the same flags (binary rename)
- Update `command -v cor24-run` availability checks and doc/comment examples.
- **Keep** intentional historical references (post-mortems) — they correctly
  attribute past behavior; rewriting falsifies history.
- Verify per repo: `scripts/test.sh` / `just test` / `scripts/run-tests.sh` pass
  using only `cor24-asm`/`cor24-emu`; no new `cor24-run-usage.log` entries.

Ordered by amount of active flag usage (`--run`/`--assemble` counts from audit):

| # | Agent | Repo | `--run` | `--assemble` | Files to edit |
|---|---|---|---|---|---|
| P2.1 | dcasm | `sw-cor24-assembler` | 1 | 5 | `justfile`, `scripts/{test,build,hex2bin,vendor-fetch}.sh` |
| P2.2 | dcoca | `sw-cor24-ocaml` | 0 | 1 | `scripts/{repl,vendor-fetch,run-parser-test,run-ocaml,run-eval-test,build,run-pascal,run-lexer-test}.sh` (8) |
| P2.3 | dcprl | `sw-cor24-prolog` | 1 | 2 | `scripts/build-vm.sh` |
| P2.4 | dcxpa | `sw-cor24-x-pc-aotc` | 2 | 0 | `scripts/{compile-test,run-tests}.sh` |
| P2.5 | dcstk | `sw-cor24-smalltalk` | 0 | 1 | `scripts/run-st2.sh` |
| P2.6 | dcscr | `sw-cor24-script` | 0 | 0 | `scripts/{build,test}.sh` (rename/refs only) |
| P2.7 | dcxtc | `sw-cor24-x-tinyc` | 0 | 0 | `scripts/{run-subset-tests,run-chibicc-test}.sh` (rename/refs) |
| P2.8 | dcfth | `sw-cor24-forth` | 0 | 0 | `scripts/build.sh` (availability check / refs) |
| P2.9 | dcyed | `sw-cor24-yocto-ed` | 0 | 0 | `justfile` (rename/refs) |
| P2.10 | dwpvm | `web-sw-cor24-pcode` | 1 | 2 | `scripts/run-pascal.sh` (pair with P0.5) |

> **Done:** dcmls · `sw-cor24-macrolisp` — migrated (on `main`). Its one remaining
> `cor24-run` line is an **intentional** `justfile` migration-rationale comment.

## P3 — Delete the `cor24-run` shim (Axis 2 close-out) · coordinator

Only after P1 + P2 + all dw* (P0) are relayed and `work/log/cor24-run-usage.log`
shows **no new entries for a quiet interval**:
1. Remove `work/bin/cor24-run` (shim) and `work/bin/cor24-run.legacy`.
2. Reinstall the stale installed interpreter artifacts that were built by the old
   compiler (e.g. `work/lib/macrolisp/repl-standard.s`) — rebuild from current
   source (separate from this axis; see Appendix re: `tc24r`).
3. Confirm no wrapper or script breaks (run each language wrapper once).

## P4 — Retire the monolith (Axis 3) · coordinator + dcxxx

1. `relay/cor24-rs` — already carries a deprecation redirect to
   `sw-cor24-emulator`. Archive once nothing references it.
2. **`rust-to-cor24`** subproject (lives in `relay/cor24-rs/rust-to-cor24`) still
   uses `cor24_emulator::assembler` — **decide: rehome onto `cor24-assembler`, or
   archive.** Owner TBD. This is the last code-level cor24-rs dependency.

---

# Definition of done

```bash
cd /disk1/github/softwarewrighter/devgroup/work
# Axis 1 — no Rust crate imports the assembler from the emulator:
grep -rln 'cor24_emulator::.*\(Assembler\|AssembledLine\)' */github/sw-embed/*/src */github/sw-embed/*/build.rs | grep -v /target/
# Axis 2 — no live cor24-run --run/--assemble (intentional historical comments excepted):
grep -rln -- 'cor24-run .*--\(run\|assemble\)' */github/sw-embed/*/{scripts,justfile} bin/cor24-interpret
# Axis 2 runtime — usage log quiet:
tail work/log/cor24-run-usage.log
# Axis 3 — monolith/rust-to-cor24 retired:
grep -rln 'cor24_emulator::assembler' relay/cor24-rs/rust-to-cor24/src 2>/dev/null
```
Migration is complete when the first two print nothing (modulo documented
historical residuals), the log is quiet, the shim + legacy are gone, and axis 3 is
archived/rehomed.

---

# Appendix — Related but NOT cor24-rs (separate tracks; do not block this on them)

- **`tc24r` codegen bug** (`tools/briefs/dcxtc-fix-cond-macroexpand-hang.md`):
  the C compiler infinite-loops on deep recursion (3-clause `cond` macroexpand);
  confirmed at dcxtc HEAD, long-standing, not a recent regression. Blocks the asm
  re-baseline below. dcxtc-owned.
- **macrolisp asm re-baseline** (`tools/briefs/dw-rebaseline-asm-tc24r.md`,
  approach A+): regenerate committed `asm/*.s` onto current `tc24r` output. **Gated
  on the `tc24r` bug.** This is compiler-output freshness, *not* cor24-rs.
- **Toolchain version-stamping** (`tools/briefs/toolchain-version-stamping.md`):
  stamp git SHA + build date into every binary's `--version` (and add `--version`
  to `tc24r`/`tc24r-pp`) so "is this binary stale?" is answerable at a glance.
  Hygiene that would have prevented much of this investigation.

---

# Per-agent dispatch quick-reference

| Priority | Linux user | Repo | Brief | Action |
|---|---|---|---|---|
| P0.1 | dwfth | web-sw-cor24-forth | `dwfth-migrate-to-cor24-assembler.md` | feat→pr `migrate-to-cor24-assembler` |
| P0.2 | dwapl | web-sw-cor24-apl | `dwapl-migrate-to-cor24-assembler.md` | feat→pr `migrate-to-cor24-assembler` |
| P0.3 | dwoca | web-sw-cor24-ocaml | `dwoca-migrate-to-cor24-assembler.md` | feat→pr `migrate-to-cor24-assembler` |
| P0.4 | dwpas | web-sw-cor24-pascal | `dwpas-migrate-to-cor24-assembler.md` | feat→pr `migrate-to-cor24-assembler` |
| P0.5 | dwpvm | web-sw-cor24-pcode | `dwpvm-migrate-to-cor24-assembler.md` | feat→pr (+ P2.10) |
| P1 | mike | `work/bin/cor24-interpret` | `cor24-interpret-migrate-off-cor24-run.md` | coordinator edit |
| P2.1 | dcasm | sw-cor24-assembler | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.2 | dcoca | sw-cor24-ocaml | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.3 | dcprl | sw-cor24-prolog | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.4 | dcxpa | sw-cor24-x-pc-aotc | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.5 | dcstk | sw-cor24-smalltalk | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.6 | dcscr | sw-cor24-script | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.7 | dcxtc | sw-cor24-x-tinyc | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.8 | dcfth | sw-cor24-forth | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P2.9 | dcyed | sw-cor24-yocto-ed | `dc-migrate-toolchain.md` | feat→pr `migrate-toolchain` |
| P3 | mike | toolchain (`work/bin/`) | — | delete shim after log quiet |
| P4 | mike + TBD | relay/cor24-rs, rust-to-cor24 | — | archive / rehome |
