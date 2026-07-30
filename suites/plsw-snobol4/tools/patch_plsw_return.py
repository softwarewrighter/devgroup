#!/usr/bin/env python3
"""Make a generated finite PL/SW demo return to the resident monitor."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("assembly")
parser.add_argument("monitor_address", type=lambda value: int(value, 0))
args = parser.parse_args()

path = Path(args.assembly)
source = path.read_text(encoding="utf-8")
old = "_halt:\n        bra     _halt"
new = f"_halt:\n        la      ir,0x{args.monitor_address:06X}"
if source.count(old) != 1:
    raise SystemExit(f"{path}: expected exactly one generated _halt loop")
path.write_text(source.replace(old, new), encoding="utf-8")
