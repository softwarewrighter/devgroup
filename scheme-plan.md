# COR24 Scheme Monitor Composite Image Plan

Status date: 2026-07-29

## Goal

Produce one hardware-safe, full COR24 `.lgo` file containing:

- the `dcdbg` resident monitor/debugger;
- the `dcmls` Macro Lisp Scheme REPL;
- the `dcscr` scripting interpreter image; and
- the `dcyed` yocto-ed image.

The first visible monitor menu is intentionally limited to:

```text
COR24 monitor
1: Scheme REPL
h: help
q: quit
mon>
```

Selecting `1` calls the Macro Lisp Scheme REPL. While Scheme is running, the
monitor retains ownership of UART RX interrupts. Pressing Ctrl-] causes the
monitor interrupt handler to abandon the interrupted Scheme execution
context, restore a saved monitor continuation, and restart the monitor menu
without resetting or reloading the board. It does not resume Lisp.

Selecting Scheme again calls its entry from the beginning. Macro Lisp must
reinitialize its heap, GC, symbols, strings, evaluator, and Scheme prelude on
every launch.

The script and editor images may be present but dormant in the first
four-component package. They should not be listed until their callable-app and
shared-data contracts are ready. This preserves the requested first user
experience while establishing a memory layout that can later expose:

```text
2: Script REPL
3: Yocto Editor
```

## Scope and repository boundary

This document is a workspace-level analysis and implementation brief.

The analysis/planning role must not modify repositories under:

- `work/dcdbg`;
- `work/dcmls`;
- `work/dcscr`; or
- `work/dcyed`.

Changes described below belong to the corresponding dc* coding agents/users
and must follow each repository's local process, tests, changelog policy, and
AgentRail workflow where applicable.

The final composite-image builder should be owned by `dcdbg`, because the
monitor owns the app directory, launch policy, memory map, and final entry
point. The other repositories produce independently testable loadable-app
artifacts and metadata.

## Existing evidence

### Uppercase/lowercase hardware demo

The workspace contains:

- `monitor-test.lgo`;
- `monitor-test.log`; and
- the more general design in `monitor-plan.md`.

The hardware transcript shows that `monitor-test.lgo`:

- uploaded successfully through `te-rs`;
- contained 47 `L` records and one final `G000000`;
- started a menu at address zero;
- called uppercase and lowercase applications;
- used Ctrl-] as a cooperative application return;
- regained the monitor after each return; and
- switched repeatedly without a board reset.

Its observed menu was:

```text
COR24 monitor 0.1
1  upper
2  lower
h  help
q  halt (S1 resets)
mon>
```

This proves the essential hardware control flow:

```text
board loader
    |
    v
one composite .lgo
    |
    v
monitor at address zero
    |
    +--> callable app
    |        |
    |        `--> return
    |
    `--> monitor menu
```

The `.lgo` and transcript are evidence, not a reproducible source build.
`dcdbg` should recreate the monitor from maintained source and tests rather
than use `monitor-test.lgo` as a binary input.

The uppercase/lowercase demo did not prove interrupt-owned preemption. Its
apps cooperatively recognized Ctrl-]. The Scheme milestone deliberately adds
an assembly interrupt/context-switching layer.

### Existing dcscr-to-dcyed call

`dcscr` already demonstrates a function-pointer call to the editor's `_main`
entry and regains control when yocto-ed executes its `quit` command. That
emulator demo supplies useful calling-convention prior art.

### Existing dcmls Scheme build

`dcmls` already contains:

- `src/repl-scheme.c`;
- `src/prelude-scheme.h`;
- `prelude/scheme.l24`;
- `just build-scheme`;
- `just run-scheme`; and
- `just eval-scheme`.

The requested Scheme image should use `src/repl-scheme.c` and its compiled-in
`src/prelude-scheme.h`.

Terminology warning: Scheme is not literally the smallest existing Macro
Lisp build. `repl-bare` and `repl-minimal` are smaller. This plan interprets
"starting with the smallest prelude (Scheme)" as "start with the requested
Scheme-flavored tier rather than standard or full." If minimum size becomes
more important than Scheme compatibility, substitute `repl-minimal.c` in a
separate experiment.

## Measurements from the current sources

The following were measured without changing any project repository. Sources
were compiled and assembled into temporary files with the current workspace
tools.

| Component | Proposed base | Current bytes | Current exclusive end |
| --- | ---: | ---: | ---: |
| dcdbg monitor budget | `0x000000` | 4 KB reserved | `0x001000` |
| dcmls Scheme REPL | `0x001000` | `0x051FFA` (335,866) | `0x052FFA` |
| dcscr `sws` | `0x060000` | `0x00F044` (61,508) | `0x06F044` |
| dcyed `swye` | `0x080000` | `0x0029D1` (10,705) | `0x0829D1` |

The Scheme build measured:

- `.s`: 268,927 bytes;
- full `.lgo`: 746,380 bytes and 9,331 lines;
- machine image: 335,866 bytes;
- `_start` at `0x001000`;
- `_repl` at `0x005912` in that measured build; and
- `_main` at `0x005A01` in that measured build.

Those symbol addresses are observations, not ABI constants. Any source,
compiler, assembler, or base-address change can move them. The packaging
pipeline must extract the callable entry from the current listing on every
build.

The Scheme image is large mainly because its three 32,768-entry, 24-bit heap
arrays consume 288 KB:

- `heap_car`: 98,304 bytes;
- `heap_cdr`: 98,304 bytes; and
- `heap_mark`: 98,304 bytes.

The combined measured machine bytes for Scheme, `sws`, and yocto-ed are about
408 KB, so the four-component image fits within the board's 1 MB SRAM with
substantial unused space. Stack remains in EBR and is not included in those
SRAM totals.

## Recommended initial memory map

This is a planning map. Every build must generate and verify an actual map.

```text
0x000000 +----------------------------------+
         | dcdbg monitor                    |
0x001000 +----------------------------------+
         | dcmls Scheme code/static/heap    |
         | measured end < 0x053000           |
0x053000 +----------------------------------+
         | guard / future trampoline space  |
0x060000 +----------------------------------+
         | dcscr sws image                  |
         | measured end < 0x070000           |
0x070000 +----------------------------------+
         | reserved shared/object space     |
0x080000 +----------------------------------+
         | dcyed swye image                 |
         | measured end < 0x083000           |
0x083000 +----------------------------------+
         | future objects/app images        |
0x0F0000 +----------------------------------+
         | legacy app I/O compatibility     |
         | cmd 0x0F0000, output 0x0F0400    |
0x0FE000 +----------------------------------+
         | app directory/monitor metadata   |
0x100000 +----------------------------------+ end SRAM

0xFEE000 +----------------------------------+
         | FPGA EBR: load-and-go boot code  |
         | plus its runtime data/stack      |
0xFEEC00 +----------------------------------+ current 3 KB initial SP
0xFF0000 +----------------------------------+
         | MMIO                             |
```

The regions above are implemented differently. Addresses below `0x100000`
select the board's external SRAM and hold the downloaded composite image.
The `0xFE....` region selects FPGA embedded block RAM (EBR). The compiled
`loadngo.c` image is initialized into that EBR by the FPGA bitstream and the
CPU reset vector is `0xFEE000`. It therefore behaves as boot ROM from the
software user's perspective, although the underlying FPGA resource is RAM.
It is not part of `mon-scheme.lgo` and does not consume external SRAM.

Required build-time assertions:

- monitor end is at or below `0x001000`;
- Scheme end is below `0x053000`;
- `sws` end is below `0x070000`;
- yocto-ed end is below `0x083000`;
- no `L` record writes at or above `0x100000`;
- no component writes into EBR or MMIO;
- component `L` record ranges do not overlap;
- app directory and compatibility ranges do not overlap generated images;
- exactly one final `G` record remains; and
- that record is `G000000` or the current generated monitor start address.

If any assertion fails, the build must stop rather than silently move a
component. Base-address changes are ABI changes and should be reviewed.

## Important current incompatibilities

### Macro Lisp currently bypasses monitor-owned UART input

`dcmls/src/io.h` currently polls and reads UART MMIO directly. With a
monitor-owned UART RX interrupt enabled, the ISR must read the received byte
to acknowledge it. Macro Lisp cannot then read that same hardware byte.

The monitor therefore needs an RX ring and Macro Lisp needs an app-mode
`getc` service that reads the ring rather than `0xFF0100`.

Ctrl-] is not delivered to the ring. The ISR consumes it as debugger
attention and transfers directly back to the monitor continuation. Macro Lisp
does not recognize Ctrl-] and does not execute a return path for this escape.

On EOF, the current Scheme REPL prints `Bye.` and calls `halt()`. The Lisp
`(exit)` primitive also calls `halt()`. Those are separate behaviors from
interrupt preemption and should eventually become monitor-aware.

### dcscr top-level exit halts

The current `sws` top-level loop eventually calls `halt()`. Its callable
`run swye` child path is useful prior art, but `sws` itself is not yet a clean
monitor-called app.

### dcyed fixed input overlaps Macro Lisp

The current yocto-ed compatibility build reads initial text from `0x010000`.
That address lies inside the relocated Scheme image/heap region
`0x001000-0x052FF9`.

Yocto-ed may be present but must not be made runnable in this composite until
its initial text and output are supplied through an app context or moved to a
non-overlapping monitor-owned object.

### Shared EBR stack requires an explicit abort restore

Macro Lisp's GC captures SP early in `main()` and conservatively scans from
the current SP to that captured value. Calling its `_main` from the monitor
should be compatible with this design: the captured top is simply the app
entry stack position.

Nevertheless, the package must test:

- SP before launch;
- SP immediately after return;
- Scheme prelude loading under the physical 3 KB EBR stack;
- GC during interactive evaluation; and
- repeated launches.

## Launch and preemption ABI for this milestone

Use the proven callable entry ABI:

```c
int app_main(void);
```

At the machine level, an assembly launch trampoline calls the current `_main`
symbol address from the Macro Lisp listing.

Initial return codes:

```text
0  normal app return
1  initialization or REPL error
2  monitor/debugger interrupt abort
```

Initial monitor responsibilities:

- print the menu;
- parse `1`, `h`, and `q`;
- save a complete monitor launch context;
- install its UART RX ISR in `iv`;
- enable UART RX interrupts;
- call the generated Scheme `_main` through an assembly trampoline;
- retain the saved monitor SP and continuation while Scheme runs;
- broker non-reserved input through a monitor RX ring;
- intercept Ctrl-] in the ISR;
- discard the interrupted Scheme register/stack context;
- restore the saved monitor context;
- leave interrupt-in-service state correctly;
- jump to the monitor's post-app continuation without returning to Lisp;
- print a return message;
- redraw the menu; and
- halt in a stable loop for `q`.

Initial Scheme app responsibilities:

- initialize its heap, symbols, strings, evaluator, GC, and Scheme prelude;
- print a Scheme REPL banner or prompt;
- read input through the monitor app-input service;
- evaluate expressions;
- tolerate its execution context being abandoned at an arbitrary instruction
  boundary; and
- reinitialize completely whenever `_main` is called again.

Macro Lisp does not clean up after Ctrl-]. Its abandoned stack frames become
irrelevant when the monitor restores its saved SP. Its SRAM globals and heap
remain stale until the next launch reinitializes the logical interpreter
state.

This milestone uses raw Ctrl-] (`0x1D`) as the monitor-owned attention byte
because the successful hardware uppercase/lowercase demo already proved that
the current terminal path transmits it and the user explicitly requested it.

The longer-term debugger transport described in `monitor-plan.md` can replace
raw Ctrl-] with an out-of-band encoded attention sequence so yocto-ed retains
its Emacs-oriented control-key namespace. The launch trampoline, saved
context, RX broker, and non-resume behavior remain the same.

## Interrupt escape mechanics

### Why a normal ISR return is wrong

On UART interrupt, COR24 records the interrupted app PC in `ir`, jumps through
`iv`, and marks interrupt service active. A normal ISR ends with:

```text
jmp (ir)
```

That would resume Lisp at the interrupted PC. Ctrl-] must take a different
path.

### Monitor launch trampoline

Before calling Scheme, a hand-written assembly trampoline saves enough state
to resume the monitor as though the app returned with status 2:

```text
monitor saved SP
monitor fp
monitor r1/link
monitor r2
monitor condition flag
prior iv
prior interrupt-enable value
post-app continuation address
launch-active flag
```

The exact save set must follow the verified COR24/tc24r calling convention.
Saving all architecturally visible state is preferable to depending on an
incomplete caller/callee-saved assumption.

Normal app return and interrupt abort converge at one assembly continuation:

```text
monitor_run_scheme:
    save monitor context
    mark launch active
    install monitor UART iv
    enable UART RX interrupt
    call scheme _main
    status = app return value

monitor_app_continuation:
    disable or restore UART interrupt policy
    clear launch active
    restore monitor context
    return status to menu loop
```

### UART ISR

The ISR must first save any registers it uses and read UART data to
acknowledge RX-ready.

For an ordinary byte:

1. enqueue the byte in the monitor RX ring;
2. restore the app registers and condition flag; and
3. execute normal `jmp (ir)` to resume Scheme.

For Ctrl-] while `launch-active`:

1. record interrupt-abort status 2;
2. optionally record the interrupted `ir` PC for diagnostics;
3. disable UART RX interrupt during the context switch;
4. discard the saved app ISR frame by restoring the saved monitor SP;
5. restore the saved monitor registers and condition state;
6. arrange for interrupt-in-service to be cleared;
7. transfer to `monitor_app_continuation`; and
8. redraw the menu.

The implementation should preferably set `ir` to a small monitor interrupt
exit stub and use `jmp (ir)`, because current COR24 behavior clears
interrupt-in-service on `jmp (ir)`. The exit stub then restores the monitor
continuation. Directly jumping through another register may leave the CPU
believing it is still in interrupt service and suppress all later UART
interrupts.

Ctrl-] received while no app is active should be ignored, reported, or
treated as a menu redraw. It must not restore an invalid saved context.

### Monitor-owned RX ring

Every received non-attention byte is consumed by the ISR, so app-mode Macro
Lisp must call:

```c
int monitor_app_getc(void);
```

That service blocks on the monitor RX ring, not UART MMIO. It must define:

- ring size;
- atomic read/write index rules;
- overflow behavior;
- whether CR/LF normalization happens in monitor or app;
- pending-byte reset at app launch and abort; and
- behavior when the monitor menu owns UART after abort.

The monitor menu may poll UART directly with interrupts disabled, or use the
same ring consistently. One consistent broker is preferable once proven.

### Fresh Scheme initialization

The current Scheme `_main` already calls, in order:

```text
heap_init
gc_init
symbol_init
string_init
eval_init
load_prelude
repl
```

The inspected initialization functions reset the important logical state:

- `heap_init()` resets `heap_next`;
- `gc_init()` resets free-list/root/statistics state and captures the new
  launch SP;
- `symbol_init()` resets symbol count/name-pool allocation;
- `string_init()` resets string-pool allocation;
- `eval_init()` resets `global_env`, catch/throw state, dynamic-wind depth,
  registers primitives, and resets the gensym counter; and
- `load_prelude()` reconstructs the Scheme environment.

It is not necessary to zero all 288 KB of heap arrays before relaunch because
the reset allocation counters make stale cells unreachable and new
allocations initialize cells before use. This must be confirmed by a
repeat-launch test, including GC.

One current detail needs explicit verification: `gc_enabled` remains `1`
after an aborted prior run, and `heap_init()` precedes `gc_init()` and
`eval_init()`. If any initialization allocates before GC bookkeeping has been
reset, stale `free_list` state could be used. The dcmls monitor entry should
set `gc_enabled = 0` before `heap_init()`, then run all initialization, then
set `gc_enabled = 1` before loading the prelude. This makes relaunch ordering
deterministic.

## Required changes by repository

### dcdbg: required

Create maintained monitor and packaging implementation based on the proven
demo behavior.

Required source behavior:

- banner and exact first menu entries:

  ```text
  1: Scheme REPL
  h: help
  q: quit
  ```

- bounded UART command input;
- acceptance of CR, LF, and CRLF without double commands;
- selection of `1`;
- assembly launch trampoline to the generated Scheme `_main`;
- saved monitor context and post-app continuation;
- UART RX ISR installation and interrupt enable/restore;
- monitor-owned RX ring;
- app-input service for Scheme;
- ordinary-byte ISR return to Scheme;
- Ctrl-] interrupt abort that does not resume Scheme;
- correct interrupt-in-service clearing;
- return to the menu with status 2;
- `h` help text mentioning Ctrl-];
- `q` stable halt text/loop;
- unknown-command handling; and
- no use of Scheme/dcscr/dcyed memory as monitor scratch.

Required build/package behavior:

- build components in a deterministic order;
- invoke repository-owned component build interfaces;
- assemble each component at its assigned base;
- request full `.lgo` output;
- request a listing for symbol extraction;
- extract `_main` for Scheme;
- generate monitor configuration or app-directory data;
- build the monitor last, once the Scheme entry is known;
- merge component `L` records;
- reject component `G` records during merge;
- validate syntax, address ranges, and overlap;
- append the single monitor `G` record;
- produce an actual memory map;
- produce artifact hashes and size statistics;
- run the exact final `.lgo` in `cor24-emu`; and
- preserve a hardware upload transcript separately.

Required low-level tests:

- interrupt entry saves the exact interrupted PC in `ir`;
- ordinary UART input reaches the RX ring exactly once;
- ordinary UART input resumes the interrupted Scheme instruction stream;
- Ctrl-] never reaches the Scheme RX ring;
- Ctrl-] never resumes the interrupted Lisp PC;
- abort restores the exact saved monitor SP;
- interrupt-in-service is clear after abort;
- a second Scheme launch can receive UART interrupts;
- Ctrl-] outside an active launch cannot restore stale context; and
- RX-ring overflow has deterministic behavior.

Suggested dcdbg outputs:

```text
build/scheme-suite.lgo
build/scheme-suite.map
build/scheme-suite.sha256
build/scheme-suite-components.txt
build/scheme-suite-emulator.log
```

The package should record component repository commit IDs and tool versions.

Do not parse human listing layout with an unbounded or ambiguous match.
Require exactly one `_main` symbol and exactly one numeric address. A future
assembler symbol-map output would be better, but current listing extraction
is acceptable for this proof.

### dcmls: required

Add a monitor-callable Scheme build variant without breaking standalone
`just run-scheme`.

Recommended shape:

```text
src/repl-scheme-monitor.c
just build-scheme-monitor BASE=0x001000
```

It may reuse the existing interpreter headers and `prelude-scheme.h`.

Required behavior changes for the monitor variant:

1. Replace direct UART RX polling with `monitor_app_getc()`.
2. Do not inspect or handle Ctrl-]; the monitor ISR owns it.
3. Set `gc_enabled = 0` before resetting interpreter state.
4. Run complete initialization on every `_main` call.
5. Set `gc_enabled = 1` only after heap/GC/evaluator state is reset.
6. Load the Scheme prelude on every `_main` call.
7. Preserve existing standalone Scheme input behavior.
8. Permit `_main` to return normally if the REPL later gains a normal exit
   request.

The monitor build needs a narrow imported service declaration or generated
ABI header. It must not depend on dcdbg internal source layout.

Recommended later monitor behavior is for `(exit)` to return normally to the
monitor. That requires changing the primitive exit path from a direct
non-returning `halt()` to an app-exit request that unwinds to the REPL.
Ctrl-] does not use this path; it preempts and abandons Lisp execution.

Required build metadata:

- base address used;
- full image path;
- listing path;
- `_start`, `_main`, and `_repl` addresses;
- exclusive occupied end address;
- machine byte count;
- full `.lgo` byte/line count; and
- required stack tier.

Required tests:

- existing standalone Scheme tests remain green;
- Scheme prelude loads at base `0x001000`;
- arithmetic evaluation works;
- list/Scheme prelude evaluation works;
- app-mode input comes from the monitor RX broker;
- `_main` can be called from the monitor trampoline;
- repeated calls reinitialize cleanly after forced interrupt abandonment;
- definitions made in one launch are absent after relaunch;
- stale partial input from one launch is absent after relaunch;
- GC operates correctly when called from a monitor stack frame.

The dcmls changelog must be updated for every dcmls commit, per its local
instructions.

### dcscr: no first-menu behavior change; packaging metadata required

For the first visible Scheme-only menu, `sws` may be included as a dormant
relocated image. It need not be callable yet.

Required packaging contribution:

- deterministic full image at base `0x060000`;
- listing and occupied range metadata;
- no automatic `G` execution in the composite;
- tests that relocation does not change standalone behavior; and
- a stable artifact interface for the dcdbg packager.

Before `sws` is exposed in a later menu:

- split standalone startup from monitor-callable entry;
- make app-mode `exit` return instead of halt;
- replace the one-name hard-coded run table with the common app directory;
- replace or version fixed buffers at `0x0F0000` and `0x0F0400`;
- define ownership of UART and pending bytes; and
- test `monitor -> sws -> return -> monitor`.

No source change is strictly necessary merely to place the current dormant
image in `scheme-suite.lgo`, provided the packager assembles it at the
reserved base and removes its `G` record.

### dcyed: no first-menu behavior change; packaging metadata required

For the first visible Scheme-only menu, yocto-ed may also be included as a
dormant relocated image.

Required packaging contribution:

- deterministic full image at base `0x080000`;
- listing and occupied range metadata;
- current callable `_main` address;
- no automatic `G` execution in the composite; and
- tests that relocation does not change standalone behavior.

Before yocto-ed is exposed in a later menu:

- replace fixed initial-text address `0x010000`;
- replace fixed command/output buffers or wrap them in a versioned app
  context;
- obtain named text from the monitor object store;
- keep editor `quit` as return-to-monitor;
- reconcile editor key bindings with debugger attention interception; and
- test `monitor -> editor -> quit -> monitor` without touching Scheme memory.

No source change is strictly necessary merely to place the current dormant
image in `scheme-suite.lgo`. It must not be invoked while it still treats
`0x010000` as its input text.

## Proposed component artifact contract

Each app repository should eventually export a small text manifest alongside
its full `.lgo` and listing:

```text
name=scheme
abi=cor24-monitor-app-1
base=0x001000
limit=0x052FFA
entry=0x005A01
stack=ebr3
callable=yes
interactive=yes
uart=monitor-rx-broker
commit=<git-commit>
```

The actual addresses are regenerated. The example entry is only the measured
current value.

The dcdbg packager should reject:

- missing fields;
- malformed hex;
- duplicate names;
- unsupported ABI versions;
- entry outside `[base, limit)`;
- `limit` beyond assigned region;
- overlapping regions;
- unexpected MMIO/EBR writes; and
- more than one component `G` record.

## Deterministic build sequence

The commands below show the intended operations, not necessarily the final
recipe names.

### 1. Record inputs

Record:

- commits for dcdbg, dcmls, dcscr, and dcyed;
- `tc24r` version/identity;
- `cor24-asm` version;
- `cor24-emu` version;
- assembler full/compact mode; and
- selected memory-map version.

All four worktrees must be clean or the package manifest must explicitly
record that a component came from a dirty worktree. Release artifacts should
require clean worktrees.

### 2. Build the monitor-callable Scheme assembly

Conceptually:

```bash
tc24r src/repl-scheme-monitor.c -o build/repl-scheme-monitor.s
```

### 3. Assemble Scheme at its final base

```bash
cor24-asm build/repl-scheme-monitor.s \
  -o build/repl-scheme-monitor.lgo \
  --bin build/repl-scheme-monitor.bin \
  --listing build/repl-scheme-monitor.lst \
  --base-addr 0x001000 \
  --lgo-full
```

Extract `_main` and occupied end from the current build metadata/listing.

### 4. Build dormant script and editor components

Conceptually:

```bash
cor24-asm sws.s \
  -o build/sws-monitor-component.lgo \
  --listing build/sws-monitor-component.lst \
  --base-addr 0x060000 \
  --lgo-full

cor24-asm swye.s \
  -o build/swye-monitor-component.lgo \
  --listing build/swye-monitor-component.lst \
  --base-addr 0x080000 \
  --lgo-full
```

Prefer repository-owned build recipes over dcdbg reaching into internal
source paths. The final recipe names should expose base, listing, and full
image explicitly.

### 5. Generate dcdbg app configuration

Generate a build-only header, assembly include, or directory data containing:

- Scheme name;
- current Scheme `_main`;
- base and limit;
- flags; and
- ABI version.

Do not edit a checked-in source constant on each build.

### 6. Build monitor at zero

Compile and assemble the monitor at `0x000000`, producing a full component
image and listing.

The monitor must fit below `0x001000`. If it grows beyond that limit, review
the whole map rather than moving Scheme automatically.

### 7. Merge load records

For every component:

1. Parse each line.
2. Accept `L`, `G`, and `;` only.
3. Validate uppercase hex and line length.
4. Retain validated `L` records.
5. Retain or generate useful `;` provenance comments if the hardware loader
   accepts the resulting line lengths.
6. Drop component `G` records after verifying their expected values.
7. Track every written byte range.
8. Reject any overlap.

Recommended merge order:

```text
monitor L records
Scheme L records
sws L records
yocto-ed L records
generated app-directory L records
one G record for monitor start
```

Because records contain absolute addresses, merge order must not be relied
upon to resolve overlap. Any duplicate byte write is an error, even if the
bytes are identical.

### 8. Validate final image

Validate:

- only `L`, `G`, and `;` records;
- uppercase hex;
- maximum 80 characters including newline;
- even data nybble count;
- addresses inside SRAM;
- no overlap;
- one final `G`;
- final `G` points to monitor;
- expected component hashes;
- expected entry addresses;
- final line count; and
- final SHA-256.

### 9. Run exact image in emulator

Use the final merged `.lgo`, not separately loaded binaries:

```bash
timeout --signal=INT --kill-after=2s 30s \
  cor24-emu \
  --lgo build/scheme-suite.lgo \
  --speed 0 \
  --stack-kilobytes 3 \
  --max-instructions 500000000 \
  --quiet
```

Interactive tests should use terminal mode. Automated tests should inject
input with the emulator's UART-file mechanism so prelude startup does not
drop bytes.

### 10. Upload exact image to COR24-TB

Use `te-rs` with the proven synchronization/pacing settings. Preserve:

- artifact hash;
- upload command;
- line count;
- last acknowledged line;
- complete UART transcript;
- board and FPGA release;
- adapter identity; and
- cold/warm load status.

Do not rebuild between emulator validation and hardware upload.

## Expected interaction

```text
COR24 monitor
1: Scheme REPL
h: help
q: quit
mon> h

1 starts a freshly initialized Macro Lisp Scheme REPL.
Inside Scheme, Ctrl-] interrupts Lisp and restarts this menu.
q halts; press S1 to return to load-and-go.

COR24 monitor
1: Scheme REPL
h: help
q: quit
mon> 1

Scheme REPL
> (+ 2 3)
5
> (map (lambda (x) (* x x)) '(1 2 3))
(1 4 9)
> <Ctrl-]>

Scheme interrupted at PC 0x......

COR24 monitor
1: Scheme REPL
h: help
q: quit
mon> 1

Scheme REPL
> 
```

Ctrl-] should not appear as a Lisp token or produce a reader error. If it is
pressed during:

```text
> (define (unfinished
```

the partial form is abandoned with the entire Lisp execution context. On the
next `1`, Macro Lisp starts again from `_main`, reloads the Scheme prelude, and
has no definitions or partial input from the prior launch.

## S1 warm-reset behavior

An S1 reset is expected to reset the CPU and peripherals without clearing the
external SRAM. Treat this as a useful board behavior that must be verified on
the exact FPGA/board revision, not as a substitute for loading the composite
image when reproducibility matters.

S1 restarts the CPU at `0xFEE000`, in the FPGA EBR-resident load-and-go
program. The load-and-go banner after reset is therefore not evidence that the
external SRAM application restarted or was reloaded. It establishes that the
bitstream-initialized boot monitor is running and can call an entry retained in
external SRAM. Reconfiguring the FPGA reloads the EBR image; loading an `.lgo`
updates external SRAM.

After `(exit)` prints `Bye.` and enters Macro Lisp's tight halt loop:

1. Press S1 and wait for the load-and-go banner.
2. Enter `G000000`, using all six address digits.
3. Expect the monitor `_start` entry to initialize monitor state and display
   the menu from the still-resident SRAM image.
4. Select `1` and require a fresh Scheme initialization.

The load-and-go `G` command is implemented as a C function call even though its
message calls the operation a jump. A program that returns can therefore
return to load-and-go. The generated `_start` stubs normally call `_main` and
then branch forever at `_halt`, so neither the monitor nor Scheme should rely
on returning to the loader.

Do not use `G001000` as the normal Scheme launch path. It enters Scheme's
`_start` directly and bypasses the monitor launch trampoline, UART RX interrupt
installation, reserved-byte interception, and saved monitor context. In the
planned monitor-callable build, Scheme reads through `monitor_app_getc()`;
without the monitor ISR feeding that ring, a direct Scheme entry can wait
forever for input. A standalone Scheme build that polls the UART could run this
way, but Ctrl-] would not return it to the monitor.

Also do not publish a generated `_main` or `_repl` address as a user entry
point: those addresses change with each build and may require calling and stack
conditions that load-and-go does not promise. If a future direct Scheme command
is useful, export a stable monitor bootstrap entry that performs the normal
monitor initialization and launch sequence before entering Scheme.

SRAM retention does not mean runtime state was restored. CPU registers,
interrupt state, UART state, stacks, Scheme heap metadata, and mutable globals
must all be initialized on the warm path. Code and immutable data must not be
self-modified. Reload the complete `.lgo` whenever a known byte-for-byte image
is required.

## Acceptance checklist

### Repository/component preparation

- [ ] dcdbg has maintained monitor source.
- [ ] dcdbg has a deterministic composite builder.
- [ ] dcdbg has an assembly launch/abort trampoline.
- [ ] dcdbg owns UART RX interrupts while an app runs.
- [ ] dcdbg brokers normal RX bytes through a ring.
- [ ] dcmls has a monitor-callable Scheme variant.
- [ ] dcmls app input uses the monitor RX service.
- [ ] dcmls does not consume Ctrl-].
- [ ] dcmls reinitializes on every `_main` call.
- [ ] dcmls standalone Scheme behavior remains tested.
- [ ] dcscr exports a relocatable full component and metadata.
- [ ] dcyed exports a relocatable full component and metadata.
- [ ] No analysis role modified a dc* repository.

### Memory and image

- [ ] Monitor fits below `0x001000`.
- [ ] Scheme fits below `0x053000`.
- [ ] `sws` fits in `0x060000-0x06FFFF`.
- [ ] yocto-ed fits in `0x080000-0x082FFF`.
- [ ] No component overlaps another.
- [ ] No component writes to EBR or MMIO.
- [ ] App directory is outside all component ranges.
- [ ] Exactly one final `G` record starts the monitor.
- [ ] Final image is full, not compact.
- [ ] Final image passes strict `.lgo` validation.
- [ ] Component commits and tool versions are recorded.
- [ ] SHA-256 is recorded.

### Emulator

- [ ] Menu text matches the requested entries.
- [ ] `h` prints useful help.
- [ ] Unknown input returns to the prompt.
- [ ] `1` enters the Scheme REPL.
- [ ] `(+ 2 3)` prints `5`.
- [ ] At least one Scheme-prelude function works.
- [ ] Ctrl-] at an empty prompt interrupts to monitor.
- [ ] Ctrl-] during a partial/multiline form interrupts to monitor.
- [ ] After Scheme `(exit)`, S1 returns to the load-and-go banner.
- [ ] Documentation distinguishes FPGA EBR-resident load-and-go from the
      external-SRAM `mon-scheme.lgo` image.
- [ ] `G000000` after S1 restarts the retained monitor image.
- [ ] Scheme selected after that warm restart has no prior definitions.
- [ ] Warm restart resets monitor RX, launch, interrupt, and app state.
- [ ] `G001000` is documented as an unsupported diagnostic entry, not a normal
      launch method.
- [ ] Interrupted Lisp PC is not resumed.
- [ ] Interrupt-in-service is clear after returning to the menu.
- [ ] Menu works after return.
- [ ] Second Scheme selection starts from `_main` and reloads the prelude.
- [ ] A definition from the prior Scheme launch is absent after relaunch.
- [ ] Scheme can be entered and interrupted at least 100 times.
- [ ] SP after each return equals the expected monitor SP.
- [ ] GC is exercised after at least one interrupt/relaunch cycle.
- [ ] `q` enters a stable halt loop.

### COR24-TB

- [ ] Upload uses the exact emulator-tested hash.
- [ ] Every load line is acknowledged.
- [ ] Menu appears over physical UART.
- [ ] `h` works.
- [ ] Scheme prelude finishes loading.
- [ ] Scheme arithmetic works.
- [ ] Scheme prelude function works.
- [ ] Physical Ctrl-] byte reaches the board ISR.
- [ ] Monitor ISR consumes Ctrl-] before the app sees it.
- [ ] Ctrl-] abandons Lisp and reaches monitor without reset.
- [ ] Re-entry performs fresh initialization.
- [ ] Definitions do not survive re-entry.
- [ ] UART interrupts still work after re-entry.
- [ ] Repeated return does not corrupt the stack.
- [ ] Cold load passes.
- [ ] Warm full-image reload passes.
- [ ] Complete transcript is preserved.

## Follow-on work

After the Scheme-only menu passes:

1. Make `sws` a true callable app and add `2: Script REPL`.
2. Give yocto-ed monitor-owned named text and add `3: Yocto Editor`.
3. Replace legacy fixed buffers with a versioned app context/object store.
4. Replace raw Ctrl-] debugger ownership with the escaped attention protocol
   described in `monitor-plan.md`, preserving yocto-ed's Emacs keys.
5. Generalize the Scheme launch/abort trampoline into the debugger's saved
   app-context facility.
6. Add emulator logical breakpoints before hardware patch breakpoints.

The first `scheme-suite.lgo` should prove packaging, relocation, callable
entry, Scheme initialization, UART interrupt ownership, non-resuming
Ctrl-] preemption, fresh reinitialization, and repeatable monitor control. It
should not simultaneously attempt dynamic loading, filesystem support, or
hardware breakpoint patching.

## Assumptions requiring confirmation during implementation

- The requested Scheme tier is `repl-scheme.c`, not the smaller
  `repl-minimal.c`.
- The current physical COR24-TB provides the 1 MB SRAM map used by existing
  documentation.
- The Scheme workload fits the verified 3 KB EBR stack.
- The first composite should physically contain dormant `sws` and yocto-ed
  images even though the menu does not expose them.
- Raw Ctrl-] is acceptable as the monitor-owned interrupt byte for this
  milestone, with the long-term editor-safe encoded attention sequence
  deferred.

If the last packaging assumption is not desired, build a smaller first
artifact containing only dcdbg plus dcmls, then add the dormant dcscr/dcyed
components without changing the visible menu. The functional acceptance
criteria remain the same.
