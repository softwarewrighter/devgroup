#pragma once

/* heap.h -- Cell heap with bump allocator
 * Parallel arrays instead of struct array (simpler codegen for tc24r)
 */

int heap_car[HEAP_SIZE];
int heap_cdr[HEAP_SIZE];
int heap_next;

void heap_init() {
    heap_next = 0;
}

int gc_enabled;
int gc_alloc_cell();  /* forward decl -- defined in gc.h */

int alloc_cell() {
    if (gc_enabled) {
        return gc_alloc_cell();
    }
    if (heap_next >= HEAP_SIZE) {
        puts_str("PANIC:OOM (pre-GC) heap=");
        print_int(heap_next);
        putc_uart(10);
        asm("_oom_halt:");
        asm("bra _oom_halt");
    }
    int idx = heap_next;
    heap_next = heap_next + 1;
    heap_car[idx] = NIL_VAL;
    heap_cdr[idx] = NIL_VAL;
    return idx;
}

int cons(int a, int d) {
    int idx = alloc_cell();
    heap_car[idx] = a;
    heap_cdr[idx] = d;
    return MAKE_CONS(idx);
}

int car(int v) { return heap_car[PTR_IDX(v)]; }
int cdr(int v) { return heap_cdr[PTR_IDX(v)]; }
