# dcpls -- enlarge SRC_BUF_SIZE (compile-side input cap)

## Symptom

Compiling the consolidated `src/sno_engine.plsw` (137 KB; merge of
`sno_util` + `sno_lex` + `sno_exec`) in `sw-cor24-snobol4` fails with:

```
=== Compile mode ===
Send FILE:name / SOURCE: blocks, or raw source + EOT.
  registered: descr.msw
  registered: heap.msw
  registered: am.msw
  registered: pat.msw
  registered: snoglob.msw
ERROR: source read failed
```

That message is the new overflow detector from commit 865cd5b
(`fix: enlarge SRC buffer + detect overflow to stop silent source
truncation (#14)`) — it now reports a hard failure instead of silently
truncating. Good behaviour; just need more headroom.

## Root cause

`src/main.c`:

```
#define SRC_BUF_SIZE 65536
char src_buf[SRC_BUF_SIZE];
```

The SOURCE: block goes into `src_buf` (FILE: blocks for macros and
library .plsw includes go into the separate `inc_buf`). 64 KiB is no
longer enough for the SNOBOL4 engine consolidation:

| file (sno-side)        | size (bytes) | notes                              |
|------------------------|-------------:|------------------------------------|
| sno_util.plsw          |       15,062 | compiles fine                      |
| sno_lex.plsw           |       59,319 | compiles fine                      |
| sno_exec.plsw          |       64,640 | compiles fine (right at the edge)  |
| **sno_engine.plsw**    |  **137,994** | **fails: SRC overflow**            |
| util+lex partial       |       73,000 | also fails                          |

## Repro

```sh
cd sw-cor24-snobol4
git switch -d 865cd5b   # or current main
# (re-)apply the consolidation drafted on feat/runtime-split-resume:
git restore --source=feat/runtime-split-resume src/sno_engine.plsw scripts/build-modular.sh .gitignore
bash scripts/build.sh include/descr.msw include/heap.msw include/am.msw \
    include/pat.msw include/snoglob.msw src/sno_engine.plsw
# -> ERROR: source read failed
```

## Why this matters for sw-cor24-snobol4

The SNOBOL4 runtime-split saga's step 1 (sno-engine consolidation) is
the consolidation that hits this. The saga's goal is three modules:
`snolib + sno_engine + snobol4`. `sno_engine` is unavoidably the size
of the three currently-split engine files combined (~137 KB after the
streaming-emit, expression-cleanup, and buffer-overflow fixes shipped
over the last few weeks). It cannot be made smaller without
re-introducing a per-module split that defeats the saga.

## Ask

Bump `SRC_BUF_SIZE` to something with comfortable headroom for the
next year of engine growth. A few size points:

- 128 KiB would unblock today's `sno_engine` with ~10 KiB to spare.
  Probably too tight — we have several expansion plans in
  `docs/plan.md` that will grow the engine.
- **256 KiB (262,144)** is the recommendation. Matches the size of
  the recently-replaced EMIT_BUF and leaves ~120 KiB room for engine
  growth. Cost: 192 KiB more static in pl-sw's COR24 image (pl-sw
  itself runs on COR24 with 1 MiB SRAM; the cost is real but
  bounded). The dropped EMIT_BUF freed up 256 KiB already, so
  the net cost since pre-streaming-emit is roughly zero.
- 512 KiB is too aggressive; pl-sw's own image plus its working
  set start to crowd the 1 MiB SRAM budget.

If 256 KiB is too costly, an interim bump to 192 KiB (3 × 64 KiB)
would unblock today's consolidation with breathing room for the next
two saga steps; we can revisit when the next ceiling hits.

## Out of scope for this ask

- Streaming SOURCE: reads. The compiler does random-access over the
  buffer (tokenizer rewinds for multi-char tokens, includes, etc.),
  so streaming the input is a much bigger lift than the output-side
  streaming-emit change that landed last week. Not needed today.
- `INC_BUF_SIZE` (32 KiB). Macros + library .plsw includes haven't
  overflowed it yet; the SNOBOL4 macro set totals ~14 KiB. Leave
  alone for now; we'll file separately if it ever cliffs.

## Suggested commit subject

`fix: bump SRC_BUF_SIZE to 256 KiB to unblock sw-cor24-snobol4 sno_engine consolidation`

## Verification

After the bump:

```sh
cd sw-cor24-snobol4
git switch feat/runtime-split-resume    # or whatever name the resume saga uses
just rebuild
just demos    # 15 halted
just test     # 14 "All tests done"
```

All green from the snobol4 side once pl-sw's SRC buffer is large
enough to swallow the consolidated source.
