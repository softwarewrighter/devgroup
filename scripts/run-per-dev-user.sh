#!/usr/bin/env bash
set -Eeuo pipefail

# ------------------------------------------------------------------------------
# run-per-dev-user.sh
#
# Run a shell command once as each d* user listed in dev-users.tsv.
#
# The command runs inside `sudo -u <user> -H bash -lc "<cmd>"`, so:
#   - HOME is the target user's home
#   - their login shell sources .bash_profile / .bashrc (managed block + PATH)
#   - SSH_CONNECTION is unset, so the managed block's tmux auto-exec is skipped
#
# Usage:
#   sudo ./run-per-dev-user.sh 'curl -fsSL https://claude.ai/install.sh | bash'
#   sudo ./run-per-dev-user.sh --manifest /path/to/dev-users.tsv 'npm install -g foo'
#
# Stops on first failure only if you pass --strict; default keeps going and
# reports a summary at the end.
# ------------------------------------------------------------------------------

MANIFEST_DEFAULT="/disk1/github/softwarewrighter/devgroup/scripts/dev-users.tsv"
MANIFEST="$MANIFEST_DEFAULT"
STRICT=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest) MANIFEST="$2"; shift 2 ;;
    --strict)   STRICT=1; shift ;;
    --) shift; break ;;
    -*) echo "unknown flag: $1" >&2; exit 1 ;;
    *)  break ;;
  esac
done

if [[ $# -eq 0 ]]; then
  cat >&2 <<USAGE
usage: sudo $0 [--manifest FILE] [--strict] '<shell command>'
  e.g. sudo $0 'curl -fsSL https://claude.ai/install.sh | bash'
USAGE
  exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run as root via sudo." >&2
  exit 1
fi

if [[ ! -f "$MANIFEST" ]]; then
  echo "ERROR: manifest not found: $MANIFEST" >&2
  exit 1
fi

cmd="$*"

users=$(awk -F'\t' '!/^#/ && NF>=3 {print $1}' "$MANIFEST")
if [[ -z "$users" ]]; then
  echo "ERROR: no users parsed from $MANIFEST" >&2
  exit 1
fi

ok=0; fail=0; fail_names=()
for u in $users; do
  echo "==> [$u] $cmd"
  if sudo -u "$u" -H bash -lc "$cmd"; then
    ok=$((ok+1))
  else
    fail=$((fail+1)); fail_names+=("$u")
    echo "    FAILED for $u" >&2
    if (( STRICT )); then
      echo "--strict set; stopping." >&2
      break
    fi
  fi
done

echo
echo "Summary: ${ok} ok, ${fail} failed"
if (( fail > 0 )); then
  printf '  failed: %s\n' "${fail_names[*]}"
  exit 1
fi
