#!/usr/bin/env bash
set -Eeuo pipefail

# ------------------------------------------------------------------------------
# install-shared-rust.sh
#
# One-time (per host): install rustup + the default stable toolchain into a
# shared prefix at /opt/rust. Mike owns it; devgroup members can read/exec
# but not write. Users share one toolchain and one ~300MB install.
#
# After install:
#   - /opt/rust/cargo/bin/<cargo|rustc|rustfmt|clippy|...>   (rustup proxies)
#   - /opt/rust/rustup/toolchains/stable-*/                  (actual toolchain)
#
# Per-user state stays in each user's $HOME/.cargo: cargo install'd tools,
# registry cache, credentials. This is NOT shared, as expected.
#
# To update the shared toolchain later:
#   sudo -u mike RUSTUP_HOME=/opt/rust/rustup CARGO_HOME=/opt/rust/cargo \
#       /opt/rust/cargo/bin/rustup update
#
# Usage:
#   sudo ./install-shared-rust.sh
# ------------------------------------------------------------------------------

PREFIX=/opt/rust
RUSTUP_HOME="${PREFIX}/rustup"
CARGO_HOME="${PREFIX}/cargo"
ADMIN_USER=mike
GROUP=devgroup

if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run as root via sudo." >&2
  exit 1
fi

command -v curl >/dev/null 2>&1 || { echo "ERROR: curl not installed" >&2; exit 1; }

if [[ -x "${CARGO_HOME}/bin/rustup" ]]; then
  echo "==> ${CARGO_HOME}/bin/rustup already exists; refreshing toolchain only."
  sudo -u "$ADMIN_USER" \
    RUSTUP_HOME="$RUSTUP_HOME" CARGO_HOME="$CARGO_HOME" \
    "${CARGO_HOME}/bin/rustup" update stable
else
  echo "==> creating $PREFIX (owned by $ADMIN_USER, group $GROUP)"
  install -d -m 2755 -o "$ADMIN_USER" -g "$GROUP" "$PREFIX"

  echo "==> running rustup installer as $ADMIN_USER"
  sudo -u "$ADMIN_USER" bash <<EOF
set -Eeuo pipefail
export RUSTUP_HOME='$RUSTUP_HOME'
export CARGO_HOME='$CARGO_HOME'
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
  | sh -s -- -y --default-toolchain stable --profile default --no-modify-path
EOF
fi

echo "==> fixing perms so devgroup can read but not write"
find "$PREFIX" -type d -exec chmod g+rxs {} +
find "$PREFIX" -type f -exec chmod g+r {} +
setfacl -R -m "g:${GROUP}:rx" "$PREFIX" 2>/dev/null || true

echo
echo "==> installed versions:"
sudo -u "$ADMIN_USER" \
  RUSTUP_HOME="$RUSTUP_HOME" CARGO_HOME="$CARGO_HOME" \
  "${CARGO_HOME}/bin/rustc" --version
sudo -u "$ADMIN_USER" \
  RUSTUP_HOME="$RUSTUP_HOME" CARGO_HOME="$CARGO_HOME" \
  "${CARGO_HOME}/bin/cargo" --version

cat <<EOF

Done.

Next: rerun the account setup so every d* user's managed .bashrc exports
RUSTUP_HOME and prepends /opt/rust/cargo/bin to PATH:

  sudo ./setup-devgroup-accounts.sh dev-users.tsv

Update the shared toolchain later (picks up new rust releases at your cadence):

  sudo -u $ADMIN_USER \\
    RUSTUP_HOME=$RUSTUP_HOME CARGO_HOME=$CARGO_HOME \\
    $CARGO_HOME/bin/rustup update stable
EOF
