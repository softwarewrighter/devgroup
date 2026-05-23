# Brief: SPI SD Card + NOR Flash web panels + demos (two-step saga)

**Owner:** dwxas
**Branch:** `pr/spi-sdcard-and-nor-flash-panels`
**Repo:** `web-sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-18)

## Cross-repo coordination

Last agent in the thread. Block on both:

1. [`dcemu-spi-sdcard-and-nor-flash.md`](dcemu-spi-sdcard-and-nor-flash.md)
   — `SdCardDevice` + `W25q32Device` in the emulator.
2. [`dcxas-spi-sdcard-and-nor-flash-demos.md`](dcxas-spi-sdcard-and-nor-flash-demos.md)
   — `spi_sdcard_read.s` + `spi_nor_flash_demo.s` in the sibling
   assembler repo.

Refresh both siblings before starting:

```bash
git -C ../sw-cor24-emulator      fetch origin --prune && git -C ../sw-cor24-emulator      switch main && git -C ../sw-cor24-emulator      merge --ff-only origin/main
git -C ../sw-cor24-x-assembler   fetch origin --prune && git -C ../sw-cor24-x-assembler   switch main && git -C ../sw-cor24-x-assembler   merge --ff-only origin/main
test -f ../sw-cor24-emulator/src/peripherals/spi/devices/sdcard.rs && echo "emu sd ok"
test -f ../sw-cor24-emulator/src/peripherals/spi/devices/w25q32.rs && echo "emu flash ok"
test -f ../sw-cor24-x-assembler/src/examples/assembler/spi_sdcard_read.s && echo "asm sd ok"
test -f ../sw-cor24-x-assembler/src/examples/assembler/spi_nor_flash_demo.s && echo "asm flash ok"
```

**Per the [serialization brief](dwxas-serialize-pr-branches.md):**
this is ONE pr/* in flight. Don't start step 2's UI work on a
parallel branch — both steps live on `pr/spi-sdcard-and-nor-flash-panels`
sequentially.

## Naming separation

Same convention as before:

- **Emulator + CLI: device-shaped.** `sdcard@cs=2?file=...` and
  `w25q32@cs=3?file=...`.
- **Web: feature-shaped.** "SPI SD Card" (or "SD Card Reader") and
  "SPI NOR Flash" in panel titles and dropdown labels.

## Step 1: SD Card panel + demo

### `src/panels/spi/sdcard.rs`

Yew component showing:

- Header strip: device address, current image size (e.g., "32 KB
  loaded"), last-accessed sector indicator (just-flashed highlight).
- A `<input type="file" accept=".img,.iso,.bin,*/*">` widget for
  uploading a disk image from the user's machine.
- Optional: a tiny hex preview of bytes around the last-accessed
  sector (16 bytes ± 8 around the most-recently-read offset). Skip
  if it crowds the UI.

Upload path:
- User picks file → JS `FileReader` reads as `ArrayBuffer` → pass
  to `SdCardHandleExt::replace_image(bytes)` via the existing
  handle plumbing.
- Persist the bytes to **IndexedDB** under key `sdcard.image`
  (localStorage is too small for typical .img files; IndexedDB
  is purpose-built). On panel mount, restore from IndexedDB if
  present.
- Show "Reset" button that clears IndexedDB and re-attaches a
  blank in-memory device.

Snapshot shape (mirror existing per-device pattern):

```rust
#[derive(Clone, PartialEq)]
pub struct SdCardSnapshot {
    pub cs: u8,
    pub size_bytes: u32,
    pub last_accessed_sector: Option<u32>,
    // Don't put the full image in the snapshot — pull-on-demand via the handle.
}
```

The full image only needs to be visible if you add the hex preview;
in that case pull `image()` once on snapshot diff and slice
`[sector*512-8 .. sector*512+8]` for display.

### `src/demos.rs`

```rust
("SPI SD Card Read",
 include_str!("../../sw-cor24-x-assembler/src/examples/assembler/spi_sdcard_read.s")),
```

Slot: alphabetical, between "SPI NOR Flash Program" and
"SPI TMP125 Read".

`DemoConfig::attach_sdcard` — single-device attach. The web demo
needs a default image bundled: ship `static/sdcard-default.img`
(a 4 KB blob with sectors 0-7 containing recognizable byte patterns
like `00 01 02 03...`, `10 11 12 13...`, etc.) and load it as the
initial image when no user upload is present.

### Panel visibility

Per the per-demo device-config pattern, "SPI SD Card Read" attaches
just the SD card device. The TMP125, OLED, RTC, etc. panels stay
hidden for this demo.

## Step 2: NOR Flash panel + demo

### `src/panels/spi/w25q32.rs`

Yew component showing:

- Header strip: device address, JEDEC ID (always `EF 40 16` for
  W25Q32), WIP / WEL bits as live indicators (color or icon).
- **Image visualization**: 4 MiB is a lot. Render as a heatmap or
  sparkline: each pixel = one 4 KB sector, color shows recent
  activity (recently-written = warm, untouched = cold/neutral, all-
  `0xFF` = explicitly "erased" pale). The canvas is small (e.g.,
  64×16 = 1024 sectors at one pixel each) so it's not visually
  heavy.
- Last-accessed address as text (hex).
- Buttons:
  - "Reset / Chip Erase" — calls `W25q32HandleExt::erase_chip()`,
    flashes the heatmap pale, clears IndexedDB.
  - "Load from file" — file upload widget, same shape as SD card,
    accepts a 4 MiB binary blob.
  - "Download image" (optional, nice-to-have) — package the current
    image as a Blob and trigger a download. Useful for "I want to
    flash this on the CH341A programmer." Skip if it bloats scope.

IndexedDB key: `w25q32.image`. 4 MiB exactly. The same load-on-
mount / save-on-program flow as SD card.

Snapshot shape:

```rust
#[derive(Clone, PartialEq)]
pub struct W25q32Snapshot {
    pub cs: u8,
    pub wip: bool,
    pub wel: bool,
    pub last_accessed_address: Option<u32>,
    pub sector_activity: Vec<u8>,  // 1024 entries: how recently each 4KB sector was touched (cooling counter)
}
```

The heatmap renderer reads `sector_activity`. Decay each tick
(e.g., subtract 1) so recently-active sectors fade back to cold
over time.

### `src/demos.rs`

```rust
("SPI NOR Flash Program",
 include_str!("../../sw-cor24-x-assembler/src/examples/assembler/spi_nor_flash_demo.s")),
```

Slot: after "SPI Echo Ping", before "SPI SD Card Read".

`DemoConfig::attach_w25q32`. On Run, the device attaches at default
CS (3 per the brief), images loaded from IndexedDB if present, else
a fresh 4 MiB all-`0xFF` (matching real flash power-on state).

### Panel visibility

"SPI NOR Flash Program" attaches just the W25Q32 device. The
heatmap should visibly update as the demo runs (one sector erase at
0x000000 → that pixel goes pale; one page program at 0x000000 →
same pixel goes warm).

## Both steps: synchronization trap reminder

Both devices have host-backed state (IndexedDB on web). On every
i2c-write-style transaction completion (here: each Page Program or
sector write for flash; each CMD24 for SD card), the panel must
write the updated image back to IndexedDB **synchronously with the
write completion** — same trap pattern as the DS1307 battery brief.
Otherwise on next page reload, persisted state is stale.

For 4 MiB blobs, "synchronous" within a single tick is fine. Don't
re-serialize the full image on every byte; coalesce: track a "dirty"
bool per tick, and if dirty at tick end, `idbPut(image)`.

## Acceptance

- Two new dropdown entries: "SPI SD Card Read" and
  "SPI NOR Flash Program".
- Two new panels: SD card and W25Q32.
- File upload works on both.
- IndexedDB persistence works on both; reset buttons clear it.
- Default SD card image bundled at `static/sdcard-default.img` so
  the demo shows recognizable bytes out of the box.
- 4 MiB blank image used as default for the flash; first page
  program after chip erase visibly updates the heatmap.
- `cargo clippy -- -D warnings` green.
- `pages/` rebuilt.
- TWO commits on `pr/spi-sdcard-and-nor-flash-panels`:
  - `feat(panels): SPI SD Card panel + read demo`
  - `feat(panels): SPI W25Q32 NOR flash panel + program demo`

Saga: two-step.

## Out of scope

- **No FAT/MBR parsing in the panel.** The image hex preview is
  raw bytes only.
- **No "browse files inside SD image" UI.** That's a much bigger
  scope (would need a FAT16/32 reader in WASM). Brief separately
  if you want it.
- **No flash sector-level write inspector.** The heatmap is the
  visualization; clicking a sector to see its bytes is nice-to-
  have, separate brief.
- **No simultaneous attach of all SPI devices.** The existing
  per-demo `DemoConfig` pattern keeps only one demo's devices
  attached at a time.
- **No download-as-image for SD card** (the user already has it on
  their disk). Flash download is optional.

## Open question

The 4 MiB NOR flash heatmap: 1024 sectors. Rendered as 64×16 pixels
(one px per sector) is tiny but legible. Alternative is 32×32 = 1024
or 16×64. Pick the aspect that fits your I/O column layout best;
I'd default to wide-and-short (64×16) so the panel stays under the
RTC/OLED panels in vertical space.

## Workflow

```bash
cd /disk1/.../work/dwxas/github/sw-embed/web-sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/spi-sdcard-and-nor-flash-panels
# step 1: sd card panel + demo wiring
./scripts/build-pages.sh
git commit -am "feat(panels): SPI SD Card panel + read demo"
# step 2: w25q32 panel + demo wiring (same branch)
./scripts/build-pages.sh
git commit -am "feat(panels): SPI W25Q32 NOR flash panel + program demo"
git branch -m feat/spi-sdcard-and-nor-flash-panels pr/spi-sdcard-and-nor-flash-panels
```
