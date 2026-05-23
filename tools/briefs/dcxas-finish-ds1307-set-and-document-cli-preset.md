# Brief: rebase `pr/i2c-ds1307-set`, refresh demo headers for `?preset=system`

**Owner:** dcxas
**Branches:**
- `pr/i2c-ds1307-set` (rebase)
- `pr/i2c-ds1307-set-saga-complete` (rebase)
- `pr/document-ds1307-cli-preset` (new, sequenced after dcemu ships)
**Repo:** `sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-16)

## Cross-repo coordination

This brief sits in the middle of a three-agent thread:

- Upstream: [`dcemu-ds1307-initial-time-and-system-preset.md`](dcemu-ds1307-initial-time-and-system-preset.md)
  — adds `?hour=&minute=&second=...` and `?preset=system` CLI params
  to the emulator's `--i2c-device ds1307@...` spec, plus a
  `Ds1307Device::with_initial_registers(...)` constructor for the
  Rust API.
- Downstream: [`dwxas-battery-backed-rtc.md`](dwxas-battery-backed-rtc.md)
  — web "No battery / With battery" toggle on top of dcemu's
  constructor.

Your demos sit between them: they're the artifact the user actually
runs to see the RTC behaviour. CLI users feed your `.s` files
through `cor24-emu --i2c-device 'ds1307@0x68?preset=system'`; web
users see them in the dropdown.

## Part 1 — Unblock the set chain (do first; doesn't wait on dcemu)

Your `pr/i2c-ds1307-set` and `pr/i2c-ds1307-set-saga-complete` are
blocked on a rename/rename conflict with the already-merged
`pr/i2c-ds1307-read` chain: both branches archive the same prior
saga to different timestamped paths under
`.agentrail-archive/i2c-examples-<TIMESTAMP>/`, plus a content
conflict on `tests/integration_tests.rs::examples()` from both
adding entries.

Resolve by rebasing onto the current `origin/dev`. The rename/rename
will resolve to keeping the read chain's archive directory (already
on dev) and dropping set's redundant archive of the same source.
The integration_tests.rs conflict resolves by keeping the read entry
already on dev and inserting the set entry alphabetically after it.

```bash
cd /disk1/.../work/dcxas/github/sw-embed/sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev

for pr in pr/i2c-ds1307-set pr/i2c-ds1307-set-saga-complete; do
  git switch "$pr"
  git branch -m "$pr" "feat/${pr#pr/}"
  git rebase origin/dev
  # in the resolver:
  #   - for the .agentrail-archive/ renames: accept origin/dev's
  #     i2c-examples-<earlier-timestamp> directory; drop set's
  #     redundant archive of the same source paths.
  #   - for tests/integration_tests.rs: keep "I2C RTC Read"
  #     already on dev; insert "I2C RTC Set" alphabetically after it.
  # then git rebase --continue, then:
  git branch -m "feat/${pr#pr/}" "$pr"
done
```

Then `dg-mark-pr` (or just leave the `pr/` name) to re-signal.

## Part 2 — Quality consolidation (recommended, not blocking)

For both the read pair and the set pair, the two-pr-branch pattern
diverged from the earlier shape (where `pr/<saga>-saga-complete` was
a strict superset of `pr/<saga>`). On `i2c-ds1307-read`, the `feat`
branch carried a follow-up "rename label to RTC" commit that the
`saga-complete` branch lacked; mike worked around it by relaying
saga-complete first and feat second so the rename auto-merged. The
set pair likely has the same shape.

Going forward (the next saga is up to you), please restore the
earlier discipline: **the `saga-complete` branch should always be
a strict superset of the `feat` branch's tip at the moment you
signal both.** If you add a follow-up "fix(label)" commit after
signaling `feat`, the corresponding `saga-complete` must include it
too (rebase saga-complete on top of the new feat tip and re-signal
both).

This is easier on the coordinator (no order-dependent merge
strategy needed) and avoids the rare case where the feat-only
commits are dropped silently.

## Part 3 — Refresh demo headers (wait for dcemu to ship)

After mike promotes `sw-cor24-emulator/main` with the
`?preset=system` and explicit-register params:

### `examples/assembler/i2c_ds1307_read.s` header

Add to the existing usage block:

```
; CLI one-liners (post-dcemu/ds1307-initial-time-and-system-preset):
;
;   # show host clock (registry preset reads SystemTime::now()):
;   cor24-asm src/examples/assembler/i2c_ds1307_read.s -o /tmp/r.lgo
;   cor24-emu --lgo /tmp/r.lgo --i2c-device 'ds1307@0x68?preset=system'
;
;   # show a specific time:
;   cor24-emu --lgo /tmp/r.lgo \
;       --i2c-device 'ds1307@0x68?hour=12&minute=34&second=56'
;
; Without any params, the device boots at 00:00:00 — the demo prints
; that and increments from there.
```

### `examples/assembler/i2c_ds1307_set.s` header

Note that the set demo is now redundant with `?preset=system` for
the "show host clock" use case (no UART input needed). Keep the
set demo — it remains the way to demonstrate i2c writes to live
registers — but add a line in the header pointing CLI users at
`?preset=system` if all they want is the time displayed:

```
; If you just want to see host time without typing it in via UART,
; use scripts/run-demo.sh i2c-ds1307-read with --preset=system
; (or the dropdown's 'I2C RTC Read' on the web demo). This setter
; demo is for cases where you want to demonstrate i2c writes to
; the RTC registers.
```

### Web dropdown labels

No change. The existing labels (`I2C RTC Read`, `I2C RTC Set`)
already match the device class (RTC), not the chip (DS1307) or the
feature (battery). Keep that convention.

## Acceptance

**Part 1**: both set branches relay cleanly via `dg-relay` (no
conflict aborts) after the rebases.

**Part 3**: demo header comments updated as above, single commit
"docs(examples): document --i2c-device ds1307 CLI params in demo
headers" on `pr/document-ds1307-cli-preset`. No `.s` source
changes; no test changes; no dropdown rename. Cross-link the
upstream brief in the commit body.

## Out of scope

- **No new RTC demo `.s` files.** The two existing demos cover
  read and write; dwxas's battery toggle handles the cross-reload
  scenario at the web layer.
- **No `Ds1307HandleExt` changes in this repo.** That API lives
  in `sw-cor24-emulator` and stays as-is.
- **No "battery" naming creeping into demos.** The web layer owns
  that metaphor; demos remain device-shaped.
