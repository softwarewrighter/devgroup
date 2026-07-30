#pragma once

/* string.h -- Immutable string support
 *
 * Strings are extended heap objects:
 *   cell[0].car = ETYPE_STRING
 *   cell[0].cdr = length (as fixnum)
 *   cell[1].car = offset into str_pool (as fixnum)
 *
 * String data is stored in a flat char buffer (str_pool).
 */

#define STR_POOL_SIZE 8192

char str_pool[STR_POOL_SIZE];
int str_pool_next;

void string_init() {
    str_pool_next = 0;
}

/* Pack offset and length into a single fixnum.
 * 22-bit fixnum payload: 11 bits offset, 11 bits length.
 * Max pool = 2048, max string = 2048. */
int make_string(char *s, int len) {
    if (str_pool_next + len + 1 > STR_POOL_SIZE) {
        puts_str("PANIC:STR-OOM pool=");
        print_int(str_pool_next);
        puts_str("/");
        print_int(STR_POOL_SIZE);
        putc_uart(10);
        asm("_str_halt:");
        asm("bra _str_halt");
    }
    int off = str_pool_next;
    int i = 0;
    while (i < len) {
        str_pool[str_pool_next] = s[i];
        str_pool_next = str_pool_next + 1;
        i = i + 1;
    }
    str_pool[str_pool_next] = 0;
    str_pool_next = str_pool_next + 1;

    /* Single cell: no GC issues */
    int idx = alloc_cell();
    heap_car[idx] = ETYPE_STRING;
    heap_cdr[idx] = MAKE_FIXNUM((off << 11) | len);
    return MAKE_EXTENDED(idx);
}

int string_len(int s) {
    int packed = FIXNUM_VAL(heap_cdr[PTR_IDX(s)]);
    return packed & 0x7FF;
}

char *string_data(int s) {
    int packed = FIXNUM_VAL(heap_cdr[PTR_IDX(s)]);
    int off = (packed >> 11) & 0x7FF;
    return &str_pool[off];
}

int string_ref(int s, int i) {
    char *data = string_data(s);
    return data[i];
}

int is_string(int v) {
    if (!IS_EXTENDED(v)) return 0;
    return heap_car[PTR_IDX(v)] == ETYPE_STRING;
}

int string_equal(int a, int b) {
    if (string_len(a) != string_len(b)) return 0;
    char *da = string_data(a);
    char *db = string_data(b);
    int len = string_len(a);
    int i = 0;
    while (i < len) {
        if (da[i] != db[i]) return 0;
        i = i + 1;
    }
    return 1;
}

int string_append(int a, int b) {
    int la = string_len(a);
    int lb = string_len(b);
    int total = la + lb;
    if (str_pool_next + total + 1 > STR_POOL_SIZE) {
        puts_str("PANIC:STR-OOM pool=");
        print_int(str_pool_next);
        puts_str("/");
        print_int(STR_POOL_SIZE);
        putc_uart(10);
        asm("_str2_halt:");
        asm("bra _str2_halt");
    }
    int off = str_pool_next;
    char *da = string_data(a);
    char *db = string_data(b);
    int i = 0;
    while (i < la) {
        str_pool[str_pool_next] = da[i];
        str_pool_next = str_pool_next + 1;
        i = i + 1;
    }
    i = 0;
    while (i < lb) {
        str_pool[str_pool_next] = db[i];
        str_pool_next = str_pool_next + 1;
        i = i + 1;
    }
    str_pool[str_pool_next] = 0;
    str_pool_next = str_pool_next + 1;

    int idx = alloc_cell();
    heap_car[idx] = ETYPE_STRING;
    heap_cdr[idx] = MAKE_FIXNUM((off << 11) | total);
    return MAKE_EXTENDED(idx);
}
