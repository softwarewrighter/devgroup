# Brief: bootstrap PL/SW into the shared toolchain

**Owner:** dcpls
**Branch:** `pr/bootstrap-toolchain`
**Repo:** `sw-cor24-plsw`

## Context

`sw-cor24-plsw` is the foundation of the COR24-target language layer. The PL/SW compiler is the bottom of the bootstrap stack: SNOBOL4, Prolog (LAM VM), and others compile through it. As long as PL/SW's outputs and tools live only inside its own `target/` and `build/` dirs, every downstream repo has to reach into our internals via `$HOME` or sibling-relative paths — fragile.

This saga makes PL/SW shippable into the shared toolchain that mike maintains. After it lands, mike's forthcoming `tools/build-all` orchestrator can install your artifacts to a shared location and downstream consumers (dcsno, dcprl, etc.) can find them on a stable path.

The other change forcing this work: the emulator no longer has `--run` or `--assemble` (dcemu's `pr/remove-internal-assembler` saga, just landed). All your build scripts that use `cor24-run --run plsw.s` and `cor24-run --assemble ...` will break the moment we install the new `cor24-emu` binary. The new pipeline is **`cor24-asm` + `cor24-emu --lgo`** as separate steps.

## Goal

Three deliverables:

1. **`build/plsw.lgo`** — the PL/SW compiler as a `.lgo` artifact, ready to load into `cor24-emu`. Today the compiler stops at `build/plsw.s`; downstream has to re-assemble it. After this saga, `plsw.lgo` is the canonical shippable.
2. **Migrate every `cor24-run --run` / `cor24-run --assemble` callsite** in this repo to the new pipeline (`cor24-asm` + `cor24-emu --lgo`). Affected files:
   - `justfile` (3 callsites: `run`, `run-input`, `test`)
   - `scripts/pipeline.sh` (2 callsites)
   - `scripts/pipeline-dump.sh` (2 callsites)
   - `components/linker/tests/demo-fixup.sh` (2 `--assemble` callsites)
   - `components/linker/tests/demo-plsw-modular.sh` (1 `--run`)
3. **Stage the Layer 1 native binaries** (`link24`, `meta-gen` from `components/linker/`) so mike's toolchain orchestrator can install them. The build already produces them in `components/linker/target/release/` — just needs a `just install-layer1` target (or equivalent) that copies them to a known relative location.

## Migration mapping (definitive)

The new `cor24-asm` binary fully replaces the emulator's `--assemble` flag:

| Old (broken) | New |
|---|---|
| `cor24-run --assemble in.s out.bin out.lst` | `cor24-asm in.s --bin out.bin --listing out.lst` |
| `cor24-run --assemble in.s out.bin /dev/null` | `cor24-asm in.s --bin out.bin` |
| `cor24-run --run prog.s [opts]` | `cor24-asm prog.s -o prog.lgo && cor24-emu --lgo prog.lgo [opts]` |

For the `--run` migration, prefer building the `.lgo` once and reusing it across multiple `cor24-emu --lgo` invocations rather than re-assembling each time. In `pipeline.sh`, that means `cor24-asm build/plsw.s -o build/plsw.lgo` once at top, then every emulator invocation uses `--lgo build/plsw.lgo`.

If you find a callsite where the assembly source is generated dynamically inside a temp file (e.g., `pipeline-dump.sh` line 112), keep the assemble + run pair adjacent: `cor24-asm $TMPASM -o $TMPLGO && cor24-emu --lgo $TMPLGO ...`.

## `cor24-emu` vs `cor24-run`

The current emulator binary is `cor24-emu`. `work/bin/cor24-run` is a stale-named copy of an older build that still has `--run` and `--assemble` flags; it works today but is being phased out.

**Use `cor24-emu` in all new/migrated invocations.** The transition plan is mike-side; you don't need to do anything special — `cor24-emu` will be on PATH by the time your saga runs.

If you find that during your migration testing, the `cor24-emu` on your PATH is the *new* one (no `--run`/`--assemble`), great — that's what we want. If by chance you still have an old build cached locally, run `hash -r` to refresh.

## `build/plsw.lgo` — concrete pipeline

Add to `justfile`:

```
plsw_lgo := "build/plsw.lgo"

# Build .lgo for shared toolchain consumers
build-lgo: build
    cor24-asm {{main_s}} -o {{plsw_lgo}}
```

And update existing recipes to depend on `build-lgo` and use `cor24-emu --lgo {{plsw_lgo}}`:

```
run: build-lgo
    cor24-emu --lgo {{plsw_lgo}} --terminal --echo --speed 0

run-input input: build-lgo
    cor24-emu --lgo {{plsw_lgo}} --speed 0 -u "{{input}}"

test: build-lgo
    cor24-emu --lgo {{plsw_lgo}} --terminal --speed 0 -n 100000000
```

The `pipeline.sh` and `pipeline-dump.sh` scripts should also build `plsw.lgo` once near the top (or accept it as already-built) and use `cor24-emu --lgo "$PLSW_LGO"` throughout.

## Layer 1 binaries (`link24` + `meta-gen`)

These are produced by `components/linker/`'s Rust workspace member. Today:

```
cd components/linker && cargo build --release
# → components/linker/target/release/link24
# → components/linker/target/release/meta-gen
```

Add a `just install-layer1` recipe (or `scripts/stage-layer1.sh`) that:
1. Builds release-mode binaries via `cargo build --release -p link24` from the repo root (workspace dispatch).
2. Copies them to a stable relative path inside the repo for the orchestrator to grab — suggest `dist/bin/link24` and `dist/bin/meta-gen` (gitignored).
3. Or — simpler — just documents in README the canonical relative path `components/linker/target/release/link24` so the orchestrator knows where to look.

Either way is fine; pick whatever feels cleanest and document it.

## Hardcoded path issue (please fix)

`justfile` line 4 has:

```
tc24r_include := env("HOME") / "github/sw-vibe-coding/tc24r/include"
```

That path is wrong for this environment (the repo is `sw-cor24-x-tinyc` under `sw-embed`, not `sw-vibe-coding`). Tc24r's includes are inside its own repo at `sw-cor24-x-tinyc/include/`. Fix this to use one of:

- The sibling-clone convention: `env("HOME") / "github/sw-embed/sw-cor24-x-tinyc/include"`
- Or better: detect dynamically — `tc24r --print-include-dir` or similar. Doesn't exist today, so use the sibling path for now and flag the dynamic-discovery enhancement as a future tc24r feature for dcxtc.

## What goes in this PR

1. New `just build-lgo` recipe that produces `build/plsw.lgo` via `cor24-asm`.
2. Migrate `justfile` recipes (`run`, `run-input`, `test`) to use `cor24-emu --lgo`.
3. Migrate `scripts/pipeline.sh` and `scripts/pipeline-dump.sh` to use `cor24-asm` + `cor24-emu --lgo`.
4. Migrate `components/linker/tests/demo-fixup.sh` and `demo-plsw-modular.sh` similarly.
5. Add `just install-layer1` (or `scripts/stage-layer1.sh`) that produces `link24` and `meta-gen` ready for the orchestrator.
6. Fix the hardcoded `tc24r_include` path.
7. Update README to document:
   - Canonical artifact: `build/plsw.lgo` (plus how downstream consumers should consume it: `cor24-emu --lgo path/to/plsw.lgo ...`)
   - Layer 1 binary paths (where `link24` and `meta-gen` end up after build)
8. Run the full test suite (your existing `just test`, `just hello-macro`, `just chain`, plus the linker tests under `components/linker/`) to verify everything still works after the migration.

## What does NOT go in this PR

- No changes to the actual PL/SW compiler logic (`src/main.c`, the COR24-asm output format, etc.). This is purely build-system migration.
- No changes to the linker's Rust code (`components/linker/src/`) — only the test scripts that use `--assemble`.
- No reaching across to other repos (snobol4, prolog) to update *their* build scripts. Those are separate sagas (each owner's repo). Just make sure your README documents the new canonical artifact paths so they can update on their side.
- No installing your binaries into `work/bin/` — that's mike's job after the orchestrator is built.
- No new compiler features, optimizations, or refactors. Strictly migration + lgo packaging.

## Verification before push

Run each of these and confirm clean:

```
just clean
just build              # produces build/plsw.s
just build-lgo          # produces build/plsw.lgo
just run-input "c\nhello\n\x04"   # quick smoke test
just test                # full test
just hello-macro
just chain
just install-layer1      # produces link24 + meta-gen
```

And in the linker tests:
```
cd components/linker
./tests/demo-fixup.sh
./tests/demo-plsw-modular.sh
```

If anything breaks during migration that wasn't broken before, that's a real regression — investigate before pushing. If everything else is green and only the migration changed observable behavior in ways that are intended (e.g., a separate `.lgo` file now exists), you're good.

## When done

Push `pr/bootstrap-toolchain` and notify mike. After relay:
- mike installs `cor24-emu` into PATH (replacing the stale `cor24-run`)
- mike installs `link24` and `meta-gen` from your repo into the shared toolchain location
- mike installs `build/plsw.lgo` to the shared lib (likely `tools/lib/cor24/plsw.lgo` or `work/lib/cor24/plsw.lgo`)
- mike updates dcsno, dcprl, dcoca, dcbas, dcscr briefs to migrate their `--run`/`--assemble` callsites and consume the new shared artifacts

Promotion to `main` is mike's call, separately from relay.
