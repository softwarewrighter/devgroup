# Brief: migrate build to PATH-resolved toolchain — generic, multi-agent

**Owners:** any `dc*` agent whose `scripts/build.sh` or `justfile` still uses `cor24-run --run`, `cor24-run --assemble`, or hardcoded `$HOME/github/...` sibling paths.
**Branch (each agent uses their own):** `pr/migrate-toolchain`
**Repos likely affected:** `sw-cor24-{basic, pascal, ocaml, prolog, rpg-ii, forth, macrolisp, apl, monitor, script, pcode, snobol4}` and possibly more — check via the audit step below.

## Context

The COR24 toolchain has consolidated onto a clean PATH-resolved model:

| Tool | What it does |
|---|---|
| `tc24r` | C → COR24 `.s` |
| `cor24-asm` | `.s` → `.lgo` / `.bin` / `.lst` (with optional `--base-addr`) |
| `cor24-emu` | run `.lgo`; load raw bytes via `--load-binary <file>@<addr> --entry <addr>` |
| `cor24-dbg` | debug `.lgo` |
| `link24`, `meta-gen` | PL/SW separate-compilation linker tools |
| `pl-sw` | run the PL/SW compiler (wraps `cor24-emu --lgo plsw.lgo`) |
| `snobol4` | run the SNOBOL4 interpreter (after dcsno's saga ships) |

All of these resolve from `/disk1/github/softwarewrighter/devgroup/work/bin/` on every `dc*`/`dw*` user's PATH. Your build scripts should call them by short name (`pl-sw`, not `$HOME/.../plsw.s` invoked through `cor24-run --run`).

The deprecated `cor24-run` binary still exists for transition but **all flags it provided beyond `--lgo`/`--load-binary`/`--demo` (i.e., `--run`, `--assemble`) have been removed from the new `cor24-emu`**. dcemu's `pr/remove-internal-assembler` saga shipped this. Your scripts using `cor24-run --run` or `--assemble` are running against a stale binary that's slated for retirement; they'll break the moment mike retires `cor24-run` from `work/bin/`.

## Pre-flight: does this apply to you?

```bash
cd ~/github/sw-embed/<your-repo>
grep -rnE 'cor24-run --(run|assemble)|\$HOME/github|\$PLSW_DIR' \
  scripts/ justfile Makefile *.sh 2>/dev/null
```

Any hits → this brief is for you. If clean → you're already migrated, skip.

## Migration mapping (definitive)

| Old | New |
|---|---|
| `cor24-run --run prog.s [opts]` | `cor24-asm prog.s -o prog.lgo && cor24-emu --lgo prog.lgo [opts]` |
| `cor24-run --assemble in.s out.bin out.lst` | `cor24-asm in.s --bin out.bin --listing out.lst` |
| `cor24-run --assemble in.s out.bin /dev/null` | `cor24-asm in.s --bin out.bin` |
| `cor24-run --assemble in.s out.bin /dev/null --base-addr 0x1000` | `cor24-asm in.s --bin out.bin --base-addr 0x1000` |
| `cor24-run --load-binary file@addr --entry addr` | `cor24-emu --load-binary file@addr --entry addr` (same flag spelling) |
| `cor24-run --terminal` (with `--lgo`/`--load-binary`) | `cor24-emu --terminal` (same) |
| `$HOME/github/sw-embed/sw-cor24-plsw/build/plsw.s` (and feeding to cor24-run --run) | `pl-sw` (already wraps the equivalent run-the-compiler dance) |
| `$HOME/.../sw-cor24-plsw/components/linker/target/release/link24` | just `link24` (on PATH) |
| `$HOME/.../sw-cor24-plsw/components/linker/target/release/meta-gen` | just `meta-gen` (on PATH) |
| `$HOME/github/sw-embed/sw-cor24-pcode/target/release/pl24r` | (still per-repo for now) `../sw-cor24-pcode/target/release/pl24r` (relative, not `$HOME`) — or wait until pl24r/pa24r are also installed to `work/bin/` (mike-side TODO) |

## Common patterns

### Compiling `.plsw` source (the PL/SW pipeline)

dcpls migrated their `scripts/pipeline.sh` — copy the pattern:

```bash
# Build INPUT via FILE:/SOURCE: protocol
INPUT=$(printf 'c\nFILE:%s\n%s\n\x1ESOURCE:\n%s\n\x04' \
    "$(basename "$MACRO_FILE")" "$(cat "$MACRO_FILE")" "$(cat "$MAIN_PLSW")")

# Compile via pl-sw (NOT cor24-run --run)
COMPILER_OUT=$(pl-sw -u "$INPUT" --speed 0 -n 200_000_000 -t 600 2>&1)

# Extract assembly markers (per-repo specific)
echo "$COMPILER_OUT" | sed -n '/^--- generated assembly ---/,/^--- end ---/p' > out.s

# Assemble with the new tool
cor24-asm out.s -o out.lgo
```

### Compiling `.c` source (the tc24r pipeline)

```bash
tc24r src/foo.c -o build/foo.s -I include   # produces COR24 .s
cor24-asm build/foo.s -o build/foo.lgo      # assembles to .lgo
cor24-emu --lgo build/foo.lgo               # runs
```

### Linker tests (with `--base-addr`)

Pass-2 reassembly now works:
```bash
# was: cor24-run --assemble mod.s mod.bin mod.lst --base-addr 0x100
cor24-asm mod.s --bin mod.bin --listing mod.lst --base-addr 0x100
```

## Robustness fixes worth folding in

While migrating, also fix these common issues if your repo has them:

1. **`build/` doesn't auto-create.** If `tc24r src/main.c -o build/foo.s` fails on a fresh clone with "cannot write build/foo.s: No such file or directory", add `mkdir -p build` to your `build:` recipe.
2. **`$HOME/...` paths assume a specific layout** that doesn't hold for d* users in the multi-user devgroup setup. Replace with sibling-relative or PATH-resolved.
3. **Mac-specific paths** (`/Users/mike/...`) are bugs; use sibling-relative paths.
4. **`stat -f%z`** (BSD/Mac) doesn't work on Linux; use `wc -c <` or `stat -c%s` instead. dcpls fixed this in their linker tests — see their commit.

## What goes in this PR

1. Replace every `cor24-run --run`/`--assemble` callsite per the mapping above.
2. Replace `$HOME/.../sw-cor24-X/...` sibling-clone paths with PATH-resolved tools where a wrapper exists, or proper sibling-relative paths otherwise.
3. Add `mkdir -p build` (or equivalent) where build artifacts are written.
4. Run your full test suite (whatever your repo has — `just test`, `./scripts/test.sh`, demos, etc.) and verify it passes against the new toolchain.
5. Update `README.md` setup instructions if they reference the old workflow (sibling clone of plsw, etc.) — point at the new workflow (`pl-sw` on PATH).
6. Single saga commit (or split per-script if cleaner).

## What does NOT go in this PR

- No actual code logic changes — purely build/script migration.
- No new features.
- No reaching into other repos (other than the read-only "sibling clone for cargo path-deps" pattern).

## When done

Workflow: `dg-new-feature migrate-toolchain` (creates `feat/migrate-toolchain` from `dev`) → implement and verify → `dg-mark-pr` to rename to `pr/migrate-toolchain` when ready. Signal mike. After relay, your build will work for any d* user without hardcoded home paths or deprecated flags. See `README.md` in this dir for the canonical workflow.

## Why this matters

Without migration, your repo's tests/demos are running against the deprecated `cor24-run` binary in `work/bin/`. That binary is slated for retirement once all consumers migrate. The longer your repo waits, the more likely it'll silently break when mike removes `cor24-run`. Migrate now, ship a clean repo, future-proof yourself.
