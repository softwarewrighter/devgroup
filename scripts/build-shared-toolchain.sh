#!/usr/bin/env bash
set -Eeuo pipefail

# ------------------------------------------------------------------------------
# build-shared-toolchain.sh
#
# Rebuild all shared toolchain binaries and library artifacts from source.
# Run as mike after cloning the devgroup repo onto a new Arch host that has:
#   - /opt/rust installed (via install-shared-rust.sh)
#   - setup-devgroup-accounts.sh already run (d* users + work/ structure)
#   - sync-bare-repos.sh already run (bare mirrors populated)
#   - pacman packages: just, python3, base-devel
#
# Idempotent: safe to re-run after pulling new source into bare repos.
#
# Usage:
#   ./scripts/build-shared-toolchain.sh
# ------------------------------------------------------------------------------

DEVGROUP="/disk1/github/softwarewrighter/devgroup"
WORK="$DEVGROUP/work"
BIN="$WORK/bin"
LIB="$WORK/lib"
BARE="$WORK/bare"

RUSTUP_HOME=/opt/rust/rustup
CARGO_HOME=/opt/rust/cargo
export RUSTUP_HOME CARGO_HOME
export PATH="$CARGO_HOME/bin:$BIN:$PATH"

log() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
ok()  { printf '    \033[32m✓\033[0m %s\n' "$*"; }
die() { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[[ "$(id -un)" == "mike" ]] || die "run as mike (not root, not a d* user)"
[[ -d "$BARE" ]] || die "bare repos not found at $BARE — run sync-bare-repos.sh first"
command -v cargo >/dev/null 2>&1 || die "cargo not found — run install-shared-rust.sh first"
command -v just >/dev/null 2>&1 || die "just not found — pacman -S just"
command -v python3 >/dev/null 2>&1 || die "python3 not found — pacman -S python"

# Scratch area for builds (not inside any d* user's sandbox)
BUILD="$WORK/relay"
mkdir -p "$BUILD" "$BIN" "$LIB"

# ---------------------------------------------------------------------------
log "Step 1: cor24-run (from cor24-rs — deprecated, still needed for bootstrap)"

if [[ ! -d "$BUILD/cor24-rs" ]]; then
  git clone "$BARE/cor24-rs.git" "$BUILD/cor24-rs"
else
  git -C "$BUILD/cor24-rs" fetch origin && git -C "$BUILD/cor24-rs" reset --hard origin/main
fi
(cd "$BUILD/cor24-rs/rust-to-cor24" && cargo build --release --bin cor24-run)
cp "$BUILD/cor24-rs/rust-to-cor24/target/release/cor24-run" "$BIN/cor24-run"
chmod 755 "$BIN/cor24-run"
ok "cor24-run"

# ---------------------------------------------------------------------------
log "Step 2: cor24-emu + cor24-dbg (from sw-cor24-emulator)"

EMU_SRC="$WORK/dcemu/github/sw-embed/sw-cor24-emulator"
EMU_ISA="$WORK/dcemu/github/sw-embed/sw-cor24-isa"

if [[ ! -d "$EMU_ISA" ]]; then
  git clone "$BARE/sw-cor24-isa.git" "$EMU_ISA"
fi
(cd "$EMU_SRC" && cargo build --workspace --release)
cp "$EMU_SRC/target/release/cor24-emu" "$BIN/cor24-emu"
cp "$EMU_SRC/target/release/cor24-dbg" "$BIN/cor24-dbg"
chmod 755 "$BIN/cor24-emu" "$BIN/cor24-dbg"
ok "cor24-emu, cor24-dbg"

# ---------------------------------------------------------------------------
log "Step 3: cor24-asm (from sw-cor24-x-assembler)"

XASM_SRC="$WORK/dcxas/github/sw-embed/sw-cor24-x-assembler"
(cd "$XASM_SRC" && cargo build --release -p cor24-asm-cli)
cp "$XASM_SRC/target/release/cor24-asm" "$BIN/cor24-asm"
chmod 755 "$BIN/cor24-asm"
ok "cor24-asm"

# ---------------------------------------------------------------------------
log "Step 4: tc24r + tc24r-pp (from sw-cor24-x-tinyc)"

TINYC_SRC="$WORK/dcxtc/github/sw-embed/sw-cor24-x-tinyc"
TINYC_ISA="$WORK/dcxtc/github/sw-embed/sw-cor24-isa"

if [[ ! -d "$TINYC_ISA" ]]; then
  git clone "$BARE/sw-cor24-isa.git" "$TINYC_ISA"
fi
(cd "$TINYC_SRC" && scripts/build.sh)
cp "$TINYC_SRC/components/cli/target/release/tc24r" "$BIN/tc24r"
cp "$TINYC_SRC/components/cli/target/release/tc24r-pp" "$BIN/tc24r-pp"
chmod 755 "$BIN/tc24r" "$BIN/tc24r-pp"
ok "tc24r, tc24r-pp"

# ---------------------------------------------------------------------------
log "Step 5: P-code tools (from sw-cor24-pcode)"

PCODE_SRC="$WORK/dcpvm/github/sw-embed/sw-cor24-pcode"
(cd "$PCODE_SRC" && cargo build --workspace --release)
for bin in pa24r pl24r p24-load p24-dump pv24t pv24d; do
  cp "$PCODE_SRC/target/release/$bin" "$BIN/$bin"
  chmod 755 "$BIN/$bin"
done
ok "pa24r, pl24r, p24-load, p24-dump, pv24t, pv24d"

# ---------------------------------------------------------------------------
log "Step 6: sw-as24.bin (from sw-cor24-assembler)"

ASM_SRC="$WORK/dcasm/github/sw-embed/sw-cor24-assembler"
(cd "$ASM_SRC" && SW_EM24_BIN="$BIN/cor24-run" just vendor-fetch && just build)
cp "$ASM_SRC/build/sw-as24.bin" "$BIN/sw-as24.bin"
chmod 755 "$BIN/sw-as24.bin"
ok "sw-as24.bin"

# ---------------------------------------------------------------------------
log "Step 7: link24 + meta-gen (from sw-cor24-plsw/components/linker)"

PLSW_SRC="$WORK/dcpls/github/sw-embed/sw-cor24-plsw"
(cd "$PLSW_SRC/components/linker" && cargo build --release)
cp "$PLSW_SRC/components/linker/target/release/link24" "$BIN/link24"
cp "$PLSW_SRC/components/linker/target/release/meta-gen" "$BIN/meta-gen"
chmod 755 "$BIN/link24" "$BIN/meta-gen"
ok "link24, meta-gen"

# ---------------------------------------------------------------------------
log "Step 8: wasm32 target (for trunk/web builds)"

rustup target add wasm32-unknown-unknown

# ---------------------------------------------------------------------------
log "Step 9: Global cargo tools"

for tool in trunk; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    cargo install "$tool"
    ok "$tool (installed)"
  else
    ok "$tool (already present)"
  fi
done

# ---------------------------------------------------------------------------
log "Step 10: Shared library artifacts (work/lib/)"

mkdir -p "$LIB/pcode" "$LIB/pascal" "$LIB/plsw"

# P-code VM
cp "$PCODE_SRC/vm/pvm.s" "$LIB/pcode/pvm.s"
cp "$PCODE_SRC/vm/pvmasm.s" "$LIB/pcode/pvmasm.s"

# Pascal compiler
PAS_SRC="$WORK/dcpas/github/sw-embed/sw-cor24-pascal"
if [[ -f "$PAS_SRC/compiler/p24p.s" ]]; then
  cp "$PAS_SRC/compiler/p24p.s" "$LIB/pascal/p24p.s"
  cp "$PAS_SRC/runtime/runtime.spc" "$LIB/pascal/runtime.spc"
  cp "$PAS_SRC/scripts/relocate_p24.py" "$LIB/pascal/relocate_p24.py"
  ok "lib/pascal (p24p.s, runtime.spc, relocate_p24.py)"
else
  printf '    \033[33m!\033[0m %s\n' "lib/pascal SKIPPED — dcpas not cloned or p24p.s not built"
fi

# PL/SW compiler .lgo
if [[ -f "$PLSW_SRC/build/plsw.lgo" ]]; then
  cp "$PLSW_SRC/build/plsw.lgo" "$LIB/plsw/plsw.lgo"
  ok "lib/plsw (plsw.lgo)"
elif [[ -f "$BIN/tc24r" ]] && [[ -f "$BIN/cor24-asm" ]]; then
  log "  Building plsw.lgo from source..."
  (cd "$PLSW_SRC" && just build-lgo)
  cp "$PLSW_SRC/build/plsw.lgo" "$LIB/plsw/plsw.lgo"
  ok "lib/plsw (plsw.lgo — built from source)"
else
  printf '    \033[33m!\033[0m %s\n' "lib/plsw SKIPPED — tc24r or cor24-asm not available"
fi

chmod -R g+rX "$LIB"
ok "lib/ permissions set"

# ---------------------------------------------------------------------------
log "Step 11: Symlinks to mike's sw-install tools"

SW_BIN="$HOME/.local/softwarewrighter/bin"
chmod o+x "$HOME" 2>/dev/null || true

for tool in agentrail sw-checklist reg-rs pjmai-rs; do
  if [[ -f "$SW_BIN/$tool" ]]; then
    ln -sf "$SW_BIN/$tool" "$BIN/$tool"
    ok "$tool -> $SW_BIN/$tool"
  else
    printf '    \033[33m!\033[0m %s\n' "$tool not in $SW_BIN — install via sw-install first"
  fi
done

# ---------------------------------------------------------------------------
log "Done"
echo ""
echo "Toolchain binaries: $BIN/"
echo "Library artifacts:  $LIB/"
echo ""
echo "All d* users have these on PATH via \$TOOLROOT."
echo "Wrapper scripts (pas24, pvm24, plsw) are tracked in git and already in place."
