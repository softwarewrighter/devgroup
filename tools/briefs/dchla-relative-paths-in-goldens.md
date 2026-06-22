# Brief: hlasm emits absolute paths → regression goldens fail on Linux

**Owner:** dchla (`sw-cor24-hlasm`)
**Branch:** `feat/relative-paths-in-output` → `pr/...`
**Drafted by:** mike (coordinator) · found in the clean-Linux portability audit
(`docs/clean-linux-portability.md`).

## Defect

`reg-rs/*.out` golden fixtures embed **absolute macOS paths** of emitted artifacts,
e.g.:
```
/Users/mike/github/sw-embed/sw-cor24-hlasm/demos/./d24_nested_named_include_main.bin
```
The hlasm tool prints the absolute path of each `.bin`/listing it writes; the
goldens captured those on macOS. On a Linux checkout the tool emits
`/disk1/.../sw-cor24-hlasm/demos/...`, so the regression suite **fails by path
mismatch** — even though the assembled output is correct.

Affected (at least): `reg-rs/hlasm_d23_named_include.out`,
`hlasm_d24_nested_named_include.out`, `hlasm_d25_bootstrap_split_loader.out`,
`hlasm_d34_copy_macro_library.out`, `hlasm_d35_listing_xref.out`,
`hlasm_d36_xref_report.out` (re-scan for the full set).

## Fix (preferred: make output path-independent)

1. Make the tool print **relative / repo-rooted** paths (e.g. `demos/x.bin`, not
   the absolute path) wherever it reports emitted-file locations. Output should not
   depend on the checkout's absolute location.
2. Regenerate the `reg-rs/*.out` goldens from the now-relative output.
3. Audit `reg-rs/` for any other embedded absolute paths.

Fallback (weaker, only if (1) is impractical): normalize absolute paths to
repo-relative in the test harness before diffing — raise with mike first.

## Verify

- `reg-rs/*.out` contain **no** `/Users/...` (or any absolute host) paths.
- The hlasm regression suite passes on a clean **Linux** checkout.

## When done

Push `pr/relative-paths-in-output`, notify mike. Relay via
`dg-relay dchla sw-cor24-hlasm pr/relative-paths-in-output`.

## Note

Not cor24-rs — a clean-Linux output-portability defect. Independent of the
assembler/`cor24-run` retirement.
