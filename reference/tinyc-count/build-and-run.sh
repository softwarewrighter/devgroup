#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/../.." && pwd)
build_dir="$script_dir/build"

tc24r="$repo_root/work/bin/tc24r"
cor24_asm="$repo_root/work/bin/cor24-asm"
cor24_emu="$repo_root/work/bin/cor24-emu"

mkdir -p "$build_dir"

"$tc24r" "$script_dir/count.c" -o "$build_dir/count.s"
"$cor24_asm" "$build_dir/count.s" \
    -o "$build_dir/count.lgo" \
    --lgo-full \
    --bin "$build_dir/count.bin" \
    --listing "$build_dir/count.lst"

echo "Built:"
wc -c -l \
    "$build_dir/count.s" \
    "$build_dir/count.lgo" \
    "$build_dir/count.bin" \
    "$build_dir/count.lst"
echo
echo "Final load-and-go record:"
tail -n 1 "$build_dir/count.lgo"
echo
echo "Running in cor24-emu (Ctrl-C stops it):"
exec "$cor24_emu" \
    --lgo "$build_dir/count.lgo" \
    --speed 0 \
    --max-instructions 5000000 \
    --quiet
