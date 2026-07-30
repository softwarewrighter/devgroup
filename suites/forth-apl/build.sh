#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$script_dir/build"
assembler=/disk1/github/softwarewrighter/devgroup/work/bin/cor24-asm

mkdir -p "$build_dir"

"$assembler" "$script_dir/src/monitor.s" \
    --base-addr 0x000000 --no-entry --lgo-full \
    -o "$build_dir/monitor.lgo" \
    --bin "$build_dir/monitor.bin" \
    --listing "$build_dir/monitor.lst"

"$assembler" "$script_dir/src/apl.s" \
    --base-addr 0x001000 --no-entry --lgo-full \
    -o "$build_dir/apl.lgo" \
    --bin "$build_dir/apl.bin" \
    --listing "$build_dir/apl.lst"

"$assembler" "$script_dir/src/forth.s" \
    --base-addr 0x020000 --no-entry --lgo-full \
    -o "$build_dir/forth.lgo" \
    --bin "$build_dir/forth.bin" \
    --listing "$build_dir/forth.lst"

monitor_bytes=$(wc -c < "$build_dir/monitor.bin")
apl_bytes=$(wc -c < "$build_dir/apl.bin")
forth_bytes=$(wc -c < "$build_dir/forth.bin")

if (( monitor_bytes > 2048 )); then
    echo "monitor overlaps RX broker state at 0x000800" >&2
    exit 1
fi
if (( 4096 + apl_bytes > 131072 )); then
    echo "APL overlaps Forth base at 0x020000" >&2
    exit 1
fi
if (( 131072 + forth_bytes >= 983040 )); then
    echo "Forth initial dictionary overlaps return stack at 0x0F0000" >&2
    exit 1
fi

awk '/^L/' \
    "$build_dir/monitor.lgo" \
    "$build_dir/apl.lgo" \
    "$build_dir/forth.lgo" \
    > "$build_dir/forth-apl-suite.lgo"
printf 'G000000\n' >> "$build_dir/forth-apl-suite.lgo"

echo "Built COR24 interrupt-owned Forth/APL suite:"
wc -c -l \
    "$build_dir/monitor.bin" \
    "$build_dir/apl.bin" \
    "$build_dir/forth.bin" \
    "$build_dir/forth-apl-suite.lgo"
echo
printf 'monitor: 0x000000-0x%06X\n' "$((monitor_bytes - 1))"
printf 'APL:     0x001000-0x%06X\n' "$((4096 + apl_bytes - 1))"
printf 'Forth:   0x020000-0x%06X\n' "$((131072 + forth_bytes - 1))"
printf 'Forth dictionary ceiling / return stack: 0x0F0000\n'
echo
tail -n 1 "$build_dir/forth-apl-suite.lgo"
