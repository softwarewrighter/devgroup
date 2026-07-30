#pragma once

/* symbol.h -- Symbol interning */

#define MAX_SYMBOLS 512
#define NAME_POOL_SIZE 4096

char name_pool[NAME_POOL_SIZE];
int name_pool_next;

int sym_name_off[MAX_SYMBOLS];
int sym_count;

int str_eq(char *a, char *b) {
    while (*a && *b && *a == *b) {
        a = a + 1;
        b = b + 1;
    }
    return *a == *b;
}

int str_len(char *s) {
    int n = 0;
    while (*s) { n = n + 1; s = s + 1; }
    return n;
}

void str_copy(char *dst, char *src) {
    while (*src) {
        *dst = *src;
        dst = dst + 1;
        src = src + 1;
    }
    *dst = 0;
}

char *sym_name(int sym) {
    int idx = PTR_IDX(sym);
    return &name_pool[sym_name_off[idx]];
}

int intern(char *name) {
    int i = 0;
    while (i < sym_count) {
        if (str_eq(&name_pool[sym_name_off[i]], name)) {
            return MAKE_SYMBOL(i);
        }
        i = i + 1;
    }
    if (sym_count >= MAX_SYMBOLS) {
        puts_str("PANIC:symbol table full (");
        print_int(sym_count);
        puts_str(")\n");
        asm("_sym_halt:");
        asm("bra _sym_halt");
    }
    int len = str_len(name);
    if (name_pool_next + len + 1 > NAME_POOL_SIZE) {
        puts_str("PANIC:symbol name pool full (");
        print_int(name_pool_next);
        puts_str("/");
        print_int(NAME_POOL_SIZE);
        puts_str(")\n");
        asm("_sym2_halt:");
        asm("bra _sym2_halt");
    }
    int idx = sym_count;
    sym_name_off[idx] = name_pool_next;
    str_copy(&name_pool[name_pool_next], name);
    name_pool_next = name_pool_next + len + 1;
    sym_count = sym_count + 1;
    return MAKE_SYMBOL(idx);
}

void symbol_init() {
    name_pool_next = 0;
    sym_count = 0;
    intern("nil");
    intern("t");
}
