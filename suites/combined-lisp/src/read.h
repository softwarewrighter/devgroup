#pragma once

/* read.h -- S-expression reader
 *
 * Reads from a char* input string. Handles:
 *   - Integers (positive and negative)
 *   - Symbols (interned)
 *   - Lists: (a b c), dotted pairs: (a . b)
 *   - Quote shorthand: 'x -> (quote x)
 */

/* Forward declarations from gc.h */
int gc_protect(int val);
void gc_unprotect(int n);

char *read_ptr;

int is_whitespace(int ch) {
    return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r';
}

int is_digit(int ch) {
    return ch >= '0' && ch <= '9';
}

int is_sym_char(int ch) {
    if (ch == 0) return 0;
    if (is_whitespace(ch)) return 0;
    if (ch == '(' || ch == ')' || ch == '\'') return 0;
    if (ch == '`' || ch == ',') return 0;
    if (ch == '.') return 0;
    return 1;
}

void skip_whitespace() {
    while (1) {
        if (is_whitespace(*read_ptr)) {
            read_ptr = read_ptr + 1;
        } else if (*read_ptr == ';') {
            /* Skip ; comment to end of line or string */
            while (*read_ptr && *read_ptr != '\n') {
                read_ptr = read_ptr + 1;
            }
        } else {
            return;
        }
    }
}

int read_expr();

int hex_digit_val(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

int read_hex() {
    /* skip '#x' prefix */
    read_ptr = read_ptr + 2;
    int n = 0;
    while (hex_digit_val(*read_ptr) >= 0) {
        n = n * 16 + hex_digit_val(*read_ptr);
        read_ptr = read_ptr + 1;
    }
    return MAKE_FIXNUM(n);
}

int read_int() {
    int neg = 0;
    if (*read_ptr == '-') {
        neg = 1;
        read_ptr = read_ptr + 1;
    }
    int n = 0;
    while (is_digit(*read_ptr)) {
        n = n * 10 + (*read_ptr - '0');
        read_ptr = read_ptr + 1;
    }
    if (neg) n = 0 - n;
    return MAKE_FIXNUM(n);
}

int read_symbol() {
    char buf[64];
    int i = 0;
    while (is_sym_char(*read_ptr) && i < 63) {
        buf[i] = *read_ptr;
        i = i + 1;
        read_ptr = read_ptr + 1;
    }
    buf[i] = 0;
    return intern(buf);
}

int read_list() {
    /* skip opening '(' */
    read_ptr = read_ptr + 1;
    skip_whitespace();

    if (*read_ptr == ')') {
        read_ptr = read_ptr + 1;
        return NIL_VAL;
    }

    /* read first element */
    int first = read_expr();
    skip_whitespace();

    /* check for dotted pair */
    if (*read_ptr == '.') {
        read_ptr = read_ptr + 1;
        skip_whitespace();
        int d = read_expr();
        skip_whitespace();
        if (*read_ptr == ')') {
            read_ptr = read_ptr + 1;
        }
        return cons(first, d);
    }

    /* build list */
    int head = cons(first, NIL_VAL);
    int tail = head;
    while (*read_ptr && *read_ptr != ')') {
        int elem = read_expr();
        int cell = cons(elem, NIL_VAL);
        heap_cdr[PTR_IDX(tail)] = cell;
        tail = cell;
        skip_whitespace();

        /* check for dot after element */
        if (*read_ptr == '.') {
            read_ptr = read_ptr + 1;
            skip_whitespace();
            int d = read_expr();
            heap_cdr[PTR_IDX(tail)] = d;
            skip_whitespace();
            if (*read_ptr == ')') {
                read_ptr = read_ptr + 1;
            }
            return head;
        }
    }

    if (*read_ptr == ')') {
        read_ptr = read_ptr + 1;
    }
    return head;
}

int read_string() {
    /* skip opening '"' */
    read_ptr = read_ptr + 1;
    char buf[120];
    int i = 0;
    while (*read_ptr && *read_ptr != 34 && i < 119) {
        if (*read_ptr == '\\') {
            read_ptr = read_ptr + 1;
            if (*read_ptr == 'n') { buf[i] = '\n'; }
            else if (*read_ptr == 'r') { buf[i] = '\r'; }
            else if (*read_ptr == 't') { buf[i] = '\t'; }
            else if (*read_ptr == '\\') { buf[i] = '\\'; }
            else if (*read_ptr == 34) { buf[i] = '"'; }
            else { buf[i] = *read_ptr; }
        } else {
            buf[i] = *read_ptr;
        }
        i = i + 1;
        read_ptr = read_ptr + 1;
    }
    if (*read_ptr == 34) {
        read_ptr = read_ptr + 1;
    }
    buf[i] = 0;
    return make_string(buf, i);
}

int read_expr() {
    skip_whitespace();

    int ch = *read_ptr;

    if (ch == 34) {
        return read_string();
    }

    if (ch == '(') {
        return read_list();
    }

    if (ch == '\'') {
        read_ptr = read_ptr + 1;
        int val = read_expr();
        int quote_sym = intern("quote");
        return cons(quote_sym, cons(val, NIL_VAL));
    }

    if (ch == '`') {
        read_ptr = read_ptr + 1;
        int val = read_expr();
        int qq_sym = intern("quasiquote");
        return cons(qq_sym, cons(val, NIL_VAL));
    }

    if (ch == ',') {
        read_ptr = read_ptr + 1;
        if (*read_ptr == '@') {
            read_ptr = read_ptr + 1;
            int val = read_expr();
            int splice_sym = intern("unquote-splicing");
            return cons(splice_sym, cons(val, NIL_VAL));
        }
        int val = read_expr();
        int uq_sym = intern("unquote");
        return cons(uq_sym, cons(val, NIL_VAL));
    }

    /* # dispatch: #t #f #xNN #_ */
    if (ch == '#') {
        int next = *(read_ptr + 1);
        if (next == 'x') { return read_hex(); }
        if (next == 't') { read_ptr = read_ptr + 2; return T_VAL; }
        if (next == 'f') { read_ptr = read_ptr + 2; return NIL_VAL; }
        if (next == '_') {
            /* Datum comment: #_ expr — read and discard next form */
            read_ptr = read_ptr + 2;
            read_expr();
            return read_expr();
        }
    }

    /* negative number: '-' followed by digit */
    if (ch == '-' && is_digit(*(read_ptr + 1))) {
        return read_int();
    }

    if (is_digit(ch)) {
        return read_int();
    }

    /* symbol */
    if (ch != 0 && ch != ')') {
        return read_symbol();
    }

    return NIL_VAL;
}

int read_str(char *input) {
    read_ptr = input;
    return read_expr();
}
