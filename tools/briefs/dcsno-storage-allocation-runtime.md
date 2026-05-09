# Brief: design SNOBOL4's storage runtime over PL/SW primitives

**Owner:** dcsno
**Branch:** `pr/storage-allocation-runtime`
**Repo:** `sw-cor24-snobol4`
**Prerequisite:** read
`/disk1/.../work/dcpls/github/sw-embed/sw-cor24-plsw/docs/storage-allocation.md`
first. That doc describes the PL/SW substrate this saga builds
on. As of dcpls's `pr/storage-macros` + `pr/getmain-freemain-macros`,
the substrate is **fully shipped**: `include/_plsw_storage.msw`
provides `_PLSW_GETMAIN(SIZE)` and `_PLSW_FREEMAIN(USERADDR, LEN)`
procedures over a free-list-backed heap (default 64 KB,
configurable via `%DEFINE PLSW_HEAP_SIZE`), AND PL/I-flavored
`?GETMAIN(LENGTH, ADDRESS, RC)` / `?FREEMAIN(LENGTH, ADDRESS, RC)`
macros wrapping them. Reg-rs cases `plsw_storage_*` cover alloc,
coalesce, OOM, double-free, and size-mismatch using the macro form.

You can implement against either surface today.

## Context

SNOBOL4 on PL/SW has a high-frequency allocation pattern:
parsing source, building pattern objects, evaluating matches,
backtracking, building strings. Today the SNOBOL4 source under
`sw-cor24-snobol4/src/` either doesn't allocate (uses static
buffers) or rolls a bump arena ad hoc. As `compiler.sno` and
larger programs land, this stops scaling.

The PL/SW design doc lays out a clean layering:

> PL/SW provides only the raw alloc/free primitives. Region
> boundaries, mark/reclaim, and garbage collection live in the
> consumer's runtime.

That means `include/_plsw_storage.msw` gives SNOBOL4 the
`?GETMAIN SET(P) LENGTH(N) RC(rc);` /
`?FREEMAIN ADDR(P) LENGTH(N) RC(rc);` macros (which expand to
`P = _PLSW_GETMAIN(N); ...` and `rc = _PLSW_FREEMAIN(P, N);`
respectively) plus the underlying procedures
`_PLSW_GETMAIN(SIZE)` and `_PLSW_FREEMAIN(ADDR, LEN)`. RC values:
GETMAIN 0=success / 4=OOM; FREEMAIN 0=success / 1=double-free /
2=size-mismatch. **Anything beyond those primitives — region
save/restore, mark/sweep, generational GC, per-context heaps —
is yours.**

Why this split: Prolog wants WAM-style trail unwinding, future
Fortran wants common-block static + stack frames, future C-like
guests want plain malloc/free. Each has its own storage policy
on top of the same dumb substrate. SNOBOL4's policy lives in
SNOBOL4's runtime.

## Goal

After this saga: SNOBOL4 has a documented, implemented storage
runtime that scales to the workloads it'll see (`compiler.sno`,
pattern-heavy demos, etc.). The runtime calls `?GETMAIN` /
`?FREEMAIN` (or the underlying `_PLSW_GETMAIN`/`_PLSW_FREEMAIN`
procedures) as primitives and adds whatever policy SNOBOL4
needs on top.

## Two reasonable designs to choose from

Both are valid. Pick the one that fits SNOBOL4's actual
allocation profile; this brief is not prescriptive about which.

### Design A: GC over a single big region

- One `?GETMAIN SET(BASE) LENGTH(BIG) RC(RET);` at startup carves
  out the whole SNOBOL4 heap (e.g. 32 KB or 64 KB).
- SNOBOL4's runtime owns that region: cell allocator, mark/sweep
  (or copying) GC, root-set traversal of the SNOBOL4 stack /
  variable table, etc. PL/SW's free-list is never touched after
  startup; `?FREEMAIN` is called once at program exit (or never).
- Pros: traditional SNOBOL4 model, no per-cell PL/SW call
  overhead, full control over headers and tags, handles arbitrary
  liveness graphs.
- Cons: more code (a real GC). Need a precise root set, which
  means cooperating with PL/SW's calling convention to find live
  cells in stack frames and registers.

### Design B: region-stack over `?GETMAIN`/`?FREEMAIN`

- Multiple `?GETMAIN SET(REGION) LENGTH(REGION_SIZE) RC(RET);`
  blocks form a stack of regions (think arena-per-pattern-match-attempt).
- Each match attempt pushes a region; on backtrack, the region
  is `?FREEMAIN`'d wholesale.
- Long-lived data (symbol table, compiled patterns) lives in a
  base region that's never popped.
- Pros: simpler than full GC; matches SNOBOL4's natural
  pattern-match scope structure; fragmentation is bounded
  because each region is freed wholesale.
- Cons: cells that escape a region (e.g. captured by an outer
  match) need explicit promotion to an older region or
  copy-out. Cells that don't escape are free; cells that do
  cost the copy.

A third hybrid (small fixed-size cell pool for high-churn
allocations, region-stack for variable-size work, GC only over
the symbol table) is also possible. Whichever approach you take,
**the doc-of-record for "what is SNOBOL4's storage model" lives
in `sw-cor24-snobol4/docs/`**, not in PL/SW.

## What this saga delivers

Pick one of these scopes; both are reasonable as standalone
sagas:

- **Scope A: Design + plan only.** Audit current allocation
  sites, write `sw-cor24-snobol4/docs/storage.md` describing
  the chosen runtime model (A, B, or hybrid), draft an interface
  for the runtime procedures (e.g. `_SNO_ALLOC_CELL`,
  `_SNO_GC_COLLECT`), but don't implement yet. Implementation is
  a follow-up saga.
- **Scope B: Design + minimum runtime.** Same docs as Scope A
  plus an MVP of the chosen runtime — enough to handle
  `examples/hello.sno` and one pattern-matching demo without
  static buffers. Subsequent sagas widen coverage.

Pick whichever fits your bandwidth. If unsure, do Scope A first
— design errors here are cheap to fix on paper, expensive to fix
in shipped runtime code.

## Migration mapping (when ready)

For Scope B (or for whenever the runtime lands), each existing
allocation site moves from `DCL ... BYTE; DCL ... INT INIT(0);`
ad-hoc bump arenas to either:

```pl/i
?GETMAIN  SET(P)  LENGTH(N) RC(RET);
/* ... use P ... */
?FREEMAIN ADDR(P) LENGTH(N) RC(RET);
```

… or a SNOBOL4-runtime call that wraps `?GETMAIN` with policy:

```pl/i
P = SNO_ALLOC_CELL(KIND_STRING, length);
/* ... use P ... */
/* GC reclaims when no longer reachable */
```

Inventory your existing call sites with:
```sh
grep -nE 'ARENA_POS|arena_alloc|ALLOC.*PROC' sw-cor24-snobol4/src/
```

## What goes in this PR (Scope A version)

1. Audit of existing allocation sites in `src/`.
2. `docs/storage.md` describing the chosen runtime model.
3. Drafted interface for `_SNO_ALLOC_*` (or whatever names fit)
   procedures.
4. Plan for which allocation sites move first when the runtime
   ships.
5. Optional: a sample `.plsw` showing the call shape, against a
   stub `_PLSW_GETMAIN` that returns 0 (so the design is at
   least syntactically validated against PL/SW today).

## What does NOT go in this PR

- No `plsw_storage.{msw,plsw}` source — that's dcpls's saga
  (`pr/storage-macros`). Don't implement the substrate; depend
  on it.
- No PL/SW compiler or macro changes.
- No SNOBOL4 language changes (no `.sno` source rewrites).
- No allocation runtime that conflicts with the eventual
  PL/SW substrate (e.g. don't reimplement `?GETMAIN`'s job in
  SNOBOL4 source — call it).

## When done

Push `pr/storage-allocation-runtime` and signal mike. Promotion
to `dev`/`main` is mike's call. After dcpls's `pr/storage-macros`
ships, SNOBOL4's runtime can land in a follow-up saga (or this
saga's Scope B can be extended in place if not yet relayed).

## Questions to answer in your design doc

1. What is the dominant allocation size in SNOBOL4? (Tiny cells?
   Medium strings? Variable patterns?) The answer should drive
   the choice between Design A (GC over big region) and Design B
   (region-stack).
2. What is the rooted set during pattern matching? (i.e. which
   pointers must remain valid across a `?GETMAIN`?) If you can't
   enumerate this concisely, GC is harder; region-stack may be a
   better fit.
3. Are there any natural "scope boundaries" in SNOBOL4 execution
   where you could drop everything cheaply? (Match attempt?
   Statement boundary? Function call?) Region-stack maps cleanly
   onto these.
4. Worst-case live set — how big can the SNOBOL4 heap get before
   you'd consider it pathological? That sets the reasonable
   default for `PLSW_HEAP_SIZE` in your `%INCLUDE plsw_storage`
   line.
