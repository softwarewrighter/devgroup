# Epic: retire the `cor24-rs` monolith

**Type:** tracking epic (index of constituent sagas — not a single-agent saga).
**Owner:** mike (coordinator).
**Goal:** move every repo onto the **split toolchain** and stop using any
`cor24-rs`-era code or artifacts.

## Background — the split

The early `cor24-rs` monolith bundled emulator + assembler + ISA (+ an in-tree
compiler lineage). It has been split into separate, single-purpose repos:

| Old (monolith) | New (split) | Crate / tool |
|---|---|---|
| `cor24-rs` emulator | `sw-cor24-emulator` | `cor24-emulator`, `cor24-emu` |
| `cor24-rs` ISA | `sw-cor24-isa` | `cor24-isa` |
| `cor24-rs` internal assembler | `sw-cor24-x-assembler` | `cor24-assembler`, `cor24-asm` |
| old `tinyc` (`tml24c`-era) | `sw-cor24-x-tinyc` | `tc24r` (`bin/tc24r`, md5 `c539a57b…`) |
| `cor24-run --run/--assemble` | `cor24-asm` + `cor24-emu --lgo` | (binary split) |

`relay/cor24-rs` is deprecated (carries a redirect notice to
`sw-cor24-emulator`).

## Definition of done

1. No repo imports the assembler symbols (`Assembler`, `AssembledLine`,
   `AssemblyResult`) from `cor24_emulator` — they come from `cor24_assembler`.
2. No script/doc invokes `cor24-run --run` / `cor24-run --assemble`.
3. No committed **old-toolchain generated artifacts** (`asm/*.s`, snapshots)
   remain; they are regenerated from the `sw-cor24-x-tinyc` `tc24r`.
4. `relay/cor24-rs` and its `rust-to-cor24` subproject are rehomed or archived;
   nothing depends on the monolith.

## The four axes

### Axis 1 — Library API (assembler moved out of the emulator crate)
Repos still importing `Assembler` from `cor24_emulator` fail to build.

| Repo | Owner | Brief | Status |
|---|---|---|---|
| `web-sw-cor24-macrolisp` | dwmls | (folded into `pr/load-save-copy`) | ✅ relayed to `dev` |
| `web-sw-cor24-forth` | dwfth | `dwfth-migrate-to-cor24-assembler.md` | 🟢 ready |
| `web-sw-cor24-apl` | dwapl | `dwapl-migrate-to-cor24-assembler.md` | 🟢 ready |
| `web-sw-cor24-ocaml` | dwoca | `dwoca-migrate-to-cor24-assembler.md` | 🟢 ready |
| `web-sw-cor24-pascal` | dwpas | `dwpas-migrate-to-cor24-assembler.md` | 🟢 ready |
| `web-sw-cor24-pcode` | dwpvm | `dwpvm-migrate-to-cor24-assembler.md` | 🟢 ready |
| fortran / plsw / web-tinyc / x-assembler | dwftn/dwpls/dwxtc/dwxas | — | ✅ already on `cor24-assembler` |
| snobol4 | dwsno | — | ✅ N/A (loads prebuilt `.lgo`, no in-app assembler) |

Audit: `grep -rn 'cor24_emulator::.*Assembler' work/*/github/sw-embed/*/{src,build.rs}`

### Axis 2 — Binaries / scripts (`cor24-run --run/--assemble` removed)
`dc*` repos (esp. `dcfth`) still call the removed flags.

| Scope | Brief | Status |
|---|---|---|
| any `dc*` agent | `dc-migrate-toolchain.md` (existing, generic) | ◻ in progress / per-repo |
| `sw-cor24-macrolisp` (dcmls) | `dcmls-migrate-toolchain.md` | ✅ promoted to `main` (`1a2d777`); **owns prelude-snapshot gen** (see axis 3) |
| **toolchain launcher `cor24-interpret`** (coordinator) | `cor24-interpret-migrate-off-cor24-run.md` | 🟢 ready — `native-s` mode still calls `cor24-run --run` (`work/bin/cor24-interpret:~92`); shared by every native-s wrapper (`lisp24`, forth, …); **final blocker to deleting the `cor24-run` shim**. |
| installed interpreter artifacts (`work/lib/<lang>/*.s`) | — | ◻ stale old-toolchain output (e.g. `work/lib/macrolisp/repl-standard.s`, 272 KB verbose) — rebuild + reinstall after each repo's re-baseline. |

`cor24-run` is now a logging deprecation shim → `cor24-run.legacy`; callers still
work but break once the shim is removed. Audit:
`grep -rn 'cor24-run' work/dc*/github/sw-embed/*/{scripts,docs,justfile}` and
`work/log/cor24-run-usage.log`.

### Axis 3 — Generated artifacts (committed old-toolchain `asm/`)
Committed `asm/*.s` were produced by the old `tml24c`-era compiler and diverge
10× from current `tc24r`. Regenerate from the blessed `tc24r`; **prefer not
committing generated asm at all** (approach B).

| Repo | Owner | Brief | Status |
|---|---|---|---|
| `web-sw-cor24-macrolisp` | dwmls | `dw-rebaseline-asm-tc24r.md` | 🟢 next up (first instance) |
| other `dw*` committing `asm/*.s` | per audit | `dw-rebaseline-asm-tc24r.md` | ◻ audit pending |

Audit: `for d in work/dw*/github/sw-embed/*/; do git -C "$d" ls-files 'asm/*.s' | grep -q . && echo "$d"; done`

**Snapshot ownership:** the standard-tier `snapshots/standard.snap` (shipped by
dwmls) is **generated upstream by dcmls** (`sw-cor24-macrolisp` `justfile`, via
`snapshot-save.s`). Re-baselining it belongs to `dcmls-migrate-toolchain.md`;
dwmls consumes the result. Sequence dcmls's snapshot regen **before** dwmls's
re-baseline so dwmls picks up a current `.snap`.

### Axis 4 — The monolith itself
| Item | Status |
|---|---|
| `relay/cor24-rs` deprecation redirect | ✅ in place |
| `rust-to-cor24` subproject (still uses `cor24_emulator::assembler`) | ◻ **unowned** — rehome onto `cor24-assembler` or archive (decision needed) |

## Verification (coordinator, run periodically)

```bash
cd /disk1/github/softwarewrighter/devgroup/work
echo "Axis 1 — stale Assembler imports:"; grep -rln 'cor24_emulator::.*Assembler' */github/sw-embed/*/src */github/sw-embed/*/build.rs 2>/dev/null | grep -v /target/
echo "Axis 2 — cor24-run --run/--assemble:"; grep -rln -- '--run\|--assemble' dc*/github/sw-embed/*/scripts 2>/dev/null
echo "Axis 3 — committed asm:"; for d in dw*/github/sw-embed/*/; do git -C "$d" ls-files 'asm/*.s' 2>/dev/null | grep -q . && echo "$d"; done
```

The epic is done when all three print nothing and axis 4 is closed — **except
documented intentional residuals**: historical/post-mortem references that
*correctly* attribute past behavior to `cor24-run` (e.g. `sw-cor24-macrolisp`
`docs/fix-repl.md` and the `justfile` migration-rationale comment) are kept, since
rewriting them would falsify history. Treat those as allowed, not regressions.
