# Clean-Linux portability: hardcoded-path audit & fix plan

**Status:** audited 2026-06-21 · **Owner:** mike (coordinator)
**Scope:** hardcoded macOS / absolute host paths that break a clean **Linux**
checkout. Separate from the cor24-rs migration (`finish-cor24-rs-migration.md`) —
this is a "works on the current host/toolchain" concern, surfaced by dwapl during
sibling provisioning.

## Audit method

```bash
cd /disk1/github/softwarewrighter/devgroup/work
PAT='/Users/|/opt/homebrew|/usr/local/opt|\.dylib|-apple-'
for info in dc*/DEVINFO; do
  a=$(dirname "$info"); prim=$(grep -m1 '^primary_repo=' "$info"|cut -d= -f2); src=$(grep -m1 '^srcroot=' "$info"|cut -d= -f2)
  R="$src/$prim"; [ -d "$R" ] || continue
  grep -rlnE "$PAT" "$R" 2>/dev/null | grep -vE '/target/|/\.git/|/\.agentrail|\.jsonl$|\.lock$' | sed "s#^#$a: #"
done
```
Excludes `target/`, `.git/`, `.agentrail` archives, `*.jsonl` session logs.

## Findings (dc* primary repos)

| Severity | Agent · repo | Location | Issue |
|---|---|---|---|
| 🔴 build-break | dcapl · sw-cor24-apl | `build.sh` | `TC24R_INCLUDE="/Users/mike/.../sw-cor24-x-tinyc/include"` |
| 🔴 build-break | dcscr · sw-cor24-script | `scripts/build.sh` | same `TC24R_INCLUDE` hardcode |
| 🟠 test-break | dchla · sw-cor24-hlasm | `reg-rs/*.out` | golden outputs embed `/Users/mike/.../sw-cor24-hlasm/demos/...` abs paths |
| 🟡 docs | dcbas · sw-cor24-basic | `docs/future/full-basic-plan.md` | stale `/Users/mike/...` doc references |
| 🟡 docs | dcpls · sw-cor24-plsw | `docs/continue20260523.md` | `/Users/mike/.local/bin/tc24r` reference |
| 🟡 docs | dcrpg · sw-cor24-rpg-ii | `docs/tutorial.md` | stale `/Users/mike/...` doc references |

All other dc* repos: clean (no host-path hardcodes outside excluded noise).

## Fix plan (prioritized)

### Fix 1 — 🔴 `TC24R_INCLUDE` build-path (dcapl + dcscr) · one fix, two repos
**Brief:** `tools/briefs/dcapl-fix-build-sh-hardcoded-path.md` (names both).
Replace the hardcoded `/Users/mike/.../sw-cor24-x-tinyc/include` with portable
resolution:
```sh
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TC24R_INCLUDE="${TC24R_INCLUDE:-$SCRIPT_DIR/../sw-cor24-x-tinyc/include}"  # adjust hops to sibling
[ -d "$TC24R_INCLUDE" ] || { echo "tc24r include not found: $TC24R_INCLUDE" >&2; exit 1; }
```
Honor `$TC24R_INCLUDE` env first; fail loudly if missing. **Priority: do first** —
dcapl's `build/apl.s` generation is a provisioning prereq for the `dw*` web builds.
Owners: dcapl (`build.sh`), dcscr (`scripts/build.sh`).

### Fix 2 — 🟠 hlasm golden paths (dchla)
**Brief:** `tools/briefs/dchla-relative-paths-in-goldens.md`.
Root cause: the hlasm tool prints **absolute** paths of emitted `.bin` files, which
the `reg-rs/*.out` goldens captured on macOS → mismatch on Linux → tests fail.
**Preferred fix:** make the tool emit **relative / repo-rooted** paths (path-
independent output), then regenerate goldens. (Normalizing in the harness is a
weaker fallback.) Priority: medium — breaks only dchla's own tests.

### Fix 3 — 🟡 doc path references (dcbas, dcpls, dcrpg)
Replace absolute `/Users/mike/...` links/examples with repo-relative paths.
Cosmetic, non-build-breaking. Fold into each repo's next routine PR or a single
coordinator doc-sweep. No brief required.

## Definition of done

Re-run the audit method above; it should print **nothing** (modulo any documented
intentional references). The 🔴 fixes are verified by a clean-Linux `./build.sh`
producing output with no manual path edits.
