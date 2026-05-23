# Brief: dcftn `classify.sno` emits 0 bytes under the new (post-cap-fixes) snobol4 — RESOLVED

**Status:** ✅ RESOLVED 2026-05-14 by dcsno's `pr/load-addr-compat`
(commit `cd5cf21`). The original diagnosis in this brief
(pattern-semantics break) was **wrong**. The actual root cause
was a load-address migration: dcsno's cap-saga moved
`SRC_LOAD_ADDR` from `0x80000` -> `0xE0000` and `INP_LOAD_ADDR`
from `0x90000` -> `0xF0000`. Scripts (including the vendored
`work/bin/fortran` wrapper) hardcoded the old addresses, so
the `--load-binary X@0x080000` calls silently loaded source
into the middle of the new binary's static state -- producing
0-byte output. **See `dcsno-load-addr-migration.md` for the
canonical explanation.**

The unblocker: dcsno shipped `scripts/snobol4-compat.sh`, a
transition wrapper that rewrites `@0x080000` -> `@0xE0000` and
`@0x090000` -> `@0xF0000` on the fly. mike installed it as
`work/bin/snobol4` (replacing the prior simple wrapper) along
with the new `snobol4.{lgo,bin}` from `sw-cor24-snobol4` main
`f76193a`. All four FTI-0 demos (hello / factorial / fibonacci
/ fizzbuzz) verified passing end-to-end against the new snobol4
+ compat wrapper, identical instruction counts to the
pre-cap-saga baseline.

**Follow-up for dcftn (optional, low priority):** migrate
`scripts/fortran` and the vendored copy at
`work/lib/cor24/fortran/scripts/fortran` to use `0xE0000` /
`0xF0000` directly, then the compat wrapper can be retired.
Sed: `s/@0x080000/@0xE0000/g; s/@0x090000/@0xF0000/g`.

The historical analysis below was preserved for the saga
record but is **superseded**. Don't bisect dcsno's commits as
the brief originally suggested -- the patterns are fine; only
the addresses moved.

---

**Owner:** dcftn
**Branch:** `pr/classify-on-new-snobol4` (NOT NEEDED -- resolved upstream)
**Repo:** `sw-cor24-fortran`
**Discovered by:** mike (2026-05-14) during install of dcsno's
`pr/cap-and-pattern-fixes` artifacts.
**Drafted by:** mike (2026-05-14).
**Resolved by:** dcsno via `pr/load-addr-compat` (2026-05-14).
**Blocks:** install of the rebuilt `work/lib/cor24/snobol4.{lgo,bin}`
from any `sw-cor24-snobol4` main at or after `c675774`. As of
`a1825d4` (dcsno's `pr/more-fixes`, landed 2026-05-14 after this
brief was first drafted), the regression is *worse*: now BOTH
phase 1 (normalize) AND phase 2 (classify) emit 0 bytes. The
new artifacts are sitting in the relay; the old ones (from main
`7959c35`) are still on PATH because installing the new ones
breaks the `fortran` wrapper for every FTI-0 example.

## Why I rolled back the snobol4 install

dcsno landed `pr/cap-and-pattern-fixes` today (2026-05-14), shipping
all four follow-on briefs from `dcsno-static-program-size-limit.md`
in one saga: `dcsno-source-byte-cap`, `dcsno-any-pattern-fails`,
`dcsno-concat-truncation`, `dcsno-pattern-captures-truncation`. The
rebuilt `build/snobol4.{lgo,bin}` landed in main too
(snobol4.bin: 281 KB -> 631 KB).

After I `install -m 0640`'d the new artifacts into
`work/lib/cor24/`, **all four FTI-0 demos broke** at the assembler
step with:

```
Line 20: Undefined label '_main'
```

`fortran hello.f` produced 4889 bytes of `.s` instead of the prior
5318 -- the prologue ran fine but `_main:` and the user-program
body never got emitted. The tail of the output had:

```
; WARN: malformed input: )2
```

I tracked this to the **classify phase emitting 0 bytes** under the
new snobol4. Phase 1 (normalize) still produces correct
4-statement records; phase 2 (classify) consumes them and emits
nothing; phase 3 (emit_asm) falls through to its "malformed
input" path.

Per-phase repro (run after `install -m 0640` of the new artifacts):

```sh
export PATH=/disk1/github/softwarewrighter/devgroup/work/bin:$PATH
VENDOR=/disk1/github/softwarewrighter/devgroup/work/lib/cor24/fortran
HELLO=/disk1/.../work/relay/sw-cor24-fortran/examples/hello.f
W=$(mktemp -d)

# Phase 1 -- works
snobol4 --load-binary "$VENDOR/snobol4/src/normalize.sno@0x080000" \
        --load-binary "$HELLO@0x090000" \
        --entry 0 --quiet --speed 0 -n 100000000 -t 60 \
        > $W/normalized.txt
wc -c $W/normalized.txt   # -> 154 bytes (correct)

# Phase 2 -- BROKEN under new snobol4
snobol4 --load-binary "$VENDOR/snobol4/src/classify.sno@0x080000" \
        --load-binary "$W/normalized.txt@0x090000" \
        --entry 0 --quiet --speed 0 -n 100000000 -t 60 \
        > $W/classified.txt
wc -c $W/classified.txt   # -> 0 bytes (was hundreds with old snobol4)
```

Same `classify.sno`, same `normalized.txt` input, *different
snobol4 interpreter*. The break is therefore in how classify.sno's
patterns / expressions interact with the new pattern-engine
semantics, not in the upstream data.

I've reinstalled the prior snobol4 artifacts (extracted from
sw-cor24-snobol4 main `7959c35`, the SHA immediately before
`pr/cap-and-pattern-fixes`) so the four demos pass again:

```
PASS hello.f       -> "Hello, World!"  (619 instructions)
PASS factorial.f   -> "120"            (459 instructions)
PASS fibonacci.f   -> "89"             (576 instructions)
PASS fizzbuzz.f    -> "1 2 Fizz ..."
```

dwftn's live web demo therefore stays correct on its current
`snobol4.lgo` until dcftn adapts.

## What changed in the new snobol4

dcsno's `pr/cap-and-pattern-fixes` (saga commits, oldest first):

1. `cff0ba9` raise `SRC_SIZE` 12 KiB -> 64 KiB
2. `7205088` **implement `ANY(class)` pattern primitive** (was
   previously silently failing; SPAN was the standard workaround)
3. `f0ed4ab` bump `EPSLOTS` 8 -> 16 (concat operand slots)
4. `1e9fd93` bump `PSTK_DEPTH` 16 -> 32 + `EPSLOTS` to 32
   (pattern-capture stack)

The two backwards-compat-risk changes are (2) and (3)/(4):

- `ANY(class)` going from "silent-fail" to "works" changes the
  meaning of any pattern that previously had ANY embedded but
  worked via a fall-through path. classify.sno may have such a
  pattern.
- `EPSLOTS` / `PSTK_DEPTH` growth shouldn't break correct
  programs but can change behaviour of programs that were
  inadvertently relying on the old truncation (e.g. a multi-`.`
  pattern whose 4th capture wasn't bound -- it now binds, which
  can change the data flow into downstream variables).

## What dcftn needs to investigate

1. **Bisect on the four dcsno commits** to identify which one
   broke classify.sno. Easiest path: in a sw-cor24-snobol4 dev
   sandbox, `git checkout` each of `cff0ba9` / `7205088` /
   `f0ed4ab` / `1e9fd93` in turn, `just build-lgo`, and run the
   per-phase repro above against each. The first commit where
   classify emits 0 bytes is the culprit.

2. **Inspect classify.sno against the culprit commit's change.**
   - If `7205088` (ANY): look for `SPAN(class)` usages where the
     subject has *exactly one* matching char and the pattern was
     using SPAN as an "ANY workaround". The new ANY may match
     successfully where SPAN's "one-or-more" succeeded for a
     different reason.
   - If `f0ed4ab` or `1e9fd93` (slot bumps): look for multi-`.`
     captures or large concat expressions where the *old*
     truncation behaviour was being silently relied on.

3. **Fix classify.sno** to work under the new pattern semantics.
   The new behaviour is correct (per the four briefs in
   `dcsno-static-program-size-limit.md`'s follow-on section);
   classify needs to adapt, not roll back.

4. **Run the four FTI-0 demos as a regression gate.** The
   `scripts/fortran` end-to-end pipeline already exists --
   `scripts/test-hello.sh`, plus the m11 examples
   (factorial / fibonacci / fizzbuzz). Wire them into your test
   suite so future snobol4 upgrades catch this class of
   regression before mike installs.

## When done

- `pr/classify-on-new-snobol4` lands on dcftn's main.
- mike re-vendors `snobol4/src/classify.sno` (and any other
  touched file) from the new dcftn main into
  `work/lib/cor24/fortran/snobol4/src/`.
- mike retries the snobol4 install from
  `sw-cor24-snobol4` main `c675774` (`build/snobol4.{lgo,bin}`).
- The four FTI-0 demos pass against the new snobol4.

## Where the new snobol4 artifacts are sitting in the meantime

Built and committed at `sw-cor24-snobol4` main `a1825d4` (the
follow-on `pr/more-fixes`, landed 2026-05-14 after the original
`pr/cap-and-pattern-fixes` at `c675774`):

```
build/snobol4.bin   631 KB   (was 281 KB pre-PR)
build/snobol4.lgo  ~1.4 MB   (was 626 KB at the dcsno PR's base)
```

They are NOT installed in `work/lib/cor24/`. The previously-installed
artifacts (extracted from main `7959c35`) remain on PATH. Once
this brief is resolved, the latest dcsno main artifacts go in.

**Note on `pr/more-fixes` (`a1825d4`):** that PR includes a fix
for the dcftn `funcall-arithmetic` brief (`concat-after-funcall`)
which dcftn wants, plus underscore-label support that the fortran
toolchain likely also wants. The regression in normalize is a
*new* break that `pr/more-fixes` introduced -- so dcftn's
bisection should also touch the commits in `pr/more-fixes`, not
just the four from `pr/cap-and-pattern-fixes`.

## Out of scope

- **Rolling back dcsno's pr/cap-and-pattern-fixes.** It's correct
  upstream work; classify.sno needs to adapt downstream.
- **Other snobol4 consumers.** The fortran wrapper is the only
  known consumer that's broken. dcftn's normalize.sno phase still
  works; if normalize ever breaks too, file separately.
- **emit_asm.sno changes.** Phase 3 is reading empty input
  (classify emitted nothing); fix phase 2 first and then check
  whether phase 3 still works correctly under the new snobol4.

## Credit

Caught at install time today. Without the per-phase repro,
the surface symptom ("undefined label _main" from cor24-asm)
would point at emit_asm.sno or the splice -- but those are
correct. The empty-classify output is the real fault.
