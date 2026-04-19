# Shared rust toolchain — layout and maintenance

Rust and cargo on this host come from a shared rustup install under `/opt/rust`,
not from pacman's `rust` package. One install serves every user; each user
keeps their own cargo state in `$HOME/.cargo`.

Don't `pacman -S rust` — it causes an llvm-libs version skew on Arch and breaks
cargo at runtime. Stick with the shared install below.

## Layout

| Path                               | Owner | Group    | Mode    | Purpose                                |
|------------------------------------|-------|----------|---------|----------------------------------------|
| `/opt/rust/`                       | mike  | devgroup | `2755`  | shared prefix                          |
| `/opt/rust/rustup/`                | mike  | devgroup | r-x     | rustup's `RUSTUP_HOME` — toolchains    |
| `/opt/rust/cargo/bin/`             | mike  | devgroup | r-x     | rustup proxies (`cargo`, `rustc`, ...) |
| `$HOME/.cargo/`                    | user  | user     | private | per-user cargo state (tools, registry) |

Managed `.bashrc` block (applied by `scripts/setup-devgroup-accounts.sh`)
exports:

```bash
export RUSTUP_HOME="/opt/rust/rustup"
# PATH: /opt/rust/cargo/bin first, then ~/.cargo/bin, then the usual
```

So `cargo`/`rustc` resolve to the shared proxies, and `cargo install <tool>`
still writes to the user's own `~/.cargo/bin` (first on PATH).

## One-time install

```
cd /disk1/github/softwarewrighter/devgroup/scripts
sudo ./install-shared-rust.sh
sudo ./setup-devgroup-accounts.sh dev-users.tsv   # propagates the bashrc change
```

The install script is idempotent — if `/opt/rust/cargo/bin/rustup` already
exists, it runs `rustup update stable` in place rather than reinstalling.

## Updating the toolchain

Any of these. The first is the short form.

```
# Re-run the installer (idempotent — refreshes stable)
sudo /disk1/github/softwarewrighter/devgroup/scripts/install-shared-rust.sh
```

```
# Or directly
sudo -u mike \
  RUSTUP_HOME=/opt/rust/rustup CARGO_HOME=/opt/rust/cargo \
  /opt/rust/cargo/bin/rustup update stable
```

Users pick up the new version on their next `cargo`/`rustc` invocation — no
shell restart needed.

## Installing additional components (rustfmt, clippy, etc.)

`rustfmt` and `clippy` come with the `default` profile. To add something
extra, for example the `rust-src` component:

```
sudo -u mike \
  RUSTUP_HOME=/opt/rust/rustup CARGO_HOME=/opt/rust/cargo \
  /opt/rust/cargo/bin/rustup component add rust-src
```

## Installing a nightly or pinned toolchain globally

```
sudo -u mike \
  RUSTUP_HOME=/opt/rust/rustup CARGO_HOME=/opt/rust/cargo \
  /opt/rust/cargo/bin/rustup toolchain install nightly
```

Switch the default for all users:

```
sudo -u mike \
  RUSTUP_HOME=/opt/rust/rustup CARGO_HOME=/opt/rust/cargo \
  /opt/rust/cargo/bin/rustup default stable   # or nightly
```

## Per-user override (opt-in)

A user who needs their own toolchain (e.g. a project with `rust-toolchain.toml`
pinning a specific version the shared install doesn't have) can run
`rustup-init` themselves. Add in their personal `.bashrc` **after** the
managed block:

```bash
export RUSTUP_HOME="$HOME/.rustup"
```

That points their RUSTUP_HOME at a private location. Because `$HOME/.cargo/bin`
is earlier on PATH than `/opt/rust/cargo/bin`, their personal `cargo`/`rustc`
proxies will win when present.

## Troubleshooting

**`cargo: error while loading shared libraries: libLLVM.so.XX`**
pacman's `rust` got installed somewhere and is winning PATH resolution. Check
`which cargo`. If it's `/usr/bin/cargo`, run `sudo pacman -Rns rust`.

**`error: could not find 'rustup' in RUSTUP_HOME`**
The user's `RUSTUP_HOME` points at an empty or missing directory. Likely a
custom override in their personal `.bashrc`. Check `echo $RUSTUP_HOME`.

**A d\* user sees `permission denied` reading under `/opt/rust/`**
Re-run the install script — it ends with a `find … chmod g+rxs` + `setfacl`
that repairs perms.

**Toolchain hasn't updated after `rustup update`**
Check `rustup show` — it reports the active channel and version. If users see
an old version, their own `~/.cargo/bin/cargo` may be shadowing the shared
one. `which cargo` to confirm.

## Why not pacman rust

Arch's `rust` package links against whatever `llvm-libs` version is current
when the package is built. Upgrade cadence between these two packages is
occasionally mismatched for days at a time, which breaks cargo at runtime
(see `libLLVM.so.22.1: cannot open shared object file`). rustup ships its
own LLVM inside each toolchain, so it's immune.
