# Brief: rebuild `snobol4.lgo` + `snobol4.bin` against the new `pl-sw`

**Owner:** dcsno
**Branch:** `pr/rebuild-snobol4-artifacts`
**Repo:** `sw-cor24-snobol4`
**Drafted by:** mike (2026-05-09).
**Depends on (blocking):** 🟡 `dcpls-rebuild-plsw-lgo.md` — **wait for that to land and for mike's install step to copy the new `plsw.lgo` into `work/lib/cor24/`** before starting. The install step is a single `install -m 0640` from the relay clone; see the bottom of `dcpls-rebuild-plsw-lgo.md` for the literal command. You can confirm the install by checking that `work/lib/cor24/plsw.lgo` has `mtime` after `c7e1262` landed on `sw-cor24-plsw/main` (2026-05-09).

## Why this brief exists

Today's installed snobol4 artifacts:

```
work/lib/cor24/snobol4.lgo   May  9 11:23   ← built with old pl-sw
work/lib/cor24/snobol4.bin   May  8 19:30
```

Both were produced by the pre-`c7e1262` PL/SW compiler that enumerates `.byte 0` for every zero-init byte. Functionally they're correct (the `.bin` content is byte-identical regardless of `.s` shape), so the live `snobol4` wrapper still works for current consumers. The problem is structural:

- `sno_main.s` is currently 261,638 bytes against `EMIT_BUF_SIZE = 262,144` — 506 bytes of headroom. Any new `INIT(0)` array in `snoglob.msw` overflows the buffer. That's blocking your in-flight runtime work.
- After rebuilding with the new pl-sw, `sno_main.s` drops to roughly 7 KB. The headroom goes from ~500 bytes to ~255 KB, and the constraint disappears.

## Goal

Re-emit `build/snobol4.s` → `build/snobol4.{bin,lgo}` using the freshly-installed pl-sw, and ship the rebuilt artifacts so mike can replace the lgo on PATH.

## What to do

1. Confirm prerequisite: `pl-sw < <(printf 'DCL Z(16) BYTE INIT(0);\nEND;\n\x04')` should produce a `.s` containing `.zero 16` (not enumerated zeros). If you still see `.byte 0,0,...`, the new `plsw.lgo` isn't installed yet — stop and ping mike.
2. Pull `main` in your sandbox: `cd $SRCROOT && git fetch origin && git switch main && git merge --ff-only origin/main`.
3. Run your existing build pipeline (`just build-lgo` or equivalent — the same recipe that produced the May 9 artifacts). It should produce both `build/snobol4.bin` and `build/snobol4.lgo` per `dcsno-bootstrap-snobol4-toolchain.md` step 3.
4. Sanity-checks:
   - Intermediate `build/snobol4.s` is dramatically smaller than before (roughly 7 KB, not 261 KB).
   - `build/snobol4.bin` byte-identical (or near-identical — only zero-fill regions differ in source representation, the bytes are the same) to today's `work/lib/cor24/snobol4.bin`. Use `cmp` or `sha256sum` after stripping any trailing zero-fill differences if relevant.
   - Run the existing snobol4 test suite / golden harness against the new lgo. All passes that were green before must still be green.
5. Ship via the same packaging shape `dcsno-bootstrap-snobol4-toolchain` established — commit `build/snobol4.bin` and `build/snobol4.lgo` and signal `pr/rebuild-snobol4-artifacts`.

## What "mike installs" means concretely

Same shape as the dcpls rebuild — after `dg-relay` + `dg-release` land your `pr/rebuild-snobol4-artifacts`, mike runs (from the relay clone):

```bash
install -m 0640 \
  /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-snobol4/build/snobol4.lgo \
  /disk1/github/softwarewrighter/devgroup/work/lib/cor24/snobol4.lgo
install -m 0640 \
  /disk1/github/softwarewrighter/devgroup/work/relay/sw-cor24-snobol4/build/snobol4.bin \
  /disk1/github/softwarewrighter/devgroup/work/lib/cor24/snobol4.bin
```

After that, every d* user's `snobol4` wrapper picks up the rebuilt interpreter on the next invocation.

## Why this matters (and to whom)

The current artifacts work for the live demo path (`web-sw-cor24-fortran` consumes the bundled `snobol4.lgo` for runtime; that's still functional). What this rebuild unlocks:

- **dcsno's own runtime work.** Any saga touching `snoglob.msw` or extending the runtime is currently brushing against the buffer ceiling — `sno_main.s` was at 99.96% of `EMIT_BUF_SIZE` (114 bytes free). Step 003 (`builtin-arg-expressions`) of `saga-expr-completeness` was explicitly blocked on this.
- **dcftn's Fortran-in-SNOBOL4 work.** dcftn's `snobol4/src/normalize.sno` (and any future fortran-compiler.sno) runs on `work/lib/cor24/snobol4.lgo`. The May 9 11:23 build predates today's landings on `sw-cor24-snobol4/main` — `pr/funcall-arithmetic`, `pr/saga-expr-completeness` (negative-int output, parens-around-expressions). Until you rebuild and reinstall, dcftn cannot use those features in their `.sno` code.
- **All d* users.** Anyone invoking `snobol4` from PATH gets the stale interpreter. Once you rebuild, every wrapper picks up the new lgo on the next invocation.

## Out of scope

- No source changes. This brief is purely a rebuild against an updated upstream compiler.
- The `snobol4` wrapper script on PATH is correct; don't touch it.
- The Fortran web demo's bundled `assets/snobol4.lgo` is a separate concern (dwftn's repo), tracked via `dw-rebuild-pages.md` if a refresh is wanted there.
