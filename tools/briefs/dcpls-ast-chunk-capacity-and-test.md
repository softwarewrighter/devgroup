# Brief: fix `ast-chunk-storage` capacity regression + add real-input regression test

**Owner:** dcpls
**Branch:** `pr/ast-chunk-capacity-and-test` (or split into two PRs if you prefer; both can land on the same dev cycle)
**Repo:** `sw-cor24-plsw`
**Drafted by:** mike (2026-05-12, after discovering Phase 3 sized the chunk table too small for real workloads).

## Context

`pr/ast-chunk-storage` (commit `136ae2f`, released `04c7ce7..136ae2f`
on 2026-05-12) migrated the AST node pool from a static
`int nd_*[NODE_POOL_MAX]` array (9 parallel arrays × 12,288 entries
× 3 bytes ≈ 332 KB) to chunk-allocated storage backed by the
`chunk.h` allocator. The migration produced a massive `.lgo`
shrink — `plsw.lgo` went from 1,657,430 → 872,174 bytes (-47%).

But it **shipped with a sizing regression that breaks real
compilations**: the new `pl-sw` exits with

```
ERROR: AST pool exhausted (chunk table full)
Program too large for single compilation unit.
```

when fed any meaningful input — including SNOBOL4's own `sno_lex.plsw`
(today's `dcsno/pr/stmt-table-cap` build failed exactly here).
Reproduction:

```bash
cor24-emu --lgo build/plsw.lgo \
  -u "$(build-uart-input sno_lex.plsw)" \
  -n 1000000000 -t 300 --speed 0
# UART output begins with PL/SW Compiler v0.1
# Then: ERROR: AST pool exhausted (chunk table full)
```

The root cause is structural: today's chunk config is
**CHUNK_SIZE = 4096 bytes × CHUNK_MAX = 16 chunks = 64 KB total AST
capacity**, but the pre-Phase-3 static pool was 332 KB. The new
configuration provides ~1/5 the AST address space and runs out on
medium-to-large inputs. Phase 0's `chunk-baseline` measurement
should have caught this against representative inputs.

## Current rollback state

- Production `work/lib/cor24/plsw.lgo` rolled back to the
  pre-Phase-3 build (SHA `712fb0be…`, 1,657,430 bytes) — still
  the 332-KB-AST version. This is the version currently installed
  on PATH for all `pl-sw` invocations.
- Production `work/lib/cor24/snobol4.{lgo,bin}` rebuilt against
  the rolled-back `pl-sw` with today's `dcsno/pr/stmt-table-cap`
  source changes. Working.
- Phase 3 code (`136ae2f` and `41f0bdd` ast-baseline follow-up)
  is on `sw-cor24-plsw/main`. **It is not installed.** Anyone
  building `plsw.lgo` from `main` produces a binary that fails
  on real workloads.

## What this brief asks for

Two coupled deliverables in the same PR (or sequenced PRs that
land in the same dev cycle — your call):

### Part 1: bump chunk capacity so real workloads fit

Update `src/chunk.h` (or wherever the chunk constants live) so
the total chunk capacity is at least the pre-Phase-3 static
pool size (~332 KB). Phase 0's measurements should tell you what
peak AST usage actually is on `sno_lex.plsw` (the biggest known
realistic input) — size CHUNK_MAX (and/or CHUNK_SIZE) to that
peak plus margin.

Concrete suggestions (pick what matches your measurements):

- **Increase CHUNK_MAX to ~128** (16 × 4 KB → 128 × 4 KB ≈ 512 KB
  total). Still smaller than pre-Phase-3's worst-case static
  allocation; should fit easily.
- **Or increase CHUNK_SIZE to 16–32 KB** with fewer chunks
  (e.g. 32 × 16 KB = 512 KB). Tradeoff: fewer chunks = lower
  metadata overhead but coarser granularity for very-small
  compilations.
- **Or both, sized to your Phase 0 numbers + 50% margin.**

Keep the `/* lint-exempt: chunk-pool */` marker (from
`dcpls-dynamic-memory-architecture.md`'s 4 KB rule) on
`chunk_storage`'s declaration.

After the bump, `plsw.lgo` will grow back from 872 KB toward the
1.6 MB range — undoing some of Phase 3's `.lgo`-size win, but
**that win was always conditional on correctness**. We can chase
size again in later phases (real chunk-on-demand, etc.); right
now the priority is restoring "the compiler can compile its
inputs."

### Part 2: add a regression test that catches this class of bug

The bug shipped because no test exercised the new chunk allocator
against an input large enough to fill it. Add one. Required
shape:

1. **A test that compiles a representative-large input
   end-to-end** through the just-built `plsw.lgo`. Candidates:
   - **Self-compile** (`plsw.lgo` compiles its own `src/main.c`).
     This is the strongest test — if it works, the compiler can
     compile anything in its own complexity class.
   - **Compile `sno_lex.plsw`** (or `sno_exec.plsw`) from the
     `sw-cor24-snobol4` repo. Requires sibling-clone or vendored
     fixture, but covers the realistic largest-input case.
   - **A synthetic stress fixture** under
     `tests/inputs/large/<name>.plsw` whose expected AST size
     is documented to exceed today's known-large inputs by a
     margin. More portable than depending on a sibling repo.

2. **Wired into `just test`** (or whatever the CI gate is).
   Build fails if the compile fails. Specifically: assert the
   compilation produces a `.s` (or `.bin` after assembling)
   without `AST pool exhausted` / `SYNTAX ERROR` / similar
   capacity-failure stderr messages.

3. **Size assertion** (optional but valuable): after the
   compile, sanity-check that the peak chunks-in-use is reported
   by the Phase 0 instrumentation, and that it's a healthy
   fraction (say 30%–70%) of CHUNK_MAX. Catches both "too small"
   (peak ≥ 100%) and "too large" (peak < 5%, indicating
   over-budget capacity worth trimming).

The synthetic fixture variant is cleanest because:
- No sibling-clone dependency.
- Fixture stays in this repo's history; any future regression is
  reproducible from a single `just test` run.
- The fixture's documented expected-AST-size makes the test's
  intent self-describing.

## Why a brief, not just a follow-up

This brief exists because:

1. The regression is in shipped main — anyone building `plsw.lgo`
   from source today gets a broken compiler. Visible documentation
   of the issue prevents accidental "fix" attempts that miss the
   point.
2. The regression test is durable infrastructure that protects
   future phase work (Phase 4 buffer migration will also touch
   memory pools; Phase 5 audit might trim caps). Without the
   test, the same class of bug can recur.
3. Future readers (human or agent) of the saga history should be
   able to see *why* the capacity got bumped and what regression
   sparked the test addition.

## What "mike installs" means after this lands

Same as the prior plsw.lgo install flow:

```bash
cd /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-plsw
git pull
just clean && just build-lgo
# Verify the regression test passes before installing!
just test
install -m 0640 build/plsw.lgo \
  /disk1/github/softwarewrighter/devgroup/work/lib/cor24/plsw.lgo
```

`just test` is the gate. If it doesn't pass, mike won't install.

After install, dcsno rebuilds `snobol4.{lgo,bin}` against the
fixed pl-sw, mike installs those, and the post-Phase-3 size win
is realized (with the capacity bump partially undoing it — the
final size will be somewhere between today's 1,657 KB and the
broken 872 KB, depending on the new chunk config).

## Out of scope

- **Phase 4 (buffer migration).** Stays on track; this brief
  doesn't touch it.
- **Static `_MAX` audit.** Phase 5's job; this fix doesn't pre-empt
  it.
- **A general benchmark harness.** The regression test is a
  pass/fail gate, not a performance benchmark. Performance work
  is a separate concern.
- **Reverting `pr/ast-chunk-storage` on main.** Not needed — the
  rollback is contained to the install side. The Phase 3 code
  stays in git history; this brief builds on it.

## When done

- `plsw.lgo` built from main can compile `sno_lex.plsw` (and the
  test fixture) without exhausting the chunk pool.
- `just test` covers the large-input case and would have caught
  this regression had it existed when Phase 3 shipped.
- mike re-installs the fixed `plsw.lgo`, dcsno rebuilds snobol4,
  mike installs `snobol4.{lgo,bin}`, downstream is fully unblocked.
- The Phase 3 architectural win (chunk-allocated AST) is preserved
  with a correct capacity.
