# Brief: migrate the `cor24-interpret` launcher off `cor24-run`

**Owner:** mike (coordinator) — `cor24-interpret` is a shared toolchain script in
`work/bin/`, not a worker repo. (Delegable to dcemu if preferred.)
**Part of:** the cor24-rs retirement epic (`retire-cor24-rs.md`, axis 2).

## Why this exists

`work/bin/cor24-interpret` is the **shared launcher** behind every `native-s`
language wrapper (`lisp24`, and the forth/etc. wrappers). Its `native-s` mode runs:

```
cor24-run --run "$INTERP" --speed … -n …        # work/bin/cor24-interpret:~92
```

`cor24-run` is now a deprecation shim → `cor24-run.legacy`. So **every native-s
language still routes through the old cor24-rs tool** via this one script. It's the
highest-leverage remaining `cor24-run` caller: fixing it (plus the per-repo script
migrations) is what finally lets the `cor24-run` shim + legacy binary be deleted.

## What to change

In `work/bin/cor24-interpret`, the `native-s` branch:

```
# before
CMD=(cor24-run --run "$INTERP" --speed "$SPEED" -n "$MAX_INSN")
# after  (assemble once, then run the .lgo)
cor24-asm "$INTERP" -o "$LGO"     # $LGO = a per-run temp, e.g. mktemp
CMD=(cor24-emu --lgo "$LGO" --speed "$SPEED" -n "$MAX_INSN")
```

- Keep all the existing flag handling (`--terminal`, `--echo`, `-u`,
  `--load-binary` for `--prog`/`--data`, `--quiet` filtering, `EXTRA_ARGS`).
- Update the header comment that documents `native-s   … run via cor24-run --run`.
- `binary` and `pcode` modes already use `cor24-emu`/`pvm24` — leave them.
- Establish a **source-of-truth**: this script currently lives only at
  `work/bin/cor24-interpret`. Put the canonical copy under version control (e.g.
  `devgroup/scripts/cor24-interpret.sh`, mirroring `cor24-run-shim.sh`) and install
  from there, so it isn't an unversioned PATH file.

## Verify

- `lisp24` and each native-s language wrapper still start a REPL and run a program
  (`lisp24 -u '(+ 1 2)'` → `3`; same for forth/etc.).
- `work/log/cor24-run-usage.log` shows **no new entries** attributable to
  `cor24-interpret` after the change.

## Note (separate, do not bundle)

The installed `work/lib/<lang>/*.s` interpreters (e.g.
`work/lib/macrolisp/repl-standard.s`) are stale old-compiler output and want a
reinstall — but that's the per-language re-baseline track, **not** this script
migration. Keep this change to the launcher's tool calls only.

## When done

This is coordinator-applied (no relay). After it lands and the per-repo `cor24-run`
callers are migrated, the `cor24-run` shim + `cor24-run.legacy` can be retired —
the final step of axis 2.
