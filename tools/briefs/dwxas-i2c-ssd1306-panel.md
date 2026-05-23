# Brief: SSD1306 web panel + I2C OLED demos

**Owner:** dwxas
**Branch:** `pr/i2c-ssd1306-panel`
**Repo:** `web-sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-17)

## Cross-repo coordination

Third agent in this thread. Order of upstream landings (block until
both complete):

1. [`dcemu-i2c-ssd1306-device.md`](dcemu-i2c-ssd1306-device.md) —
   `Ssd1306Device` and `Ssd1306HandleExt` in the emulator. Your
   wasm build embeds the emulator via Cargo path-dep; the constructor
   and the framebuffer accessor must exist in your sibling clone.
2. [`dcxas-i2c-ssd1306-demos.md`](dcxas-i2c-ssd1306-demos.md) — the
   `i2c_ssd1306_hello.s` and `i2c_ssd1306_rtc_clock.s` demos in
   `sw-cor24-x-assembler/src/examples/assembler/`. You `include_str!`
   them into the web dropdown.

Refresh siblings before starting:

```bash
git -C ../sw-cor24-emulator       fetch origin --prune && git -C ../sw-cor24-emulator       switch main && git -C ../sw-cor24-emulator       merge --ff-only origin/main
git -C ../sw-cor24-x-assembler    fetch origin --prune && git -C ../sw-cor24-x-assembler    switch main && git -C ../sw-cor24-x-assembler    merge --ff-only origin/main
# verify both:
test -f ../sw-cor24-emulator/src/peripherals/i2c/devices/ssd1306.rs && echo "emu OK"
test -f ../sw-cor24-x-assembler/src/examples/assembler/i2c_ssd1306_hello.s && echo "asm OK"
```

## Naming separation

Same pattern as the RTC thread:

- **Emulator + CLI: device-shaped.** `ssd1306@0x3C?width=128&height=64`.
  No "display" or "OLED" metaphor in Rust types or registry params.
- **Web: feature-shaped.** Dropdown labels say `OLED`. Panel title
  says `OLED Display`. The user doesn't need to know the chip name
  to understand they're looking at a small monochrome screen.

## What changes

### 1. `src/panels/i2c/ssd1306.rs`

Yew component that renders the 128×64 (or 128×32) framebuffer.

Snapshot shape (mirror the existing per-device snapshot pattern):

```rust
#[derive(Clone, PartialEq)]
pub struct Ssd1306Snapshot {
    pub width: u16,    // from device
    pub height: u16,
    pub display_on: bool,
    pub framebuffer: Vec<u8>,  // page-major, LSB-up (matches Ssd1306HandleExt)
}
```

Rendering:
- An HTML `<canvas>` sized `(width * SCALE)` × `(height * SCALE)`
  with `SCALE = 3` (gives a 384×192 canvas for 128×64, comfortable
  to view).
- Use `wasm-bindgen` + `web-sys::CanvasRenderingContext2d`. Fill
  background black. For each pixel where the bit is set, draw a
  `SCALE × SCALE` foreground rectangle (white or pale green to
  evoke a real OLED — pick what reads cleanest in the dark theme).
- Header strip: device address, `display_on` state, frame counter
  (optional: just for "is it being updated?" feedback).

When `display_on == false`: render the canvas as solid black with
a "(display off)" overlay.

### 2. `DemoConfig` and attach paths

Add two new attach helpers in `main.rs` (mirroring `attach_rtc`):

```rust
fn attach_ssd1306(...);
fn attach_rtc_and_ssd1306(...);
```

Extend `DemoConfig` with `attach_ssd1306` (single-device, for the
Hello demo) and `attach_rtc_and_ssd1306` (for the Clock demo).

Snapshot polling: in the tick, pull `Ssd1306HandleExt::framebuffer()`
+ `display_on()` + size, build the snapshot, push via `use_state`.
The vec compare in `PartialEq` will short-circuit identical frames
(common case while the display is idle between updates).

### 3. `src/demos.rs`

Append two entries at alphabetical slot:

```rust
("I2C OLED Hello",
 include_str!("../../sw-cor24-x-assembler/src/examples/assembler/i2c_ssd1306_hello.s")),
("I2C OLED RTC Clock",
 include_str!("../../sw-cor24-x-assembler/src/examples/assembler/i2c_ssd1306_rtc_clock.s")),
```

Slot: between `I2C Add1 Ping` (or whatever its current
dwxas-side label is) and `I2C RTC Read`.

Each entry's `DemoConfig`:
- Hello: `attach_ssd1306` only.
- Clock: `attach_rtc_and_ssd1306` — both panels appear.

### 4. Panel visibility

Per the existing per-demo device-config pattern:
- I2C OLED Hello shows just the SSD1306 panel.
- I2C OLED RTC Clock shows BOTH the SSD1306 panel AND the RTC
  panel (the battery toggle still applies to the RTC side; if
  user has battery on with persisted state, the clock demo starts
  with that effective time).
- Non-OLED demos don't render the SSD1306 panel.

## Acceptance

- New panel renders the framebuffer with visible pixels for the
  Hello demo (`HELLO` legible at the top of the canvas).
- Clock demo's display updates each demo-tick, showing time
  advancing.
- Display-off state renders correctly (init sequence ends with
  display ON; if a demo issues 0xAE the panel shows the "display
  off" overlay).
- Clock demo + RTC battery on + persisted state: time on display
  matches `effective_now()` (i.e. the localStorage value + elapsed
  real time, mod 86400).
- `cargo clippy -D warnings` green; `pages/` rebuilt.
- Saga step recorded.

## Out of scope

- **No interactive pixel painting in the panel.** The display is
  output-only from the user's perspective. (Slider/button doesn't
  apply.)
- **No "save framebuffer as PNG" download button.** Nice-to-have,
  future brief if anyone asks.
- **No scrolling/animation rendering.** Emulator doesn't support
  it yet; nothing to render.
- **No mid-tick redraw.** One framebuffer pull per tick is fine
  (~16ms) — humans don't perceive sub-tick OLED refreshes.

## Open question (push back if you disagree)

The RTC clock demo combines two devices. Should the dropdown name
emphasize the **clock** aspect (`I2C OLED RTC Clock`) or the
**combination** (`I2C RTC + OLED Clock`)? Brief draft uses the
former for alphabetical sort cleanliness (all `I2C OLED *` cluster
together), but the latter reads better as a description of what
the demo does. Your call — pick one and stick with it.

## Workflow

```bash
cd /disk1/.../work/dwxas/github/sw-embed/web-sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
git switch -c feat/i2c-ssd1306-panel
# implement; ./scripts/build-pages.sh for the wasm rebuild
cargo clippy -- -D warnings
git commit -am "feat: SSD1306 panel + I2C OLED Hello / RTC Clock demos"
git branch -m feat/i2c-ssd1306-panel pr/i2c-ssd1306-panel
```
