# Brief: fix hardcoded macOS path in sw-cor24-apl/build.sh (portability)

**Owner:** dcapl (`sw-cor24-apl`) · **Also affects:** dcscr (`sw-cor24-script`) — same pattern.
**Branch:** `feat/portable-build-paths` → `pr/portable-build-paths`
**Drafted by:** mike (coordinator) · flagged by dwapl during the cor24-rs migration.

## Defect

`sw-cor24-apl/build.sh:20` hardcodes an absolute macOS path:

```sh
TC24R_INCLUDE="/Users/mike/github/sw-embed/sw-cor24-x-tinyc/include"
```

On a clean **Linux** checkout this path doesn't exist, so `build.sh` (which
generates the gitignored `build/apl.s` consumed by `web-sw-cor24-apl/build.rs`)
fails. dwapl hit and worked around this provisioning its sibling; it will recur on
any fresh checkout.

## Fix

Resolve `TC24R_INCLUDE` portably — no user/OS-specific absolute paths:
1. Honor an explicit `$TC24R_INCLUDE` if set.
2. Else derive it relative to the script / sibling layout, e.g.
   ```sh
   SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
   TC24R_INCLUDE="${TC24R_INCLUDE:-$SCRIPT_DIR/../sw-cor24-x-tinyc/include}"
   ```
   (adjust the relative hops to the actual sibling location).
3. **Fail loudly** with a clear message if the resolved include dir is missing —
   never a silent wrong path.
4. Audit the rest of `build.sh` for any other `/Users/...`, `/opt/homebrew`,
   `/usr/local/opt`, `.dylib`, or `*-apple-*` assumptions and make them portable.

## Also affects (route in parallel)

`sw-cor24-script/scripts/build.sh` (dcscr) carries the same class of hardcoded
path — same fix. Either fold into a shared `dc-*` brief or hand dcscr a copy.

## Verify

- On Linux, a clean clone + `./build.sh` produces `build/apl.s` with no manual
  path edits.
- No absolute `/Users/...` (or other host-specific) paths remain in `build.sh`.

## When done

Push `pr/portable-build-paths`, notify mike. Relay via
`dg-relay dcapl sw-cor24-apl pr/portable-build-paths`.

## Note (not cor24-rs)

This is a build-script portability defect, surfaced by — but separate from — the
cor24-rs migration. It's a provisioning prerequisite for clean-Linux `dw*` sibling
builds, not part of the assembler/`cor24-run` retirement.
