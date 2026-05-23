# Brief: fix `i2c_ssd1306_rtc_clock.s` — DS1307 read loop runs away

**Owner:** dcxas
**Branch:** `pr/fix-i2c-ssd1306-rtc-clock-read`
**Repo:** `sw-cor24-x-assembler`
**Drafted by:** dwxas (2026-05-17)

## What the user sees on the deploy

On the web demo at sw-embed.github.io/web-sw-cor24-x-assembler/, running
**I2C OLED RTC Clock** with battery on (any persisted state) produces:

- RTC card on the I2C panel: shows whatever the chip currently is
  (e.g. `00:00:00` if battery off, or `eff_now()` if battery on).
- OLED display: shows `00:40:40` (or a different time) — and **the
  two disagree**.

The user-visible report: *"the I2C RTC OLED demo shows the wrong
values in the display. Clock says 10:44:44, OLED says 00:40:40. note
that when the RTC is 00:00:00 the OLED display also shows 00:40:40."*

## What I confirmed via CLI

```bash
cd /disk1/.../work/dwxas/github/sw-embed/sw-cor24-x-assembler
cargo run --quiet -p cor24-asm-cli -- \
    src/examples/assembler/i2c_ssd1306_rtc_clock.s -o /tmp/c.lgo
cor24-emu --lgo /tmp/c.lgo \
    --i2c-device ds1307@0x68 \
    --i2c-device 'ssd1306@0x3C' \
    --time 0.05 --speed 100000 --dump-i2c \
  | grep -E 'STOP|START|ADDR|RD   0x68'
```

Output (with a zero-register DS1307):

```
i=    11  START                  (init burst begin)
i=  1794  STOP                   (init burst end)
i=  1808  START                  (read-seq begin)
i=  1937  ADDR 0x68 WR ACK
i=  2244  START                  (restart for read)
i=  2244  ADDR 0x68 RD ACK
i=  2398  RD   0x68 0x00
i=  2555  RD   0x68 0x00
i=  2712  RD   0x68 0x00
i=  2863  RD   0x68 0x00
... 17+ RD entries continue, NO STOP follows ...
```

After the demo issues `ADDR 0x68 RD`, the bus does **17+ byte reads in
a single transaction with no STOP**, no return to OLED writes, and the
main loop never reaches `i2cstop`. So `i2cread` (or the loop it sits
in) doesn't return after 8 bits / 1 byte the way it should.

The hello demo (`i2c_ssd1306_hello.s`) works correctly on the same
deploy — so `i2cwrite` and the SSD1306 init path are fine. The fault is
isolated to `i2cread` (or its call site in this demo's main_loop) when
chained back-to-back for multi-byte register reads against DS1307.

## Where to look

```
src/examples/assembler/i2c_ssd1306_rtc_clock.s
```

Two candidate root causes — please trace which:

### A) `i2cread` itself doesn't exit after 8 iterations

The loop body is:

```
.ir_loop:
        la      r1, -65504
        lcu     r0, 1
        sb      r0, 1(r1)       ; SDA = 1
        sb      r0, 0(r1)       ; SCL = 1
        lbu     r0, 1(r1)       ; r0 = SDA
        push    r0
        lw      r0, 0(fp)
        add     r0, r0
        pop     r1
        or      r0, r1
        sw      r0, 0(fp)
        la      r1, -65504
        lc      r0, 0
        sb      r0, 0(r1)       ; SCL = 0
        add     r2, -1
        ceq     r2, z
        brf     .ir_loop
```

with `lcu r2, 8` setup. By inspection the counter should hit 0 after 8
iterations and `brf` should fall through. But the CLI dump shows ~17
SCL rises within one transaction, so this loop is running multiple
multiples-of-8 times somewhere. Worth verifying with `--trace-cpu` that
PC actually reaches the `.ret_rrh:` and `i2cstop` labels in
`main_loop`.

### B) main_loop's `i2cstop` after the 3 reads is unreachable

If the loop exits cleanly but the call/return chain (`push r0` after
each read, fp-frame setup) ends up skipping the `i2cstop` jal, the bus
stays in TxByte phase and continues clocking out bytes — which is what
the dump shows. The `push fp; mov fp, sp` right after the 3 reads
might overlap something the bus is still pumping if SCL/SDA writes
inside the prologue are happening at a phase boundary.

A `--trace-cpu --cycles 3000` run from after `ADDR 0x68 RD ACK` should
show whether the PC ever reaches `.rl_rsp:` (the i2cstop site) or
loops back into the read primitive.

## Acceptance

- Run loop exits `i2cread` after exactly 8 SCL rises + 1 ACK clock,
  and the 3-read sequence ends with a STOP — `--dump-i2c` shows
  exactly **3 RD entries and one STOP** per main-loop iteration
  against a zero-register DS1307.
- Demo writes the correct `HH:MM:SS` glyphs for whatever the chip
  currently holds; web users with battery on + persisted state see
  the OLED match the RTC panel.
- `i2c_ssd1306_hello.s` and `i2c_ds1307_read.s` still pass (no
  regressions to the shared i2c primitives).
- `cargo test` green in `sw-cor24-x-assembler`.

## Out of scope

- Re-architecting i2cread/i2cwrite — keep them backwards-compatible
  with the other demos that use the same primitives.
- Changing the demo's font tables or render layout.
- Anything in `sw-cor24-emulator` — the bus state machine appears
  correct (hello demo, ds1307_read demo, etc. all work against the
  same bus). If you find an emulator bug while investigating, brief
  dcemu separately.

## After this lands

dwxas refreshes the sibling clone and rebuilds `pages/`; the web
demo's OLED RTC Clock then renders matching values against the panel.
No web-side code change needed (the demo source flows through
`include_str!` from the sibling).
