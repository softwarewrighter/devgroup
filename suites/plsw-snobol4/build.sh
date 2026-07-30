#!/usr/bin/env bash
set -euo pipefail

suite_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$suite_dir/build"
tool_dir=/disk1/github/softwarewrighter/devgroup/work/bin
plsw_repo=/disk1/github/softwarewrighter/devgroup/work/dcpls/github/sw-embed/sw-cor24-plsw
snobol_repo=/disk1/github/softwarewrighter/devgroup/work/dcsno/github/sw-embed/sw-cor24-snobol4
mkdir -p "$build_dir"

# Discover the complete zero-only records before assembling the monitor so
# its initializer can consume the generated range table.
python3 "$suite_dir/tools/bin_to_lgo.py" \
    "$snobol_repo/build/snobol4.bin" 0 "$build_dir/snobol4-zero-scan.lgo" \
    --skip-zero --ranges-asm "$build_dir/snobol-zero-ranges.s"

{
    sed -n '1,$p' "$suite_dir/src/monitor.s"
    sed -n '1,$p' "$build_dir/snobol-zero-ranges.s"
} > "$build_dir/monitor-generated.s"

"$tool_dir/cor24-asm" "$build_dir/monitor-generated.s" \
    --base-addr 0x0D0000 --no-entry --lgo-full \
    -o "$build_dir/monitor.lgo" --bin "$build_dir/monitor.bin" \
    --listing "$build_dir/monitor.lst"

monitor_getchar=$(awk '/_MONITOR_GETCHAR:/{getline; sub(/:.*/, "", $1); print "0x"$1; exit}' \
    "$build_dir/monitor.lst")
test -n "$monitor_getchar"

PATH="$tool_dir:$PATH" plsw "$plsw_repo/examples/hello.plsw" \
    --asm -o "$build_dir/hello.s"
PATH="$tool_dir:$PATH" plsw "$plsw_repo/examples/loop.plsw" \
    --asm -o "$build_dir/loop.s"

# These are finite output-only demos. Preserve their PL/SW sources, but make
# the generated standalone `_halt` transfer back to the resident menu.
python3 "$suite_dir/tools/patch_plsw_return.py" "$build_dir/hello.s" 0x0D0000
python3 "$suite_dir/tools/patch_plsw_return.py" "$build_dir/loop.s" 0x0D0000

"$tool_dir/cor24-asm" "$build_dir/hello.s" --base-addr 0x0D4000 \
    --no-entry --lgo-full -o "$build_dir/hello.lgo" --bin "$build_dir/hello.bin"
"$tool_dir/cor24-asm" "$build_dir/loop.s" --base-addr 0x0D8000 \
    --no-entry --lgo-full -o "$build_dir/loop.lgo" --bin "$build_dir/loop.bin"

python3 "$suite_dir/tools/patch_snobol_input.py" \
    "$snobol_repo/build/snobol4.bin" "$build_dir/snobol4-monitor.bin" \
    "$monitor_getchar"
python3 "$suite_dir/tools/bin_to_lgo.py" \
    "$build_dir/snobol4-monitor.bin" 0 "$build_dir/snobol4-monitor.lgo" \
    --skip-zero

python3 "$suite_dir/tools/bin_to_lgo.py" \
    "$snobol_repo/demos/eliza.sno" 0x0B0000 "$build_dir/eliza-source.lgo"
python3 "$suite_dir/tools/bin_to_lgo.py" \
    "$suite_dir/src/echo.sno" 0x0B1000 "$build_dir/echo-source.lgo"
python3 "$suite_dir/tools/add_snobol_menu_wait.py" \
    "$snobol_repo/demos/palindrome.sno" "$build_dir/palindrome-menu.sno"
python3 "$suite_dir/tools/bin_to_lgo.py" \
    "$build_dir/palindrome-menu.sno" 0x0B2000 "$build_dir/palindrome-source.lgo"

awk '/^L/' \
    "$build_dir/snobol4-monitor.lgo" \
    "$build_dir/eliza-source.lgo" \
    "$build_dir/echo-source.lgo" \
    "$build_dir/palindrome-source.lgo" \
    "$build_dir/monitor.lgo" \
    "$build_dir/hello.lgo" \
    "$build_dir/loop.lgo" > "$build_dir/plsw-apps-and-snobol4.lgo"
printf 'G0D0000\n' >> "$build_dir/plsw-apps-and-snobol4.lgo"

monitor_bytes=$(wc -c < "$build_dir/monitor.bin")
hello_bytes=$(wc -c < "$build_dir/hello.bin")
loop_bytes=$(wc -c < "$build_dir/loop.bin")
if (( 0x0D0000 + monitor_bytes > 0x0D3E00 )); then
    echo "monitor overlaps its RX broker state" >&2
    exit 1
fi
if (( 0x0D4000 + hello_bytes > 0x0D8000 )); then
    echo "hello overlaps loop" >&2
    exit 1
fi
if (( 0x0D8000 + loop_bytes > 0x0E0000 )); then
    echo "loop overlaps SNOBOL source area" >&2
    exit 1
fi

echo "Patched SNOBOL input calls to $monitor_getchar"
echo "Sparse SNOBOL records and generated clear ranges:"
wc -l "$build_dir/snobol4-monitor.lgo" "$build_dir/snobol-zero-ranges.s"
wc -c -l "$build_dir/plsw-apps-and-snobol4.lgo"
tail -n 1 "$build_dir/plsw-apps-and-snobol4.lgo"
