#pragma once
/* snapshot.h -- Heap snapshot save/restore */
#define SNAPSHOT_ADDR 0x080000

char hex_chars[16];
void snap_hex_init() { hex_chars[0]='0'; hex_chars[1]='1'; hex_chars[2]='2'; hex_chars[3]='3'; hex_chars[4]='4'; hex_chars[5]='5'; hex_chars[6]='6'; hex_chars[7]='7'; hex_chars[8]='8'; hex_chars[9]='9'; hex_chars[10]='a'; hex_chars[11]='b'; hex_chars[12]='c'; hex_chars[13]='d'; hex_chars[14]='e'; hex_chars[15]='f'; }
void snap_hex_byte(int b) { putc_uart(hex_chars[(b >> 4) & 15]); putc_uart(hex_chars[b & 15]); }
void snap_write_int(int v) { snap_hex_byte(v & 255); snap_hex_byte((v >> 8) & 255); snap_hex_byte((v >> 16) & 255); }
void snap_write_ints(int *arr, int n) { int i = 0; while (i < n) { snap_write_int(arr[i]); i = i + 1; } }
void snap_write_bytes(char *arr, int n) { int i = 0; while (i < n) { snap_hex_byte(arr[i] & 255); i = i + 1; } }

void snapshot_save() {
    snap_hex_init();
    /* Magic */
    putc_uart('T'); putc_uart('M'); putc_uart('L');

    /* Scalar state */
    snap_write_int(heap_next);
    snap_write_int(free_list);
    snap_write_int(global_env);
    snap_write_int(sym_count);
    snap_write_int(name_pool_next);
    snap_write_int(str_pool_next);
    snap_write_int(gensym_counter);

    /* Pre-interned symbols */
    snap_write_int(sym_quote);
    snap_write_int(sym_if);
    snap_write_int(sym_define);
    snap_write_int(sym_lambda);
    snap_write_int(sym_defmacro);
    snap_write_int(sym_begin);
    snap_write_int(sym_quasiquote);
    snap_write_int(sym_unquote);
    snap_write_int(sym_unquote_splicing);
    snap_write_int(sym_set);
    snap_write_int(sym_catch);

    /* Heap data (only used portion) */
    snap_write_ints(heap_car, heap_next);
    snap_write_ints(heap_cdr, heap_next);

    /* Symbol table */
    snap_write_bytes(name_pool, name_pool_next);
    snap_write_ints(sym_name_off, sym_count);

    /* String pool */
    snap_write_bytes(str_pool, str_pool_next);
}

int snap_cursor;
int snap_read_int() { char *p = (char *)snap_cursor; int v = (p[0] & 255) | ((p[1] & 255) << 8) | ((p[2] & 255) << 16); snap_cursor = snap_cursor + 3; return v; }
void snap_read_ints(int *arr, int n) { int i = 0; while (i < n) { arr[i] = snap_read_int(); i = i + 1; } }
void snap_read_bytes(char *arr, int n) { char *p = (char *)snap_cursor; int i = 0; while (i < n) { arr[i] = p[i]; i = i + 1; } snap_cursor = snap_cursor + n; }

/* Returns 1 if snapshot found and restored, 0 otherwise */
int snapshot_restore(int addr) {
    char *p = (char *)addr;
    /* Check magic */
    if (p[0] != 'T' || p[1] != 'M' || p[2] != 'L') return 0;

    snap_cursor = addr + 3;

    /* Scalar state */
    heap_next = snap_read_int();
    free_list = snap_read_int();
    global_env = snap_read_int();
    sym_count = snap_read_int();
    name_pool_next = snap_read_int();
    str_pool_next = snap_read_int();
    gensym_counter = snap_read_int();

    /* Pre-interned symbols */
    sym_quote = snap_read_int();
    sym_if = snap_read_int();
    sym_define = snap_read_int();
    sym_lambda = snap_read_int();
    sym_defmacro = snap_read_int();
    sym_begin = snap_read_int();
    sym_quasiquote = snap_read_int();
    sym_unquote = snap_read_int();
    sym_unquote_splicing = snap_read_int();
    sym_set = snap_read_int();
    sym_catch = snap_read_int();

    /* Heap data */
    snap_read_ints(heap_car, heap_next);
    snap_read_ints(heap_cdr, heap_next);

    /* Symbol table */
    snap_read_bytes(name_pool, name_pool_next);
    snap_read_ints(sym_name_off, sym_count);

    /* String pool */
    snap_read_bytes(str_pool, str_pool_next);

    /* Runtime state */
    gc_enabled = 1;
    gc_root_count = 0;
    gc_collections = 0;
    catch_depth = 0;
    catch_throwing = 0;
    wind_depth = 0;

    return 1;
}
