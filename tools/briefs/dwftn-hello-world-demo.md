# Brief: stand up the `web-sw-cor24-fortran` live demo with hello-world

**Owner:** dwftn
**Branch:** start as `feat/hello-world-demo`; `dg-mark-pr` when ready (becomes `pr/hello-world-demo`)
**Repo:** `web-sw-cor24-fortran` (mike just created the bare; clone it as your primary)
**Prerequisite:** dcftn's `pr/fortran-hello-world` saga must be relayed and `examples/hello.lgo` must exist (either inside dcftn's repo or installed at `work/lib/cor24/hello-fortran.lgo`). Mike will signal when ready.

## Why this matters

This is the **third and final saga** unblocking the Fortran hello-world live demo at `https://sw-embed.github.io/web-sw-cor24-fortran/`. dcsno shipped SNOBOL4; dcftn produced `hello.lgo`; you embed it into a Yew/WASM web frontend that runs `cor24-emu` in the browser and shows the output. **Goal: someone visiting the URL sees "Hello, World!" rendered live** (not as static text — actually executed by the embedded emulator).

## One-time setup

Clone your repo from the bare:

```bash
mkdir -p ~/github/sw-embed && cd ~/github/sw-embed
git clone /disk1/github/softwarewrighter/devgroup/work/bare/web-sw-cor24-fortran.git
```

You also need the same Cargo path-deps every other web-sw-cor24-* repo uses:

```bash
[ -d sw-cor24-emulator ] || git clone /disk1/github/softwarewrighter/devgroup/work/bare/sw-cor24-emulator.git
[ -d sw-cor24-isa ]      || git clone /disk1/github/softwarewrighter/devgroup/work/bare/sw-cor24-isa.git
```

The repo is essentially empty (default README from `gh repo create`). You're starting fresh.

## Reference templates

The cleanest existing template is **`web-sw-cor24-x-tinyc`** (recently rebuilt, Trunk-based, embeds `cor24-emu` via the emulator's `cdylib`/wasm32 build). dwxtc just shipped a `pr/rebuild-pages-and-title` saga there — read their repo to mirror the pattern:

- `Cargo.toml` workspace shape (cor24-emulator + cor24-isa as path-deps)
- `Trunk.toml` for the WASM bundling
- `index.html` with the Yew root
- `src/main.rs` with the Yew app
- `scripts/build-pages.sh` that does `trunk build --release --public-url /web-sw-cor24-fortran/` and rsyncs `dist/` to `pages/`
- `.github/workflows/pages.yml` that uploads `pages/` and deploys

## Goal

A Yew/WASM web app at `https://sw-embed.github.io/web-sw-cor24-fortran/` that:

1. Embeds the hello-world `.lgo` (committed as a build asset — see Embedding strategy below).
2. On page load (or button click), invokes the embedded `cor24-emu` to run the lgo.
3. Displays the UART output ("Hello, World!") in a div on the page.
4. Shows the `examples/hello.f` source alongside the output for context.

## Embedding strategy

Three options, in order of preference for this saga:

**Option 1: bake the `.lgo` into the wasm bundle as a static byte slice.** Easiest. Copy `examples/hello.lgo` from dcftn's repo (it's at `/disk1/.../work/dcftn/github/sw-embed/sw-cor24-fortran/examples/hello.lgo` or fetch via the bare). Add it to your repo as `examples/hello.lgo`. `include_bytes!("../examples/hello.lgo")` in `src/main.rs`, hand to the embedded emulator. Single asset, single deploy.

(Note: demos and demo outputs live in the producing repo's `examples/` dir, not the shared `work/lib/cor24/` toolchain library. The shared lib is for compilers/interpreters only — see README "Architecture" section.)

**Option 2: fetch the `.lgo` at runtime via HTTP.** Place at `pages/hello.lgo`, fetch it on page load. Slightly more flexible (could swap without rebuilding wasm), but adds a fetch failure mode and a CORS/path concern.

**Option 3: inline the assembly source and assemble in the browser.** Out of scope — that'd require a wasm-compiled cor24-asm. Skip.

Use Option 1.

## Verification

```bash
cd ~/github/sw-embed/web-sw-cor24-fortran
trunk serve
# open http://127.0.0.1:8080/ in browser, see "Hello, World!"

# build for deploy
./scripts/build-pages.sh
git status   # pages/ should have fresh artifacts

# CI verification: the .github/workflows/pages.yml should successfully upload + deploy
```

Final smoke test post-promotion: `curl -sL https://sw-embed.github.io/web-sw-cor24-fortran/ | grep -i hello` should find "Hello, World!" in the page (or in JS that embeds it).

## What goes in this PR

1. `Cargo.toml` workspace + `[lib] cdylib` config matching the web-sw-cor24-x-tinyc template.
2. `Trunk.toml`.
3. `index.html` with Yew root + page title "FORTRAN Hello World on COR24".
4. `src/main.rs` — minimal Yew app that loads `hello.lgo` (Option 1: `include_bytes!`) and runs it through the embedded emulator. Display the source on the left, UART output on the right.
5. `examples/hello.f` and `examples/hello.lgo` (copied from dcftn's repo or fetched from `work/lib/cor24/hello-fortran.lgo`).
6. `scripts/build-pages.sh` — Trunk build + rsync to pages/.
7. `.github/workflows/pages.yml` — upload-pages-artifact + deploy.
8. `pages/` — the built-and-committed artifacts (initial bake; rebuild pattern from dwxtc applies for future updates).
9. `README.md` linking to the live demo URL + the `dcftn/sw-cor24-fortran` upstream.
10. `CLAUDE.md` and `LICENSE` (MIT, matching the rest).

## What does NOT go in this PR

- No live compilation in the browser. The `.lgo` is pre-built.
- No multiple Fortran demos. Just hello world.
- No editor/REPL UI — read-only display is fine for v1.
- No additions to the Fortran source / runtime — that's dcftn's territory.

## When done

Workflow: `dg-new-feature hello-world-demo` → implement → `dg-mark-pr`. Signal mike. After mike relays + promotes to main, GitHub Pages workflow rebuilds and deploys; the live URL serves your demo. Verify with `curl` and a browser visit.

Promotion to `main` is mike's call, separately.

## After this saga

When dcftn's compiler matures (more Fortran statements supported), update `examples/hello.lgo` (or add more demos) and rebuild pages. The Trunk + Yew shape is reusable for any future Fortran demo. dcftn could even push you a fresh `.lgo` per their saga progress and you ship a `pr/refresh-fortran-demo` saga periodically.
