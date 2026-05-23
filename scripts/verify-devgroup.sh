#!/usr/bin/env bash
set -Eeuo pipefail

# ------------------------------------------------------------------------------
# verify-devgroup.sh
#
# Sanity-check a devgroup host against the canonical manifest. Designed to be
# run on two machines (e.g. queenbee + large12) so their outputs can be diffed.
#
# Usage:
#   ./verify-devgroup.sh                # PASS/FAIL summary
#   ./verify-devgroup.sh --dump         # also emit a snapshot block per check
#   ./verify-devgroup.sh --snapshot DIR # write per-check snapshot files into DIR
#
# Exit:
#   0 if every check passes; 1 otherwise.
# ------------------------------------------------------------------------------

BASE="/disk1/github/softwarewrighter/devgroup"
MANIFEST="${BASE}/scripts/dev-users.tsv"
BARE_DIR="${BASE}/work/bare"
WORK_ROOT="${BASE}/work"
SHARED_GROUP="devgroup"
ADMIN_USER="mike"

MODE="summary"
SNAP_DIR=""
case "${1:-}" in
  --dump)     MODE="dump" ;;
  --snapshot) MODE="snapshot"; SNAP_DIR="${2:?usage: --snapshot DIR}" ;;
  "")         ;;
  *)          echo "usage: $0 [--dump | --snapshot DIR]" >&2; exit 1 ;;
esac

[[ "$MODE" == "snapshot" ]] && mkdir -p "$SNAP_DIR"

fail=0
pass_count=0
fail_count=0
skip_count=0

emit() {
  local name="$1"; shift
  local body
  body="$("$@" 2>&1 || true)"
  case "$MODE" in
    dump)     printf '\n=== %s ===\n%s\n' "$name" "$body" ;;
    snapshot) printf '%s\n' "$body" > "${SNAP_DIR}/${name}.txt" ;;
  esac
}

check() {
  local name="$1" expected_rc="$2"; shift 2
  if "$@" >/dev/null 2>&1; then
    local rc=0
  else
    local rc=$?
  fi
  if [[ "$rc" == "$expected_rc" ]]; then
    printf 'PASS  %s\n' "$name"
    pass_count=$((pass_count+1))
  else
    printf 'FAIL  %s (rc=%s expected=%s)\n' "$name" "$rc" "$expected_rc"
    fail_count=$((fail_count+1))
    fail=1
  fi
}

skip() {
  local name="$1" reason="$2"
  printf 'SKIP  %s (%s)\n' "$name" "$reason"
  skip_count=$((skip_count+1))
}

# --- structural prereqs ----------------------------------------------------

check "base-dir-exists"            0 test -d "$BASE"
check "manifest-readable"          0 test -r "$MANIFEST"
check "bare-dir-exists"            0 test -d "$BARE_DIR"
check "work-root-exists"           0 test -d "$WORK_ROOT"
check "devgroup-group-exists"      0 getent group "$SHARED_GROUP"
check "admin-user-exists"          0 id "$ADMIN_USER"

# --- per-user account checks (from manifest) -------------------------------

users=()
while IFS=$'\t' read -r user family repo extra; do
  [[ -z "${user// }" ]] && continue
  [[ "${user:0:1}" == "#" ]] && continue
  users+=("$user")
done < "$MANIFEST"

for u in "${users[@]}"; do
  check "user-exists:$u"           0 id "$u"
  check "user-in-devgroup:$u"      0 bash -c "id -nG '$u' | tr ' ' '\n' | grep -qx '$SHARED_GROUP'"
  check "home-exists:$u"           0 test -d "/home/$u"
  check "sandbox-exists:$u"        0 test -d "${WORK_ROOT}/${u}"
  check "devinfo-exists:$u"        0 test -f "${WORK_ROOT}/${u}/DEVINFO"
  if [[ -r "/home/${u}/.bashrc" ]]; then
    check "bashrc-managed:$u"      0 grep -q '### devgroup managed block ###' "/home/${u}/.bashrc"
  else
    skip "bashrc-managed:$u" "home not readable as $(id -un); run as root for this check"
  fi
done

# --- per-user primary repo present ----------------------------------------

while IFS=$'\t' read -r user family repo extra; do
  [[ -z "${user// }" ]] && continue
  [[ "${user:0:1}" == "#" ]] && continue
  [[ -z "${repo:-}" ]] && continue
  check "primary-clone:$user/$repo" 0 test -d "${WORK_ROOT}/${user}/github/sw-embed/${repo}/.git"
  check "bare-mirror:$repo"         0 test -d "${BARE_DIR}/${repo}.git"
done < "$MANIFEST"

# --- system config ---------------------------------------------------------

check "system-gitconfig-safedir"  0 bash -c "git config --system --get-all safe.directory | grep -qxF '*'"
check "tmux-managed-block"        0 grep -q '### devgroup managed block ###' /etc/tmux.conf
check "shared-rust-prefix"        0 test -x /opt/rust/cargo/bin/rustc

# --- snapshots / dumps for diff ------------------------------------------

emit "passwd-d-users"   bash -c "getent passwd | awk -F: '\$1 ~ /^d[cw][a-z0-9]+\$/ {print \$1\":\"\$3\":\"\$4\":\"\$7}' | sort"
emit "devgroup-members" bash -c "getent group $SHARED_GROUP | tr ',' '\n' | sort"
emit "bare-list"        bash -c "ls $BARE_DIR | sort"
emit "manifest-shape"   bash -c "awk -F'\t' '!/^#/ && NF>=3 {print \$1\"\t\"\$2\"\t\"\$3}' $MANIFEST | sort"
emit "sandbox-tree"     bash -c "cd $WORK_ROOT && find . -maxdepth 4 -type d 2>/dev/null | sort"
emit "system-safedir"   bash -c "git config --system --get-all safe.directory | sort"
emit "tmux-conf"        cat /etc/tmux.conf
emit "rustc-version"    /opt/rust/cargo/bin/rustc --version
fingerprint_bashrc() {
  local u
  for u in "${users[@]}"; do
    local rc="/home/${u}/.bashrc"
    [[ -f "$rc" ]] || continue
    local sum
    sum=$(awk '
      /^### devgroup managed block ###$/      {p=1; next}
      /^### end devgroup managed block ###$/  {p=0; next}
      p
    ' "$rc" | md5sum | cut -d" " -f1)
    printf '%s %s\n' "$u" "$sum"
  done
}
emit "bashrc-fingerprint" fingerprint_bashrc

echo
echo "Summary: ${pass_count} pass, ${fail_count} fail, ${skip_count} skip"
exit "$fail"
