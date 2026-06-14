# Brief: re-baseline committed `asm/` against the blessed `tc24r`

**Owner:** any `dw*` agent whose repo **commits generated `asm/*.s`** (first/worked
instance: **dwmls** / `web-sw-cor24-macrolisp`). Audit step below identifies the rest.
**Branch:** `feat/rebaseline-asm-tc24r` → `pr/rebaseline-asm-tc24r`
**Depends on:** the blessed `tc24r` on PATH (see "Canonical tool").
**Drafted by:** mike (coordinator)
**Part of:** the cor24-rs retirement epic — see `retire-cor24-rs.md`.

## Why this exists (verified diagnosis)

> **Migration framing.** The committed `asm/*.s` are **old-toolchain (pre-split,
> "tml24c"-era) artifacts** — exactly the kind we are retiring. The goal is not
> just to silence the diff; it is to **stop shipping old-toolchain output** and
> move onto the new `sw-cor24-x-tinyc` compiler. Reverting to the committed asm
> (as was done to keep the feature PR clean) is a temporary hold, not the target
> state.


`web-sw-cor24-macrolisp` commits generated `asm/repl-*.s`. Running
`build-all.sh` regenerates them and produced a ~10× line collapse
(`repl-bare.s`: 114,231 → 10,941 lines) plus a huge diff. This was **surfaced and
quarantined** by dwmls (kept out of `pr/load-save-copy`), then investigated by the
coordinator. Findings:

- **`bin/tc24r` is the blessed compiler.** It is byte-identical (md5 `c539a57b…`)
  to the `sw-cor24-x-tinyc` release build. The **committed asm is the stale side**
  (produced by an older `tc24r`).
- The collapse is **mostly formatting** — ~15,741 removed lines are `.word`
  (old `tc24r` emitted one data word per line; new one packs them).
- It is **also a real codegen change** (peephole optimization): the new compiler
  drops no-op `bra`→next-instruction branches etc. Assembled output is **not**
  byte-identical (329,013 → 328,891 bytes, −122).
- It **is behaviorally equivalent**: running both REPL binaries in `cor24-emu`
  with identical Lisp input gave byte-identical UART output (`3`, `42`,
  `(1 . 2)`, `9`). So the new output is safe — but equivalence must be *verified*,
  not assumed, because the bytes changed.

**Root hazard:** `build-all.sh` silently falls back to "whatever `tc24r` is on
PATH" when the sibling isn't checked out, so a *different* compiler than the one
that produced the committed asm can be used without warning. Committing a
generated artifact that nothing keeps in sync is the underlying defect.

## Canonical tool

The blessed compiler is the **`sw-cor24-x-tinyc` release `tc24r`** (== `bin/tc24r`,
md5 `c539a57b6ec33397120a00990d8a2876`). Pin to it. Never regenerate asm with an
unpinned PATH binary of unknown provenance.

## Audit first (which repos are affected)

```bash
cd /disk1/github/softwarewrighter/devgroup/work
# dw* repos that COMMIT generated .s (the drift class):
for d in dw*/github/sw-embed/*/; do
  git -C "$d" ls-files 'asm/*.s' '*.s' 2>/dev/null | grep -q . && echo "$d"
done
```
Repos that only assemble in `build.rs` from `.c` at build time (no committed `.s`)
are **not** in scope here — they regenerate every build already.

## Approach B — asm as a build artifact (DEFAULT — do this)

This is the standard for the migration: **no committed generated artifacts.** An
old-toolchain artifact can only linger if it's committed; remove that and the
whole drift class disappears for good.

`asm/*.s` is currently a **compile-time input** (`src/config.rs` `include_str!`s
it), so it cannot simply be deleted — the build must produce it first.

1. Add a **`build.rs`** that invokes the pinned `tc24r` to regenerate each
   `asm/repl-*.s` (or emit into `OUT_DIR`) from the `sw-cor24-macrolisp` source,
   and switch `config.rs` `include_str!` to the generated path.
2. `git rm` the committed `asm/*.s` and add `asm/*.s` (or the `OUT_DIR` path) to
   `.gitignore`.
3. Make the build **fail loudly** if the pinned `tc24r` or the source sibling is
   absent — never a silent PATH fallback.
4. Result: every `cargo build` / `trunk build` regenerates asm deterministically;
   no committed artifact can drift again.

Trade-off: introduces a hard build-time dependency on `tc24r` + the source
sibling. Acceptable here because the shared toolchain ships `tc24r` on PATH.

## Approach A — re-baseline and keep committing (escape hatch only)

Use **only** if approach B's build-time codegen is genuinely impractical for a
repo (raise it with mike first). It still satisfies "stop using old-toolchain
output" — the committed file becomes new-`tc24r` output — but it leaves a
committed artifact that must be kept in sync, so it does not close the drift
class. If a repo must keep plain `cargo build` working with **no** new build-time
deps:

1. Regenerate all `asm/repl-*.s` with the **pinned** `tc24r`.
2. **Verify** (see protocol) — do not commit until green.
3. Commit the new compact asm **and** the rebuilt `pages/` together, in a commit
   that says "re-baseline asm onto sw-cor24-x-tinyc tc24r (behavior-verified)".
4. Harden `build-all.sh` to **require** the pinned `tc24r` and fail (not warn) on
   absence. (dwmls already added a warning — upgrade it to fail-loud.)

## Validation protocol (required for either approach)

Behavioral equivalence, because the bytes change:

```bash
ASM=.../work/bin/cor24-asm ; EMU=.../work/bin/cor24-emu
# for each tier: assemble OLD (git show HEAD:asm/repl-T.s) and NEW, run both,
# diff UART output over a representative input set.
$ASM old.s -o old.lgo ; $ASM new.s -o new.lgo
$EMU --lgo old.lgo -u "$INPUT" --speed 0 -n 80000000 --quiet > old.out
$EMU --lgo new.lgo -u "$INPUT" --speed 0 -n 80000000 --quiet > new.out
diff old.out new.out   # must be empty
```

- Cover **all five tiers** (bare/minimal/standard/full/scheme) and the **demo
  corpus** in `src/demos.rs`, not just a smoke test.
- **`standard` tier wrinkle:** it also ships `asm/repl-snapshot.s` + a
  pre-compiled `snapshots/standard.snap` loaded at `0x080000`. The snapshot is
  codegen/address-dependent — **regenerate and re-verify it** alongside the asm,
  or the standard tier will load a mismatched heap image.

## Out of scope

- No changes to `sw-cor24-x-tinyc` (the compiler) or `sw-cor24-macrolisp` (the
  source). This brief only re-syncs the consuming web repo to them.
- No feature work. Artifact re-baseline + build wiring only.

## When done

Push `pr/rebaseline-asm-tc24r`, notify mike. Mike relays via
`dg-relay <agent> <repo> pr/rebaseline-asm-tc24r`. **Default to approach B.** Only
fall back to A after raising the specific build-time blocker with mike.
