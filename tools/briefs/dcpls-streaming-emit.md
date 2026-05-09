# Brief: stream `.s` emission to UART, eliminate the 256 KB `emit_buf`

**Owner:** dcpls
**Branch:** `pr/streaming-emit`
**Repo:** `sw-cor24-plsw`
**Drafted by:** dcsno (replaces a now-retracted "bump `EMIT_BUF_SIZE`
to 2 MB" brief — that proposal was wrong for the SRAM constraint;
this one corrects the fix path).

## Background and constraint

PL/SW runs *on* COR24 (it's `plsw.lgo` loaded into `cor24-emu`).
COR24 has **1 MB total SRAM** (`000000-0FFFFF` per `cor24-emu
--help`). All of PL/SW's code, data, heap, and `emit_buf` share
that 1 MB.

Today's `plsw.lgo` is ~1 MB encoded — i.e. PL/SW already fills the
SRAM near-entirely. A naive bump of `EMIT_BUF_SIZE` (256 KB → 2 MB)
**does not fit** — exceeds total physical SRAM by 1 MB. So the
fix can't be "make `emit_buf` bigger"; it has to be "make
`emit_buf` smaller or remove it."

## What `emit_buf` is

`include/emit.h` defines a 256 KB static char array `emit_buf`
that accumulates the entire `.s` output for one compilation unit.
Each `emit_ch` / `emit_str` / `emit_int` / `emit_label` writes
into this buffer; at end-of-compilation the buffer is dumped to
UART. The shell wrapper captures UART output and writes the `.s`
file.

The accumulation is an implementation choice, not a semantic
requirement:

- The `.s` is a **one-pass forward-only text stream.** PL/SW
  doesn't seek backward into already-emitted text.
- `cor24-asm` (the downstream consumer) does its own multi-pass
  assembly with its own internal state — it doesn't depend on
  PL/SW's emission strategy.
- UART output already flows live from the emulator to the
  shell stdout to the `.s` file. Buffering in PL/SW just delays
  the flow; it adds no semantic value.

## The blocker the buffer is causing

After the recent `pr/emit-zero-fill` shipped (2026-05-09),
`sno_main.s` dropped from 261 KB to 8 KB — the `.zero N` codegen
fixed the entry-module-specific `INIT(0)` array bloat. But
*library* modules (`%DEFINE LIBRARY`) had their DCLs already
suppressed; `.zero N` finds nothing to compact in them.
Empirical numbers post-install:

```
sno_main.s    261,638 →   8,019  (.zero N replaced .byte 0,0,...)
sno_util.s     71,125 →  71,125  (LIBRARY mode, no static data)
sno_lex.s     220,517 → 222,362  (LIBRARY mode, no static data)
sno_exec.s    261,029 → 262,030  (LIBRARY mode, all code)
```

`grep -cE '\.byte|\.word|\.zero' build/mod/sno_exec.s` returns
**0** — the 262,030 bytes is entirely instruction emission,
nothing the `.zero` codegen can shrink.

So `sno_exec.s` sits at **99.96% of `EMIT_BUF_SIZE`** with
**114 bytes free**. Any new feature in `sno_exec.plsw` (e.g.
dcsno's `saga-expr-completeness` step 003 — extending builtin
arg parsing for nested calls) overflows the buffer. The library-
module ceiling is the load-bearing limit going forward; the
entry-module case was the one already-shipped fix.

## Prerequisite — verify no other readers of `emit_buf`

Before either option below, run a static-analysis sweep on the
PL/SW source tree (workstation-side, before any code change):

```
grep -rn 'emit_buf\|emit_pos\|EMIT_BUF_SIZE' include/ src/
```

The expectation is that the only references are the writer-side
helpers (`emit_ch`, `emit_str`, `emit_int`, `emit_label`, the
final dump call) and the `EMIT_BUF_SIZE` definition itself.
**If any other code reads back from `emit_buf` or queries
`emit_pos`** — for example a size estimator, an in-place patcher,
a debug dumper, or anything that relies on the buffer being a
contiguous addressable byte array — flag it before continuing.
That kind of reader is broken by the streaming change and needs
a different lowering, not just a flush boundary.

If grep shows zero unexpected readers (the expected case), the
recommended option below works as-is.

## The fix: stream emission to UART

Two viable shapes; both replace the 256 KB accumulator with
either a small coalescing buffer or direct UART writes. The
recommendation is **Option B** (small coalescer) — the same
pattern as a stdio line-buffer. Option A (zero-byte direct) is
documented as a variant if profiling shows UART syscall overhead
is negligible.

### Option B (recommended) — tiny coalescing buffer

Keep an `emit_buf` of e.g. 4 KB and flush when full or at
end-of-compilation. UART on cor24-emu is char-at-a-time; PL/SW
emits a *lot* of small writes (every operand, comma, newline),
so coalescing dramatically reduces the syscall count without
costing meaningful SRAM.

```c
#define EMIT_BUF_SIZE 4096

void emit_ch(int ch) {
    if (emit_pos >= EMIT_BUF_SIZE) emit_flush();
    emit_buf[emit_pos] = ch;
    emit_pos = emit_pos + 1;
}

void emit_flush(void) {
    int i = 0;
    while (i < emit_pos) {
        uart_putchar(emit_buf[i]);
        i = i + 1;
    }
    emit_pos = 0;
}
```

Plus a final `emit_flush()` at end-of-compilation (and any
existing `emit_dump`-style call becomes `emit_flush`).

**SRAM cost**: 4 KB (was 256 KB) — frees ~252 KB.
**Code change**: low-level emit primitives in `include/emit.h`,
plus the `emit_flush` helper. The thousands of `emit_str` /
`emit_label` callsites don't change.
**Compile time**: ~0% slowdown (every ~4 KB is one UART burst,
roughly the same shape as today's single end-of-compilation
flush, just N times).

### Option A (variant) — direct streaming, zero buffer

If profiling on a representative compile (e.g. `sno_exec.plsw`)
shows UART overhead is negligible even at one-character-at-a-
time, simplify further by deleting the buffer entirely:

```c
void emit_ch(int ch) {
    /* OLD:
     * if (emit_pos >= EMIT_BUF_SIZE - 1) return;
     * emit_buf[emit_pos] = ch;
     * emit_pos = emit_pos + 1;
     */
    /* NEW: stream directly to UART, no buffer needed. */
    uart_putchar(ch);
}
```

Delete `emit_buf`, `emit_pos`, `EMIT_BUF_SIZE`, and the final
flush.

**SRAM cost**: 0 bytes (was 256 KB).
**Code change**: same site as B, but smaller — no helper
needed.
**Compile time**: potentially significant slowdown if
cor24-emu's UART path is per-call expensive. **Profile before
choosing this over B.**

## Side benefits

- **`.s` output is unbounded.** No more buffer-overflow truncation
  silently corrupting downstream codegen. Any compilation unit
  any consumer wants to compile, fits.
- **PL/SW gets ~256 KB of SRAM headroom.** That's available for
  AST nodes, symbol tables, larger compile-time data structures.
- **`EMIT_BUF_SIZE` constant disappears.** No more "what should
  this be" debate; no more silently-truncated overflow. The
  failure mode is "PL/SW runs longer to emit larger output,"
  which is graceful.
- **All language consumers benefit.** Prolog, future Fortran
  runtime, and every other PL/SW client get the same unbounded
  output ceiling for free.

## Tests

1. **Re-run reg-rs golden suite.** All goldens should stay green.
   `.s` outputs may differ in benign ways (trailing whitespace,
   final-flush newline placement) depending on flush boundaries —
   what matters is that `cor24-asm`'s `.bin` output from each
   golden is byte-identical to today's. The semantic invariant is
   `.bin` equality, not `.s` text equality.
2. **Build SNOBOL4 against the new compiler.** The four modules
   (`sno_main`, `sno_util`, `sno_lex`, `sno_exec`) should produce
   a `build/snobol4.bin` byte-identical to today's
   `work/lib/cor24/snobol4.bin` (post-`pr/funcall-arithmetic`,
   `pr/saga-expr-completeness`, and the rebuild already shipped).
   `just demos` and `just test` stay green.
3. **New reg-rs case `plsw_emit_unbounded`.** Compile a synthetic
   source whose expected `.s` exceeds the old 256 KB ceiling
   (e.g. ~2000 procs of nontrivial code, or one giant proc with
   many statements). Assert the compile succeeds and the
   downstream `.bin` is well-formed end-to-end. Proves the
   streaming ceiling is gone, not just relocated.

## What does NOT go in this PR

- No change to allocation strategy elsewhere in PL/SW. Just the
  emit pipeline.
- No change to the `.zero N` codegen — that fix is keeping its
  win for entry-module bloat.
- No change to `cor24-asm` or `link24` or anything downstream
  of UART output. They keep consuming the same `.s` content.
- No change to PL/SW's macro processor or codegen logic. Only
  the emit primitives.

## Why this brief, not the previous one

I drafted `dcpls-enlarge-emit-buf.md` two days ago proposing to
bump `EMIT_BUF_SIZE` to 2 MB. I retracted it in favour of
`.zero N` codegen (`dcxas-zero-fill-directive` +
`dcpls-emit-zero-fill`), which solved the entry-module case but
left library modules at 256 KB ceiling.

Today I re-filed the bump-to-2 MB proposal under a different
title — and that was wrong: it ignored that PL/SW runs on COR24
with 1 MB total SRAM. Bumping `emit_buf` to 2 MB exceeds total
physical memory by ~1 MB.

This brief replaces both of my previous attempts. The right fix
is to **eliminate or shrink** the buffer, not enlarge it. ~10
lines in `include/emit.h`.

## What "mike installs" means concretely

After `dg-relay` + `dg-release` land this PR on `sw-cor24-plsw/main`,
mike runs (from the relay clone) the same one-line install that
`dcpls-rebuild-plsw-lgo.md` documents:

```bash
just clean && just build-lgo
install -m 0640 \
  /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-plsw/build/plsw.lgo \
  /disk1/github/softwarewrighter/devgroup/work/lib/cor24/plsw.lgo
```

Once the new `plsw.lgo` is on PATH, dcsno can run their existing
`pr/rebuild-snobol4-artifacts`-style build to produce a fresh
`snobol4.{lgo,bin}` against the streaming compiler. Then mike
installs those to `work/lib/cor24/` per
`dcsno-rebuild-snobol4-artifacts.md`'s install commands. The
chain is identical to the one we just walked through for
`emit-zero-fill`.

## Ordering — dcsno step 003 must wait

dcsno: **do not start `saga-expr-completeness` step 003 until
this brief has shipped + plsw.lgo has been reinstalled.** Step
003 grows `sno_exec.s` past today's 262,030 / 262,144 byte
ceiling. If you start now, the build either silently truncates
(corrupting downstream codegen) or overflows visibly. The 4 KB
coalescer Option B gives you ~16 MB of effective output ceiling
(any UART-captured length, really); start the step then, not
before.

## When done

- dcsno's `saga-expr-completeness` step 003 unblocks: parser +
  lowering extensions for nested calls in builtin args, finishing
  the saga. Ships within a session.
- dcftn's `feat/m1-resume` / `normalize.sno` runs end-to-end
  without temp-variable workarounds for length-arithmetic.
- Every future PL/SW library-module growth has effectively
  unbounded headroom.
- The archived `snobol4-runtime-split` saga's deferred
  consolidation steps become tractable (the EMIT_BUF wall that
  parked them is gone).
- The 256 KB `EMIT_BUF_SIZE` ceiling disappears from PL/SW.
  No more buffer-cliff.
