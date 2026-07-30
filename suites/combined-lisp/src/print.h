#pragma once

/* print.h -- Value printer */

void print_val(int v);

void print_list(int v) {
    putc_uart('(');
    int first = 1;
    while (IS_CONS(v)) {
        if (!first) putc_uart(' ');
        first = 0;
        print_val(car(v));
        v = cdr(v);
    }
    if (!IS_NIL(v)) {
        puts_str(" . ");
        print_val(v);
    }
    putc_uart(')');
}

void print_val(int v) {
    if (IS_FIXNUM(v)) {
        print_int(FIXNUM_VAL(v));
    } else if (IS_NIL(v)) {
        puts_str("nil");
    } else if (IS_SYMBOL(v)) {
        puts_str(sym_name(v));
    } else if (IS_CONS(v)) {
        print_list(v);
    } else if (IS_EXTENDED(v)) {
        if (is_string(v)) {
            putc_uart('"');
            char *s = string_data(v);
            int len = string_len(v);
            int i = 0;
            while (i < len) {
                if (s[i] == '\n') { puts_str("\\n"); }
                else if (s[i] == '"') { puts_str("\\\""); }
                else if (s[i] == '\\') { puts_str("\\\\"); }
                else { putc_uart(s[i]); }
                i = i + 1;
            }
            putc_uart('"');
        } else {
            puts_str("#<obj>");
        }
    }
}
