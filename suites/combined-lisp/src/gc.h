#pragma once

/* gc.h -- Mark-sweep garbage collector
 *
 * Cells are allocated from a free list. When the free list is empty,
 * GC marks all reachable cells from roots, then sweeps unmarked cells
 * back onto the free list.
 *
 * Roots: global_env, pre-interned symbols, and the explicit root stack
 * (for protecting temporaries across allocations).
 */

#define MAX_GC_ROOTS 256

int heap_mark[HEAP_SIZE];
int free_list;      /* head of free list (cell index), -1 = empty */
int gc_roots[MAX_GC_ROOTS];
int gc_root_count;
int gc_collections;  /* statistics */
int gc_initial_sp;   /* captured at startup for conservative stack scan */

void gc_init() {
    free_list = -1;
    gc_root_count = 0;
    gc_collections = 0;
    /* Capture initial SP — must be called early in main() */
    asm("mov r0, sp");
    asm("la r1, _gc_initial_sp");
    asm("sw r0, 0(r1)");
}

/* --- Root stack for protecting temporaries --- */

int gc_protect(int val) {
    if (gc_root_count >= MAX_GC_ROOTS) {
        puts_str("PANIC:GC root stack full\n");
        asm("_gcr_halt:");
        asm("bra _gcr_halt");
    }
    gc_roots[gc_root_count] = val;
    gc_root_count = gc_root_count + 1;
    return val;
}

void gc_unprotect(int n) {
    gc_root_count = gc_root_count - n;
}

/* --- Mark phase --- */

void gc_mark_val(int v) {
    /* Iterate on cdr, recurse on car — prevents stack overflow
     * when marking long alists (global_env with 60+ bindings) */
    while (1) {
        if (IS_CONS(v)) {
            int idx = PTR_IDX(v);
            if (heap_mark[idx]) return;
            heap_mark[idx] = 1;
            gc_mark_val(heap_car[idx]);
            v = heap_cdr[idx];
        } else if (IS_EXTENDED(v)) {
            int idx = PTR_IDX(v);
            if (heap_mark[idx]) return;
            heap_mark[idx] = 1;
            gc_mark_val(heap_car[idx]);
            v = heap_cdr[idx];
        } else {
            return;
        }
    }
}

/* --- Sweep phase --- */

void gc_sweep() {
    free_list = -1;
    int i = heap_next - 1;
    while (i >= 0) {
        if (heap_mark[i]) {
            heap_mark[i] = 0;  /* clear for next cycle */
        } else {
            /* Add to free list */
            heap_cdr[i] = free_list;
            heap_car[i] = NIL_VAL;  /* clear for safety */
            free_list = i;
        }
        i = i - 1;
    }
}

/* --- Conservative stack scan --- */

int gc_scan_sp;  /* captured at GC time */

void gc_scan_stack() {
    /* Capture current SP */
    asm("mov r0, sp");
    asm("la r1, _gc_scan_sp");
    asm("sw r0, 0(r1)");

    /* Scan from current SP to initial SP, word by word (3 bytes each).
     * Any word that looks like a valid tagged heap pointer is treated
     * as a root. This over-retains (false positives from random ints
     * matching heap tags) but never misses live pointers. */
    int dist = gc_initial_sp - gc_scan_sp;
    int words = dist / 3;
    int addr = gc_scan_sp;
    while (words > 0) {
        int word = *(int *)addr;
        int tag = word & TAG_MASK;
        if (tag == TAG_CONS || tag == TAG_EXTENDED) {
            int idx = word >> 2;
            if (idx >= 0 && idx < heap_next) {
                gc_mark_val(word);
            }
        }
        addr = addr + 3;
        words = words - 1;
    }
}

/* --- Collect --- */

void gc_collect() {
    gc_collections = gc_collections + 1;

    /* Clear marks */
    int i = 0;
    while (i < heap_next) {
        heap_mark[i] = 0;
        i = i + 1;
    }

    /* Mark from global environment */
    gc_mark_val(global_env);

    /* Mark explicit root stack */
    i = 0;
    while (i < gc_root_count) {
        gc_mark_val(gc_roots[i]);
        i = i + 1;
    }

    /* Mark dynamic-wind after thunks and catch values */
    i = 0;
    while (i < wind_depth) {
        gc_mark_val(wind_after[i]);
        i = i + 1;
    }
    i = 0;
    while (i < catch_depth) {
        gc_mark_val(catch_tags[i]);
        gc_mark_val(catch_vals[i]);
        i = i + 1;
    }

    /* Conservative: scan C stack for heap pointers */
    gc_scan_stack();

    /* Sweep */
    gc_sweep();
}

/* --- Allocation with GC --- */

int gc_alloc_cell() {
    /* Try free list first */
    if (free_list >= 0) {
        int idx = free_list;
        free_list = heap_cdr[idx];
        heap_car[idx] = NIL_VAL;
        heap_cdr[idx] = NIL_VAL;
        return idx;
    }

    /* Try bump allocator */
    if (heap_next < HEAP_SIZE) {
        int idx = heap_next;
        heap_next = heap_next + 1;
        heap_car[idx] = NIL_VAL;
        heap_cdr[idx] = NIL_VAL;
        return idx;
    }

    /* Out of space: collect and retry free list */
    gc_collect();

    if (free_list >= 0) {
        int idx = free_list;
        free_list = heap_cdr[idx];
        heap_car[idx] = NIL_VAL;
        heap_cdr[idx] = NIL_VAL;
        return idx;
    }

    /* Truly out of memory */
    puts_str("PANIC:OOM heap=");
    print_int(heap_next);
    puts_str(" free=0 gc=");
    print_int(gc_collections);
    putc_uart(10);
    asm("_oom2_halt:");
    asm("bra _oom2_halt");
    return 0;
}

int gc_count_free() {
    int count = 0;
    int node = free_list;
    while (node >= 0) {
        count = count + 1;
        node = heap_cdr[node];
    }
    return count;
}
