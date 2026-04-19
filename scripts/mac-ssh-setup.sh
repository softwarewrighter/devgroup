#!/usr/bin/env bash
set -Eeuo pipefail

# ------------------------------------------------------------------------------
# mac-ssh-setup.sh
#
# RUN THIS ON THE MAC (not on the Arch host). Invoke with bash, not sh:
#   ./mac-ssh-setup.sh        # shebang -> bash
#   bash mac-ssh-setup.sh     # explicit
#
# What it does:
#   1. Ensures ~/.ssh/id_ed25519 exists (generates one if missing, no passphrase).
#   2. In a single ssh call on the Arch host:
#        - installs the Mac pubkey into mike's own ~/.ssh/authorized_keys so
#          subsequent runs don't prompt for mike's password
#        - appends the pubkey to scripts/authorized_keys (deduped); the
#          devgroup setup script installs that file into every d* user's
#          ~/.ssh/authorized_keys
#        - prints the d* user list parsed from the manifest
#   3. Writes a managed block in ~/.ssh/config with one Host stanza per user.
#      (The User directive does NOT support %n token expansion, so each user
#      gets its own stanza.)
#
# Config (override via env):
#   ARCH_HOST   default: queenbee.local
#   ARCH_USER   default: mike
#   REPO_PATH   default: /disk1/github/softwarewrighter/devgroup
#   KEY         default: ~/.ssh/id_ed25519
# ------------------------------------------------------------------------------

ARCH_HOST="${ARCH_HOST:-queenbee.local}"
ARCH_USER="${ARCH_USER:-mike}"
REPO_PATH="${REPO_PATH:-/disk1/github/softwarewrighter/devgroup}"
KEY="${KEY:-$HOME/.ssh/id_ed25519}"

MANIFEST_REMOTE="${REPO_PATH}/scripts/dev-users.tsv"
AUTH_REMOTE="${REPO_PATH}/scripts/authorized_keys"

if [[ ! -f "$KEY" ]]; then
  echo "==> generating ed25519 key at $KEY"
  mkdir -p "$(dirname "$KEY")"
  chmod 0700 "$(dirname "$KEY")"
  ssh-keygen -t ed25519 -f "$KEY" -N "" -C "${USER}@$(hostname -s)"
fi

PUB=$(cat "${KEY}.pub")
PUB_Q=$(printf '%q' "$PUB")

echo "==> syncing pubkey to ${ARCH_USER}@${ARCH_HOST} and fetching user list (one ssh call)"
REMOTE_OUT=$(ssh "${ARCH_USER}@${ARCH_HOST}" bash -s <<EOF
set -euo pipefail
PUB=${PUB_Q}
AUTH="${AUTH_REMOTE}"
MIKE_AUTH="\$HOME/.ssh/authorized_keys"

mkdir -p "\$(dirname "\$AUTH")" "\$HOME/.ssh"
chmod 0700 "\$HOME/.ssh"
touch "\$AUTH" "\$MIKE_AUTH"
chmod 0600 "\$MIKE_AUTH"

for target in "\$AUTH" "\$MIKE_AUTH"; do
  if ! grep -qxF -- "\$PUB" "\$target"; then
    printf '%s\n' "\$PUB" >> "\$target"
  fi
done

echo '---USERS---'
awk -F'\t' '!/^#/ && NF>=3 {print \$1}' '${MANIFEST_REMOTE}'
EOF
)

USERS_LIST=$(printf '%s\n' "$REMOTE_OUT" | awk '/^---USERS---$/{flag=1;next} flag')
if [[ -z "$USERS_LIST" ]]; then
  echo "ERROR: no users parsed from $MANIFEST_REMOTE" >&2
  exit 1
fi
COUNT=$(printf '%s\n' "$USERS_LIST" | grep -c .)
echo "   -> ${COUNT} users found; key installed on ${ARCH_USER}@${ARCH_HOST} and in ${AUTH_REMOTE}"

echo "==> updating ~/.ssh/config (managed block; one Host stanza per user)"
CONFIG="$HOME/.ssh/config"
mkdir -p "$HOME/.ssh"
chmod 0700 "$HOME/.ssh"
touch "$CONFIG"
chmod 0600 "$CONFIG"

BEGIN='# >>> devgroup-ssh (managed - do not edit by hand) >>>'
END='# <<< devgroup-ssh <<<'
tmp=$(mktemp)
awk -v b="$BEGIN" -v e="$END" '
  $0==b {skip=1; next}
  $0==e {skip=0; next}
  !skip {print}
' "$CONFIG" > "$tmp"

{
  cat "$tmp"
  echo "$BEGIN"
  while IFS= read -r u; do
    [[ -z "$u" ]] && continue
    printf 'Host %s\n' "$u"
    printf '    HostName %s\n' "$ARCH_HOST"
    printf '    User %s\n' "$u"
    printf '    IdentityFile %s\n' "$KEY"
    printf '    IdentitiesOnly yes\n\n'
  done <<< "$USERS_LIST"
  echo "$END"
} > "$CONFIG"
rm -f "$tmp"

echo
echo "Done."
echo "  Mac pubkey      : ${KEY}.pub"
echo "  Remote keyfile  : ${ARCH_USER}@${ARCH_HOST}:${AUTH_REMOTE}"
echo "  Users in config : ${COUNT}"
echo
echo "Next (on ${ARCH_HOST}):"
echo "  cd ${REPO_PATH}/scripts && sudo ./setup-devgroup-accounts.sh dev-users.tsv"
echo
echo "Then from this Mac:"
echo "  ssh dcapl    # or any other d* user from the manifest"
