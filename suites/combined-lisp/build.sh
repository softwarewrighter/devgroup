#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$script_dir/build"
tool_root=/disk1/github/softwarewrighter/devgroup/work/bin

mkdir -p "$build_dir"

"$tool_root/tc24r" "$script_dir/src/combined-suite.c" \
    -o "$build_dir/combined-suite.s"
"$tool_root/cor24-asm" "$build_dir/combined-suite.s" \
    -o "$build_dir/combined-suite.lgo" \
    --lgo-full \
    --bin "$build_dir/combined-suite.bin" \
    --listing "$build_dir/combined-suite.lst"
"$tool_root/cor24-asm" "$build_dir/combined-suite.s" \
    -o "$build_dir/combined-suite-compact.lgo" \
    --lgo-compact

echo "Built COR24 combined Lisp suite:"
wc -c -l \
    "$build_dir/combined-suite.s" \
    "$build_dir/combined-suite.lgo" \
    "$build_dir/combined-suite-compact.lgo" \
    "$build_dir/combined-suite.bin" \
    "$build_dir/combined-suite.lst"
echo
echo "Final record:"
tail -n 1 "$build_dir/combined-suite.lgo"
