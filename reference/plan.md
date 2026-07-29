# COR24-TB and sw-embed integration plan

This document turns the checklist in `plan.txt` into an ordered validation
procedure.  The order matters: each phase establishes one contract before the
next phase adds another variable.

No sw-embed application has yet been validated on a physical COR24-TB.  Do not
describe emulator success as hardware support until the corresponding hardware
row in the results log passes.

## Repository map

| Role | Location | Notes |
| --- | --- | --- |
| Board release | `reference/20260715-cor24 -> /home/mike/tools/20260715-cor24` | Manual, release notes, reference demos, `te.c`, and experimental `te-rs` |
| Copied reference artifacts | `reference/*.{c,lgo,lst}` | Stable inputs for emulator and board tests |
| Host cross-assembler | `work/dcxas/.../sw-cor24-x-assembler` | `cor24-asm`; canonical current `.s -> .lgo` producer |
| Emulator | `work/dcemu/.../sw-cor24-emulator` | `cor24-emu`; consumes `.lgo`, does not assemble |
| Tiny C compiler | `work/dcxtc/.../sw-cor24-x-tinyc` | `tc24r`; produces `.s` |
| Native/self-hosted assembler | `work/dcasm/.../sw-cor24-assembler` | Future on-COR24 assembler; currently only a single-`nop` smoke milestone |
| Forth | `work/dcfth/.../sw-cor24-forth` | Native interpreter in `forth.s` |
| Macro Lisp | `work/dcmls/.../sw-cor24-macrolisp` | C source compiled by `tc24r`, then assembled to a REPL `.lgo` |

Use full `.lgo` output for hardware:

```bash
cor24-asm program.s -o program.lgo --lgo-full
```

`--lgo-compact` assumes zero-filled memory and is unsafe after a warm reload.

## Record every experiment

For each run, record:

- date, board revision, COR24 release/commit, and cold or warm start;
- adapter make, USB VID:PID, Linux driver, stable device path, and direct/hub;
- exact tool version and command;
- artifact name, byte count, line count, maximum line length, and final `G`
  record;
- loader mode (`te.c`, `te-rs` options, or another loader);
- last acknowledged line and exact monitor error;
- expected and actual UART, LED, and switch behavior.

Keep failures: the last good line and first bad line distinguish transport
loss from a bad image.

## Bound non-interactive commands

Every emulator run must have both an emulator instruction/time bound and a
host-side timeout. The emulator bound makes successful tests deterministic;
the host timeout protects against a deadlock in the emulator itself.

Use this pattern:

```bash
timeout --signal=INT --kill-after=2s 10s \
  work/bin/cor24-emu \
  --lgo program.lgo \
  --speed 0 \
  --max-instructions 100000 \
  --quiet
```

Expected exit status is `0` for a normal emulator run and `124` when GNU
`timeout` expires. Assembly and document validation should also use a
reasonable host timeout. Do not automatically time out an interactive
`te-rs` hardware upload: interrupting it in the middle of a record can leave
the board monitor waiting for the remainder of that record.

## Phase 0: freeze the known inputs

1. Confirm the board is revision 3 and the installed FPGA image corresponds to
   COR24 commit `bd805538f6` (release 2026-07-15).
2. Preserve hashes of `reference/*.lgo` before rebuilding anything.
3. Build the three current host tools and capture their versions:

   ```bash
   (cd work/dcxas/github/sw-embed/sw-cor24-x-assembler &&
     ./scripts/build.sh)

   (cd work/dcemu/github/sw-embed/sw-cor24-emulator &&
     cargo build --release)

   (cd work/dcxtc/github/sw-embed/sw-cor24-x-tinyc &&
     ./scripts/build-all.sh)
   ```

   The relative `cd` examples above assume the repository layout remains
   unchanged.  Prefer putting these exact binaries on a test-only `PATH`:

   ```bash
   export PATH="$PWD/work/dcxas/github/sw-embed/sw-cor24-x-assembler/target/release:$PWD/work/dcemu/github/sw-embed/sw-cor24-emulator/target/release:$PWD/work/dcxtc/github/sw-embed/sw-cor24-x-tinyc/components/cli/target/release:$PATH"
   cor24-asm --version
   cor24-emu --version
   tc24r --help
   ```

Gate: the tool paths and versions are recorded and their repository tests
pass.  Do not rebuild the proprietary/reference `cc24` yet.

## Phase 1: validate supplied COR24 images in software

This establishes whether the emulator accepts MakerLisp `.lgo` records and
implements enough of the shipped hardware ABI.

```bash
cor24-emu --lgo reference/hello.lgo --speed 0 \
  --max-instructions 100000 --quiet

cor24-emu --lgo reference/blinky.lgo --switch on --speed 0 \
  --max-instructions 1000 --dump

cor24-emu --lgo reference/blinky.lgo --switch off --speed 0 \
  --max-instructions 1000 --dump
```

Expected:

- `hello.lgo` prints `hello world`;
- blinky reflects S2 at D2 (the demo is button-follow, not timed blinking);
- the `G` record supplies the entry point.

Then test `fib`, `memtest`, `sieve`, and finally the larger floating-point
demos.  Compare UART output to the supplied sources/listings rather than only
checking that execution continues.

Current observation (2026-07-29): `hello.lgo` prints `hello world` and
`blinky.lgo` responds to the emulated switch.  `uartintr.lgo` loaded at entry
`G0000E8` immediately moved SP to `0x002000`, which the current emulator
reported as below its stack region.  Treat UART-interrupt compatibility as an
open emulator/ABI investigation.

Gate: classify every reference demo as pass, expected unsupported peripheral,
or reproducible emulator defect.

## Phase 2: isolate the physical serial transport

Follow the board manual and `reference/20260715-cor24/docs/analysis.md`.
The required link is 921600 baud, 8N1, raw mode, 3.3 V signaling, with working
RTS/CTS.

Wire signals by function:

```text
adapter TX  -> J4-2 RX
adapter RX  <- J4-1 TX
adapter RTS -> J4-3 CTSN
adapter CTS <- J4-4 RTSN
adapter GND -- J4-5 GND
```

Do not connect adapter power or J4-6.

For every initial attempt:

1. Bypass USB hubs.
2. Disconnect both J1 power and adapter USB until the board is dark.
3. Apply J1 board power first, then connect adapter USB.
4. Use `/dev/serial/by-id/...`, verify permissions and exclusive ownership.
5. Start the terminal, press S1 once, and require a repeatable monitor banner.
6. From another shell, use `stty -F "$SERIAL" -a` read-only and confirm
   `921600`, `crtscts`, `cs8`, `-cstopb`, and `-parenb`.

Test `te-rs` in a matrix, beginning with the strongest diagnostics:

```bash
cd reference/20260715-cor24/tools/te-rs
cargo test
cargo build --release
./target/release/te-rs --sync --verbose --delay 10 "$SERIAL"
```

Inside the terminal, press Ctrl-R, enter the `.lgo` path, and press Enter.
If that passes, repeat at delays 5 ms, 2 ms, 1 ms, and no delay.  Repeat once
with `--sync` but no delay.  `--sync` checks each line's exact echo before
sending the next; a delay only adds pacing.

Also run adapter loopback and RTS/CTS blocking tests described in
`docs/analysis.md`.  A TX/RX loopback alone does not prove that the adapter or
driver honors CTS.

Gate: the same adapter produces a stable banner over ten cold starts, passes
flow-control testing, and loads a multi-line file without an echo mismatch.

## Phase 3: validate the board monitor with untouched images

Load supplied artifacts in increasing size:

1. `reference/blinky.lgo` (3 lines);
2. `reference/hello.lgo` (7 lines);
3. `reference/uartintr.lgo` (16 lines);
4. `reference/memtest.lgo` (21 lines);
5. `reference/fib.lgo` (182 lines);
6. `reference/sieve.lgo` (185 lines);
7. a 400+ line supplied image.

After each load, cold-reset before retrying and test the documented behavior.
For blinky, press and release S2 and observe D2.  For `uartintr`, send a known
short string and then the supplied `test.txt`; save exact received bytes.

Interpretation:

- a failure at the same source line with multiple proven adapters suggests a
  monitor/record problem;
- a moving failure point suggests transport, pacing, or flow control;
- complete echoed input followed by wrong execution suggests image/CPU/ABI
  compatibility rather than transport.

Gate: one small, one UART, and one 100+ line supplied image load and run.

## Phase 4: validate the sw-embed assembler

Do not start with Forth.  First create a minimal assembly program with an
observable UART or LED result, assemble it with `cor24-asm --lgo-full`, and:

1. inspect that its last record is `G...`;
2. run that exact `.lgo` in `cor24-emu`;
3. load that exact `.lgo` on the board;
4. compare behavior and preserve the artifact.

Where an equivalent reference `.s` or listing exists, compare emitted bytes
and entry addresses with the MakerLisp toolchain.  Differences must be
explained before moving to C.

Gate: identical `.lgo` passes in emulator and hardware, including after a warm
reload when emitted with `--lgo-full`.

### Validate stack locations and capacities

The detailed specification is in `reference/stack-validation.md`; generated
artifacts and exact results are in `reference/stack-tests/README.md`.

Three five-line, 135-byte machine-code probes exist:

| Image | Initial SP | Stack exercised | Purpose |
| --- | ---: | ---: | --- |
| `stack-ebr3.lgo` | `0xFEEC00` | 3,000 bytes | Current 3 KB EBR baseline |
| `stack-sram.lgo` | `0x100000` | 6,144 bytes | Stack in the top of 1 MB SRAM |
| `stack-ebr8.lgo` | `0xFF0000` | 6,000 bytes | Future/emulated 8 KB EBR |

Run the bounded emulator matrix:

```bash
timeout --signal=INT --kill-after=2s 10s \
  work/bin/cor24-emu \
  --lgo reference/stack-tests/stack-ebr3.lgo \
  --speed 0 --max-instructions 100000 --quiet

timeout --signal=INT --kill-after=2s 10s \
  work/bin/cor24-emu \
  --lgo reference/stack-tests/stack-sram.lgo \
  --speed 0 --max-instructions 100000 --quiet

timeout --signal=INT --kill-after=2s 10s \
  work/bin/cor24-emu \
  --lgo reference/stack-tests/stack-ebr8.lgo \
  --speed 0 --max-instructions 100000 --quiet

timeout --signal=INT --kill-after=2s 10s \
  work/bin/cor24-emu \
  --lgo reference/stack-tests/stack-ebr8.lgo \
  --stack-kilobytes 8 \
  --speed 0 --max-instructions 100000 --quiet
```

Expected emulator results:

- 3 KB EBR prints `STACK EBR3 PASS`;
- SRAM is currently rejected after two instructions because the emulator
  incorrectly imposes EBR-only stack bounds;
- 8 KB EBR is rejected in default 3 KB mode;
- 8 KB EBR prints `STACK EBR8 PASS` with `--stack-kilobytes 8`.

The physical-board matrix is:

1. 3 KB EBR must print `STACK EBR3 PASS`.
2. SRAM must print `STACK SRAM PASS`.
3. 8 KB EBR must not print `STACK EBR8 PASS` on current hardware.

The 8 KB probe uses the future range immediately: it sets `sp = 0xFF0000`,
pushes 2,000 words, and reaches `0xFEE890`. Merely assigning SP should not trap
on hardware. The released design routes `0xFE....` through a 12-bit EBR
address into 3,072 implemented rows, so unimplemented offsets and address
aliasing should corrupt sentinel readback. On mismatch the probe resets SP to
the known-valid `0xFEEC00` before reporting. The preferred result is
`STACK EBR8 FAIL`; a hang, reset, partial text, or no text is also possible.
`STACK EBR8 PASS` is categorically unexpected.

Gate: all six hardware/emulator rows are recorded. An emulator that prevents
an SRAM-based stack is defective even if its 3 KB and 8 KB EBR modes pass.

## Phase 5: validate Tiny C (`.c -> .s -> .lgo`)

Use this explicit pipeline:

```bash
tc24r sample.c -o sample.s
cor24-asm sample.s -o sample.lgo --lgo-full \
  --bin sample.bin --listing sample.lst
cor24-emu --lgo sample.lgo --speed 0 --max-instructions 1000000 --dump
```

Then load the same `sample.lgo` with the proven `te-rs` command.  Start with a
minimal return/halt program, then LED, polled UART output, UART echo, and an
interrupt example.  Only after these pass should larger compiler demos be
tried.

Gate: at least one GPIO program and one UART program compiled by `tc24r` behave
the same in emulator and hardware.

## Phase 6: load interpreters

### Forth

`forth.s` is already the native interpreter image; it does not need a second
guest image:

```bash
cor24-asm work/dcfth/github/sw-embed/sw-cor24-forth/forth.s \
  -o /tmp/forth.lgo --lgo-full
cor24-emu --lgo /tmp/forth.lgo --uart-input '2 3 + .\n' \
  --speed 0 --max-instructions 5000000
```

Load `/tmp/forth.lgo` only after the smaller assembler artifacts pass.  On
hardware, exercise `VER`, `2 3 + .`, a new colon definition, `SW?`, and
`LED!`.  The built-in monitor is sufficient to start the Forth `.lgo`; no
extra monitor should be assumed necessary.

### Macro Lisp

Build the smallest REPL before the standard/full variants:

```bash
cd work/dcmls/github/sw-embed/sw-cor24-macrolisp
just build-minimal
cor24-emu --lgo build/repl-minimal.lgo --terminal --echo --speed 0
```

Then load the same `build/repl-minimal.lgo` on hardware.  Record image size and
upload duration.  Test a literal, arithmetic, definition, and UART output
before attempting standard preludes or snapshots.

Gate: Forth and minimal Lisp reach their prompt and evaluate a saved smoke
script on both emulator and board.

## Phase 7: decide whether a resident sw-embed load-and-go image is needed

Do not implement a new loader merely because large uploads fail.  First use
the line-by-line `te-rs` evidence to locate the failure.

A resident loader becomes justified if the board monitor reliably loads small
images but cannot reliably accept the validated larger record stream despite
proven RTS/CTS and pacing.  Candidate architecture:

1. load one small, reference-compatible resident loader with the ROM monitor;
2. have it receive a framed binary protocol with address, length, checksum,
   and explicit ACK/NAK;
3. move record parsing, retry, and final jump into COR24 code;
4. use one host sender for both physical UART and emulator tests.

The existing MakerLisp `loadngo.c`, `loadngo.lst`, and `loadngo.mem` are useful
behavioral references, but there is no copied `loadngo.lgo`.  Determine how
the delivered FPGA boots the monitor before assuming that `.mem` can be
installed through the current ROM monitor.

## Phase 8: rebuild and compare MakerLisp `cc24`

This is deliberately last.  First reproduce a supplied `.c -> .lgo` demo with
the supplied `cc24/as24/longlgo` Makefile and compare it against the shipped
`.lgo`.  Then compile the same deliberately small C subset with `tc24r`.

Compare:

- entry point and startup sequence;
- stack initialization and ABI;
- code/data addresses and zero-fill;
- MMIO sequences;
- UART polling/interrupt behavior;
- binary size and observable output.

The goal is behavioral and ABI compatibility, not byte identity between two
different C compilers.

## Completion criteria

The integration plan is complete when results contain:

- a proven direct-connect UART adapter and repeatable cold-start procedure;
- reference `.lgo` results in both emulator and hardware;
- a `cor24-asm` assembly artifact run unchanged in both;
- a `tc24r -> cor24-asm` GPIO and UART artifact run unchanged in both;
- Forth and minimal Macro Lisp smoke tests on hardware;
- a documented decision, backed by transport logs, on whether a resident
  loader is necessary;
- a scoped emulator defect list, including disposition of `uartintr.lgo`.

## Open decisions

1. What exact adapter chipsets/modules are available for the test matrix?
2. Is the FPGA still running the factory 2026-07-15 image, and is there a safe
   recovery/reprogramming path if a monitor experiment changes it?
3. Where should hardware result logs and generated golden artifacts live:
   this integration repository or the owning tool repositories?
4. Is byte-for-byte compatibility with MakerLisp `as24` required for ordinary
   syntax, or is matching execution on the released CPU the acceptance rule?
