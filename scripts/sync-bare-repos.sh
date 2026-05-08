#!/usr/bin/env bash
set -Eeuo pipefail

# ------------------------------------------------------------------------------
# sync-bare-repos.sh
#
# Purpose:
#   Maintain bare mirrors of every non-archived repo in a GitHub org under:
#     /disk1/github/softwarewrighter/devgroup/work/bare/<repo>.git
#
#   Dev (d*) users clone from these locally and never push. mike periodically
#   runs this to refresh, integrates pr/* branches from dev sandboxes, and
#   pushes back to GitHub from these bare mirrors.
#
# Runs as:
#   mike (NOT root/sudo). Clones must be owned by mike; devgroup gets read-only
#   via the default ACLs already set by setup-devgroup-accounts.sh.
#
# Prereqs:
#   - setup-devgroup-accounts.sh has been run at least once (so BARE_DIR and
#     its ACLs exist).
#   - gh CLI authed as mike: `gh auth status` succeeds.
#   - mike has pull access to every repo in the org.
#
# Usage:
#   ./sync-bare-repos.sh                  # defaults to org 'sw-embed'
#   ./sync-bare-repos.sh <org>
#   ORG=<org> ./sync-bare-repos.sh
# ------------------------------------------------------------------------------

BARE_DIR="/disk1/github/softwarewrighter/devgroup/work/bare"
ADMIN_USER="mike"
ORG="${1:-${ORG:-sw-embed}}"

if [[ "$(id -un)" != "$ADMIN_USER" ]]; then
  echo "ERROR: run as $ADMIN_USER (not root/sudo)." >&2
  exit 1
fi

for cmd in gh git awk; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "ERROR: missing command: $cmd" >&2; exit 1; }
done

if [[ ! -d "$BARE_DIR" ]]; then
  echo "ERROR: $BARE_DIR missing. Run setup-devgroup-accounts.sh first." >&2
  exit 1
fi

gh auth status >/dev/null 2>&1 || {
  echo "ERROR: gh is not authenticated. Run: gh auth login" >&2
  exit 1
}

echo "==> listing non-archived repos in org '$ORG'"
mapfile -t lines < <(
  gh repo list "$ORG" --limit 1000 \
    --json name,sshUrl,isArchived \
    --jq '.[] | select(.isArchived==false) | [.name, .sshUrl] | @tsv'
)

if [[ ${#lines[@]} -eq 0 ]]; then
  echo "ERROR: no repos returned for '$ORG'. Check org name and access." >&2
  exit 1
fi

echo "==> ${#lines[@]} repos to process"

cloned=0; updated=0; failed=0; fail_names=()
for line in "${lines[@]}"; do
  name="${line%%$'\t'*}"
  url="${line#*$'\t'}"
  target="${BARE_DIR}/${name}.git"

  if [[ -d "$target" ]]; then
    if git -C "$target" remote update --prune >/dev/null 2>&1; then
      printf '    upd %s\n' "$name"
      updated=$((updated+1))
    else
      printf '    ERR %s (fetch failed)\n' "$name" >&2
      failed=$((failed+1)); fail_names+=("$name")
    fi
  else
    if git clone --mirror --quiet "$url" "$target"; then
      printf '    new %s\n' "$name"
      cloned=$((cloned+1))
    else
      printf '    ERR %s (clone failed)\n' "$name" >&2
      failed=$((failed+1)); fail_names+=("$name")
    fi
  fi

  # Ensure bare is readable by the devgroup. Belt-and-suspenders: (1)
  # sharedRepository=group should make git create objects group-readable,
  # but in practice pack-writing paths escape this; (2) chmod -R g+rX fixes
  # the ACL mask via kernel semantics; (3) a post-receive hook re-chmods
  # on every future push so new packs can't regress.
  if [[ -d "$target" ]]; then
    git -C "$target" config core.sharedRepository group >/dev/null 2>&1 || true
    cat > "$target/hooks/post-receive" <<'HOOK'
#!/bin/sh
chmod -R g+rX "$(git rev-parse --git-dir)"
HOOK
    chmod 0755 "$target/hooks/post-receive"
    chmod -R g+rX "$target" 2>/dev/null || true
  fi
done

echo
echo "Summary: ${cloned} cloned, ${updated} updated, ${failed} failed"
if (( failed > 0 )); then
  printf '  failed: %s\n' "${fail_names[*]}"
  exit 1
fi
