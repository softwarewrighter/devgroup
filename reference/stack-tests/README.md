# COR24 stack test artifacts

These standalone probes implement the test-image specification in
[`../stack-validation.md`](../stack-validation.md). They were generated with
the workspace `work/bin/cor24-asm` and do not modify any `d*` product
repository.

## Artifacts

| Test | Initial SP | Exercised storage | Expected current hardware | Expected emulator |
| --- | ---: | ---: | --- | --- |
| `stack-ebr3` | `0xFEEC00` | 3,000 bytes | Pass | Pass in default mode |
| `stack-sram` | `0x100000` | 6,144 bytes | Pass | Pass after SRAM bounds bug is fixed |
| `stack-ebr8` | `0xFF0000` | 6,000 bytes | Must not pass on 3 KB EBR | Pass with `--stack-kilobytes 8` |

Every probe:

- initializes SP explicitly;
- pushes increasing 24-bit sentinel values;
- pops them in reverse order and verifies every value;
- verifies that SP returns to its initial value;
- calls a polled-UART output routine, which also uses the selected stack;
- prints one CRLF-terminated `PASS` or `FAIL` line;
- finishes in a stable loop.

Each `.lgo` is a full five-line image containing 135 machine-code bytes. Its
last record is `G000000`.

## Rebuild

From the devgroup repository root:

```bash
for name in stack-ebr3 stack-sram stack-ebr8; do
  work/bin/cor24-asm "reference/stack-tests/$name.s" \
    -o "reference/stack-tests/$name.lgo" \
    --lgo-full \
    --bin "reference/stack-tests/$name.bin" \
    --listing "reference/stack-tests/$name.lst"
done
```

## Emulator commands

### 3 KB EBR

```bash
work/bin/cor24-emu \
  --lgo reference/stack-tests/stack-ebr3.lgo \
  --speed 0 \
  --max-instructions 100000 \
  --quiet
```

Observed:

```text
STACK EBR3 PASS
Executed 12289 instructions
```

### SRAM stack

This is the required command once the emulator supports bounds for an SRAM
stack:

```bash
work/bin/cor24-emu \
  --lgo reference/stack-tests/stack-sram.lgo \
  --stack-base 0x0F0000 \
  --stack-top 0x100000 \
  --speed 0 \
  --max-instructions 100000 \
  --quiet
```

`--stack-base` and `--stack-top` are proposed options and are not implemented
by the current workspace binary.

The current emulator instead stops after the program assigns SP:

```text
Stack overflow: SP=0x100000 below stack base
Executed 2 instructions
```

That is the EBR-only stack-guard bug this probe is intended to demonstrate.

### 8 KB EBR

```bash
work/bin/cor24-emu \
  --lgo reference/stack-tests/stack-ebr8.lgo \
  --stack-kilobytes 8 \
  --speed 0 \
  --max-instructions 100000 \
  --quiet
```

Observed:

```text
STACK EBR8 PASS
Executed 24289 instructions
```

Without `--stack-kilobytes 8`, the expected emulator diagnostic is:

```text
Stack underflow: SP=0xFF0000 above stack top
Executed 2 instructions
```

## Hardware order

Upload these exact full images in order:

1. `stack-ebr3.lgo` - must print `STACK EBR3 PASS`;
2. `stack-sram.lgo` - must print `STACK SRAM PASS`;
3. `stack-ebr8.lgo` - must not print `STACK EBR8 PASS` on the current 3 KB
   board.

Use the proven cold-start and `te-rs` synchronization procedure for each
image. Reset the board between images. Preserve the upload transcript and
UART output, especially for the 8 KB negative test: current hardware may hang,
reset, return incorrect data and print `FAIL`, or exhibit another result. Only
an 8 KB `PASS` is categorically unexpected.

The 8 KB probe sets `sp = 0xFF0000` and immediately uses the future 8 KB
range. It does not start at `0xFEEC00` and overflow downward. It pushes 2,000
words down to `0xFEE890`.

Assigning SP itself is not expected to trap on current hardware; the CPU has
no emulator-style stack guard. The released Verilog routes every address whose
top byte is `0xFE` to EBR, passes only the low 12 address bits to the EBR, and
instantiates 3,072 rows. The probe therefore traverses unimplemented low-12-bit
offsets and crosses a 4 KB low-address wrap where implemented EBR may alias.
Sentinel verification should fail.

The failure path resets SP to the known-valid `0xFEEC00` before printing.
Therefore, the most useful expected hardware result is:

```text
STACK EBR8 FAIL
```

Undefined EBR reads could instead produce a hang, reset, partial text, or no
text. `STACK EBR8 PASS` is the only categorically unexpected result.

## Artifact hashes

```text
c92475c2979bc3d839ff508226944c6b11a030351ccfdd97e017dc601a10dc9b  stack-ebr3.s
db73aa25169b0dffc438d267143e5ece5f0044ebc77c87272098431c9ab56156  stack-ebr3.lgo
c3b0e2a6a96d6ec3c6ac7972bb0b89b181de23595fda32181c56ca02be554046  stack-sram.s
30a7bc5dd4c3ebf7998dd3a1cb1161c2a7e5f575b05cb6b342c166f1b68e361d  stack-sram.lgo
311073fd0e67ad6850466d14a6b3f1e5c1eed4b0da07dfc0983fa184a8cf257d  stack-ebr8.s
5a8138d238ad5a6ff83e65c9efd8ab00753fdbb0ada8dc2758dd48e7cc3e0a42  stack-ebr8.lgo
```
