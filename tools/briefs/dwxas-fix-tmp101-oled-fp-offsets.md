# Brief: fix fp-relative offsets in `i2c_ssd1306_tmp101.s` temperature display

**Owner:** dwxas
**Branch:** `pr/fix-tmp101-oled-fp-offsets`
**Repo:** `web-sw-cor24-x-assembler`
**Drafted by:** mike (2026-05-18)

## Why this brief exists

You signaled `pr/i2c-tmp101-oled-demo` containing a 12-line fix to
`src/examples/i2c_ssd1306_tmp101.s` that corrects the stack-frame
layout in the temperature-display path. The fix itself is correct
— the demo was reading `lw r0, 6(fp)` for `|T|` when `|T|` actually
sits at `fp+0` (stack grows down, last push has lowest address;
push order was `fp, r2, r0` so `r0=|T|` is at `fp+0` and the saved
`fp` is at `fp+6`).

The PR couldn't relay because the branch was based on `21f4db7`
(before the original demo merged at `9738f59`), so dg-relay saw an
`add/add` conflict on the entire `.s` file plus the usual
`pages/` artifact collisions. This brief is the tight redo.

Also see [`dwxas-serialize-pr-branches.md`](dwxas-serialize-pr-branches.md)
— same root cause, structural fix.

## The change (exact diff)

`src/examples/i2c_ssd1306_tmp101.s`, two hunks:

```diff
@@ -168,10 +168,12 @@ main_loop:
         pop     r2

         ; ----- Stash sign glyph + abs value on fp frame -----
+        ; Stack grows down (push decrements sp), so the LAST push has
+        ; the LOWEST address -- i.e. fp+0.
         ; Frame layout (relative to fp set below):
-        ;   fp+0  = saved fp
+        ;   fp+0  = |T|              (pushed last)
         ;   fp+3  = sign glyph addr
-        ;   fp+6  = |T|
+        ;   fp+6  = saved fp         (pushed first)
         push    fp
         push    r2              ; sign glyph addr
         push    r0              ; |T|
@@ -228,7 +230,7 @@ main_loop:
         jal     r1, (r2)
 .ml_dat_c:

-        ; (1) sign glyph
+        ; (1) sign glyph (at fp+3 per frame layout above)
         lw      r0, 3(fp)
         la      r1, .ml_w_sign
         la      r2, write5
@@ -236,8 +238,8 @@ main_loop:
 .ml_w_sign:

         ; (2,3) Decompose |T| into tens, ones via subtract-by-10. After the
-        ; loop r0 = ones, the count of subtractions = tens.
-        lw      r0, 6(fp)
+        ; loop r0 = ones, the count of subtractions = tens. |T| is at fp+0.
+        lw      r0, 0(fp)
         lc      r1, 0           ; tens counter
 .ml_tens:
         lcu     r2, 10
```

That's the entire source change. Two comment blocks made accurate,
one load offset corrected from `6(fp)` to `0(fp)`.

## How to land it (concrete commands)

The shipping version of the demo is already on `origin/dev`
(currently SHA `ccfc6cb` on `main`, whatever `origin/dev` is at
fetch time). Branch off **current dev**, not off your previous
attempt's base:

```bash
cd /disk1/.../work/dwxas/github/sw-embed/web-sw-cor24-x-assembler
git fetch origin --prune
git switch dev && git merge --ff-only origin/dev
# delete the stale broken branch (do NOT carry it forward)
git branch -D pr/i2c-tmp101-oled-demo
git switch -c feat/fix-tmp101-oled-fp-offsets

# apply the two-hunk fix to src/examples/i2c_ssd1306_tmp101.s
$EDITOR src/examples/i2c_ssd1306_tmp101.s
# (or `git apply` the diff above if you saved it as a .patch)

./scripts/build-pages.sh         # rebuild pages/ — required
git add src/examples/i2c_ssd1306_tmp101.s pages/
git commit -m "fix(demos): correct fp-relative offsets in OLED thermometer"
git branch -m feat/fix-tmp101-oled-fp-offsets pr/fix-tmp101-oled-fp-offsets
```

Then signal via `dg-mark-pr` or just leave the `pr/` name.

## Acceptance

- Commit message starts with `fix(demos):` not `feat(demos):` —
  this is a behaviour-correction, not a new feature. The original
  demo addition is already on main.
- The diff contains exactly the two source-file hunks above, plus
  the regenerated `pages/` artifacts (new wasm hash, updated
  `index.html`).
- No other source files touched.
- After mike's relay + dg-release, https://sw-embed.github.io/web-sw-cor24-x-assembler/
  's `I2C OLED Thermometer` demo shows actual temperature values
  responding to the TMP101 slider (was previously rendering whatever
  bytes the runaway `lw r0, 6(fp)` produced — probably garbage that
  *looked* roughly numeric and didn't change with the slider).

## Out of scope

- **Don't re-do the demo from scratch.** It's already on dev; only
  the two-hunk fix is needed.
- **Don't bundle other changes into this PR.** Per
  `dwxas-serialize-pr-branches.md`, one `pr/*` in flight at a time.
  If you've got other fixes queued, do them in subsequent PRs after
  this one reaps.
- **Don't rename the upstream sibling demo.** This is the web-local
  fork (`src/examples/i2c_ssd1306_tmp101.s`). The sibling
  `sw-cor24-x-assembler` repo doesn't have this demo file
  (it's web-local-only, like `tmp101_read.s`, `tmp125_read.s`,
  `spi_echo_ping.s`). No cross-repo coordination needed.

## Workflow lesson (carried over from the serialization brief)

When you make a fix on top of a previously-shipped PR:
1. `git fetch origin --prune` first — your local `origin/dev` may
   be stale.
2. Branch off **fetched** `origin/dev`, not off `dev` (which could
   be stale even after fetch if you haven't FF'd it).
3. Use a fresh slug (`pr/fix-<what>`), not the same slug as the
   shipped PR. dg-relay matches branches by name; reusing the name
   confuses everyone reading `dg-list-pr` output.
