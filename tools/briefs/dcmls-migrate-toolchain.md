# Brief: migrate sw-cor24-macrolisp off the deprecated `cor24-run`

**Owner:** dcmls
**Branch:** `feat/migrate-toolchain` → `pr/migrate-toolchain`
**Repo:** `sw-cor24-macrolisp`
**Depends on:** `cor24-asm` + `cor24-emu` on PATH — already installed.
**Drafted by:** mike (coordinator)
**Part of:** the cor24-rs retirement epic (`retire-cor24-rs.md`, axis 2).
**See also:** the generic `dc-migrate-toolchain.md` for the full mapping table.

## Why this exists

`cor24-run` is now a **deprecation shim**: it logs every call to
`work/log/cor24-run-usage.log`, prints a `DEPRECATED` notice, and forwards to
`cor24-run.legacy`. It still works today, but once the usage log is quiet the
shim and the legacy binary get **deleted** — and every caller breaks. This is the
same "stop using old cor24-rs tools" move dwmls made on the web side; dcmls is the
C/source side and is still entirely on `cor24-run`.

The old `cor24-run` bundles the **old internal assembler** — the same one that was
removed from the emulator and whose output drifted 10× from the current `tc24r`
(see `dw-rebaseline-asm-tc24r.md`). Keeping dcmls on `cor24-run` keeps producing
old-toolchain artifacts.

## Migration mapping (from the shim itself)

- `cor24-run --run prog.s [opts]`
  → `cor24-asm prog.s -o prog.lgo && cor24-emu --lgo prog.lgo [opts]`
- `cor24-run --assemble in.s out.bin out.lst`
  → `cor24-asm in.s --bin out.bin --listing out.lst`
- `cor24-run --assemble in.s out.bin /dev/null`
  → `cor24-asm in.s --bin out.bin`

(Use a stable per-run build dir, not `/tmp`, in CI/tests.)

## Sites to migrate (audit confirmed)

Executables (the real targets):
- `justfile` — the `cor24_run` variable, **and `justfile:61`** (snapshot gen, see below).
- `scripts/build.sh` — the `command -v cor24-run` availability check (point at
  `cor24-asm` + `cor24-emu`; fix the "Build sw-cor24-emulator first" message).
- `scripts/run-tests.sh`, `scripts/profile.sh`, `scripts/repl.sh`,
  `scripts/eval-expr.sh`, `scripts/load-eval.sh`.

Docs / comments (update so they stop teaching the old flags):
- `src/*.c` usage-comment headers (`repl-bare.c`, `repl-snapshot.c`, `compiler.c`,
  `snapshot-save.c`), `examples/prelude.l24`, and `docs/*.md`
  (`ml2asm-demos.md`, `multi-module-demo.md`, `eval-file-plan.md`, `bugs/bug006-*`).

Re-run the audit to confirm zero remaining: `grep -rn 'cor24-run' . | grep -v build/`

## The snapshot — coordinate with dwmls

`justfile:61` generates `build/prelude.snap.raw` (→ the prelude snapshot that
`web-sw-cor24-macrolisp` ships as `snapshots/standard.snap`) by running
`snapshot-save.s` under the **old** `cor24-run` assembler+emulator. Migrating it
to `cor24-asm` + `cor24-emu --lgo` re-assembles `snapshot-save.s` with the
**current** `tc24r`/assembler, which **may change the snapshot bytes** (codegen
addresses shifted).

- **Verify behaviorally**, do not assume: run the standard-tier REPL with the
  regenerated snapshot and confirm identical output to the pre-migration snapshot.
- This snapshot is the artifact `dw-rebaseline-asm-tc24r.md` (dwmls) consumes.
  dcmls owns its generation; once dcmls re-baselines it under the new tools,
  dwmls picks up the new `.snap`. Flag mike to sequence the two.

## Verification

- `just build` (or `scripts/build.sh`) and `scripts/run-tests.sh` pass using only
  `cor24-asm`/`cor24-emu` — no `cor24-run` invocations.
- `work/log/cor24-run-usage.log` shows no new entries from dcmls after the change.

## Out of scope

- No language/compiler feature changes. Toolchain-call migration only.
- No changes to `cor24-asm`/`cor24-emu`/`tc24r` themselves.

## When done

Push `pr/migrate-toolchain`, notify mike. Mike relays via
`dg-relay dcmls sw-cor24-macrolisp pr/migrate-toolchain`.
