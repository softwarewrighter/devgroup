/* COR24 resident monitor with Scheme and Full Macro Lisp REPLs. */

#include "tml.h"
#include "io.h"
#include "heap.h"
#include "symbol.h"
#include "string.h"
#include "print.h"
#include "read.h"
#include "eval.h"
#include "gc.h"

void eval_str(char *s) { eval(read_str(s), global_env); }

/*
 * Both preludes use the historical load_prelude name. Rename that symbol
 * while including each generated prelude so both can live in one image.
 */
#define load_prelude load_scheme_prelude
#include "prelude-scheme.h"
#undef load_prelude

#define load_prelude load_full_prelude
#include "prelude-full.h"
#undef load_prelude

int repl() {
    char line[1024];
    puts_str("> ");
    while (1) {
        int len = read_line(line, 1024);
        if (len == -2) {
            puts_str("\n");
            return 2;
        }
        if (len < 0) {
            puts_str("Bye.\n");
            return 0;
        }
        if (len == 0) {
            puts_str("> ");
            continue;
        }
        read_ptr = line;
        skip_whitespace();
        while (*read_ptr) {
            int expr = read_expr();
            int result = eval(expr, global_env);
            print_val(result);
            putc_uart('\n');
            skip_whitespace();
        }
        puts_str("> ");
    }
}

void reset_interpreter() {
    gc_enabled = 0;
    heap_init();
    gc_init();
    symbol_init();
    string_init();
    eval_init();
    gc_enabled = 1;
}

int scheme_main() {
    reset_interpreter();
    puts_str("Loading Scheme prelude...\n");
    load_scheme_prelude();
    puts_str("Scheme REPL\n");
    return repl();
}

int full_main() {
    reset_interpreter();
    puts_str("Loading Full Macro Lisp prelude...\n");
    load_full_prelude();
    puts_str("Full Macro Lisp REPL\n");
    return repl();
}

void monitor_menu() {
    puts_str("\nCOR24 Lisp monitor\n");
    puts_str("1: Scheme REPL\n");
    puts_str("2: Full Macro Lisp REPL\n");
    puts_str("h: help\n");
    puts_str("q: quit\n");
    puts_str("mon> ");
}

void monitor_status(char *name, int status) {
    puts_str("\n");
    puts_str(name);
    puts_str(" returned ");
    putc_uart('0' + status);
    puts_str("\n");
}

int main() {
    int command;
    int status;

    puts_str("\nCOR24 combined Scheme and Full Macro Lisp suite\n");
    while (1) {
        monitor_menu();
        command = getc_uart();

        if (command == '\r' || command == '\n') {
            /* Ignore an empty command and redraw. */
        } else if (command == '1') {
            putc_uart(command);
            puts_str("\n");
            status = scheme_main();
            monitor_status("Scheme", status);
        } else if (command == '2') {
            putc_uart(command);
            puts_str("\n");
            status = full_main();
            monitor_status("Full Macro Lisp", status);
        } else if (command == 'h' || command == 'H' || command == '?') {
            putc_uart(command);
            puts_str("\n");
            puts_str("1 starts Scheme; 2 starts Full Macro Lisp.\n");
            puts_str("Each selection initializes a fresh interpreter and prelude.\n");
            puts_str("Inside either REPL, Ctrl-] returns to this menu.\n");
            puts_str("q halts; press S1 for load-and-go.\n");
        } else if (command == 'q' || command == 'Q') {
            putc_uart(command);
            puts_str("\nMonitor halted; press S1 for load-and-go.\n");
            halt();
        } else {
            putc_uart(command);
            puts_str("\n? unknown selection\n");
        }
    }
}
