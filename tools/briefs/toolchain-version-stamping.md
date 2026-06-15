# Brief: stamp git provenance into toolchain binary `--version`

**Owner:** the tool-repo agents — **dcemu** (`cor24-emu`, `cor24-dbg`), **dcxas**
(`cor24-asm`), **dcxtc** (`tc24r`, `tc24r-pp`).
**Branch (per repo):** `feat/version-provenance` → `pr/version-provenance`
**Drafted by:** mike (coordinator)

## Why this exists

We just burned real time unable to answer "is the installed `bin/tc24r` current
or stale?" because the binaries don't report their provenance:

```
cor24-emu --version  → cor24-emu 0.1.0     # static; every build says 0.1.0
cor24-asm --version  → cor24-asm 0.1.0     # static
cor24-dbg --version  → cor24-dbg 0.1.0     # static
tc24r     --version  → error (no such flag)# tc24r/tc24r-pp have NO --version
```

After the cor24-rs split (emulator/assembler into separate repos) and ongoing
`tc24r` codegen work, we have multiple binaries from multiple repos on `PATH` and
no way to tell builds apart. A static `0.1.0` is useless; a missing flag is worse.

## The standard (apply to every toolchain binary)

`--version` (and `-V`) must print, on one line:

```
<name> <semver> (<repo> <git-short-sha><+dirty?>, built <ISO-8601-date>)
e.g.  tc24r 0.1.0 (sw-cor24-x-tinyc 96090c8, built 2026-05-23)
```

- **git short SHA** of `HEAD` at build time (and a `+dirty` marker if the worktree
  had uncommitted changes).
- **build date** (UTC, date-granularity is fine).
- the **repo name** so cross-repo binaries are unambiguous.

## How (per repo)

1. Add a `build.rs` that captures provenance into compile-time env vars:
   - `git rev-parse --short HEAD`, a dirty check (`git status --porcelain`), and a
     build timestamp → emit via `println!("cargo:rustc-env=GIT_SHA=…")` etc.
   - Make it degrade gracefully (e.g. `unknown`) when built outside a git tree.
   - Re-run when `HEAD` moves: `println!("cargo:rerun-if-changed=.git/HEAD")`.
2. Wire those env vars into the `--version` string (clap `version=` or manual).
3. **`tc24r` and `tc24r-pp`: add the `--version`/`-V` flag** — they have none today
   (they treat `--version` as an input filename).
4. Keep the human semver (`0.1.0`) — just append the provenance.

Avoid pulling in a heavy crate if a ~20-line `build.rs` suffices; `vergen` is
acceptable if a repo already uses it.

## Coordinator-side (mike, separate/optional)

When binaries are installed to `work/bin/`, record a provenance manifest
(`work/bin/VERSIONS`: `name  sha  date`) so the toolchain state is auditable
without running each binary. The `cor24-run` shim already logs usage; this is the
install-time analogue.

## Verification

- Each binary's `--version` prints name + semver + repo + short SHA + build date.
- Rebuilding after a new commit changes the reported SHA.
- A dirty worktree shows `+dirty`.

## Out of scope

- No behavior changes to the tools. Provenance reporting only.

## When done

Each agent pushes `pr/version-provenance` for its repo; mike relays per repo.
After relay + reinstall, `--version` is the source of truth for "what's on PATH."
