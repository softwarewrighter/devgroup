# Brief: SNOBOL4 caps source input at ~12,280 bytes (silent truncation, now diagnostic)

**Owner:** dcsno
**Branch:** `pr/source-byte-cap`
**Repo:** `sw-cor24-snobol4`
**Discovered by:** dcftn during `milestone-4-print-int` saga (2026-05-12).
**Drafted by:** mike (2026-05-13).
**Parent brief:** `dcsno-static-program-size-limit.md` (follow-on #1 in
its 2026-05-12T17 "Verification -- partial fix landed" section).

## Symptom

A SNOBOL4 source file above ~12,280 bytes triggers the
overflow diagnostic added in `pr/stmt-table-cap`:

```
truncated at byte=12280
```

This is *independent* of statement count. A comment-heavy or
string-literal-heavy file hits the byte cap well before the
just-raised STMAX=1024 statement cap.

Pre-`pr/stmt-table-cap` this was a silent miscompile (downstream
behaviour undefined: label dispatch wrong, statements past the
cutoff vanished). With that PR landed, the cap is now reported
honestly -- but the cap itself is still in place and still
blocks real workloads.

## Repro

```sh
{
    # ~250 comment lines of ~50 bytes each -> ~12.5 KB
    for i in $(seq 1 260); do
        echo "* comment line $i used to inflate the byte count past 12280"
    done
    echo "        OUTPUT = 'hello'"
    echo "END"
} > /tmp/r.sno

wc -c /tmp/r.sno   # > 12280

snobol4 --load-binary /tmp/r.sno@0x080000 \
        --entry 0 --quiet --speed 0 \
        -n 1000000 -t 30 2>&1 | head
# -> "truncated at byte=12280"
```

## Real-world hit (dcftn FTI-0 compiler)

dcftn's `snobol4/src/emit_asm.sno` (in `sw-cor24-fortran`) started
failing at 13,176 bytes during the FTI-0 `_putint` inlining work.
Trimming inline comments down to 9,123 bytes makes the file pass.
That isn't sustainable: the next FTI milestone (m5..m10 lowering /
emit_plsw passes) will push the emitter back over the cap with no
comments left to trim.

dcftn's current dodge: split the runtime into
`snobol4/runtime/{prelude,putint}.s` (static files) and have
`scripts/fortran` `awk`-splice them into the SNOBOL4 emitter's
output at marker lines. The split is defensible on its own (most
real compilers separate codegen from runtime) but it was *forced*
by this cap, not chosen for design reasons. Once the cap lifts,
the runtime can be inlined back into `emit_asm.sno` -- the
dogfooded form.

See `dcsno-static-program-size-limit.md` section "Real-world hit
(dcftn FTI-0 compiler)" for the full saga.

## Hypothesis

The 12280 number is suspiciously close to 12 KiB (12288) minus a
small header. That shape is consistent with a fixed
`DCL SRC(12288) BYTE` (or similar power-of-two near 12 KiB) static
allocation in the SNOBOL4 input reader. The `pr/stmt-table-cap`
work raised STMAX 256 -> 1024 (4x) in the statement-table layer;
this is the partner constraint at the byte-buffer layer.

The diagnostic "truncated at byte=<N>" already prints the exact
threshold -- so the overflow-detection path landed in
`pr/stmt-table-cap` knows the buffer size. Locating the
allocation should be straightforward by grep'ing the constant
near that diagnostic emission.

## What dcsno needs to investigate

1. Locate the source-buffer allocation. Likely in `src/sno_lex.plsw`
   near the SOURCE: ingestion path, or in the consolidated reader
   if step 1 of the runtime-split saga has landed. The new
   "truncated at byte=" message points at the relevant code path.
2. Confirm 12280 is the constant. (It may be `12288 - 8` for a
   small header.)
3. Raise the cap. Parallel to the STMAX 4x growth, a bump from
   ~12 KiB to 48-64 KiB gives ~4-5x headroom over today's
   emit_asm.sno and leaves comfortable margin for the
   FTI-0 m5..m10 emitter passes.
4. Keep the overflow diagnostic at the new cap (don't reintroduce
   the silent-truncation behaviour). Just raise the threshold.

## Tests to add

- **Byte-cap regression:** build SNOBOL4 sources of N bytes
  (padded with comment lines) for N in `{8000, 12000, 12280,
  13000, 20000, 40000, 60000}`. For each N below the new cap,
  assert clean compile + correct execution. For each N above,
  assert the "truncated at byte=" diagnostic fires (don't drop
  the safety check).
- **Real-world stand-in:** if a `tests/inputs/large/` directory
  exists, drop a ~40 KB synthetic SNOBOL4 program in it and wire
  into `just test` so future regressions surface immediately.

## Workaround posture on the dcftn side

`snobol4/runtime/{prelude,putint}.s` and the `awk` splice in
`scripts/fortran` stay until this brief lands. They are marked
with comments referencing this brief + the parent
`dcsno-static-program-size-limit.md`. Once the new cap is verified
large enough for emit_asm.sno + the lower / emit_plsw passes,
dcftn inlines the runtime back into `emit_asm.sno` and deletes
the runtime-split files in a follow-up dcftn PR.

## Out of scope

- The other three `dcsno-static-program-size-limit.md` follow-ons
  (`ANY()` silent fail, concat truncation, pattern-capture
  truncation) -- separate briefs in this batch.
- **`dcpls-enlarge-src-buf.md`** is a different cap entirely:
  that's `SRC_BUF_SIZE` in `pl-sw`'s `src/main.c`, which limits
  source fed *into* the pl-sw compiler. This brief is about the
  SNOBOL4 *runtime's* read of its own SOURCE: input. Both caps
  exist; both need raising; they're at different layers and
  shouldn't be conflated.
- Streaming SOURCE: reads. SNOBOL4's tokenizer does random-access
  (multi-char tokens, label backpatching) so streaming is a much
  bigger lift. A bigger buffer is the right fix today.

## Credit

Caught by dcftn during the FTI-0 m4-print-int saga, alongside the
parent statement-cap bug. The `pr/stmt-table-cap` PR fixed half
of that pair; this brief is the byte-side half.
