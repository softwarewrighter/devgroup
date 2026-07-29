# COR24 Interactive Monitor Integration Plan

Status date: 2026-07-29

## Purpose and scope

This document records analysis and recommendations for combining ideas and
capabilities from:

- `work/dcyed`: `sw-cor24-yocto-ed`
- `work/dcscr`: `sw-cor24-script`
- `work/dcdbg`: `sw-cor24-debugger`

The target is a small, single-task COR24 resident monitor for testing on a
COR24-TB. The monitor should eventually:

- list available applications and named data;
- select and run an application;
- let an interactive application return to the monitor;
- switch repeatedly between applications;
- receive loadable images and data over UART;
- edit named text, source, and script objects;
- run interpreters, p-code virtual machines, and compiled applications; and
- provide a foundation for debugging, breakpoints, watches, and memory
  inspection.

The first proof of concept is deliberately smaller:

1. A setup/upload process loads one monitor and two applications.
2. The monitor starts and presents a UART menu.
3. The user selects an uppercase echo application.
4. The user interacts with it and uses a reserved control byte to return to
   the monitor.
5. The user selects a lowercase echo application.
6. The user interacts with it and uses the reserved control byte to return to
   the monitor.
7. This switching can be repeated without resetting the board.

## Repository modification boundary

This file is an analysis, planning, and coordination artifact in the devgroup
workspace. It is not an implementation change to any `work/dc*` project.

The analysis/planning role must not modify repositories under `work/dcyed`,
`work/dcscr`, `work/dcdbg`, or other `work/dc*` trees. Implementation,
repository documentation, tests, commits, and pushes belong to the
corresponding dc* coding agents/users and must follow each repository's local
instructions and AgentRail process where present.

Permitted work for the analysis/planning role includes:

- inspecting repository state and existing interfaces;
- comparing approaches;
- specifying contracts and memory maps;
- preparing pseudocode, manifests, transcript examples, and test cases;
- maintaining workspace-level coordination documents such as this one; and
- synthesizing implementation briefs for dc* coding agents/users.

## Evidence inspected

The findings below came from the current local repositories and workspace
documentation, including:

- `work/dcyed/.../sw-cor24-yocto-ed/README.md`
- `work/dcyed/.../sw-cor24-yocto-ed/src/swye.c`
- `work/dcyed/.../sw-cor24-yocto-ed/src/compat.h`
- `work/dcscr/.../sw-cor24-script/README.md`
- `work/dcscr/.../sw-cor24-script/CHANGES.md`
- `work/dcscr/.../sw-cor24-script/src/sws.c`
- `work/dcscr/.../sw-cor24-script/docs/examples/editor-demo.sh`
- `work/dcdbg/.../sw-cor24-debugger/README.md`
- `work/dcdbg/.../sw-cor24-debugger/docs/research.txt`
- `docs/lgo-format.md`
- `reference/plan.md`
- `reference/status.md`
- `reference/stack-validation.md`

All three repositories were on `main` and matched their configured
`origin/main` at inspection time. A shared untracked `work/reference/` path
was visible from the repository worktrees; it was not changed.

## Current project state

### dcyed: sw-cor24-yocto-ed

The editor is a functional, modal UART application written in the C subset
supported by `tc24r`. Its documented features include:

- a fixed-capacity gap buffer;
- edit and command modes;
- a three-line UART display;
- commands for movement, insertion, deletion, and quit; and
- regression scenarios driven through `reg-rs`.

The single-translation-unit hardware/emulator build in `src/swye.c` already
has behavior useful to the monitor design:

- its `main()` is callable;
- `quit` exits its application loop;
- it returns `0` to its caller;
- it can consume commands from a fixed buffer at `0x0F0000`;
- after buffered commands are exhausted, it falls back to live UART input;
- it reads initial text from `0x010000`; and
- it copies edited output to `0x0F0400` before returning.

This is not yet a general loadable-app ABI. The addresses are fixed,
application-specific conventions. Nevertheless, the return behavior is a
working example of a cooperatively terminating application.

Current limitations relevant to the monitor:

- one fixed input text object;
- one fixed output object;
- no named object directory or filesystem;
- no independent app memory declaration;
- no explicit app context supplied by the caller; and
- no protection from collisions with other loaded programs.

### dcscr: sw-cor24-script

The scripting interpreter is the most advanced existing control-plane
prototype. It has a UART REPL and a Tcl-like command language with variables,
control flow, arithmetic, strings, records, and a `run` command.

The unreleased v0.2 implementation demonstrates:

- a run-table word at `0x0FFE00`;
- lookup of the name `swye`;
- reading a 24-bit callable entry address from the run table;
- calling the entry through a C function pointer;
- regaining control after the child returns;
- passing preloaded commands through `0x0F0000`;
- receiving child output through `0x0F0400`; and
- reporting a structured `$rc` result.

The included editor demo co-loads `sws` and `swye` in the emulator, assembles
the editor at base `0x080000`, extracts `_main` from the assembler listing,
patches the entry address, calls the editor, and observes its return.

That establishes an important proof:

```text
sws -> function-pointer call to swye._main -> editor quit -> return to sws
```

The demonstration is scripted rather than a complete interactive monitor,
but it proves the central callable-application mechanism in the current
compiler and emulator.

Current limitations relevant to the monitor:

- `run_resolve()` recognizes only the hard-coded name `swye`;
- there is only one run-table slot;
- argument and result buffers are fixed global addresses;
- applications have no declared base, limit, stack, or capability flags;
- the top-level `sws` `exit` path calls `halt()` rather than returning;
- filesystem and `source` storage operations remain stubbed; and
- the single C source is already large, making it a poor place to mix an
  initial loader, monitor kernel, object store, and debugger.

### dcdbg: sw-cor24-debugger

The debugger repository currently contains research and project/process
documentation but no monitor or debugger implementation.

Its research describes:

- software breakpoints made by patching program code;
- preservation of displaced instructions;
- condition/register restoration;
- UART stop/resume interaction;
- register and memory display;
- emulator-aware logical breakpoints; and
- later hardware support.

This is relevant future design work, but it does not yet supply a CLI,
loader, application directory, callable-app ABI, build, or tests.

The lack of implementation makes `dcdbg` a suitable location for a clean
resident monitor without first disentangling an existing application.

## Existing hardware and toolchain status

Workspace evidence dated 2026-07-29 says:

- `tc24r` is the current C-to-COR24-assembly compiler.
- `cor24-asm` is the canonical current `.s` to `.lgo` producer.
- `cor24-emu` consumes `.lgo` and does not assemble `.s`.
- The native/self-hosted assembler is not yet the current artifact producer.
- Supplied images and a Tiny C UART counter have run in the emulator.
- No sw-embed-generated `.lgo` has yet been validated on a physical COR24-TB.
- Reliable `te-rs` pacing/synchronization has not yet been established on
  the board.
- The emulator currently enforces EBR stack bounds unless configured
  otherwise.
- The physical board stack layout and capacity still need validation.

These are prerequisites and risks for the monitor proof of concept. Emulator
success must not be reported as COR24-TB success.

## The `.lgo` constraint

A `.lgo` is a text load image, not an executable file container. Its verified
record types are:

- `L<address><data>`: write bytes at an absolute 24-bit address;
- `G<address>`: call or jump to the given address; and
- `;...`: comment.

It contains no:

- filename;
- application name;
- relocation data;
- section metadata;
- length declaration;
- checksum;
- symbol table;
- end-of-file record; or
- standard load-without-running command.

The hardware loader accepts uppercase hex, has an 80-character line limit,
and can partially modify memory before reporting malformed input. Images must
therefore be generated and validated strictly.

For physical hardware, use full `.lgo` output:

```bash
cor24-asm program.s -o program.lgo --lgo-full
```

A compact `.lgo` assumes omitted areas are already zero and is unsafe after a
warm reload unless the destination memory has been explicitly cleared.

## Recommended ownership and architecture

Use `dcdbg` as the resident monitor project.

- `dcdbg` owns the monitor loop, app directory, launcher, memory validation,
  loader, and eventual debugging facilities.
- `dcyed` remains the editor application and is later adapted to the common
  app ABI.
- `dcscr` remains the scripting application and is later adapted to the
  common app ABI and monitor services.
- A shared ABI should begin as a tiny, versioned specification. Avoid
  creating another repository until multiple implementations establish
  which parts really are shared.

Using `sws` as the first monitor is technically possible, and its current
`run` command is valuable prior art. It is not the recommended first
structure because application management, loading, and debugging would become
coupled to the scripting interpreter's pools, evaluator, error handling, and
top-level halt behavior.

## Proof-of-concept loading recommendation

Do not begin with an on-device `.lgo` parser. Build one composite full `.lgo`
on the host:

```text
+---------------------------+
| monitor code and data     |
+---------------------------+
| uppercase app code/data   |
+---------------------------+
| lowercase app code/data   |
+---------------------------+
| app directory             |
+---------------------------+
| shared ABI area           |
+---------------------------+
| reserved stack region     |
+---------------------------+
```

Each component is compiled/assembled for a fixed, non-overlapping address.
The composite builder:

1. collects component `L` records;
2. rejects overlapping address ranges;
3. removes or rejects application `G` records;
4. generates the app directory;
5. emits exactly one final `G` record for the monitor entry; and
6. emits a full image for safe hardware loading.

The exact composite `.lgo` that passes in `cor24-emu` should be uploaded to
the COR24-TB. Do not rebuild different emulator and hardware artifacts.

## Initial callable-app ABI

The simplest first ABI is:

```c
int app_main(void);
```

The monitor calls the application's entry as a function. The application
returns an integer rather than halting.

Initial return codes:

```text
0  normal quit
1  application error
2  monitor-requested or detected abort
```

Initial rules:

- The active component exclusively owns UART input.
- An application may use UART directly while it is active.
- An application must return from `app_main`.
- An application must not call the target halt routine on ordinary quit.
- An application must obey the compiler calling convention.
- App code, initialized data, BSS, scratch data, and stack use must fit its
  declared memory contract.
- The application must not overwrite monitor, directory, MMIO, or another
  app's memory.
- The monitor regains UART ownership after the call returns.
- A stuck application requires reset in the first cooperative version.

### Reserved UART escape

Use an on-wire attention protocol instead of permanently reserving a normal
keyboard control character.

Recommended long-term on-wire encoding:

```text
FF 00  debugger attention/break
FF FF  deliver one literal FF byte to the application
```

`FF` means byte `0xFF`, not two ASCII `F` characters. Byte `0xFF` is not a
valid standalone UTF-8 byte and is not generated by ordinary ASCII terminal
input, ANSI cursor keys, or Emacs control/meta key sequences. The host bridge
maps a configurable local gesture to `FF 00`; the target application never
needs to give up a normal Emacs key binding.

The short proof of concept may use Ctrl-] internally if it is substantially
easier, but this must be treated as a temporary test convention rather than
the final ABI.

Do not use Ctrl-Q as the default. In an Emacs-inspired editor, Ctrl-Q is
conventionally useful as quoted insert, and at the host-terminal layer it is
ASCII XON (`0x11`) and may be consumed by software flow control.

Other control bytes also have host-side conventions:

- Ctrl-C may be treated as a local interrupt;
- Ctrl-\ may be treated as local SIGQUIT;
- Ctrl-S and Ctrl-Q are XOFF/XON;
- Ctrl-D is commonly EOF; and
- Ctrl-] is used as an escape by some terminal programs and may also be a
  legitimate editor binding.

The host-side terminal/bridge must therefore:

- use raw input mode;
- disable `ixon`, `ixoff`, and local signal-character handling;
- transmit all eight data bits unchanged;
- map a configurable local debugger gesture to `FF 00`; and
- document any local escape needed to exit the host terminal itself.

There are two distinct implementation stages:

1. **Cooperative proof of concept:** the app's monitor-aware input function
   recognizes the attention sequence and returns an app status requesting
   return to the monitor.
2. **Preemptive debugger:** the UART RX interrupt handler owns input,
   recognizes `FF 00`, saves the interrupted app context, and transfers
   control to the debugger even if the app is not polling for input.

The user-visible host gesture is configurable and is not part of the target
ABI. For example, a host bridge might map F12, a command palette action, or a
locally chosen chord to `FF 00`. This keeps all Emacs control/meta sequences
available to yocto-ed.

For binary input, `FF` must be quoted as `FF FF`. Text and `.lgo` input do not
normally contain `FF`, but the bridge should still implement quoting so the
transport contract is complete. Escape decoding applies only to inbound UART
data, never to application output.

Once the no-argument function call is proven for two applications, introduce
a versioned context:

```c
struct cor24_app_context {
    int abi_version;
    int app_id;
    int command;
    char *input;
    int input_capacity;
    char *output;
    int output_capacity;
};

int app_main(struct cor24_app_context *context);
```

If the relevant `tc24r` calling convention or function-pointer argument
behavior is not yet tested, a fixed-address context block is an acceptable
intermediate step.

## App directory recommendation

Replace the single word at `0x0FFE00` with a versioned directory. The exact
binary packing should be specified by the implementing agent after confirming
24-bit alignment and C layout behavior.

Logical fields:

```text
directory:
  magic
  ABI version
  entry count

app entry:
  numeric id
  flags
  base address
  exclusive limit address
  callable entry address
  stack bottom
  stack top
  fixed-size inline ASCII name
```

Suggested initial flags:

```text
CALLABLE
INTERACTIVE
USES_UART
HAS_INPUT_OBJECT
HAS_OUTPUT_OBJECT
```

Inline fixed-size names are recommended initially. Host-generated C pointers
would require relocation and make the directory less portable.

## Monitor loop example

Pseudocode:

```text
initialize UART
validate app directory

forever:
    print menu from app directory
    read one command line

    if command is list:
        list apps
    else if command selects an app:
        validate app entry and memory bounds
        reset shared ABI state
        reset or select application stack
        call app entry
        restore monitor stack/state
        print return status
    else if command is help:
        print help
    else if command is halt:
        halt or request board reset
    else:
        print error
```

Example UART transcript:

```text
COR24 monitor 0.1

1  upper
2  lower
h  help
q  halt
mon> 1

upper: enter text; debugger-attention returns
upper> Hello Cor24
HELLO COR24
upper> <BREAK>

upper returned 0

1  upper
2  lower
h  help
q  halt
mon> 2

lower: enter text; debugger-attention returns
lower> Hello Cor24
hello cor24
lower> <BREAK>

lower returned 0
mon>
```

`<BREAK>` above is transcript notation for the host bridge sending `FF 00`;
the application does not echo those bytes.

## Demo application examples

These are behavioral examples, not repository-ready code.

Uppercase application:

```text
loop:
    read a bounded line from UART
    if input reports debugger attention:
        return 0
    for each character:
        if character is between 'a' and 'z':
            character = character - 'a' + 'A'
        write character to UART
    write CRLF
```

Lowercase application:

```text
loop:
    read a bounded line from UART
    if input reports debugger attention:
        return 0
    for each character:
        if character is between 'A' and 'Z':
            character = character - 'A' + 'a'
        write character to UART
    write CRLF
```

Both must define:

- maximum line length;
- CR, LF, and CRLF behavior;
- backspace behavior;
- input echo policy;
- overflow behavior; and
- EOF or UART error behavior.

The echo apps should use a shared monitor-aware input routine so their
cooperative escape behavior matches the later interrupt-driven input
contract.

## Recommended implementation phases

### Phase 0: validate the physical-board foundation

Goal: distinguish loader/UART/stack failures from monitor failures.

Checklist:

- [ ] Record COR24-TB board revision.
- [ ] Record installed FPGA image/release.
- [ ] Record UART adapter chipset, VID:PID, driver, and stable device path.
- [ ] Confirm 3.3 V UART wiring with no adapter power connection.
- [ ] Confirm 921600 baud, 8N1, raw mode, and hardware flow control.
- [ ] Obtain ten repeatable monitor banners from cold starts.
- [ ] Validate adapter loopback and RTS/CTS behavior.
- [ ] Build and test `te-rs`.
- [ ] Establish reliable synchronized upload with conservative pacing.
- [ ] Load and run the supplied `hello.lgo`.
- [ ] Load and run the existing Tiny C UART counter.
- [ ] Load and run a minimal Tiny C UART echo image.
- [ ] Run the 3 KB EBR stack probe.
- [ ] Record the usable stack region and capacity.
- [ ] Preserve commands, artifacts, hashes, and UART transcripts.

Gate:

- [ ] At least one supplied image and one sw-embed Tiny C image load and
      behave correctly on the physical board.
- [ ] The monitor implementation can rely on a documented stack range and a
      repeatable upload path.

### Phase 1: prove two callable apps in the emulator

Proposed owner: `dcdbg` coding agent/user, with ABI questions coordinated with
`dcxtc` if compiler behavior needs clarification.

Checklist:

- [ ] Create the monitor project build and test scaffold.
- [ ] Add polled UART input and output.
- [ ] Add a static two-entry app directory.
- [ ] Add menu rendering from directory entries.
- [ ] Add bounded line input.
- [ ] Add app selection by id or name.
- [ ] Add a callable-entry trampoline.
- [ ] Add normal-return status reporting.
- [ ] Create uppercase echo app.
- [ ] Create lowercase echo app.
- [ ] Assemble all three components at fixed, non-overlapping bases.
- [ ] Start execution at the monitor only.
- [ ] Test upper interaction and return.
- [ ] Test lower interaction and return.
- [ ] Test repeated upper/lower switching.
- [ ] Test invalid menu choices.
- [ ] Test empty lines, maximum lines, backspace, CR, LF, and CRLF.
- [ ] Test that `FF 00` returns from each app.
- [ ] Test that `FF FF` delivers one literal `0xFF` to the app.
- [ ] Test that Emacs-oriented Ctrl, Meta/ESC, and prefix bindings remain
      available to yocto-ed.
- [ ] Test that Ctrl-Q remains available for editor quoted-insert behavior
      and is not treated as the monitor escape.
- [ ] Test the host terminal with `ixon`, `ixoff`, and signal-character
      processing disabled.
- [ ] Confirm monitor state remains valid after at least 100 app returns.
- [ ] Confirm stack pointer returns to its expected monitor value.
- [ ] Bound all automated emulator runs by instruction count and host timeout.

Gate:

- [ ] One emulator session supports repeated interactive selection, use, and
      cooperative quit of both apps without resetting.

### Phase 2: build a reproducible composite `.lgo`

Proposed owner: `dcdbg` coding agent/user, potentially coordinated with
`dcxas` for assembler output/listing interfaces.

Checklist:

- [ ] Define a host-readable app manifest.
- [ ] Declare monitor and app base/limit ranges.
- [ ] Generate full `.lgo` component images.
- [ ] Extract callable entries from a stable assembler interface or listing.
- [ ] Generate the app directory.
- [ ] Reject address overlaps.
- [ ] Reject writes into MMIO or reserved stack ranges.
- [ ] Reject unexpected component `G` records.
- [ ] Append exactly one monitor `G` record.
- [ ] Emit a human-readable memory map.
- [ ] Emit artifact size, line count, maximum line length, and hash.
- [ ] Validate all `.lgo` lines before execution/upload.
- [ ] Run the final unchanged image in `cor24-emu`.

Example manifest shape:

```text
monitor base=0x000000
upper   base=0x020000
lower   base=0x030000
directory address=0x0FFD00
```

The addresses above are examples only. The implementing agent must select
them after measuring actual images, BSS, shared buffers, MMIO, and verified
stack placement.

Gate:

- [ ] A deterministic build produces one validated, full `.lgo` and one
      reviewable memory map.

### Phase 3: run the same composite image on COR24-TB

Checklist:

- [ ] Record the composite artifact hash.
- [ ] Upload the exact emulator-tested artifact.
- [ ] Capture the complete upload transcript.
- [ ] Capture boot/menu UART output.
- [ ] Run upper and return.
- [ ] Run lower and return.
- [ ] Repeat switching at least 100 times or with a bounded UART script.
- [ ] Test a cold load.
- [ ] Test a warm reload using the full `.lgo`.
- [ ] Verify stack and monitor state after repeated returns.
- [ ] Record failures with last acknowledged load line and exact behavior.

Gate:

- [ ] The physical board meets the proof-of-concept interaction contract
      without reset between applications.

### Phase 4: adapt dcyed and dcscr as applications

Proposed owners: their corresponding coding agents/users. The `dcdbg` agent
supplies the versioned ABI specification and integration tests, not edits to
the other repositories.

dcyed checklist:

- [ ] Add an ABI-compatible callable entry without breaking standalone use.
- [ ] Replace fixed text/output addresses with supplied object/context
      pointers or a documented compatibility adapter.
- [ ] Keep `quit` as return-to-monitor.
- [ ] Report normal/error return codes.
- [ ] Make rendering and returned edited content distinguishable.
- [ ] Test standalone behavior.
- [ ] Test monitor-called behavior.

dcscr checklist:

- [ ] Separate interpreter entry from standalone startup.
- [ ] Make app-mode `exit` return rather than halt.
- [ ] Preserve standalone halt behavior where appropriate.
- [ ] Replace hard-coded `run_resolve("swye")` with a versioned monitor
      service or app-directory lookup.
- [ ] Replace fixed run buffers with monitor objects/context.
- [ ] Test standalone REPL behavior.
- [ ] Test monitor-called behavior.
- [ ] Test `sws -> editor -> return -> sws -> return -> monitor`.

Integration gate:

- [ ] The monitor can list, run, and regain control from `dcyed` and `dcscr`
      in addition to the two echo apps.

### Phase 5: add a RAM-resident named object store

Do this before claiming filesystem support.

Logical object fields:

```text
id
type: text | script | data | app-image
name
address
length
capacity
flags
```

Checklist:

- [ ] Define object table binary layout and version.
- [ ] Reserve non-overlapping object storage.
- [ ] Add `ls`.
- [ ] Add object selection.
- [ ] Add bounded UART data loading.
- [ ] Add object deletion/reset.
- [ ] Pass a text object to the editor.
- [ ] Pass a script object to `sws`.
- [ ] Preserve object metadata across app calls.
- [ ] Define behavior on storage exhaustion.
- [ ] Define whether objects survive warm app reloads.

Gate:

- [ ] Named text or script data can be loaded, listed, edited, and consumed
      without an external filesystem.

### Phase 6: add live `.lgo` reception

Initial live loading remains fixed-address, not relocatable.

Checklist:

- [ ] Define framing around `.lgo`, because `.lgo` has no EOF record.
- [ ] Accept only `L`, `G`, and `;` records.
- [ ] Enforce uppercase hex.
- [ ] Enforce line-length and even-nybble rules.
- [ ] Parse into temporary state before committing directory metadata.
- [ ] Range-check every write.
- [ ] Reject monitor, directory, stack, MMIO, and occupied app ranges.
- [ ] Treat incoming `G` as a proposed entry, not immediate execution.
- [ ] Require or synthesize an app name.
- [ ] Commit an app entry only after a complete valid transfer.
- [ ] Define cancel and timeout recovery.
- [ ] Define rollback or invalid-slot behavior after a partial transfer.
- [ ] Test malformed records without corrupting the resident monitor.
- [ ] Test warm replacement using full images or explicit clearing.

Gate:

- [ ] A new fixed-address application image can be received over UART,
      listed, selected, run, and returned from without rebooting.

### Phase 7: add debugger facilities

Start with emulator-native logical breakpoints, which do not patch program
bytes.

Checklist:

- [ ] Define saved architectural state.
- [ ] Define whether stop occurs before or after instruction execution.
- [ ] Add register display.
- [ ] Add memory display.
- [ ] Add logical emulator breakpoint set/clear/list.
- [ ] Add step and continue.
- [ ] Add watches evaluated at stops.
- [ ] Import symbols/listings on the host.
- [ ] Make displays app-directory aware.
- [ ] Design hardware breakpoint patching only after callable app, stack, and
      memory ownership contracts are stable.
- [ ] Specify displaced-instruction and condition-state restoration.
- [ ] Specify recovery from a breakpoint in a branch or PC-relative sequence.

Gate:

- [ ] Emulator debugging works without changing target program bytes.
- [ ] Any hardware breakpoint proposal includes an instruction-level
      correctness argument and tests.

## Follow-on preemptive UART debugger

### Viability

The proposed GDB-like direction is viable, with important constraints.

The current COR24 model provides the necessary basic UART interrupt behavior:

- UART RX interrupt enable is at `0xFF0010`, bit 0.
- A received byte can cause an interrupt at an instruction boundary.
- The processor saves the interrupted PC in `ir`.
- The processor jumps through `iv`.
- Nested interrupts are suppressed while interrupt service is active.
- Reading UART data acknowledges/clears RX-ready.
- `jmp (ir)` returns from the interrupt and clears interrupt-in-service.

Existing workspace assembly demonstrates saving `r0`, `r1`, `r2`, `fp`, and
the condition flag, then returning through `ir`. This is evidence for the
mechanism, not yet physical-board validation. The supplied UART interrupt
image has not been meaningfully validated in the current emulator because of
its selected stack address, and hardware UART interrupt behavior remains a
Phase 0 test item.

### UART ownership required for preemption

An interrupt handler cannot safely inspect a reserved byte and then let an
unmodified polling application reread that same UART register. Reading the
register to classify the byte acknowledges and consumes it.

Therefore, preemptive escape requires UART RX virtualization:

```text
UART hardware RX
       |
       v
monitor-owned ISR
       |
       +-- FF 00 ----------> save app context and enter debugger
       |
       +-- FF FF ----------> enqueue literal FF
       |
       `-- other byte -----> enqueue in monitor RX ring
                                  |
                                  v
                           app_getc service
```

Consequences:

- monitor-aware apps must call `app_getc` or an ABI service instead of reading
  `0xFF0100` directly;
- apps must not replace `iv`, disable the reserved UART interrupt, or return
  from the monitor ISR themselves;
- legacy direct-MMIO apps may run only in a compatibility mode without
  preemptive UART escape, or must be adapted;
- the RX ring needs overflow behavior and tests; and
- a multi-byte escape sequence needs buffering and timeout/quoting rules.

The `FF` prefix requires one byte of decoder state. A pending `FF` is retained
until the next byte arrives: `00` means attention and `FF` means literal
`FF`. Other second bytes should be rejected as protocol errors or handled by
explicit future protocol versions. This avoids timer-dependent guard
sequences.

### Entering the debugger from the ISR

The interrupt entry stub must save the complete app state before using C or
the monitor stack:

```text
r0, r1, r2, fp, app sp, condition flag, iv, ir, interrupt-enable state
```

`ir` is the interrupted PC and is the most important stop field. The entry
stub should switch to a private debugger stack before invoking substantial
debugger code. It must not depend on the app stack being valid.

For an ordinary received character, restore state and execute `jmp (ir)`.
For debugger attention, retain the saved frame and enter the debugger command
loop. A
later `continue` restores the saved state and returns through the saved
interrupted PC.

The implementing agent must verify on both emulator and hardware:

- the exact PC stored in `ir`;
- whether the interrupted instruction has or has not executed;
- all condition-state save/restore sequences;
- interrupt-enable and interrupt-in-service transitions;
- stack switching;
- behavior when the interrupt arrives during UART MMIO code; and
- behavior when the interrupt arrives while debugger code is already active.

### Emulator versus hardware breakpoints

Keep two breakpoint backends behind one user-facing command set:

```text
break ADDRESS
delete BREAKPOINT
info break
continue
step
```

Emulator backend:

- stores logical breakpoint addresses outside target memory;
- stops before instruction execution;
- does not patch bytes; and
- should be implemented and validated first.

Hardware backend:

- patches writable application code;
- has stricter placement and spacing rules;
- needs a breakpoint table containing original bytes and state; and
- may support fewer breakpoints.

The emulator already has logical breakpoint, step, step-over, register,
memory, and disassembly facilities. The resident monitor need not duplicate
disassembly to provide useful hardware debugging.

### Hardware breakpoint patch design

The proposed branch-to-debug-handler design can work, but a short `bra`
cannot necessarily reach a central debugger handler. COR24 short branches:

- occupy 2 bytes;
- use a signed 8-bit offset;
- calculate from instruction address plus 4; and
- reach only approximately -128 through +127 bytes.

A hardware breakpoint therefore needs either:

1. a reachable nearby trampoline;
2. a 4-byte absolute jump patch; or
3. a future hardware/ISA trap facility.

The nearby-trampoline model is recommended for investigation:

```text
patched breakpoint address:
    bra breakpoint_trampoline_N

breakpoint_trampoline_N:
    establish breakpoint id N
    jump absolute to common debugger entry
```

Each trampoline can identify the breakpoint explicitly, avoiding a search
based on uncertain PC state. Possible identification methods include:

- one unique trampoline per active breakpoint;
- a trampoline that loads a breakpoint id before jumping; or
- a trampoline whose known address maps directly to a table entry.

An absolute patch can reach the common handler but occupies 4 bytes and may
overwrite several variable-length instructions. COR24 instructions are 1, 2,
or 4 bytes, so neither a 2-byte nor a 4-byte patch is universally safe at an
arbitrary instruction boundary.

### Placement and spacing limitations

It is reasonable for the first hardware debugger to impose restrictions:

- breakpoint address must be a known instruction boundary;
- breakpoint patch must cover whole instructions;
- no branch or call target may enter the middle of the displaced range;
- no two active breakpoint patch ranges may overlap;
- nearby trampolines must be within short-branch range;
- breakpoints may be forbidden near code-region edges;
- breakpoints may be forbidden in monitor, ISR, trampoline, or read-only
  regions; and
- breakpoints may require assembler/compiler-generated debug pads.

Debug pads are a particularly robust option for compiled apps. The build can
reserve a patchable 4-byte sequence at selected statement/function
boundaries and include their addresses in a compact host-side map. This trades
code space for predictable breakpoint placement and avoids needing a complete
on-target disassembler.

### Continuing after a hardware breakpoint

When breakpoint N is hit:

1. Save the complete stopped context.
2. Mark breakpoint N as hit.
3. Restore N's original instruction bytes.
4. Determine every possible next PC after the displaced instruction or
   instruction group.
5. Install temporary breakpoints at those successor addresses.
6. Resume at the original breakpoint address.
7. When a temporary breakpoint fires, remove temporary patches.
8. Reinstall breakpoint N.
9. Stop for `step`, or continue for `continue`.

This requires instruction decoding, but not necessarily formatted
disassembly. A compact decoder only needs:

- instruction length;
- whether control flow is sequential, conditional, direct, or indirect;
- direct branch target calculation;
- fall-through address; and
- which register supplies an indirect target.

Successor handling:

- ordinary instruction: one successor at `PC + length`;
- unconditional direct branch: branch target;
- conditional branch: branch target and fall-through;
- indirect jump: target from the saved register;
- call: call target for step-into, return/fall-through address for step-over;
- return: target from the saved link register.

Conditional branches require temporary breakpoints at both possible
successors unless the saved condition is used to choose the one actual path.
Using both is simpler but consumes more breakpoint/trampoline capacity.

If safe temporary patches cannot be placed at all successors, the debugger
must reject that step/continue operation or use an emulator-only facility. It
must not silently resume with incorrect semantics.

### Breakpoint state and concurrency

Suggested logical breakpoint record:

```text
id
state: armed | hit | stepping | disabled
address
patch length
original byte count
original bytes
trampoline address
temporary successor addresses
hit count
```

Breakpoint-table lookup should not depend solely on the common handler's
current PC. A unique trampoline or explicit id makes identification
deterministic.

UART interrupts must be disabled while patch bytes, trampoline state, and
breakpoint-table state are being changed. Patch updates should be treated as
a small critical section. Resume must not occur until code bytes and table
state agree.

Instruction prefetch may also matter: after changing code at or near the
current PC, the implementation must verify whether the hardware requires
branching away, pipeline flushing, or another synchronization sequence before
the restored bytes are fetched. This must be tested on COR24-TB rather than
inferred solely from the emulator.

### Minimal debugger without disassembly

Space constraints do not prevent a useful debugger. A first hardware command
set can be:

```text
break ADDRESS
delete ID
info break
continue
step
regs
x ADDRESS LENGTH
set ADDRESS VALUE
quit
```

The target can print:

- breakpoint id;
- stopped PC;
- registers and condition state;
- raw memory in hex;
- app name and address range; and
- breakpoint/watch tables.

Source names, labels, formatted disassembly, and line mappings can remain in
the host tooling or assembler listing.

### Preemptive debugger checklist

- [ ] Validate UART RX interrupts on the physical COR24-TB.
- [ ] Validate exact `ir` semantics at instruction boundaries.
- [ ] Reserve on-wire `FF 00` as debugger attention.
- [ ] Reserve on-wire `FF FF` as a quoted literal `FF`.
- [ ] Verify the host serial path is eight-bit clean.
- [ ] Add a configurable host gesture that emits `FF 00`.
- [ ] Verify yocto-ed retains its Emacs-oriented key namespace.
- [ ] Add monitor-owned UART RX ISR.
- [ ] Add RX ring for non-reserved bytes.
- [ ] Add ABI `app_getc`.
- [ ] Define compatibility behavior for direct-MMIO apps.
- [ ] Save and restore every architectural register and condition state.
- [ ] Switch from app stack to private debugger stack on interrupt.
- [ ] Prevent nested debugger entry.
- [ ] Enter the debugger on receipt of `FF 00`.
- [ ] Continue from the exact interrupted PC.
- [ ] Implement emulator logical breakpoints first.
- [ ] Define a compact instruction-boundary/successor decoder.
- [ ] Define hardware breakpoint placement restrictions.
- [ ] Prototype nearby unique trampolines.
- [ ] Record original bytes and patch lengths.
- [ ] Reject overlapping or unreachable breakpoints.
- [ ] Restore original bytes on hit.
- [ ] Install temporary successor breakpoints for step-over.
- [ ] Re-arm the original breakpoint after the temporary stop.
- [ ] Protect patch/table updates as interrupt-disabled critical sections.
- [ ] Test pipeline/prefetch behavior after code modification on hardware.
- [ ] Keep disassembly and symbol formatting optional and preferably host-side.

## Cross-project contract checklist

- [ ] One written ABI version is shared by monitor and apps.
- [ ] Entry address means callable function entry, not standalone halt stub.
- [ ] Return and halt semantics are unambiguous.
- [ ] UART ownership is unambiguous.
- [ ] Stack ownership and bounds are documented.
- [ ] Monitor state restoration is tested.
- [ ] App memory ranges include code, initialized data, BSS, and scratch.
- [ ] Shared buffers have declared owners, capacities, and lifetimes.
- [ ] Directory and object layouts avoid relying on unverified C padding.
- [ ] Every artifact has a memory map.
- [ ] Every hardware image is a full `.lgo` until clearing is proven.
- [ ] Emulator and hardware tests use the same hashed image.
- [ ] Physical-board results are recorded separately from emulator results.
- [ ] Each dc* repository change is made by its coding agent/user under its
      own process.

## Test matrix

| Test | Emulator | COR24-TB | Expected result |
| --- | --- | --- | --- |
| Monitor boot | Required | Required | Banner and menu |
| List two apps | Required | Required | `upper`, `lower` |
| Invalid selection | Required | Required | Error, menu remains usable |
| Uppercase line | Required | Required | ASCII lowercase converted |
| Lowercase line | Required | Required | ASCII uppercase converted |
| App quit | Required | Required | Returns to `mon>` |
| Alternating apps | Required | Required | No reset or corruption |
| 100 returns | Required | Required | Stable SP and monitor state |
| Maximum input | Required | Required | Bounded deterministic result |
| Warm full-image reload | Required | Required | Same behavior as cold load |
| Malformed live load | Later | Later | Rejected without monitor loss |
| Stuck app | Later | Later | Reset initially; debugger later |

## Risks and mitigations

### Stack placement and leakage

Risk: independently compiled applications share or corrupt the monitor stack,
or each call leaks stack space.

Mitigation:

- validate the physical and emulator stack contract first;
- record SP before and after every launch in debug builds;
- repeat calls many times;
- add a launcher trampoline and per-app stack when the ISA/compiler mechanics
  are proven.

### Absolute-address and BSS collision

Risk: relocating only machine-code bytes leaves absolute data references or
BSS colliding with another component.

Mitigation:

- assemble each complete application for its final base;
- obtain all occupied ranges from the assembler/build;
- reject overlap in the composite builder;
- include zero-initialized areas in full hardware images.

### Wrong entry point

Risk: the monitor calls standalone startup, which halts when `main` returns.

Mitigation:

- call the compiler-generated callable entry demonstrated by the existing
  `swye._main` integration;
- verify entry extraction from the assembler listing;
- add a test that execution resumes immediately after the call.

### UART contention

Risk: monitor and application both consume bytes, or a menu keystroke leaks
into the application.

Mitigation:

- use cooperative exclusive UART ownership;
- read complete monitor command lines;
- flush or explicitly preserve pending input according to a written policy;
- add preemptive interruption only after an interrupt/debug design exists.

### Malformed or unsafe `.lgo`

Risk: partial writes corrupt memory before an error is detected.

Mitigation:

- generate images on the host for the first proof;
- validate syntax and all address ranges before hardware upload;
- for live loading, parse and range-check each complete line before writing;
- reserve the monitor, directory, stack, and MMIO ranges.

### Warm reload state

Risk: omitted zero regions retain prior app data.

Mitigation:

- use `--lgo-full` for hardware;
- later allow compact loading only after explicit destination clearing.

### No memory protection

Risk: a defective app can overwrite the resident monitor.

Mitigation:

- begin with tiny reviewed apps;
- enforce ranges in loaders and builders;
- use canaries and state checks;
- accept that software conventions cannot stop arbitrary erroneous stores
  without hardware protection.

## Open decisions for the implementing users

- [ ] Confirm that `dcdbg` should own the resident monitor.
- [ ] Confirm whether the first setup script means a host uploader/composite
      builder or an on-board `sws` startup script.
- [ ] Confirm that host-side composition of fixed-address apps is acceptable
      for the first proof.
- [ ] Record the actual COR24-TB SRAM and stack map.
- [ ] Decide whether initial app quit is cooperative only.
- [ ] Decide whether monitor selection is numeric, named, or both.
- [ ] Decide CR/LF, echo, editing, and line-overflow behavior.
- [ ] Decide whether app stacks are shared initially or isolated from the
      first implementation.
- [ ] Decide the initial directory and shared-context addresses only after
      measuring actual images and reserved hardware ranges.
- [ ] Decide how implementation briefs are handed to the individual dc*
      coding agents/users.

## Recommended immediate next actions

1. Finish the existing physical-board Phase 0 validation in
   `reference/plan.md` and `reference/status.md`.
2. Have the `dcdbg` coding agent/user write an ABI/memory-map design step,
   using the existing `dcscr -> dcyed` call as prior art.
3. Have the `dcdbg` coding agent/user implement the emulator-only two-app
   proof with fixed addresses and cooperative return.
4. Add a deterministic composite full-`.lgo` builder and overlap checks.
5. Run the exact hashed image on COR24-TB.
6. Only after the proof passes, prepare separate adaptation briefs for the
   `dcyed` and `dcscr` coding agents/users.

The recommended first milestone is intentionally not a filesystem, dynamic
relocator, preemptive operating system, or on-board compiler. It is a small
resident monitor that proves memory layout, callable entry, stack integrity,
UART ownership, cooperative return, and repeatable app switching on both the
emulator and the physical board.
