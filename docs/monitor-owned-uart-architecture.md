# COR24 monitor-owned UART architecture

## Purpose

The target design keeps UART policy, Ctrl-] recognition, buffering, and
monitor attention handling in the resident monitor. Applications should not:

- compare input against Ctrl-];
- know the Ctrl-] byte value;
- inspect a monitor-attention flag; or
- contain their own non-local jump to the monitor.

Applications may have a monitor-loaded build flavor whose console input calls
a documented monitor ABI. That is transport integration, not knowledge of the
monitor's attention key.

## Current Forth/APL suite

The current image is an intermediate implementation.

The resident monitor does own the UART interrupt:

1. It installs `uart_isr` in `iv`.
2. It enables UART RX interrupts.
3. The ISR reads and acknowledges every received byte.
4. Ordinary bytes are placed in the monitor RX ring.
5. Ctrl-] is consumed by the ISR and is never placed in the ring.

However, the applications are not completely monitor-oblivious:

- the copied APL `_getchar` checks the monitor-attention flag;
- the copied Forth input broker checks the same flag; and
- those routines jump to monitor address zero when it is set.

APL and Forth do not compare against Ctrl-] or receive the Ctrl-] byte, but
their suite-specific input implementations know that monitor attention
exists. This is the part that should be centralized in a future version.

Do not change the current image merely to restructure this division. It has
passed the repeated Forth/APL/Ctrl-] emulator test and should first receive
hardware testing as built.

## Why the current compromise exists

COR24 enters a UART interrupt by:

1. setting the interrupt-in-service latch;
2. storing the interrupted PC in `ir`; and
3. jumping through `iv`.

The latch is cleared by `jmp (ir)`. Current COR24 instructions do not provide
a supported way to assign a new continuation to `ir`. Consequently, an ISR
can either:

- execute `jmp (ir)`, clear the latch, and resume the interrupted application;
  or
- jump directly to the monitor while leaving the latch set, preventing later
  UART interrupts.

The current suite records attention, returns once through `ir`, and lets the
application-side input broker notice the flag and abandon the application.
This works for REPLs blocked in console input, but it is not arbitrary
instruction preemption and it leaks monitor-attention policy into the input
broker.

## Desired software architecture

The resident monitor should export a stable console ABI. At minimum:

```text
monitor_getc       wait for and return one ordinary RX byte
monitor_pollc      return a byte or a no-data indication
monitor_putc       optional centralized TX service
monitor_exit       return normally to the monitor menu
```

`monitor_getc` should reside in monitor code and perform all of the following:

- consume bytes from the monitor-owned RX ring;
- observe any pending monitor-attention state;
- perform the required monitor restart or context restoration; and
- never return the attention byte to the application.

Application code calls `monitor_getc` but does not inspect attention state.
The attention key and abort mechanics remain monitor implementation details.

The build should generate an ABI include file from monitor symbols rather than
duplicating numeric addresses in applications. A versioned entry table is
preferable:

```text
magic
ABI version
monitor_getc address
monitor_pollc address
monitor_putc address
monitor_exit address
```

The packager must verify that every monitor-loaded component was built for the
same ABI version and memory map.

## Standalone and monitor-loaded flavors

Each canonical interpreter repository can expose two build flavors without
duplicating the interpreter implementation.

### Standalone

```text
CONSOLE_BACKEND=standalone
```

Behavior:

- input polls UART MMIO directly;
- output polls and writes UART MMIO directly;
- no dependency on a resident monitor ABI;
- its existing standalone `_start` and halt behavior remain available.

### Monitor-loaded

```text
CONSOLE_BACKEND=monitor
```

Behavior:

- input calls `monitor_getc`;
- output may remain direct or call `monitor_putc`;
- application source contains no Ctrl-] constant or attention check;
- normal application exit calls `monitor_exit`;
- the image is relocatable and exports a restartable entry point;
- initialization completely resets interpreter state on every launch.

The flavor should select a small console backend or assembly adapter. It
should not fork or copy the interpreter.

## Repository-specific work

### APL

APL already has a central `getchar`/`io_getline` path. Its build flag should
select one of:

```text
standalone getchar -> UART MMIO
monitor getchar    -> monitor_getc ABI
```

The APL evaluator and REPL should remain unchanged. Generated `apl.s` should
be a build artifact, not the maintained integration source.

### Forth

Forth currently has several direct UART receive loops in addition to `KEY`.
Refactor them to one platform input primitive first. `WORD`, `CREATE`,
comments, and other readers should all use that primitive.

The build flag then selects:

```text
standalone platform_getc -> UART MMIO
monitor platform_getc    -> monitor_getc ABI
```

No Forth word or parser path should know Ctrl-] or the attention flag.

### Macro Lisp

Macro Lisp already centralizes line input sufficiently for a console backend.
Its monitor-loaded flavor should replace direct `getc_uart` polling with
`monitor_getc`. Remove cooperative Ctrl-] comparison from the application
flavor once the monitor ABI path is proven.

The Scheme and Full preludes do not need monitor-specific changes.

## True arbitrary preemption

Centralizing the input service makes REPL attention policy monitor-owned, but
the current ISA still requires attention completion at a monitor service
boundary. To abort an application immediately at any instruction, COR24 needs
one of:

- a writable interrupt return register;
- an interrupt-return instruction accepting an explicit target;
- a supported instruction that clears the interrupt-in-service latch before a
  direct monitor jump; or
- a monitor-visible interrupt-latch clear operation.

With such hardware or ISA support, the ISR can discard the application
context and restore the saved monitor context without any application-side
attention polling.

Until then, the cleanest software-only design is:

1. monitor ISR consumes Ctrl-] and records attention;
2. ISR returns through `ir` to clear interrupt service;
3. centralized `monitor_getc` observes attention;
4. `monitor_getc` restores or restarts the monitor;
5. application code remains unaware of the attention byte and flag.

## Migration sequence

1. Hardware-test the current known artifact without restructuring it.
2. Define and version the monitor ABI and entry table.
3. Move RX-ring consumption and attention handling into `monitor_getc`.
4. Add standalone/monitor console backends to canonical APL.
5. Refactor canonical Forth to one platform input primitive, then add both
   backends.
6. Add the same backend choice to canonical Macro Lisp.
7. Build relocated components from their canonical repositories.
8. Stop committing copied generated interpreter assembly in the suite.
9. Package components with overlap, ABI-version, entry-point, and sole-`G`
   validation.
10. Retest ordinary input, repeated Ctrl-] returns, ring overflow, warm
    relaunch, and hardware RTS/CTS upload behavior.
