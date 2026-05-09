# Brief: ship a working `hello.f → hello.lgo → "Hello, World!"` demo

**Owner:** dcftn
**Branch:** start as `feat/fortran-hello-world`; `dg-mark-pr` when ready (becomes `pr/fortran-hello-world`)
**Repo:** `sw-cor24-fortran`
**Prerequisite:** dcsno's `pr/bootstrap-toolchain` saga must be relayed and `snobol4.lgo` + `snobol4` wrapper installed on PATH. Mike will signal when ready.

## What sw-cor24-fortran is and isn't

`sw-cor24-fortran` is a **Fortran compiler** for COR24 (FTI-0 subset). The compiler is written in SNOBOL4 (a `fortran-compiler.sno` source file, eventually). It runs as a SNOBOL4 program on top of `snobol4.lgo`. **The compiler is the artifact** the repo produces — `hello.f` and other examples are *demos that exercise the compiler*, not the compiler itself.

Two equally valid packaging models for the shipped compiler (mike picks at install time, agent doesn't have to choose):

- **Script form:** `fortran` is a shell wrapper that composes at runtime: `cor24-emu --lgo $TOOLROOT/../lib/cor24/snobol4.lgo --load-binary $TOOLROOT/../lib/cor24/fortran-compiler.sno@0x080000 --entry 0 -u "$(cat <input.f>)"$'\x04' …`. Two installed files: `snobol4.lgo` (already shipped) + `fortran-compiler.sno` (you ship).
- **Bundled form:** at build time, mike combines `snobol4.lgo` + `fortran-compiler.sno` into a single `fortran-compiler.lgo`. Wrapper points at it directly. One installed file.

Both are legitimate. Past me wrongly claimed the bundled form "would never exist by design"; that was wrong, and dcftn shouldn't be constrained by it. Either packaging works for any future Fortran-compiler saga — pick whichever feels cleaner when you get there.

## This particular saga is for a *demo*, not the compiler itself

Despite the long preamble: **this saga's scope is narrow** — it's specifically the **Hello-World demo**, not "build the Fortran compiler." Path A (hand-write hello.s, ship as a fixture) is the recommended approach. The full Fortran compiler is a much larger parallel effort, tracked separately under the FTI-0 milestone-1 saga in your repo.

## Why this matters (revised framing)

This is the **second of three sagas** unblocking the Fortran hello-world live demo at `https://sw-embed.github.io/web-sw-cor24-fortran/`. dcsno ships SNOBOL4 to PATH; you ship `hello.f` + a way to produce `hello.lgo` (Path A: hand-write); dwftn embeds the demo output in their web frontend. The Fortran-compiler-itself saga continues separately and *will* eventually exist as either `fortran-compiler.sno` (script form) or `fortran-compiler.lgo` (bundled form) at `work/lib/cor24/`. This saga is just the demo precursor.

## Known dcemu bug affecting the SNOBOL4 invocation pattern

There's a verified bug (`tools/briefs/dcemu-lgo-load-binary-merge.md`) where `cor24-emu --lgo X.lgo --load-binary Y@addr` silently drops the `--load-binary`. dcsno's `snobol4` wrapper is using a documented workaround (binary-only invocation: `cor24-emu --load-binary snobol4.bin@0 --load-binary <input>@0x080000 --entry 0`) until dcemu fixes it.

For your saga: this means **Path A (hand-write `hello.s`) is meaningfully simpler than Path B (run full SNOBOL4 compiler)** — Path A doesn't touch the SNOBOL4 wrapper at all and so isn't affected by the bug. **Pick Path A unless Path B is clearly faster.**

## Goal

When this saga ships, running:

```
fortran examples/hello.f > /tmp/hello.s
cor24-asm /tmp/hello.s -o /tmp/hello.lgo
cor24-emu --lgo /tmp/hello.lgo --terminal --speed 0
```

emits `Hello, World!` (followed by newline) on stdout/UART. The `examples/hello.f` is the canonical FORTRAN-IV-style hello world:

```fortran
      PROGRAM HELLO
      PRINT *, 'Hello, World!'
      END
```

(Or whatever flavor your FTI-0 subset supports; ASCII output is what matters.)

## Acceptable scope reductions

You're encouraged to **shortcut the full compiler** for this saga. Two flavors:

### Path A: hand-write hello.s, ship as a fixture

If the SNOBOL4-based compiler isn't ready to compile hello.f end-to-end:

1. Write `examples/hello.f` with the canonical source.
2. Hand-write the equivalent COR24 assembly in `examples/hello.s` (or generate it via a one-shot run of your draft `normalize.sno` + downstream phases that DO work).
3. Update `scripts/fortran` so it short-circuits for hello.f: detect this specific input and emit the pre-baked `.s`, OR document that `scripts/fortran` is a stub for now and `examples/hello.s` is the canonical reference output until the compiler is complete.
4. Ship the assembly as a regression fixture (`work/reg-rs/hello.s.expected` or similar).

This unblocks dwftn immediately. You then catch up the actual compiler in subsequent sagas.

### Path B: actual end-to-end compilation

If the compiler is far enough along (normalize.sno verified, parser/codegen drafted), wire the full pipeline and emit the assembly automatically. Then `scripts/fortran examples/hello.f` produces real output. Keep PRINT-statement-only scope — no expressions, no control flow, no I/O variants.

**Pick Path A if it's any faster.** The goal is shipping the demo, not finishing the compiler.

## Verification

```bash
# Step 1: produce assembly
scripts/fortran examples/hello.f > /tmp/hello.s

# Step 2: assemble
cor24-asm /tmp/hello.s -o /tmp/hello.lgo

# Step 3: run; should print "Hello, World!"
cor24-emu --lgo /tmp/hello.lgo --terminal --speed 0
```

Exit code 0, expected output captured as a regression fixture committed to the repo.

Also run `scripts/verify-snobol4.sh` — if it returns 0 (SIZE/SUBSTR/CHAR all good), continue toward Path B. If it returns 2 (snobol4.lgo not deployed), block on dcsno. If 1 (builtins broken), file a precise saga back to dcsno.

## What goes in this PR

1. `examples/hello.f` — canonical hello world source.
2. `examples/hello.s` — corresponding COR24 assembly (hand-written or generated).
3. `examples/hello.lgo` — pre-built lgo (committed; dwftn embeds this directly until the live compile path works).
4. Updated `scripts/fortran` to handle hello.f end-to-end (Path A: short-circuit; Path B: full pipeline).
5. A regression test under `tests/` or `work/reg-rs/` that runs the pipeline and diffs against expected output.
6. README update: "Fortran hello world live demo: see `examples/hello.f` + `examples/hello.lgo`."

## What does NOT go in this PR

- No full FTI-0 compiler. Just hello world.
- No control flow (IF, DO, GOTO).
- No expressions beyond a string literal.
- No additional examples beyond hello.
- No web frontend work (that's dwftn).

## When done

Workflow: `dg-new-feature fortran-hello-world` → implement → `dg-mark-pr`. Signal mike. After mike relays:
- mike installs `examples/hello.lgo` to `work/lib/cor24/hello-fortran.lgo` (or just leaves it in your repo for dwftn to fetch directly)
- mike installs `scripts/fortran` to `work/bin/fortran` (the wrapper, similar to `pl-sw`/`snobol4`)
- mike clears dwftn to start their live-demo saga

Promotion to `main` is mike's call, separately.

## After this saga

Subsequent sagas continue building out the FTI-0 compiler (more statements, expressions, control flow) without blocking the demo. The demo stays live throughout — it just renders a fixed hello.lgo. As your compiler grows, dwftn can eventually swap in dynamic compilation, but that's out of scope here.
