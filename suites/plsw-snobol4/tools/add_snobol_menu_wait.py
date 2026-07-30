#!/usr/bin/env python3
"""Add a monitor-aware wait before a batch SNOBOL program's final END."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("input")
parser.add_argument("output")
args = parser.parse_args()

source = Path(args.input).read_text(encoding="utf-8")
# Batch demos commonly branch explicitly to their terminating END label.
# Route those exits through the generated wait instead.
source = source.replace(":(END)", ":(MONWAIT)")
lines = source.splitlines()
end_lines = [index for index, line in enumerate(lines) if line.strip() == "END"]
if len(end_lines) != 1:
    raise SystemExit(f"{args.input}: expected exactly one standalone END line")

index = end_lines[0]
epilogue = [
    "* Suite wrapper: batch work is complete; wait in centralized INPUT",
    "* so the monitor-owned Ctrl-] attention flag can be observed.",
    "MONWAIT OUTPUT = 'Press Ctrl-] to return to the menu.'",
    "MWLOOP  MWLINE = INPUT :F(MWLOOP)",
    "        :(MWLOOP)",
]
wrapped = lines[:index] + epilogue + lines[index:]
Path(args.output).write_text("\n".join(wrapped) + "\n", encoding="utf-8")
