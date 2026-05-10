#!/usr/bin/env bash
# cor24-run -- DEPRECATION SHIM
#
# The original cor24-run binary has been renamed to cor24-run.legacy
# in $TOOLROOT (work/bin/). This shim is installed as cor24-run on PATH;
# it logs every invocation to work/log/cor24-run-usage.log so we can
# identify remaining callers, then forwards to the legacy binary so
# existing scripts don't break in the meantime.
#
# Production users should switch to:
#   cor24-emu  -- COR24 emulator (formerly cor24-run --run)
#   cor24-asm  -- COR24 assembler (formerly cor24-run --assemble)
#
# Once the usage log shows zero invocations for a quiet interval, both
# this shim and cor24-run.legacy can be deleted.
#
# ----------------------------------------------------------------------
# Source-of-truth lives at scripts/cor24-run-shim.sh in the devgroup
# repo. Install / refresh into PATH with:
#
#   install -m 0755 \
#     /disk1/github/softwarewrighter/devgroup/scripts/cor24-run-shim.sh \
#     /disk1/github/softwarewrighter/devgroup/work/bin/cor24-run
#
# ----------------------------------------------------------------------

set -euo pipefail

LOG="/disk1/github/softwarewrighter/devgroup/work/log/cor24-run-usage.log"
LEGACY="$(dirname "$0")/cor24-run.legacy"

ts="$(date -Iseconds)"
user="${USER:-$(id -un)}"
ppid_cmd="$(tr '\0' ' ' < /proc/$PPID/cmdline 2>/dev/null | sed 's/[[:space:]]*$//')"

# Single-line tab-separated record. Writes under PIPE_BUF (4 KB on
# Linux) are atomic, so concurrent invocations won't interleave.
# Don't fail the invocation if logging fails -- always forward.
printf '%s\tuser=%s\tpid=%d\tppid=%d\tppid_cmd=%q\tcwd=%q\targs=%q\n' \
    "$ts" "$user" "$$" "$PPID" "$ppid_cmd" "$PWD" "$*" \
    >> "$LOG" 2>/dev/null || true

# Forward to legacy binary, preserving exit code + stdio.
if [[ -x "$LEGACY" ]]; then
    exec "$LEGACY" "$@"
fi

cat >&2 <<'ERR'
cor24-run: legacy binary missing. Use the supported tools instead:
  cor24-run --run prog.s [opts]
    -> cor24-asm prog.s -o prog.lgo && cor24-emu --lgo prog.lgo [opts]
  cor24-run --assemble in.s out.bin out.lst
    -> cor24-asm in.s --bin out.bin --listing out.lst
  cor24-run --assemble in.s out.bin /dev/null
    -> cor24-asm in.s --bin out.bin
ERR
exit 127
