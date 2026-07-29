# COR24 integration status

Status date: 2026-07-29

This is the evidence ledger for `reference/plan.md`. Emulator success does not
count as hardware validation until the corresponding hardware checkbox is
completed.

## Completed analysis and documentation

- [x] Surveyed the COR24 release, manuals/release notes, symlinked release
  tree, `te.c`, experimental `te-rs`, and the `work/dc*` tool repositories.
- [x] Identified `work/dcxas`/`cor24-asm` as the current host `.s -> .lgo`
  assembler.
- [x] Identified `work/dcasm` as the future self-hosted assembler, not the
  current host artifact producer.
- [x] Documented the staged integration plan in `reference/plan.md`.
- [x] Documented stack requirements and the six-case matrix in
  `reference/stack-validation.md`.
- [x] Documented bounded command policy: emulator instruction limit plus GNU
  `timeout`.
- [x] Kept product changes out of the `d*` repositories; those remain for
  their coding agents and AgentRail workflows.

## Created Tiny C UART counter

Source and build instructions:

- `reference/tinyc-count/count.c`
- `reference/tinyc-count/build-and-run.sh`
- `reference/tinyc-count/README.md`

Generated artifacts:

| Artifact | Size |
| --- | ---: |
| `build/count.s` | 5,700 bytes |
| `build/count.bin` | 389 bytes |
| `build/count.lgo` | 874 bytes, 12 lines |
| `build/count.lst` | 9,226 bytes |

The `.lgo` is full, not compact, and ends with `G000000`.

Observed emulator result:

```text
0
1
Executed 5000000 instructions
```

The program is intentionally infinite; the emulator instruction limit ends
the test. On hardware it prints increasing CRLF-terminated decimal numbers
with an approximate software delay.

## Created stack probes

All three probes explicitly initialize SP, push distinct 24-bit sentinels,
pop and verify every sentinel, verify restored SP, print through polled UART,
and finish in a stable loop.

| Source/image | Machine code | LGO form | Stack exercised |
| --- | ---: | ---: | ---: |
| `stack-tests/stack-ebr3.{s,lgo}` | 135 bytes | 5 lines | 3,000-byte EBR |
| `stack-tests/stack-sram.{s,lgo}` | 135 bytes | 5 lines | 6,144-byte SRAM |
| `stack-tests/stack-ebr8.{s,lgo}` | 135 bytes | 5 lines | 6,000-byte EBR |

Each image ends with `G000000`. Corresponding `.bin` and `.lst` files are also
preserved. Artifact hashes are recorded in
`reference/stack-tests/README.md`.

## Emulator tests performed

The commands below have deterministic instruction limits. Future repetitions
must also be wrapped in:

```bash
timeout --signal=INT --kill-after=2s 10s COMMAND
```

### Supplied MakerLisp images

- [x] `reference/hello.lgo`

  Observed `hello world`; 3,683 instructions.

- [x] `reference/blinky.lgo --switch on`

  The pressed active-low switch produced LED value `0x00` and the live
  red-circle display.

- [x] `reference/blinky.lgo --switch off`

  With `--max-instructions 1000 --dump`, observed LED `0x01` off, switch
  `0x01` released, and `Halted: false`. The apparent hang without a limit is
  expected because blinky loops forever and the unchanged off state produces
  no live LED transition.

- [x] `reference/uartintr.lgo`

  The entry sequence assigns `sp = 0x002000`. The emulator then stops after
  two instructions with `Stack overflow: SP=0x002000 below stack base`. This
  is evidence of the same EBR-only emulator stack guard, not proof that the
  supplied image fails on hardware.

### Tiny C counter

- [x] Compiled `count.c` with `work/bin/tc24r`.
- [x] Assembled with `work/bin/cor24-asm --lgo-full`.
- [x] Ran in `work/bin/cor24-emu`.
- [x] Observed increasing UART output before the instruction limit.

### Stack matrix

- [x] 3 KB EBR in default emulator mode.

  ```text
  STACK EBR3 PASS
  Executed 12289 instructions
  ```

- [x] SRAM stack in current default emulator.

  ```text
  Stack overflow: SP=0x100000 below stack base
  Executed 2 instructions
  ```

  This is the known emulator defect. The image has not yet been run in an
  emulator with configurable SRAM stack bounds.

- [x] 8 KB EBR image in default 3 KB emulator mode.

  ```text
  Stack underflow: SP=0xFF0000 above stack top
  Executed 2 instructions
  ```

- [x] 8 KB EBR image with `--stack-kilobytes 8`.

  ```text
  STACK EBR8 PASS
  Executed 24289 instructions
  ```

## Hardware validation checklist

### Host, adapter, and monitor

- [ ] Record board revision and confirm the installed FPGA image/release.
- [ ] Record each UART adapter chipset, VID:PID, driver, and stable
  `/dev/serial/by-id/...` path.
- [ ] Bypass USB hubs for baseline testing.
- [ ] Verify TX/RX and RTS/CTS wiring and 3.3 V signaling.
- [ ] Verify 921600 baud, 8N1, raw mode, and `crtscts`.
- [ ] Perform the documented cold-start sequence.
- [ ] Obtain ten repeatable monitor banners from cold starts.
- [ ] Run adapter loopback and an RTS/CTS blocking test.
- [ ] Build and test `te-rs`.
- [ ] Establish a reliable `te-rs --sync --verbose --delay 10` upload.
- [ ] Reduce pacing through 5, 2, 1, and 0 ms while preserving transcripts.

### Supplied reference images

- [ ] Load and operate `blinky.lgo`.
- [ ] Load `hello.lgo` and capture `hello world`.
- [ ] Load and test `uartintr.lgo` with a short string.
- [ ] Test `uartintr.lgo` with the supplied `test.txt`.
- [ ] Load one 100+ line supplied image.
- [ ] Record whether failures occur at fixed or moving source lines.

### Tiny C counter

- [ ] Upload the exact `reference/tinyc-count/build/count.lgo`.
- [ ] Observe ordered increasing numbers on UART.
- [ ] Record upload transcript and UART output.
- [ ] Reset the board and confirm return to the monitor.

### Stack probes

- [ ] Upload `stack-ebr3.lgo`.
- [ ] Observe `STACK EBR3 PASS`.
- [ ] Upload `stack-sram.lgo`.
- [ ] Observe `STACK SRAM PASS`.
- [ ] Upload `stack-ebr8.lgo`.
- [ ] Record the exact negative-test behavior.
- [ ] Preferably observe `STACK EBR8 FAIL`.
- [ ] Accept hang, reset, partial text, or no text as evidence requiring
  classification.
- [ ] Treat `STACK EBR8 PASS` as unexpected and investigate EBR
  implementation/aliasing.

### Larger sw-embed applications

- [ ] Build and upload a minimal hand-written assembly UART image.
- [ ] Build and upload a minimal Tiny C GPIO image.
- [ ] Build and upload a Tiny C UART echo image.
- [ ] Assemble and upload the Forth kernel.
- [ ] Reach the Forth prompt and run the documented smoke commands.
- [ ] Build and upload the minimal Macro Lisp REPL.
- [ ] Reach the Lisp prompt and evaluate a saved smoke script.

## Known open issues

1. The emulator rejects an explicitly selected SRAM stack because default
   bounds require SP to remain in EBR.
2. The supplied `uartintr.lgo` cannot currently be meaningfully tested in the
   emulator for the same reason.
3. `te-rs` pacing/synchronization has not yet been validated against hardware.
4. No sw-embed-generated `.lgo` has yet been validated on the COR24-TB.
5. The exact current-hardware behavior of unimplemented 8 KB EBR addresses is
   intentionally left for the negative hardware test.
