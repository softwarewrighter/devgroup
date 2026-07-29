# COR24 stack-location validation specification

## Scope

This document specifies three future `.lgo` test images and the six-result
hardware/emulator validation matrix. It does not implement the images or
change emulator behavior.

The central requirement is:

> Stack capacity and stack location are independent choices. A COR24 program
> may use the hardware EBR stack or place its stack in the 1 MB SRAM. The
> emulator must not reject a valid SRAM stack merely because it is outside
> EBR.

## Memory model

The relevant address regions are:

| Region | Address range | Size | Purpose |
| --- | --- | ---: | --- |
| SRAM | `0x000000`-`0x0FFFFF` | 1 MB | Code, static data, heap/arena, or an SRAM stack |
| Current EBR stack | `0xFEE000`-`0xFEEBFF` | 3 KB | Stack in the current COR24-TB |
| Future/emulated EBR extension | `0xFEEC00`-`0xFEFFFF` | 5 KB additional | Extends EBR to 8 KB |
| Full 8 KB EBR | `0xFEE000`-`0xFEFFFF` | 8 KB | Future hardware and emulator mode |
| I/O | starts at `0xFF0000` | N/A | Must not be used as stack storage |

COR24 stacks grow downward. Therefore:

- the 3 KB EBR stack starts with `sp = 0xFEEC00`;
- the 8 KB EBR stack starts with `sp = 0xFF0000`;
- a stack using all available SRAM starts with `sp = 0x100000`, one byte past
  the highest SRAM address;
- the first 24-bit push stores at `sp - 3`.

An application will normally reserve lower SRAM for code and static data and
place heap or arena allocations between the end of static storage and the
descending SRAM stack. A practical runtime must also detect collision between
those growing regions.

## Current emulator behavior and bug

The emulator currently initializes stack checking to:

```text
base = 0xFEE000
top  = 0xFEEC00
```

It reports:

- stack overflow if `sp < base`;
- stack underflow if `sp > top`.

This detects overflow for the default 3 KB EBR configuration, but it also
assumes that every program uses EBR. A program that deliberately executes:

```asm
la  r0,0x100000
mov sp,r0
```

is stopped because `0x100000 < 0xFEE000`, even though the next downward push
would write to valid SRAM at `0x0FFFFD`.

That is a bug under the required memory model. Bounds checking must describe
the selected stack region, not impose EBR as the only legal stack location.

The supplied `reference/uartintr.lgo` demonstrates the same class of problem
with a smaller SRAM stack. Its entry sequence sets `sp = 0x002000`, after
which the emulator stops with:

```text
Stack overflow: SP=0x002000 below stack base
```

This result does not establish that the image would fail on hardware. It
establishes that the emulator's EBR-only guard prevents the image from being
tested.

## Required test images

The synthesized images and their `.s`, `.lgo`, `.bin`, and `.lst` outputs are
in [`stack-tests/`](stack-tests/). They are assembled with
`cor24-asm --lgo-full`.

Each image must:

1. set its intended initial SP explicitly;
2. print a short test identifier over the polled UART;
3. exercise stack memory rather than merely assigning SP;
4. use multiple distinct 24-bit sentinel values;
5. pop or directly read the sentinels and compare them;
6. print `PASS` only after every comparison succeeds;
7. print `FAIL` if a readable but incorrect value is returned;
8. finish in a stable loop so UART output can be captured.

The tests must not depend on UART interrupts. UART transmission is only the
result channel.

### Image A: 3 KB EBR stack

Artifact:

```text
stack-tests/stack-ebr3.lgo
```

Configuration:

```text
initial SP: 0xFEEC00
valid base: 0xFEE000
valid top:  0xFEEC00
```

The program should verify:

- an ordinary push/pop near the top;
- storage near the bottom of the 3 KB region;
- at least one nested call using the normal call convention;
- SP restoration to `0xFEEC00`.

It should not write below `0xFEE000`.

Expected UART result:

```text
STACK EBR3 PASS
```

This is the compatibility baseline for the current board.

### Image B: SRAM stack

Artifact:

```text
stack-tests/stack-sram.lgo
```

Configuration:

```text
initial SP: 0x100000
example base: 0x0F0000
valid top:   0x100000
capacity:    64 KB in this test
```

The 64 KB test window is intentionally smaller than the maximum possible SRAM
stack. It stays well above small code and data images while proving more than
3 KB of usable stack.

The program should verify:

- the first push at `0x0FFFFD`;
- nested calls with SP in SRAM;
- successful use of more than 3 KB, preferably at least 6 KB;
- sentinel readback near both ends of the exercised range;
- SP restoration to `0x100000`.

Expected UART result:

```text
STACK SRAM PASS
```

This test proves that SRAM can be used as a conventional downward-growing
stack. It does not by itself validate heap/stack collision detection.

### Image C: 8 KB EBR stack

Artifact:

```text
stack-tests/stack-ebr8.lgo
```

Configuration:

```text
initial SP: 0xFF0000
valid base: 0xFEE000
valid top:  0xFF0000
```

The program must exercise addresses in the additional 5 KB region above
`0xFEEC00`; otherwise it would not distinguish 8 KB EBR from the existing
3 KB hardware.

It should:

- push and verify sentinels immediately below `0xFF0000`;
- use more than 3 KB of total stack;
- verify a sentinel below `0xFEEC00`;
- restore SP to `0xFF0000`.

Expected UART result in 8 KB emulator mode:

```text
STACK EBR8 PASS
```

The current 3 KB COR24-TB is expected not to print `PASS`. Its exact failure
mode must be recorded rather than assumed; unimplemented EBR addresses may
return zeros, alias, ignore writes, or otherwise behave differently.

## Six-case acceptance matrix

| Case | Image | Environment | Required configuration | Expected result |
| --- | --- | --- | --- | --- |
| a | `stack-ebr3.lgo` | Current COR24-TB | Factory 3 KB EBR | `STACK EBR3 PASS` |
| b | `stack-ebr3.lgo` | Emulator | 3 KB EBR mode | `STACK EBR3 PASS` |
| c | `stack-sram.lgo` | Current COR24-TB | SRAM stack window | `STACK SRAM PASS` |
| d | `stack-sram.lgo` | Emulator | Matching SRAM stack bounds | `STACK SRAM PASS` |
| e | `stack-ebr8.lgo` | Current COR24-TB | Factory 3 KB EBR | Must not print `STACK EBR8 PASS` |
| f | `stack-ebr8.lgo` | Emulator | 8 KB EBR mode | `STACK EBR8 PASS` |

Case e should be described as an expected capability failure, not necessarily
a clean trap. The current hardware has no requirement to diagnose access to
unimplemented EBR.

## Required emulator semantics

The existing option:

```text
--stack-kilobytes <3|8>
```

expresses EBR capacity but does not express an SRAM stack location. A complete
interface needs either named modes or explicit bounds.

One possible named interface is:

```text
--stack-region ebr3
--stack-region ebr8
--stack-region sram
```

with meanings:

| Mode | Base | Top | Initial SP override |
| --- | ---: | ---: | --- |
| `ebr3` | `0xFEE000` | `0xFEEC00` | optional `0xFEEC00` |
| `ebr8` | `0xFEE000` | `0xFF0000` | optional `0xFF0000` |
| `sram` | application-selected | application-selected | normally set by program startup |

An explicit interface is more flexible:

```text
--stack-base 0x0F0000 --stack-top 0x100000
```

The following behavioral rules are required regardless of spelling:

1. Default mode remains hardware-accurate 3 KB EBR.
2. The 8 KB mode provides and bounds the full `0xFEE000`-`0xFEFFFF` region.
3. SRAM mode accepts a descending stack wholly inside the 1 MB SRAM.
4. Stack bounds are checked after the program has selected its runtime SP.
5. Assigning SP to the configured top is valid.
6. A push that moves SP below the configured base reports overflow.
7. A pop that moves SP above the configured top reports underflow.
8. Stack configuration must not silently relocate code or static data.
9. Loading an `.lgo` must not discard the selected stack configuration.
10. A program's explicit SP initialization and a CLI SP override must have
    documented precedence.

For these tests, the `.lgo` image should set SP explicitly. The CLI should
configure valid bounds and memory capacity, not replace the program's startup
policy.

## Proposed emulator commands

These commands are specifications for the eventual interface, not commands
that are guaranteed to work today:

```bash
# Case b: current 3 KB EBR
work/bin/cor24-emu --lgo stack-ebr3.lgo \
  --stack-region ebr3 --max-instructions 1000000 --quiet

# Case d: 64 KB SRAM stack at the top of SRAM
work/bin/cor24-emu --lgo stack-sram.lgo \
  --stack-base 0x0F0000 --stack-top 0x100000 \
  --max-instructions 5000000 --quiet

# Case f: future 8 KB EBR
work/bin/cor24-emu --lgo stack-ebr8.lgo \
  --stack-region ebr8 --max-instructions 5000000 --quiet
```

If the existing spelling is retained for EBR, case f may instead use:

```bash
work/bin/cor24-emu --lgo stack-ebr8.lgo \
  --stack-kilobytes 8 --max-instructions 5000000 --quiet
```

The SRAM case still needs explicit support.

## Hardware procedure

For each image:

1. Cold-start the board using the documented J1/UART sequence.
2. Capture the monitor banner.
3. Upload the full `.lgo` with the proven `te-rs` pacing/synchronization
   configuration.
4. Preserve the exact upload transcript.
5. Capture UART output after the `G` record executes.
6. Reset before loading the next image.

Run the hardware tests in this order:

1. 3 KB EBR baseline;
2. SRAM stack;
3. 8 KB EBR negative test.

Do not run the 8 KB negative test until the first two pass. That ordering
ensures a missing `PASS` is attributable to EBR capacity rather than loader,
UART, assembler, or general stack-operation failure.

## Evidence to preserve

For each test, record:

- source and artifact hashes;
- assembler version;
- `.lgo` line count and final `G` record;
- initial SP and lowest observed SP;
- configured stack base and top;
- emulator command or hardware revision;
- complete UART output;
- whether failure was a guard diagnostic, bad readback, reset, hang, or other
  behavior.

The minimum completion evidence is the six populated rows of the acceptance
matrix with links to their transcripts.
