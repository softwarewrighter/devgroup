# Brief: bootstrap SNOBOL4 onto the shared toolchain

**Owner:** dcsno
**Branch:** `pr/bootstrap-toolchain`
**Repo:** `sw-cor24-snobol4`
**Prerequisites (all satisfied 2026-05-08):** ✅ tc24r reinstalled with array-size + string-literal-concat fixes, ✅ `build/plsw.lgo` installed at `/disk1/.../work/lib/cor24/plsw.lgo`, ✅ `pl-sw` wrapper on PATH at `/disk1/.../work/bin/pl-sw`. **You can start now.**

## Why this is critical-path

This saga is the **single gating step for the Fortran hello-world live demo**:

```
dcsno (this saga)        →  snobol4.lgo + snobol4 wrapper on PATH
        ↓ unblocks
dcftn (separate saga)    →  hello.f compiler chain (or pre-baked hello.lgo)
        ↓ unblocks
dwftn (separate saga)    →  web-sw-cor24-fortran live demo deployed
```

dcftn already shipped `pr/verify-snobol4-and-vendor` with `scripts/verify-snobol4.sh` ready to gate-check the SNOBOL4 builtins (SIZE/SUBSTR/CHAR) — but it can't run until `snobol4` is on PATH. Your saga unblocks them, which unblocks dwftn.

## Known dcemu bug — workaround required for the canonical wrapper

You discovered (during this saga) that `cor24-emu --lgo X.lgo --load-binary Y@addr` silently drops the `--load-binary` flag. The fix is briefed at `tools/briefs/dcemu-lgo-load-binary-merge.md` — a one-line miss in `cli/src/run.rs`'s lgo arm. dcemu plans to tackle it after finishing prior saga work.

**Don't wait for dcemu.** Ship your saga with a **documented workaround** using the binary-only invocation path:

```bash
# Working today (no --lgo, just --load-binary; bug-free path):
cor24-emu --load-binary build/snobol4.bin@0 \
          --load-binary "$INPUT"@0x080000 \
          --entry 0 -n 200000000 --speed 0 -q

# Canonical (post-dcemu-fix, blocked today):
cor24-emu --lgo build/snobol4.lgo \
          --load-binary "$INPUT"@0x080000 \
          --entry 0 -n 200000000 --speed 0 -q
```

In your `scripts/build.sh` / wrapper / README:

1. Use the binary-only path as the working invocation.
2. Note in the README + a comment in the wrapper: *"`cor24-emu --lgo` + `--load-binary` is the canonical form per project convention but is currently silently broken (see `tools/briefs/dcemu-lgo-load-binary-merge.md`). This wrapper uses `--load-binary` for both the interpreter image and the source input as a workaround. Flip back to `--lgo` once dcemu's fix is reinstalled in `work/bin/`."*
3. Produce both `build/snobol4.bin` and `build/snobol4.lgo` in your `just build-lgo` recipe — `cor24-asm in.s --bin out.bin -o out.lgo` produces both. The binary is the workaround consumer; the lgo is what mike installs to `work/lib/cor24/snobol4.lgo` for downstream use.

After dcemu's fix ships, a tiny follow-up saga (`pr/snobol4-canonical-lgo-wrapper`) flips the workaround comments + wrapper invocation. ~3-line change.

## Context

`sw-cor24-snobol4` is the SNOBOL4 interpreter (written in PL/SW, compiled to COR24). Today its build scripts assume:
- A sibling clone of `sw-cor24-plsw` under `$HOME/github/sw-embed/sw-cor24-plsw` (works on the original mac dev setup, fails in the multi-user devgroup environment)
- The PL/SW compiler is consumed as a `.s` file (`$PLSW_DIR/build/plsw.s`) rather than the canonical `.lgo`
- `cor24-run --run`/`--assemble` flags (deprecated; removed from `cor24-emu` in a recent dcemu saga)
- `link24` and `meta-gen` referenced via the upstream's `target/release/` dirs rather than `$PATH`

This saga migrates SNOBOL4's build to the **shared toolchain** that mike now hosts on every dc/dw user's PATH. After it lands, `sw-cor24-snobol4` can be built by anyone in the devgroup with no sibling-clone setup, and produces `build/snobol4.lgo` as a canonical shippable artifact.

## Goal

Three deliverables (mirrors dcpls's `pr/bootstrap-toolchain` for PL/SW):

1. **Migrate every callsite** of `cor24-run --run`, `cor24-run --assemble`, and `$HOME/.../sw-cor24-plsw/...` paths to PATH-resolved tools.
2. **Add `just build-lgo` recipe** (or equivalent) that produces `build/snobol4.lgo` via the canonical pipeline:
   ```
   pl-sw <snobol4-source.plsw> > build/snobol4.s
   cor24-asm build/snobol4.s -o build/snobol4.lgo
   ```
   (or with link24 + meta-gen if the build is modular — see your existing `scripts/build-modular.sh` for the pattern dcpls migrated to.)
3. **Document** the build pipeline + expected artifact location in README.

## Tools available on PATH after prereqs land

| Binary | Use | Source repo |
|---|---|---|
| `tc24r` | C → COR24 `.s` | sw-cor24-x-tinyc |
| `cor24-asm` | `.s` → `.lgo` / `.bin` / `.lst` | sw-cor24-x-assembler |
| `cor24-emu` | run `.lgo` | sw-cor24-emulator |
| `cor24-dbg` | debug `.lgo` | sw-cor24-emulator |
| `link24`, `meta-gen` | PL/SW separate-compilation linker | sw-cor24-plsw |
| **`pl-sw`** | wrapper: `pl-sw <input.plsw> > output.s` | wraps `cor24-emu --lgo /disk1/.../work/lib/cor24/plsw.lgo` |

## Migration mapping

| Old | New |
|---|---|
| `cor24-run --assemble in.s out.bin out.lst` | `cor24-asm in.s --bin out.bin --listing out.lst` |
| `cor24-run --run prog.s [opts]` | `cor24-asm prog.s -o prog.lgo && cor24-emu --lgo prog.lgo [opts]` |
| `$HOME/github/sw-embed/sw-cor24-plsw/build/plsw.s` (and feeding to cor24-run --run) | `pl-sw` (already does the equivalent run-the-compiler dance) |
| `$HOME/.../sw-cor24-plsw/components/linker/target/release/link24` | just `link24` (on PATH) |
| `$HOME/.../sw-cor24-plsw/components/linker/target/release/meta-gen` | just `meta-gen` (on PATH) |

## Concrete callsites to fix

Audit (`grep -nE 'cor24-run --(run|assemble)|\$HOME/github|\$PLSW_DIR'` in your repo) — at minimum these files, possibly more:

- `scripts/build.sh:11-12` — `PLSW_DIR=$HOME/...`, `COMPILER_ASM=$PLSW_DIR/build/plsw.s`
- `scripts/build-modular.sh:30` — `LINKER_DIR=$HOME/...`
- `scripts/build-modular.sh:102, 122` — `cor24-run --assemble`
- Other build scripts as found by grep
- `justfile` if it references `cor24-run` or `$PLSW_DIR`-style paths
- `README.md` setup instructions (if they tell users to clone PL/SW as a sibling, that's no longer needed for builds)

## What goes in this PR

1. Replace all `$HOME/...sw-cor24-plsw/...` references with PATH-resolved binaries.
2. Replace `cor24-run --run`/`--assemble` with `cor24-asm` + `cor24-emu --lgo` (or just `pl-sw`/`cor24-emu --lgo` as appropriate).
3. Add a `just build-lgo` recipe (or equivalent) that produces `build/snobol4.lgo`.
4. Update README/docs to remove the sibling-clone-of-plsw setup note and reference the new PATH-based tooling.
5. Verify end-to-end: `just build` produces the shippable. Run the full existing demo suite (justfile's `hello`, `count`, `pattern`, etc. — whatever you have) and ensure they still pass with the migrated invocations.

## What does NOT go in this PR

- No SNOBOL4 source-code changes (no .plsw rewrites). This is purely build-system migration.
- No new SNOBOL4 features.
- No installing your binaries into `work/bin/` or `work/lib/cor24/` — that's mike's job after relay.

## When done

Workflow: `dg-new-feature bootstrap-toolchain` (creates `feat/bootstrap-toolchain` from `dev`) → implement and verify → `dg-mark-pr` to rename to `pr/bootstrap-toolchain` when ready. Signal mike. After mike relays:
- mike runs `just build-lgo` (or equivalent) in your relay clone, installs `build/snobol4.lgo` to `work/lib/cor24/snobol4.lgo`, drops a `snobol4` wrapper script at `work/bin/snobol4` (`exec cor24-emu --lgo $TOOLROOT/lib/cor24/snobol4.lgo "$@"`).
- After that lands, **dcftn** is unblocked — the Fortran compiler is in SNOBOL4, so `snobol4` on PATH is the bottom of their build pipeline.

Promotion to `main` is mike's call, separately from relay.
