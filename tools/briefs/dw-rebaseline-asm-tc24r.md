# Brief: re-baseline committed `asm/` against the blessed `tc24r`

**Owner:** any `dw*` agent whose repo **commits generated `asm/*.s`** (first/worked
instance: **dwmls** / `web-sw-cor24-macrolisp`). Audit step below identifies the rest.
**Branch:** `feat/rebaseline-asm-tc24r` → `pr/rebaseline-asm-tc24r`
**Depends on:** the blessed `tc24r` on PATH (see "Canonical tool").
**Drafted by:** mike (coordinator)
**Part of:** the cor24-rs retirement epic — see `retire-cor24-rs.md`.

## DECISION (governs — supersedes any "default to B" wording below)

**Approach A+ (re-baseline & commit, with a staleness gate), in two PRs.** This
overrides the earlier "default to B." Why B is rejected *for this repo*: the
standard-tier snapshot is generated **upstream by dcmls** (`sw-cor24-macrolisp`
`justfile` / `snapshot-save.s`), so it is **not dwmls's artifact to produce in a
`build.rs`**, and B would force a build-time emulator run to regenerate it.

- **Pin to `sw-cor24-macrolisp` `dev`/`main` `1a2d777`** (the promoted state;
  tree-identical to `86ce8e3`) — **not** the stale `f6459e4`. The local sibling
  must be re-provisioned to this first (coordinator handles — see below).
- **`lazy.l24` and `fullbig` are now VERIFICATION TARGETS, not exclusions** —
  dcmls fixed them in `86ce8e3` (tail-recursive lazy-take; fullbig→8K EBR). The
  earlier "skip them" note is obsolete.
- **PR1 — script-only, no artifact change:** harden `build-all.sh` to fail-loud on
  a missing/unpinned `tc24r`, **and fix the snapshot-path gap** dwmls found —
  `build-all.sh` regenerates the *unused* `repl-standard.s` but not the
  *consumed* `repl-snapshot.s` / `standard.snap`.
- **PR2 — the resync:** regenerate the `.s` + `standard.snap` pinned to `1a2d777`,
  functionally verify (protocol below), commit asm + snapshot + rebuilt `pages/`.
- **Snapshot generation:** produce `standard.snap` by running dcmls's (now
  migrated) `justfile` snapshot recipe against the pinned source, then commit the
  output here. The *recipe* is dcmls's; dwmls *runs* it against the pin.

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

## Approach B — asm as a build artifact (CONSIDERED, REJECTED for this repo)

Rejected per the DECISION above: the standard-tier `standard.snap` is generated
upstream by dcmls, so B would (a) force a build-time emulator run to regenerate it
and (b) put an artifact dwmls doesn't own into dwmls's `build.rs`. Kept here only
as reference, and as a possible future option for repos that have **no** snapshot
dependency.

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

## Approach A+ — re-baseline & commit, with a staleness gate (CHOSEN)

Keeps plain `cargo build` dependency-free (it just reads committed `.s`), while a
**staleness gate** removes the "silent drift" failure mode that justified B. Done
as two PRs (see DECISION):

**PR1 — `build-all.sh` hardening (script-only, no artifact change):**
1. Fail-loud if the pinned `tc24r` is missing/unpinned — never a silent PATH
   fallback. (dwmls already added a warning — upgrade it to a hard failure.)
2. **Fix the snapshot-path gap:** regenerate the files the tiers actually consume
   (`repl-snapshot.s` + `standard.snap` for the standard tier), not the unused
   `repl-standard.s`.
3. Add the **staleness gate**: a check (in `build-all.sh` and/or a test/CI) that
   re-running the pinned `tc24r` reproduces the committed `.s` **byte-for-byte** —
   fail on mismatch. This makes future drift impossible-to-be-silent.

**PR2 — the asm + snapshot resync (the artifact change):**
1. Regenerate all `asm/repl-*.s` with the **pinned** `tc24r` against
   `sw-cor24-macrolisp` `1a2d777`.
2. Regenerate `standard.snap` via dcmls's `justfile` snapshot recipe against the
   same pin.
3. **Verify** (protocol below) — do not commit until green, including `lazy.l24`
   and `fullbig` as targets.
4. Commit the new asm + snapshot + rebuilt `pages/` together, message:
   "re-baseline asm+snapshot onto sw-cor24-x-tinyc tc24r @ macrolisp 1a2d777
   (behavior-verified)".

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
- **Include `lazy.l24` and `fullbig`** as explicit pass/verify targets — dcmls
  fixed them at `86ce8e3`, so they must now run correctly post-re-baseline
  (lazy-take terminates; fullbig runs at 8K).
- **`standard` tier wrinkle:** it ships `asm/repl-snapshot.s` + the pre-compiled
  `snapshots/standard.snap` loaded at `0x080000` — **not** `repl-standard.s` (the
  snapshot-path gap PR1 fixes). The snapshot is codegen/address-dependent, so
  regenerate it via dcmls's recipe and re-verify, or the standard tier loads a
  mismatched heap image.

## Out of scope

- No changes to `sw-cor24-x-tinyc` (the compiler) or `sw-cor24-macrolisp` (the
  source). This brief only re-syncs the consuming web repo to them.
- No feature work. Artifact re-baseline + build wiring only.

## When done

Two PRs (see DECISION): push `pr/rebaseline-build-harden` (PR1) then
`pr/rebaseline-asm-tc24r` (PR2); notify mike to relay each via
`dg-relay <agent> <repo> <pr-branch>`. **Approach A+ governs** — B is rejected for
this repo.
