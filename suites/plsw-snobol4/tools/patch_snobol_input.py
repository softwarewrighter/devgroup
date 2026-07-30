#!/usr/bin/env python3
"""Redirect only SNOBOL4 READ_INPUT/READ_RAW_INPUT UART calls."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("input")
parser.add_argument("output")
parser.add_argument("target", type=lambda value: int(value, 0))
args = parser.parse_args()

image = bytearray(Path(args.input).read_bytes())

# The checked-in SNOBOL image has sno_util at 0x09EBDB. These are the
# two link fixups emitted for MONITOR_GETCHAR in a source-identical rebuild.
fixups = (0x09ED2A, 0x09EFB2)
for instruction in fixups:
    if image[instruction] != 0x2B:  # la r2,imm24
        raise SystemExit(f"unexpected opcode at 0x{instruction:06X}")
    address = instruction + 1
    old = int.from_bytes(image[address:address + 3], "little")
    if old != 0x24:  # standalone _UART_GETCHAR
        raise SystemExit(f"unexpected call target 0x{old:06X} at 0x{address:06X}")
    image[address:address + 3] = args.target.to_bytes(3, "little")

Path(args.output).write_bytes(image)
