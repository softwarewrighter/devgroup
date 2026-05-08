#!/usr/bin/env bash
set -Eeuo pipefail

# ------------------------------------------------------------------------------
# setup-devgroup-accounts.sh
#
# Purpose:
#   Create local developer accounts for a shared multi-user Arch Linux host.
#
# Model:
#   - Each dev account owns exactly one writable sandbox under:
#       /disk1/github/softwarewrighter/devgroup/work/<user>
#   - Peer dev accounts can read/traverse, but not write, other sandboxes.
#   - Directory layout:
#       devgroup/bin/          admin binaries         (mike only, 0700)
#       devgroup/scripts/      admin shell scripts    (mike only, 0700)
#       devgroup/work/bin/     binaries for d* users  (claude, codex, ...)
#       devgroup/work/scripts/ helper scripts (dg-*)  for d* users
#       devgroup/work/bare/    bare git mirrors
#   - mike is the sole admin/integrator account with broader write/admin access.
#   - Dev accounts do NOT push. They commit locally and mark ready branches as
#     pr or pr/<topic>. mike scans local working clones and merges/pushes.
#
# Usage:
#   sudo ./setup-devgroup-accounts.sh
#   sudo ./setup-devgroup-accounts.sh /path/to/dev-users.tsv
#
# Manifest format (TSV, tabs preferred, comments allowed):
#   user<TAB>family<TAB>primary_repo
#
# Example:
#   dcapl    c-lisp         sw-cor24-apl
#   dcoca    pascal-ocaml   sw-cor24-ocaml
#   dwoca    pascal-ocaml   web-sw-cor24-ocaml
#
# SSH keys (optional):
#   If a file named 'authorized_keys' exists alongside the manifest (or the
#   AUTHORIZED_KEYS_FILE env var points to one), every non-blank / non-comment
#   line is installed into each dev user's ~/.ssh/authorized_keys. Idempotent.
#   This is how mike gets passwordless SSH access into every d* account.
# ------------------------------------------------------------------------------

BASE="/disk1/github/softwarewrighter/devgroup"
WORK_ROOT="${BASE}/work"
BIN_DIR="${WORK_ROOT}/bin"              # binaries for d* users
WORK_SCRIPTS_DIR="${WORK_ROOT}/scripts" # dg-* helpers for d* users
BARE_DIR="${WORK_ROOT}/bare"
ADMIN_BIN_DIR="${BASE}/bin"             # admin binaries (mike-only)
ADMIN_SCRIPTS_DIR="${BASE}/scripts"     # admin scripts (mike-only; this file lives here)
SHARED_GROUP="devgroup"
ADMIN_USER="mike"
DEFAULT_SHELL="/bin/bash"
MANIFEST_DEFAULT="./dev-users.tsv"

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run as root via sudo." >&2
  exit 1
fi

MANIFEST="${1:-$MANIFEST_DEFAULT}"
if [[ ! -f "$MANIFEST" ]]; then
  echo "ERROR: manifest not found: $MANIFEST" >&2
  exit 1
fi

AUTHORIZED_KEYS_FILE="${AUTHORIZED_KEYS_FILE:-$(dirname "$MANIFEST")/authorized_keys}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "ERROR: missing required command: $1" >&2
    exit 1
  }
}

for cmd in \
  getent groupadd useradd usermod install chown chmod setfacl getfacl \
  grep awk sed cut stat git id sudo; do
  need_cmd "$cmd"
done

if ! id "$ADMIN_USER" >/dev/null 2>&1; then
  echo "ERROR: admin user '$ADMIN_USER' does not exist. Create it first." >&2
  exit 1
fi

acl_sanity_check() {
  local test_root
  test_root="$(dirname "$WORK_ROOT")"
  mkdir -p "$test_root"
  local f="${test_root}/.acl-test.$$"
  : > "$f"
  if ! setfacl -m u:${ADMIN_USER}:rw "$f" >/dev/null 2>&1; then
    echo "WARNING: setfacl failed under $test_root" >&2
    echo "WARNING: filesystem or mount may not support ACLs as expected." >&2
    echo "WARNING: the script will continue, but peer read-only behavior may be wrong." >&2
  fi
  rm -f "$f"
}

ensure_group() {
  if ! getent group "$SHARED_GROUP" >/dev/null; then
    echo "==> creating group: $SHARED_GROUP"
    groupadd "$SHARED_GROUP"
  fi
}

ensure_shared_dirs() {
  echo "==> ensuring shared directories"

  # BASE: group-readable (traverse), not group-writable. setgid so work/ and
  # friends inherit the devgroup group.
  install -d -m 2755 -o root -g "$SHARED_GROUP" "$BASE"
  install -d -m 2775 -o root -g "$SHARED_GROUP" "$WORK_ROOT"
  install -d -m 2775 -o root -g "$SHARED_GROUP" "$BIN_DIR"
  install -d -m 2775 -o root -g "$SHARED_GROUP" "$WORK_SCRIPTS_DIR"
  install -d -m 2775 -o root -g "$SHARED_GROUP" "$BARE_DIR"

  # work/{bin,scripts,bare}: mike rwx, devgroup r-x, others none.
  for d in "$BIN_DIR" "$WORK_SCRIPTS_DIR" "$BARE_DIR"; do
    setfacl -bn "$d" || true
    setfacl -m "u:${ADMIN_USER}:rwx,g:${SHARED_GROUP}:r-x,o::---,m::rwx" "$d"
    setfacl -d -m "u:${ADMIN_USER}:rwx,g:${SHARED_GROUP}:r-x,o::---,m::rwx" "$d"
  done

  # Admin dirs: mike-only (0700, no devgroup ACL). d* users cannot enter.
  install -d -m 0700 -o "$ADMIN_USER" -g "$ADMIN_USER" "$ADMIN_BIN_DIR"
  install -d -m 0700 -o "$ADMIN_USER" -g "$ADMIN_USER" "$ADMIN_SCRIPTS_DIR"
  setfacl -b "$ADMIN_BIN_DIR" 2>/dev/null || true
  setfacl -b "$ADMIN_SCRIPTS_DIR" 2>/dev/null || true
}

ensure_user() {
  local user="$1"

  if id "$user" >/dev/null 2>&1; then
    echo "==> user exists: $user"
  else
    echo "==> creating user: $user"
    useradd -m -s "$DEFAULT_SHELL" -G "$SHARED_GROUP" "$user"
  fi

  usermod -aG "$SHARED_GROUP" "$user"

  # Passwordless / pubkey-only model:
  #   - passwd -l makes the locked-password state explicit and idempotent.
  #   - chage clears any aging/expiry (repairs accounts previously touched by
  #     'passwd -e' in earlier versions of this script).
  passwd -l "$user" >/dev/null 2>&1 || true
  chage -d "$(date +%Y-%m-%d)" -M -1 -E -1 "$user" >/dev/null 2>&1 || true
}

append_bashrc_block() {
  local user="$1"
  local primary_repo="$2"
  local home
  home="$(getent passwd "$user" | cut -d: -f6)"
  local bashrc="${home}/.bashrc"

  touch "$bashrc"

  # Strip any prior managed block so re-runs update cleanly.
  local tmp
  tmp="$(mktemp)"
  awk '
    /^### devgroup managed block ###$/ {skip=1; next}
    skip && /^### end devgroup managed block ###$/ {skip=0; next}
    !skip {print}
  ' "$bashrc" > "$tmp"
  mv "$tmp" "$bashrc"

  # First heredoc (unquoted): bake primary_repo in at setup-time.
  cat >> "$bashrc" <<EOF

### devgroup managed block ###
export DEVWORK="/disk1/github/softwarewrighter/devgroup/work/\${USER}"
export PRIMARY_REPO="${primary_repo}"
export ORGROOT="\${DEVWORK}/github/sw-embed"
export SRCROOT="\${ORGROOT}/\${PRIMARY_REPO}"
EOF

  # Second heredoc (quoted): literal runtime logic.
  cat >> "$bashrc" <<'DEVGROUP_BASHRC_BLOCK'
export REPOROOT="/disk1/github/softwarewrighter/devgroup/work/bare"
export TOOLROOT="/disk1/github/softwarewrighter/devgroup/work/bin"
export SCRIPTROOT="/disk1/github/softwarewrighter/devgroup/work/scripts"

# Shared rustup/cargo toolchain at /opt/rust. Your personal CARGO_HOME stays
# at ~/.cargo (default). Override RUSTUP_HOME in your own .bashrc if you want
# a private toolchain.
export RUSTUP_HOME="/opt/rust/rustup"

# Prevent git from walking up past devgroup/ and finding the admin repo.
export GIT_CEILING_DIRECTORIES="/disk1/github/softwarewrighter/devgroup"

for __p in "$SCRIPTROOT" "$TOOLROOT" "$HOME/.local/bin" "$HOME/.cargo/bin" /opt/rust/cargo/bin; do
  case ":$PATH:" in
    *":${__p}:"*) ;;
    *) export PATH="${__p}:$PATH" ;;
  esac
done
unset __p

# Interactive SSH login: attach/create tmux session 'main', then cd to SRCROOT
# (the primary repo). Falls back to ORGROOT if the primary isn't cloned yet.
# Opt out with NO_TMUX=1.
if [[ $- == *i* ]]; then
  if [[ -z "${TMUX:-}" && -z "${NO_TMUX:-}" ]] \
     && [[ -n "${SSH_CONNECTION:-}${SSH_TTY:-}" ]] \
     && command -v tmux >/dev/null 2>&1; then
    if tmux has-session -t main 2>/dev/null; then
      exec tmux attach-session -t main
    else
      exec tmux new-session -s main
    fi
  fi
  if [[ -d "$SRCROOT" ]]; then
    cd "$SRCROOT"
  elif [[ -d "$ORGROOT" ]]; then
    cd "$ORGROOT"
  fi
fi
### end devgroup managed block ###
DEVGROUP_BASHRC_BLOCK

  chown "$user:$user" "$bashrc"
  chmod 0644 "$bashrc"
}

set_git_identity() {
  local user="$1"
  local full_name email
  case "$user" in
    dc*) full_name="dev cli ${user#dc}" ;;
    dw*) full_name="dev web ${user#dw}" ;;
    *)   echo "WARNING: unknown user prefix for git identity: $user" >&2; return 0 ;;
  esac
  email="${user}@softwarewrighter.com"
  sudo -u "$user" -H git config --global user.name  "$full_name"
  sudo -u "$user" -H git config --global user.email "$email"
}

install_ssh_keys() {
  local user="$1"
  [[ -f "$AUTHORIZED_KEYS_FILE" ]] || return 0

  local home
  home="$(getent passwd "$user" | cut -d: -f6)"
  local sshdir="${home}/.ssh"
  local authfile="${sshdir}/authorized_keys"

  install -d -m 0700 -o "$user" -g "$user" "$sshdir"
  touch "$authfile"
  chown "$user:$user" "$authfile"
  chmod 0600 "$authfile"

  local added=0
  while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "${line// }" ]] && continue
    [[ "${line:0:1}" == "#" ]] && continue
    if ! grep -qxF -- "$line" "$authfile"; then
      printf '%s\n' "$line" >> "$authfile"
      added=$((added+1))
    fi
  done < "$AUTHORIZED_KEYS_FILE"

  if (( added > 0 )); then
    echo "    installed $added ssh key(s) for $user"
  fi
}

create_sandbox() {
  local user="$1"
  local family="$2"
  local primary_repo="$3"

  local sandbox="${WORK_ROOT}/${user}"
  local srcroot="${sandbox}/github/sw-embed"

  echo "==> ensuring sandbox for $user"

  install -d -m 0750 -o "$user" -g "$SHARED_GROUP" "$sandbox"
  install -d -m 0750 -o "$user" -g "$SHARED_GROUP" "${sandbox}/github"
  install -d -m 0750 -o "$user" -g "$SHARED_GROUP" "$srcroot"

  chmod g+s "$sandbox" "${sandbox}/github" "$srcroot"

  setfacl -bn "$sandbox" || true
  setfacl -bn "${sandbox}/github" || true
  setfacl -bn "$srcroot" || true
  setfacl -k  "$sandbox" || true
  setfacl -k  "${sandbox}/github" || true
  setfacl -k  "$srcroot" || true

  for p in "$sandbox" "${sandbox}/github" "$srcroot"; do
    setfacl -m "u:${user}:rwx,u:${ADMIN_USER}:rwx,g:${SHARED_GROUP}:r-x,o::---,m::rwx" "$p"
    setfacl -d -m "u:${user}:rwx,u:${ADMIN_USER}:rwx,g:${SHARED_GROUP}:r-x,o::---,m::rwx" "$p"
  done

  local info="${sandbox}/DEVINFO"
  cat > "$info" <<DEVINFO
user=${user}
family=${family}
primary_repo=${primary_repo}
srcroot=${srcroot}
bare_root=${BARE_DIR}
tool_root=${BIN_DIR}
DEVINFO
  chown "$user:$SHARED_GROUP" "$info"
  chmod 0640 "$info"

  local repo_dir="${srcroot}/${primary_repo}"
  local bare_repo="${BARE_DIR}/${primary_repo}.git"

  if [[ -d "$repo_dir/.git" ]]; then
    : # already cloned, leave alone
  elif [[ -d "$bare_repo" ]]; then
    # If an empty placeholder exists (from an earlier run before the bare repo
    # was available), clear it so git clone can populate the directory.
    if [[ -d "$repo_dir" ]] && [[ -z "$(ls -A "$repo_dir" 2>/dev/null)" ]]; then
      rmdir "$repo_dir"
    fi
    if [[ ! -e "$repo_dir" ]]; then
      echo "    cloning primary repo from local bare repo: $primary_repo"
      if ! sudo -u "$user" git clone "$bare_repo" "$repo_dir"; then
        echo "WARNING: clone failed for $user -> $primary_repo" >&2
      fi
    else
      echo "    WARNING: $repo_dir exists with content but no .git; leaving alone" >&2
    fi
  else
    # Bare repo missing. Do NOT create an empty placeholder directory:
    # that silently masks manifest typos (an agent would then populate the
    # wrong-named dir, and downstream agents that vendor by the correct name
    # can't find it). Login will land in ORGROOT instead; once the bare
    # mirror appears, re-running this script will clone into place.
    echo "    NOTE: no bare mirror for $primary_repo; skipping clone (planned placeholder or run sync-bare-repos.sh)"
  fi
}

install_helper_scripts() {
  echo "==> installing helper scripts into ${WORK_SCRIPTS_DIR}"

  # One-time migration: remove dg-* copies from the old location (work/bin).
  for old in dg-env dg-policy dg-new-feature dg-new-fix dg-mark-pr dg-list-pr; do
    rm -f "${BIN_DIR}/${old}"
  done

  cat > "${WORK_SCRIPTS_DIR}/dg-env" <<'DG_ENV'
#!/usr/bin/env bash
set -euo pipefail
echo "USER=${USER}"
echo "DEVWORK=${DEVWORK:-}"
echo "SRCROOT=${SRCROOT:-}"
echo "REPOROOT=${REPOROOT:-}"
echo "TOOLROOT=${TOOLROOT:-}"
echo "SCRIPTROOT=${SCRIPTROOT:-}"
DG_ENV

  cat > "${WORK_SCRIPTS_DIR}/dg-policy" <<'DG_POLICY'
#!/usr/bin/env bash
cat <<'TXT'
Devgroup policy:
- Work only inside your own writable sandbox.
- Do not push. mike is the only integrator.
- Base new branches on origin/dev (NOT origin/main).
- While developing: feat/<topic> or fix/<topic>.
- When ready: rename feat/<topic> -> pr/<topic>  (use dg-mark-pr).
- mike scans local clones for pr/* and relays them to dev on GitHub.
- Full policy: /disk1/github/softwarewrighter/devgroup/docs/branching-pr-strategy.md
TXT
DG_POLICY

  cat > "${WORK_SCRIPTS_DIR}/dg-new-feature" <<'DG_NEW_FEATURE'
#!/usr/bin/env bash
set -euo pipefail
topic="${1:-}"
if [[ -z "$topic" ]]; then
  echo "usage: dg-new-feature <topic>" >&2
  exit 1
fi
git rev-parse --is-inside-work-tree >/dev/null
git fetch origin --prune || true
if git rev-parse --verify --quiet origin/dev >/dev/null; then
  git switch -c "feat/${topic}" origin/dev
else
  git switch dev 2>/dev/null || git switch -c dev origin/main
  git switch -c "feat/${topic}"
fi
echo "Created branch feat/${topic}"
DG_NEW_FEATURE

  cat > "${WORK_SCRIPTS_DIR}/dg-new-fix" <<'DG_NEW_FIX'
#!/usr/bin/env bash
set -euo pipefail
topic="${1:-}"
if [[ -z "$topic" ]]; then
  echo "usage: dg-new-fix <topic>" >&2
  exit 1
fi
git rev-parse --is-inside-work-tree >/dev/null
git switch dev
git fetch --all --prune || true
git switch -c "fix/${topic}"
echo "Created branch fix/${topic}"
DG_NEW_FIX

  cat > "${WORK_SCRIPTS_DIR}/dg-mark-pr" <<'DG_MARK_PR'
#!/usr/bin/env bash
set -euo pipefail
git rev-parse --is-inside-work-tree >/dev/null
current="$(git branch --show-current)"
if [[ -z "$current" ]]; then
  echo "ERROR: not on a named branch" >&2
  exit 1
fi
if [[ "$current" == "dev" || "$current" == "main" ]]; then
  echo "ERROR: refusing to rename $current to pr" >&2
  exit 1
fi
topic="${1:-}"
if [[ -n "$topic" ]]; then
  target="pr/${topic}"
else
  case "$current" in
    feat/*)    target="pr/${current#feat/}" ;;
    feature/*) target="pr/${current#feature/}" ;;   # legacy form, still stripped
    fix/*)     target="pr/${current#fix/}" ;;
    pr|pr/*)   target="$current" ;;
    *)
      echo "ERROR: don't know how to mark '$current' as a pr branch." >&2
      echo "Switch to a feat/<slug> or fix/<slug> branch first, or pass an explicit slug:" >&2
      echo "    dg-mark-pr <slug>" >&2
      exit 1
      ;;
  esac
fi
if [[ "$current" == "$target" ]]; then
  echo "Already on $target"
  exit 0
fi
git branch -m "$target"
echo "Renamed $current -> $target"
DG_MARK_PR

  cat > "${WORK_SCRIPTS_DIR}/dg-list-pr" <<'DG_LIST_PR'
#!/usr/bin/env bash
set -euo pipefail
ROOT="/disk1/github/softwarewrighter/devgroup/work"
shopt -s nullglob
found=0
for userdir in "$ROOT"/dc* "$ROOT"/dw*; do
  [[ -d "$userdir/github/sw-embed" ]] || continue
  for repo in "$userdir"/github/sw-embed/*; do
    [[ -d "$repo/.git" ]] || continue
    while IFS= read -r branch; do
      [[ -z "$branch" ]] && continue
      found=1
      printf "%s\t%s\t%s\n" "$(basename "$userdir")" "$(basename "$repo")" "$branch"
    done < <(
      git -c safe.directory='*' -C "$repo" \
        for-each-ref --format='%(refname:short)' refs/heads/pr refs/heads/pr/\*
    )
  done
done
if [[ "$found" -eq 0 ]]; then
  echo "No local pr branches found."
fi
DG_LIST_PR

  cat > "${WORK_SCRIPTS_DIR}/dg-reap" <<'DG_REAP'
#!/usr/bin/env bash
set -euo pipefail
git rev-parse --is-inside-work-tree >/dev/null
git fetch origin --prune

if git rev-parse --verify --quiet origin/dev >/dev/null; then
  git switch dev 2>/dev/null || git switch -c dev --track origin/dev
  git branch --set-upstream-to=origin/dev dev >/dev/null 2>&1 || true
  git merge --ff-only origin/dev
else
  echo "WARN: origin/dev does not exist yet; staying on current branch" >&2
fi

reaped=0 pending=0
while IFS= read -r pr; do
  [[ -z "$pr" ]] && continue
  if git rev-parse --verify --quiet origin/dev >/dev/null \
     && git merge-base --is-ancestor "$pr" origin/dev 2>/dev/null; then
    git branch -D "$pr"
    echo "reaped:  $pr"
    reaped=$((reaped+1))
  else
    echo "pending: $pr (not yet in origin/dev)"
    pending=$((pending+1))
  fi
done < <(git for-each-ref --format='%(refname:short)' refs/heads/pr refs/heads/pr/\*)

echo
echo "Summary: ${reaped} reaped, ${pending} still pending."
DG_REAP

  local helpers=(dg-env dg-policy dg-new-feature dg-new-fix dg-mark-pr dg-list-pr dg-reap)
  for h in "${helpers[@]}"; do
    chmod 0755 "${WORK_SCRIPTS_DIR}/${h}"
    chown root:"$SHARED_GROUP" "${WORK_SCRIPTS_DIR}/${h}"
  done
}

append_mike_bashrc_block() {
  local home bashrc
  home="$(getent passwd "$ADMIN_USER" | cut -d: -f6)"
  bashrc="${home}/.bashrc"

  touch "$bashrc"

  local tmp
  tmp="$(mktemp)"
  awk '
    /^### devgroup admin managed block ###$/ {skip=1; next}
    skip && /^### end devgroup admin managed block ###$/ {skip=0; next}
    !skip {print}
  ' "$bashrc" > "$tmp"
  mv "$tmp" "$bashrc"

  cat >> "$bashrc" <<'DEVGROUP_ADMIN_BLOCK'

### devgroup admin managed block ###
export DEVGROUP="/disk1/github/softwarewrighter/devgroup"
export ADMIN_BIN="${DEVGROUP}/bin"
export ADMIN_SCRIPTS="${DEVGROUP}/scripts"
export WORKROOT="${DEVGROUP}/work"
export BAREROOT="${WORKROOT}/bare"

for __p in "$ADMIN_SCRIPTS" "$ADMIN_BIN"; do
  case ":$PATH:" in
    *":${__p}:"*) ;;
    *) export PATH="${__p}:$PATH" ;;
  esac
done
unset __p
### end devgroup admin managed block ###
DEVGROUP_ADMIN_BLOCK

  chown "$ADMIN_USER:$ADMIN_USER" "$bashrc"
  chmod 0644 "$bashrc"
}

ensure_system_gitconfig() {
  # Whitelist paths so git's "dubious ownership" check does not trip when:
  #   - d* users clone from / read bare mirrors (bare owned by mike)
  #   - d* users read each other's sandboxes (peer repos owned by another user)
  #   - mike runs git ops inside worker-owned sandboxes (relay, dg-list-pr, etc.)
  #
  # Note: git's safe.directory wildcarding only honors '*' as a trailing path
  # component (and the bare string "*" to mean "any path"). Intermediate
  # '*' patterns like work/*/github/*/* DO NOT work, even in modern git. So we
  # rely on the catch-all "*" — acceptable inside a trusted devgroup where
  # every interactive user is either mike or a d* worker.
  echo "==> ensuring /etc/gitconfig safe.directory entries"
  local entries=(
    "*"
    "$BARE_DIR"
    "${BARE_DIR}/*"
  )
  for d in "${entries[@]}"; do
    if ! git config --system --get-all safe.directory 2>/dev/null | grep -qxF -- "$d"; then
      git config --system --add safe.directory "$d"
    fi
  done
}

ensure_system_tmux_conf() {
  echo "==> ensuring /etc/tmux.conf managed block"
  local conf="/etc/tmux.conf"
  touch "$conf"

  local tmp
  tmp="$(mktemp)"
  awk '
    /^### devgroup managed block ###$/ {skip=1; next}
    skip && /^### end devgroup managed block ###$/ {skip=0; next}
    !skip {print}
  ' "$conf" > "$tmp"
  mv "$tmp" "$conf"

  cat >> "$conf" <<'DEVGROUP_TMUX_BLOCK'

### devgroup managed block ###
set -g status-right '#{=-30:pane_current_path}'
### end devgroup managed block ###
DEVGROUP_TMUX_BLOCK

  chmod 0644 "$conf"
}

summarize() {
  echo
  echo "Setup complete."
  echo
  echo "Manifest        : $MANIFEST"
  echo "Work root       : $WORK_ROOT"
  echo "Work bin (d*)   : $BIN_DIR"
  echo "Work scripts    : $WORK_SCRIPTS_DIR"
  echo "Bare root       : $BARE_DIR"
  echo "Admin bin       : $ADMIN_BIN_DIR"
  echo "Admin scripts   : $ADMIN_SCRIPTS_DIR"
  echo
  echo "Try:"
  echo "  sudo -u <devuser> bash -lc 'dg-env'"
  echo "  sudo -u <devuser> bash -lc 'cd \$SRCROOT/<repo> && dg-new-feature parser-cleanup'"
  echo "  sudo -u <devuser> bash -lc 'cd \$SRCROOT/<repo> && dg-mark-pr'"
  echo "  sudo -u ${ADMIN_USER} bash -lc 'dg-list-pr'"
}

validate_manifest() {
  # Cross-check the TSV against ground truth BEFORE we mutate any accounts.
  #
  # Ground truth sources (in order):
  #   1. ${BARE_DIR}/<name>.git  -- local bare mirror exists, ready to clone
  #   2. gh repo list <org>      -- exists on GitHub but no mirror yet
  #                                 (setup will skip clone; admin must run
  #                                  sync-bare-repos.sh before agents can use it)
  #   3. neither                 -- treated as a planned placeholder for a
  #                                 repo that hasn't been created yet (this
  #                                 is expected and allowed per project policy)
  #
  # Failure mode we're guarding against: a typo in the TSV silently matches
  # category 3 and the agent's sandbox gets an empty dir at the wrong name.
  # Heuristic: if the TSV name is a strict prefix of an existing GitHub repo
  # (e.g., TSV 'sw-cor24-emu' with GitHub 'sw-cor24-emulator') we treat it
  # as an almost-certain truncation typo and hard-fail. Other near misses
  # we only surface in the pending-placeholder list for human eyeballing,
  # since we can't distinguish a future repo from a typo automatically.
  local manifest="$1"
  local org="${GH_ORG:-sw-embed}"
  echo "==> validating manifest against bare mirrors and GitHub org '$org'"

  local gh_available=0
  local -a gh_names=()
  if command -v gh >/dev/null 2>&1 \
     && sudo -u "$ADMIN_USER" -H gh auth status >/dev/null 2>&1; then
    mapfile -t gh_names < <(
      sudo -u "$ADMIN_USER" -H gh repo list "$org" --limit 1000 \
        --json name,isArchived \
        --jq '.[] | select(.isArchived==false) | .name' 2>/dev/null
    )
    gh_available=1
    echo "    fetched ${#gh_names[@]} non-archived repos from $org"
  else
    echo "    WARNING: gh not available or not authenticated as $ADMIN_USER; GitHub typo check disabled" >&2
  fi

  local ok=0 pending=0 needs_sync=0 typo=0
  local -a typo_msgs=() sync_msgs=()
  local user family primary_repo extra
  while IFS=$'\t' read -r user family primary_repo extra; do
    [[ -z "${user// }" ]] && continue
    [[ "${user:0:1}" == "#" ]] && continue
    [[ -z "${primary_repo:-}" ]] && continue

    if [[ -d "${BARE_DIR}/${primary_repo}.git" ]]; then
      ok=$((ok+1))
      continue
    fi

    local pending_suggestion=""
    if (( gh_available )); then
      local in_github=0 prefix_match="" n
      for n in "${gh_names[@]}"; do
        if [[ "$n" == "$primary_repo" ]]; then
          in_github=1
          break
        fi
        # strict-prefix check: GitHub name starts with TSV name AND is longer.
        # This catches truncation typos like sw-cor24-emu -> sw-cor24-emulator.
        if [[ "$n" == "${primary_repo}"* ]] && [[ "$n" != "$primary_repo" ]]; then
          prefix_match="$n"
        fi
      done

      if (( in_github )); then
        sync_msgs+=("$user -> $primary_repo")
        needs_sync=$((needs_sync+1))
        continue
      fi

      if [[ -n "$prefix_match" ]]; then
        typo_msgs+=("$user: '$primary_repo' -> did you mean '$prefix_match'?")
        typo=$((typo+1))
        continue
      fi
    fi

    pending=$((pending+1))
  done < "$manifest"

  echo "    ${ok} ready, ${pending} pending placeholder(s), ${needs_sync} on GitHub but unmirrored, ${typo} likely typo(s)"

  if (( needs_sync > 0 )); then
    echo "WARNING: the following repos exist on GitHub but have no local bare mirror." >&2
    echo "         Agents for these users will skip cloning. Run sync-bare-repos.sh as mike" >&2
    echo "         and re-run this script to materialize the clones:" >&2
    printf '         - %s\n' "${sync_msgs[@]}" >&2
  fi

  if (( typo > 0 )); then
    echo "ERROR: manifest contains ${typo} likely typo(s):" >&2
    printf '       - %s\n' "${typo_msgs[@]}" >&2
    echo "       Fix dev-users.tsv (GitHub repo names are authoritative) and re-run." >&2
    exit 1
  fi
}

acl_sanity_check
ensure_group
ensure_shared_dirs
usermod -aG "$SHARED_GROUP" "$ADMIN_USER"
ensure_system_gitconfig
validate_manifest "$MANIFEST"

while IFS=$'\t' read -r user family primary_repo extra; do
  [[ -z "${user// }" ]] && continue
  [[ "${user:0:1}" == "#" ]] && continue

  if [[ -z "${family:-}" || -z "${primary_repo:-}" ]]; then
    echo "WARNING: skipping malformed manifest line for user='$user'" >&2
    continue
  fi

  ensure_user "$user"
  append_bashrc_block "$user" "$primary_repo"
  set_git_identity "$user"
  install_ssh_keys "$user"
  create_sandbox "$user" "$family" "$primary_repo"
done < "$MANIFEST"

install_helper_scripts
append_mike_bashrc_block
ensure_system_tmux_conf
summarize

