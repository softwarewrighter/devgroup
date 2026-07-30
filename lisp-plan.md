# COR24 Monitor plus Standard Lisp Plan

## Purpose

This is the next milestone after [scheme-plan.md](scheme-plan.md). It keeps the
same COR24-TB monitor, memory-map, UART-interrupt, Ctrl-], warm-reset, composite
`.lgo`, and application-launch design. It changes the Macro Lisp payload from
the small Scheme prelude to the standard prelude and demonstrates two ways to
deliver Lisp applications:

1. send a `.l24` source file over UART and evaluate it; and
2. embed selected `.l24` sources in the composite `.lgo`, evaluate one after a
   menu selection, and leave the user at an interactive REPL.

This document is a plan for dcdbg, dcmls, dcscr, dcyed, and terminal-tool
developers. It does not implement changes in those repositories.

## Recommended proof of concept

Use one relocated Macro Lisp runtime with several callable entry wrappers, not
one copy of the interpreter per menu item.

```text
COR24 Lisp monitor
1: Standard Lisp REPL
2: Embedded list demo
3: Embedded conversion demo
l: Load and evaluate .l24
h: help
q: quit
mon>
```

All four Lisp choices perform a fresh interpreter initialization and load the
standard prelude. Choices `2` and `3` then evaluate a build-time embedded
source and enter the REPL in the resulting environment. Choice `l` enters a
source-load session, receives a host file, evaluates it, and then enters the
REPL. Ctrl-] abandons any Lisp choice and restarts the monitor menu.

This proves:

- one interpreter image can support multiple Lisp applications;
- standard-prelude initialization is repeatable;
- `.l24` can be delivered independently of `.lgo`;
- `.l24` can instead be packaged inside `.lgo`;
- a loaded or embedded application remains interactively inspectable; and
- UART interrupt escape remains owned by the monitor.

Do not include dcscr or dcyed in the visible menu for this milestone. Keep
their address reservations and packaging metadata from `scheme-plan.md` so
they can be added later without redesigning the Lisp interface.

## What Ctrl-R does today

The inspected `te` and `te-rs` implementations consume Ctrl-R (`0x12`) on the
host. The byte is not sent to COR24. The terminal prompts locally for a path,
opens that file, and sends its contents to the serial port.

This already makes the following interaction conceptually possible:

```text
mon> l
L24 LOAD READY -- press Ctrl-R and select a .l24 file

<Ctrl-R>
file: /absolute/path/demo.l24
```

The legacy `te` sends the bytes without an application-level completion or
acknowledgement protocol. `te-rs` adds line delay, byte delay, and a `--sync`
mode. Its present `--sync` mode is designed for load-and-go records: it sends
a line and expects that exact line to be echoed. A Lisp REPL prints evaluation
results and prompts instead, so present `--sync` is not a correct `.l24`
protocol.

Unsynchronized Ctrl-R may work for a very small, deliberately paced source,
but it is not sufficient as the accepted design. Hardware RTS/CTS protects
the UART device FIFO; it does not by itself prove that the monitor's software
RX ring cannot overflow while Lisp is evaluating an earlier form.

## Recommended `.l24` UART protocol

Extend `te-rs` with a source-transfer mode while preserving existing `.lgo`
behavior. Selecting a pathname ending in `.l24` may select this mode
automatically, but an explicit prompt or option is preferable so behavior is
visible:

```text
file: demo.l24
mode: l24 source
```

The first proof should use a simple stop-and-wait protocol:

1. The monitor launches the Lisp `load` wrapper.
2. Lisp initializes a fresh heap, evaluator, and standard prelude.
3. The target prints `L24 READY`.
4. Ctrl-R remains a host-only terminal command.
5. `te-rs` sends one source byte.
6. When the Lisp input path consumes that byte, the target transmits ACK
   (`0x06`).
7. `te-rs` forwards other target output to the terminal and waits for ACK
   before sending the next source byte.
8. At host-file EOF, `te-rs` sends EOT (`0x04`).
9. The target completes the current form, reports a form/error count, prints
   `L24 DONE`, and enters the interactive REPL.

ACK travels target-to-host only and is protocol framing, not Lisp input. EOT
is meaningful only while the explicit Lisp load mode is active. Ctrl-]
remains a monitor-owned incoming attention byte and may abort a transfer.

Stop-and-wait will be slower than a windowed protocol, but `.l24` demos are
small and correctness is more important for the proof. It naturally applies
backpressure while Lisp evaluates: `te-rs` cannot send the next byte until
the application consumes the prior byte. A later version may advertise a
receive window or use framed blocks with sequence numbers and checksums.

### Abort and failure behavior

- Ctrl-] during transfer aborts Lisp and returns to the menu.
- `te-rs` Ctrl-C exits the host terminal as it does today; it is not a target
  abort command.
- Disconnect, ACK timeout, unexpected reset banner, or RX overflow fails the
  transfer visibly.
- S1 abandons the transfer and starts the EBR-resident load-and-go program.
- A source file containing raw EOT is rejected; `.l24` is a text format.
- A source `(exit)` follows the monitor-app exit policy. For the first proof,
  demo files should omit `(exit)` and finish at EOF.
- A Lisp read/evaluation error reports the form number and either stops the
  load or continues according to one documented policy. Stop-on-first-error is
  recommended initially.

### Source reader requirements

The current REPL reads into a 1,024-byte buffer, tracks parentheses and
strings, and evaluates forms after balanced input. The production loader must:

- define whether 1,023 bytes is the maximum physical line, complete form, or
  buffered source unit;
- detect overflow rather than evaluating truncated text;
- preserve physical newlines so `;` comments end at the correct place;
- handle CR, LF, and CRLF consistently;
- accept blank and comment-only lines;
- support multiple top-level forms;
- recognize EOT only outside a string/comment and only after a complete form,
  or report a truncated-form error;
- count successfully evaluated top-level forms; and
- prevent Ctrl-] and protocol bytes from entering Lisp data.

The present multiline reader replaces an internal newline with a space. That
can make a semicolon comment consume text from a following physical line.
Fix this for source loading by preserving newline in the buffer, or use a
stream reader that has explicit string/comment/parenthesis state.

For an initial terminal-only experiment before the ACK protocol exists, use a
small `.l24` file with short forms, remove comment lines, put each complete
form on one physical line, run `te-rs` with conservative byte pacing, and
inspect results. This is a diagnostic bridge, not an acceptance path.

## Embedded `.l24` applications

The composite builder should turn selected `.l24` files into immutable byte
arrays in the relocated Macro Lisp image. Do not manually maintain C copies of
the Lisp source.

Recommended generated interface:

```c
struct embedded_l24 {
    const char *name;
    const unsigned char *source;
    unsigned int length;
    const char *sha256;
};

extern const struct embedded_l24 embedded_l24_table[];
extern const unsigned int embedded_l24_count;
```

The generator must preserve source bytes, escape them safely for the selected
assembler/C path, include a terminating byte only when the evaluator requires
one, and emit source path plus hash into the build manifest.

Add an evaluator that processes all forms, because the current `eval_str()`
evaluates only the first expression:

```c
int eval_source(const unsigned char *source,
                unsigned int length,
                struct eval_report *report);
```

It should share parsing and error reporting with the UART source loader. The
only difference is the byte provider: memory for an embedded demo and the
monitor RX service for a transferred file.

Each embedded menu selection performs:

```text
fresh Lisp initialization
load standard prelude
evaluate selected embedded source
print application help
enter ordinary interactive REPL
```

Definitions made by the source are then callable and editable through normal
REPL definitions. Returning to the menu with Ctrl-] discards that environment.
Selecting the same demo again reconstructs it from its immutable embedded
source.

## Suggested demos

Use small sources that require only the standard prelude and finish loading
without `(exit)`.

### Demo 1: list workspace

Embed definitions such as:

```lisp
(define samples '(3 -2 7 0 -5 8))
(define (squares xs) (map (lambda (x) (* x x)) xs))
(define (positive-squares xs) (squares (filter positive? xs)))
(define (demo) (positive-squares samples))
```

After loading:

```text
List demo loaded.
Try: (demo), (squares '(2 4 6)), or redefine samples.
> (demo)
(9 49 64)
>
```

### Demo 2: conversion workspace

Embed a small interactive-by-invocation application:

```lisp
(define (c->f c) (+ (/ (* c 9) 5) 32))
(define (f->c f) (/ (* (- f 32) 5) 9))
(define (demo) (list (c->f 0) (c->f 20) (c->f 100)))
```

After loading:

```text
Conversion demo loaded.
Try: (c->f 25), (f->c 77), or (demo).
> (c->f 25)
77
>
```

These are interactive because the user calls and redefines application
functions at the REPL. A conversational application that itself prompts for
arbitrary values would require a Lisp-visible input primitive or a separate
application command loop. That is useful later but is not required to prove
application packaging.

Existing dcmls demos may be substituted only after checking their stated
prelude and stack tier. In particular, do not put a full-prelude demo behind a
standard-prelude menu label, and remove terminal `(exit)` forms from embedded
workspace variants.

## Macro Lisp entry structure

Build one monitor-callable standard Lisp image at the `0x001000` base proposed
by `scheme-plan.md`. Export stable wrapper symbols and include their addresses
in machine-readable build metadata:

```text
lisp_standard_repl_main
lisp_standard_load_main
lisp_standard_demo1_main
lisp_standard_demo2_main
```

Each wrapper calls a shared fresh-initialization routine. Demo wrappers then
call `eval_source()` for their table entry. Do not publish `_repl` or a
build-specific numeric address as a permanent ABI.

An alternative is one entry taking a mode/application ID in a register. Use it
only if the dcdbg-to-dcmls calling convention is explicitly specified and
tested. Separate wrappers are easier to inspect in the first symbol map.

All wrappers retain the requirements from `scheme-plan.md`:

- input comes from `monitor_app_getc()`, not direct UART polling;
- monitor owns UART RX interrupts and Ctrl-];
- `gc_enabled` is disabled before resetting interpreter state;
- heap, GC, symbols, strings, evaluator, globals, reader, and source-loader
  state are reset on every launch;
- standard prelude is loaded after initialization;
- a normal application exit can return through the launch trampoline; and
- interrupt abort abandons the app stack and restores monitor context.

The standard image must be measured again. Do not reuse the Scheme-prelude
exclusive end address as an assertion. Regenerate the actual occupied range
and confirm it remains below the next reserved region.

## Composite packaging

Reuse the deterministic `.lgo` composition rules in `scheme-plan.md`:

- relocate the dcdbg monitor at `0x000000`;
- relocate the single standard Lisp runtime at `0x001000`;
- include embedded demo source bytes in that runtime's measured range;
- retain dormant dcscr/dcyed reservations if desired;
- reject overlaps, out-of-range writes, EBR writes, and MMIO writes;
- remove component `G` records;
- append exactly one final `G000000`;
- record source hashes, component commits, tool versions, entry symbols, and
  occupied ranges; and
- validate the exact artifact in the emulator before hardware upload.

Suggested artifact names:

```text
build/mon-lisp.lgo
build/mon-lisp.map
build/mon-lisp-components.txt
build/mon-lisp-sources.txt
build/mon-lisp.sha256
```

`mon-lisp-sources.txt` should include each embedded `.l24` path, SHA-256,
length, required prelude, and menu entry.

## Required project changes

### dcdbg

- Extend the Scheme proof menu with standard REPL, source load, and two
  embedded-demo entries.
- Launch the generated wrapper symbols through the existing trampoline.
- Keep Ctrl-] preemption and UART RX ownership unchanged.
- Expose transfer-active status so an abort clears the RX ring and protocol
  state.
- Package one dcmls runtime plus its generated embedded-source table.
- Generate the composite map and source manifest.

### dcmls

- Add a monitor-callable standard-prelude build variant.
- Factor fresh initialization into a shared function used by every wrapper.
- Add the four stable wrapper entries.
- Factor an all-forms `eval_source()` path shared by memory and UART sources.
- Add bounded, newline-correct source reading and explicit error reports.
- Add loader-mode EOT handling and target-to-host ACK support.
- Enter the REPL after a successful transfer or embedded evaluation.
- Generate embedded source arrays from `.l24`; do not copy sources by hand.
- Keep standalone builds and tests working.
- Update the dcmls changelog as required by that repository.

### te-rs

- Preserve legacy Ctrl-R `.lgo` upload behavior.
- Add explicit `.l24` source mode.
- Wait for `L24 READY` or require the user to enter source mode first.
- Send source using the ACK/backpressure protocol.
- Send EOT after file EOF.
- Display target output while waiting for ACK.
- Report byte offset and source path on timeout or protocol failure.
- Never send the host Ctrl-R byte to the target.

Legacy `te` may remain a best-effort paced sender for the proof, but `te-rs`
should be the acceptance tool. If both tools must support `.l24`, implement
the same protocol rather than two subtly different formats.

### dcscr and dcyed

No source behavior change is required for this milestone. Preserve their
future memory reservations and document that yocto-ed key bindings do not
affect host-only Ctrl-R during a Lisp source-load session. When the editor is
later active, Ctrl-R belongs to Emacs-style application input unless a
separate framed debugger-attention protocol says otherwise.

## Suggested implementation order

1. Reproduce the `scheme-plan.md` monitor launch and Ctrl-] behavior.
2. Build the monitor-callable standard-prelude runtime and measure its range.
3. Add `eval_source()` and test multiple embedded forms in the emulator.
4. Generate and run embedded demo 1 from a stable wrapper.
5. Add demo 2 and verify that each selection starts with fresh state.
6. Add UART load mode with a test byte provider and explicit EOT.
7. Add the `te-rs` `.l24` ACK sender.
8. Test comments, multiline forms, strings, errors, overflow, abort, and
   transfer completion.
9. Compose `mon-lisp.lgo`, validate it, then upload that exact artifact.
10. Repeat after `(exit)`, Ctrl-], and S1 warm restart.

## Acceptance checklist

### Build and package

- [ ] One standard Lisp interpreter image is present, not one per menu item.
- [ ] Standard Lisp occupied range is measured from the current build.
- [ ] Embedded `.l24` files are generated into immutable data.
- [ ] Embedded source paths, sizes, and hashes appear in the manifest.
- [ ] All component ranges are non-overlapping and inside external SRAM.
- [ ] Composite contains exactly one final `G000000`.
- [ ] Emulator and board receive the same hashed artifact.

### Standard REPL

- [ ] Menu `1` performs fresh initialization and loads the standard prelude.
- [ ] Arithmetic, `map`, `filter`, `reduce`, and one macro work.
- [ ] Ctrl-] returns to the monitor without resuming interrupted Lisp.
- [ ] Selecting `1` again removes definitions from the prior session.

### Embedded applications

- [ ] Menu `2` evaluates all forms in demo 1 and enters its REPL.
- [ ] Menu `3` evaluates all forms in demo 2 and enters its REPL.
- [ ] Demo functions produce documented results.
- [ ] User can redefine a demo function interactively.
- [ ] Reselecting a demo restores its original definitions.
- [ ] Embedded source errors identify the demo and form number.

### `.l24` transfer

- [ ] Ctrl-R is consumed by `te-rs`, not sent as Lisp input.
- [ ] `.lgo` Ctrl-R behavior remains unchanged.
- [ ] `.l24` mode waits for target readiness.
- [ ] Every source byte is backpressured or covered by an equivalent tested
      credit protocol.
- [ ] Host EOF produces target EOT and `L24 DONE`.
- [ ] Loaded definitions are usable in the following REPL.
- [ ] Multiple top-level forms load.
- [ ] Blank lines, comments, CRLF, strings, and multiline forms load.
- [ ] Oversized or incomplete forms fail without evaluation of truncation.
- [ ] Evaluation errors stop cleanly and report their location/form number.
- [ ] Ctrl-] during transfer returns to a clean monitor menu.
- [ ] A subsequent transfer starts with no stale bytes or parser state.

### Reset and recovery

- [ ] `(exit)` follows the documented monitor-app exit policy.
- [ ] S1 returns to FPGA EBR-resident load-and-go.
- [ ] `G000000` restarts the retained external-SRAM monitor image.
- [ ] Selecting a Lisp entry after warm reset fully reinitializes it.
- [ ] Reloading the complete `.lgo` restores a known byte-for-byte image.

## Decisions for the first implementation

Unless testing invalidates them, use these defaults:

- standard rather than full prelude;
- two small embedded workspace demos;
- explicit menu `l` before Ctrl-R source transfer;
- `te-rs` as the supported transfer client;
- stop-and-wait ACK plus EOT for the first reliable protocol;
- stop on the first read/evaluation error;
- enter a REPL after successful load/evaluation;
- Ctrl-] as the temporary monitor attention byte; and
- fresh interpreter state for every menu selection.

The only design question that materially changes this proof is whether a demo
must prompt for raw user input from inside Lisp. If so, dcmls also needs a
Lisp-visible line/read primitive with well-defined interaction with the
monitor RX broker. Otherwise, function invocation at the resulting REPL gives
an interactive demo without expanding the first milestone.
