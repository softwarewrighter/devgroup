#pragma once

/* eval.h -- Evaluator with core special forms
 *
 * Special forms: quote, if, define, lambda, defmacro, begin, catch
 * Environment: alist of (symbol . value) pairs
 * Closures/macros: extended heap objects
 */

/* Forward declarations */
int eval(int expr, int env);
int apply_fn(int fn, int args);

/* Global environment */
int global_env;


/* Pre-interned special form symbols */
int sym_quote;
int sym_if;
int sym_define;
int sym_lambda;
int sym_defmacro;
int sym_begin;
int sym_quasiquote;
int sym_unquote;
int sym_unquote_splicing;
int sym_set;
int sym_catch;
int sym_asm;
int gensym_counter;

/* --- Escape continuation support (catch/throw) --- */

#define MAX_CATCH_DEPTH 16
#define MAX_WIND_DEPTH  16

int catch_tags[MAX_CATCH_DEPTH];   /* tag symbol for each catch frame */
int catch_vals[MAX_CATCH_DEPTH];   /* return value (filled by throw) */
int catch_depth;                   /* current stack depth */
int catch_throwing;                /* flag: unwinding in progress? */
int catch_target;                  /* depth to unwind to */

/* Wind stack for dynamic-wind: after-thunks to run during unwind */
int wind_after[MAX_WIND_DEPTH];    /* after thunk (closure) for each wind frame */
int wind_depth;                    /* current wind stack depth */
/* Primitive IDs */
#define PRIM_ADD     0
#define PRIM_SUB     1
#define PRIM_MUL     2
#define PRIM_DIV     3
#define PRIM_MOD     4
#define PRIM_LT      5
#define PRIM_EQ_NUM  6
#define PRIM_CONS    7
#define PRIM_CAR     8
#define PRIM_CDR     9
#define PRIM_LIST   10
#define PRIM_NULLP  11
#define PRIM_PAIRP  12
#define PRIM_ATOMP  13
#define PRIM_EQP    14
#define PRIM_NOT    15
#define PRIM_PRINT  16
#define PRIM_NUMP   17
#define PRIM_EXIT   18
#define PRIM_PEEK   19
#define PRIM_POKE   20
#define PRIM_DELAY  21
#define PRIM_PRINTLN 22
#define PRIM_NEWLINE 23
#define PRIM_APPLY   24
#define PRIM_STR_LEN    25
#define PRIM_STR_REF    26
#define PRIM_STR_APPEND 27
#define PRIM_STR_EQ     28
#define PRIM_STRINGP    29
#define PRIM_DISPLAY    30
#define PRIM_GC         31
#define PRIM_HEAP_USED  32
#define PRIM_HEAP_SIZE  33
#define PRIM_NUM_TO_STR 34
#define PRIM_FNQP       35
#define PRIM_EVAL       36
#define PRIM_MACEXPAND  37
#define PRIM_GENSYM     38
#define PRIM_SYM_TO_STR 39
#define PRIM_STR_TO_SYM 40
#define PRIM_THROW      41
#define PRIM_DYN_WIND   42
#define PRIM_SYMBOLP    43
#define PRIM_SUBSTR     44
#define PRIM_FORMAT     45
#define PRIM_CHR_TO_INT 46
#define PRIM_INT_TO_CHR 47
#define PRIM_STR_INDEX  48
#define PRIM_STR_CONTAINS 49

/* --- Extended object accessors --- */

int ext_type(int v) {
    return heap_car[PTR_IDX(v)];
}

int ext_data(int v) {
    return heap_cdr[PTR_IDX(v)];
}

/* --- Extended object constructors --- */

int make_closure(int params, int body, int env) {
    int inner = cons(body, env);
    int data = cons(params, inner);
    int idx = alloc_cell();
    heap_car[idx] = ETYPE_CLOSURE;
    heap_cdr[idx] = data;
    return MAKE_EXTENDED(idx);
}

int make_macro(int params, int body, int env) {
    int inner = cons(body, env);
    int data = cons(params, inner);
    int idx = alloc_cell();
    heap_car[idx] = ETYPE_MACRO;
    heap_cdr[idx] = data;
    return MAKE_EXTENDED(idx);
}

int make_primitive(int id) {
    int idx = alloc_cell();
    heap_car[idx] = ETYPE_PRIMITIVE;
    heap_cdr[idx] = MAKE_FIXNUM(id);
    return MAKE_EXTENDED(idx);
}

/* Closure/macro field access */
int closure_params(int v) { return car(ext_data(v)); }
int closure_body(int v)   { return car(cdr(ext_data(v))); }
int closure_env(int v)    { return cdr(cdr(ext_data(v))); }

/* --- Environment --- */

int env_extend(int sym, int val, int env) {
    return cons(cons(sym, val), env);
}

int env_lookup(int sym, int env) {
    int e = env;
    while (!IS_NIL(e)) {
        int binding = car(e);
        if (car(binding) == sym) {
            return cdr(binding);
        }
        e = cdr(e);
    }
    e = global_env;
    while (!IS_NIL(e)) {
        int binding = car(e);
        if (car(binding) == sym) {
            return cdr(binding);
        }
        e = cdr(e);
    }
    puts_str("ERR:unbound ");
    print_val(sym);
    putc_uart(10);
    return NIL_VAL;
}

int env_bind(int params, int args, int env) {
    gc_protect(env);
    gc_protect(args);
    while (IS_CONS(params)) {
        gc_roots[gc_root_count - 2] = env;  /* update protected env */
        env = env_extend(car(params), car(args), env);
        params = cdr(params);
        args = cdr(args);
        gc_roots[gc_root_count - 1] = args;  /* update protected args */
    }
    /* Rest arg: (a b . rest) or bare symbol */
    if (IS_SYMBOL(params)) {
        env = env_extend(params, args, env);
    }
    gc_unprotect(2);
    return env;
}

/* --- Eval --- */

int eval_list(int list, int env) {
    if (IS_NIL(list)) return NIL_VAL;
    /* Iterative: build result list in-place to avoid C stack depth
     * proportional to argument count */
    int val = eval(car(list), env);
    if (catch_throwing) return NIL_VAL;
    int result = cons(val, NIL_VAL);
    gc_protect(result);
    int tail = result;
    list = cdr(list);
    while (IS_CONS(list)) {
        val = eval(car(list), env);
        if (catch_throwing) { gc_unprotect(1); return NIL_VAL; }
        int cell = cons(val, NIL_VAL);
        heap_cdr[PTR_IDX(tail)] = cell;
        tail = cell;
        list = cdr(list);
    }
    gc_unprotect(1);
    return result;
}

int apply_primitive(int id, int args) {
    int a = NIL_VAL;
    int b = NIL_VAL;

    if (!IS_NIL(args)) {
        a = car(args);
        if (!IS_NIL(cdr(args))) {
            b = car(cdr(args));
        }
    }

    /* Variadic arithmetic: +, -, * fold over all args */
    if (id == PRIM_ADD || id == PRIM_SUB || id == PRIM_MUL) {
        int identity = (id == PRIM_MUL) ? 1 : 0;
        int result = identity;
        int rest = args;
        int count = 0;
        while (!IS_NIL(rest)) {
            int v = car(rest);
            if (!IS_FIXNUM(v)) { puts_str("ERR:not-number\n"); return NIL_VAL; }
            if (count == 0 && id == PRIM_SUB) {
                result = FIXNUM_VAL(v);
            } else if (id == PRIM_ADD) {
                result += FIXNUM_VAL(v);
            } else if (id == PRIM_SUB) {
                result -= FIXNUM_VAL(v);
            } else {
                result *= FIXNUM_VAL(v);
            }
            count++;
            rest = cdr(rest);
        }
        /* (- x) is negation */
        if (id == PRIM_SUB && count == 1) result = -result;
        return MAKE_FIXNUM(result);
    }

    /* Binary arithmetic with type checks */
    if (id == PRIM_DIV || id == PRIM_MOD || id == PRIM_LT || id == PRIM_EQ_NUM) {
        if (!IS_FIXNUM(a) || !IS_FIXNUM(b)) {
            puts_str("ERR:not-number\n");
            return NIL_VAL;
        }
        if (id == PRIM_DIV) {
            if (FIXNUM_VAL(b) == 0) { puts_str("ERR:div-by-zero\n"); return NIL_VAL; }
            return MAKE_FIXNUM(FIXNUM_VAL(a) / FIXNUM_VAL(b));
        }
        if (id == PRIM_MOD) {
            if (FIXNUM_VAL(b) == 0) { puts_str("ERR:div-by-zero\n"); return NIL_VAL; }
            return MAKE_FIXNUM(FIXNUM_VAL(a) % FIXNUM_VAL(b));
        }
        if (id == PRIM_LT) {
            if (FIXNUM_VAL(a) < FIXNUM_VAL(b)) return T_VAL;
            return NIL_VAL;
        }
        if (id == PRIM_EQ_NUM) {
            if (FIXNUM_VAL(a) == FIXNUM_VAL(b)) return T_VAL;
            return NIL_VAL;
        }
    }
    if (id == PRIM_CONS) return cons(a, b);
    if (id == PRIM_CAR) {
        if (!IS_CONS(a)) { puts_str("ERR:car-of-non-pair\n"); return NIL_VAL; }
        return car(a);
    }
    if (id == PRIM_CDR) {
        if (!IS_CONS(a)) { puts_str("ERR:cdr-of-non-pair\n"); return NIL_VAL; }
        return cdr(a);
    }
    if (id == PRIM_LIST) return args;
    if (id == PRIM_NULLP) {
        if (IS_NIL(a)) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_PAIRP) {
        if (IS_CONS(a)) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_ATOMP) {
        if (!IS_CONS(a)) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_EQP) {
        if (a == b) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_NOT) {
        if (IS_NIL(a)) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_PRINT) {
        print_val(a);
        return a;
    }
    if (id == PRIM_PRINTLN) {
        print_val(a);
        putc_uart('\n');
        return a;
    }
    if (id == PRIM_NEWLINE) {
        putc_uart('\n');
        return NIL_VAL;
    }
    if (id == PRIM_NUMP) {
        if (IS_FIXNUM(a)) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_SYMBOLP) {
        if (IS_SYMBOL(a) && !IS_NIL(a) && a != T_VAL) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_FORMAT) {
        /* (format fmt-string args...) — ~a = display, ~~ = literal ~ */
        char *fmt = string_data(a);
        int flen = string_len(a);
        int fargs = cdr(args);
        char buf[256];
        int bi = 0;
        int fi = 0;
        while (fi < flen && bi < 254) {
            if (fmt[fi] == '~' && fi + 1 < flen) {
                fi = fi + 1;
                if (fmt[fi] == 'a' && !IS_NIL(fargs)) {
                    int arg = car(fargs);
                    fargs = cdr(fargs);
                    /* Convert arg to string data */
                    char *s = 0;
                    int sl = 0;
                    if (is_string(arg)) {
                        s = string_data(arg); sl = string_len(arg);
                    } else if (IS_FIXNUM(arg)) {
                        int sv = apply_primitive(PRIM_NUM_TO_STR, cons(arg, NIL_VAL));
                        s = string_data(sv); sl = string_len(sv);
                    } else if (IS_SYMBOL(arg)) {
                        s = sym_name(arg); sl = 0;
                        while (s[sl]) sl = sl + 1;
                    }
                    if (s) { int si = 0; while (si < sl && bi < 254) { buf[bi] = s[si]; bi = bi + 1; si = si + 1; } }
                } else if (fmt[fi] == '~') {
                    buf[bi] = '~'; bi = bi + 1;
                }
                fi = fi + 1;
            } else {
                buf[bi] = fmt[fi]; bi = bi + 1; fi = fi + 1;
            }
        }
        buf[bi] = 0;
        return make_string(buf, bi);
    }
    if (id == PRIM_CHR_TO_INT) { return a; }
    if (id == PRIM_INT_TO_CHR) { return a; }
    if (id == PRIM_STR_INDEX) {
        /* (string-index str char-code) — returns index or -1 */
        char *s = string_data(a);
        int len = string_len(a);
        int ch = FIXNUM_VAL(b);
        int i = 0;
        while (i < len) { if (s[i] == ch) return MAKE_FIXNUM(i); i = i + 1; }
        return MAKE_FIXNUM(-1);
    }
    if (id == PRIM_STR_CONTAINS) {
        /* (string-contains? haystack needle) */
        char *h = string_data(a);
        int hlen = string_len(a);
        char *n = string_data(b);
        int nlen = string_len(b);
        if (nlen == 0) return T_VAL;
        int i = 0;
        while (i <= hlen - nlen) {
            int j = 0;
            while (j < nlen && h[i + j] == n[j]) j = j + 1;
            if (j == nlen) return T_VAL;
            i = i + 1;
        }
        return NIL_VAL;
    }
    if (id == PRIM_SUBSTR) {
        /* (substring str start end) */
        int start = FIXNUM_VAL(b);
        int end = FIXNUM_VAL(car(cdr(cdr(args))));
        char *s = string_data(a);
        int slen = end - start;
        if (slen < 0) slen = 0;
        return make_string(s + start, slen);
    }
    if (id == PRIM_EXIT) {
        puts_str("Bye.\n");
        halt();
    }
    if (id == PRIM_PEEK) {
        return MAKE_FIXNUM(*(char *)FIXNUM_VAL(a));
    }
    if (id == PRIM_POKE) {
        *(char *)FIXNUM_VAL(a) = FIXNUM_VAL(b);
        return b;
    }
    if (id == PRIM_DELAY) {
        /* Spin-loop delay calibrated for --speed 500000 (500K IPS).
         * The loop body compiles to ~10 instructions on COR24.
         * 500 instructions/ms ÷ 10 instructions/iter = 50 iters/ms. */
        int ms = FIXNUM_VAL(a);
        int iters = ms * 50;
        while (iters > 0) {
            iters = iters - 1;
        }
        return NIL_VAL;
    }
    if (id == PRIM_APPLY) {
        /* (apply f args-list) */
        return apply_fn(a, b);
    }
    if (id == PRIM_STR_LEN) {
        return MAKE_FIXNUM(string_len(a));
    }
    if (id == PRIM_STR_REF) {
        return MAKE_FIXNUM(string_ref(a, FIXNUM_VAL(b)));
    }
    if (id == PRIM_STR_APPEND) {
        return string_append(a, b);
    }
    if (id == PRIM_STR_EQ) {
        if (string_equal(a, b)) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_STRINGP) {
        if (is_string(a)) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_DISPLAY) {
        /* Print string contents without quotes */
        if (is_string(a)) {
            char *s = string_data(a);
            int len = string_len(a);
            int i = 0;
            while (i < len) {
                putc_uart(s[i]);
                i = i + 1;
            }
        } else {
            print_val(a);
        }
        return NIL_VAL;
    }
    if (id == PRIM_GC) {
        gc_collect();
        return MAKE_FIXNUM(gc_count_free());
    }
    if (id == PRIM_HEAP_USED) {
        return MAKE_FIXNUM(heap_next - gc_count_free());
    }
    if (id == PRIM_HEAP_SIZE) {
        return MAKE_FIXNUM(HEAP_SIZE);
    }
    if (id == PRIM_FNQP) {
        if (IS_EXTENDED(a) && ext_type(a) == ETYPE_CLOSURE) return T_VAL;
        if (IS_EXTENDED(a) && ext_type(a) == ETYPE_PRIMITIVE) return T_VAL;
        return NIL_VAL;
    }
    if (id == PRIM_EVAL) {
        return eval(a, global_env);
    }
    if (id == PRIM_MACEXPAND) {
        /* (macroexpand-1 '(when x y)) — expand macro call once without eval */
        if (!IS_CONS(a)) return a;
        int mhead = car(a);
        int margs = cdr(a);
        if (IS_SYMBOL(mhead)) {
            /* Silent lookup — don't print error for special forms */
            int e = global_env;
            int fn = NIL_VAL;
            while (!IS_NIL(e)) {
                int binding = car(e);
                if (car(binding) == mhead) { fn = cdr(binding); break; }
                e = cdr(e);
            }
            if (IS_EXTENDED(fn) && ext_type(fn) == ETYPE_MACRO) {
                int mac_env = env_bind(closure_params(fn), margs, closure_env(fn));
                return eval(closure_body(fn), mac_env);
            }
        }
        return a;  /* not a macro call, return as-is */
    }
    if (id == PRIM_GENSYM) {
        /* Generate a unique symbol: _g0, _g1, _g2, ... */
        char buf[12];
        buf[0] = '_';
        buf[1] = 'g';
        int n = gensym_counter;
        gensym_counter = gensym_counter + 1;
        int i = 2;
        if (n == 0) { buf[i] = '0'; i = i + 1; }
        else {
            /* write digits reversed, then reverse */
            int start = i;
            while (n > 0) { buf[i] = '0' + n % 10; n = n / 10; i = i + 1; }
            int j = start;
            int k = i - 1;
            while (j < k) { char c = buf[j]; buf[j] = buf[k]; buf[k] = c; j = j + 1; k = k - 1; }
        }
        buf[i] = 0;
        return intern(buf);
    }
    if (id == PRIM_SYM_TO_STR) {
        if (IS_SYMBOL(a)) {
            char *name = sym_name(a);
            return make_string(name, str_len(name));
        }
        return make_string("", 0);
    }
    if (id == PRIM_STR_TO_SYM) {
        if (is_string(a)) {
            return intern(string_data(a));
        }
        return NIL_VAL;
    }
    if (id == PRIM_THROW) {
        /* (throw tag value) -- initiate non-local exit to matching catch */
        int tag = a;
        int val = b;
        int i = catch_depth - 1;
        while (i >= 0) {
            if (catch_tags[i] == tag) {
                /* Run dynamic-wind after thunks before unwinding */
                while (wind_depth > 0) {
                    int after = wind_after[wind_depth - 1];
                    wind_depth = wind_depth - 1;
                    apply_fn(after, NIL_VAL);
                }
                catch_throwing = 1;
                catch_target = i;
                catch_vals[i] = val;
                return NIL_VAL;  /* value ignored; unwind via flag */
            }
            i = i - 1;
        }
        puts_str("ERR:no-catch ");
        print_val(tag);
        putc_uart(10);
        return NIL_VAL;
    }
    if (id == PRIM_DYN_WIND) {
        /* (dynamic-wind before thunk after) */
        int before = a;
        int thunk = b;
        int after = car(cdr(cdr(args)));
        /* Call before */
        apply_fn(before, NIL_VAL);
        /* Push after thunk onto wind stack */
        if (wind_depth >= MAX_WIND_DEPTH) {
            puts_str("ERR:wind-overflow\n");
            return NIL_VAL;
        }
        int saved_wind = wind_depth;
        wind_after[wind_depth] = after;
        wind_depth = wind_depth + 1;
        /* Call thunk */
        int result = apply_fn(thunk, NIL_VAL);
        /* Restore wind depth (throw may have already popped) */
        wind_depth = saved_wind;
        if (!catch_throwing) {
            apply_fn(after, NIL_VAL);
        }
        return result;
    }
    if (id == PRIM_NUM_TO_STR) {
        int n = FIXNUM_VAL(a);
        char buf[12];
        int neg = 0;
        if (n < 0) { neg = 1; n = 0 - n; }
        if (n == 0) { buf[0] = '0'; buf[1] = 0; return make_string(buf, 1); }
        int i = 0;
        while (n > 0) { buf[i] = '0' + n % 10; n = n / 10; i = i + 1; }
        if (neg) { buf[i] = '-'; i = i + 1; }
        /* reverse in place */
        int j = 0;
        int k = i - 1;
        while (j < k) { char t = buf[j]; buf[j] = buf[k]; buf[k] = t; j = j + 1; k = k - 1; }
        buf[i] = 0;
        return make_string(buf, i);
    }

    return NIL_VAL;
}

/* --- Quasiquote expansion --- */

int qq_expand(int tmpl, int env) {
    /* Atom: return as-is (like quote) */
    if (!IS_CONS(tmpl)) return tmpl;

    int head = car(tmpl);

    /* (unquote expr) => eval expr */
    if (head == sym_unquote) {
        return eval(car(cdr(tmpl)), env);
    }

    /* Process list element by element, handling splicing */
    int result = NIL_VAL;
    int tail = NIL_VAL;
    int rest = tmpl;
    while (IS_CONS(rest)) {
        int elem = car(rest);
        if (IS_CONS(elem) && car(elem) == sym_unquote_splicing) {
            /* (unquote-splicing expr) => splice eval'd list */
            int spliced = eval(car(cdr(elem)), env);
            while (IS_CONS(spliced)) {
                int cell = cons(car(spliced), NIL_VAL);
                if (IS_NIL(result)) {
                    result = cell;
                } else {
                    heap_cdr[PTR_IDX(tail)] = cell;
                }
                tail = cell;
                spliced = cdr(spliced);
            }
        } else {
            /* Recursively expand */
            int expanded = qq_expand(elem, env);
            int cell = cons(expanded, NIL_VAL);
            if (IS_NIL(result)) {
                result = cell;
            } else {
                heap_cdr[PTR_IDX(tail)] = cell;
            }
            tail = cell;
        }
        rest = cdr(rest);
    }
    /* Handle dotted pair tail */
    if (!IS_NIL(rest) && !IS_NIL(tail)) {
        heap_cdr[PTR_IDX(tail)] = qq_expand(rest, env);
    }
    return result;
}

int eval(int expr, int env) {
  while (1) {
    /* Self-evaluating */
    if (IS_FIXNUM(expr)) return expr;
    if (IS_NIL(expr)) return expr;
    if (expr == T_VAL) return expr;
    if (IS_EXTENDED(expr)) return expr;

    /* Symbol lookup */
    if (IS_SYMBOL(expr)) {
        return env_lookup(expr, env);
    }

    /* List: special form or function call */
    if (!IS_CONS(expr)) return expr;

    int head = car(expr);
    int args = cdr(expr);

    /* quote */
    if (head == sym_quote) {
        return car(args);
    }

    /* quasiquote */
    if (head == sym_quasiquote) {
        return qq_expand(car(args), env);
    }

    /* if — tail call both branches */
    if (head == sym_if) {
        int cond_val = eval(car(args), env);
        if (catch_throwing) return NIL_VAL;
        if (!IS_NIL(cond_val)) {
            expr = car(cdr(args));
            continue;
        }
        int else_part = cdr(cdr(args));
        if (IS_NIL(else_part)) return NIL_VAL;
        expr = car(else_part);
        continue;
    }

    /* define — supports (define x val) and (define (f params...) body...) */
    if (head == sym_define) {
        int first = car(args);
        if (IS_CONS(first)) {
            /* (define (f x y) body...) => (define f (lambda (x y) body...)) */
            int sym = car(first);
            int params = cdr(first);
            int body_forms = cdr(args);
            int body;
            if (IS_NIL(cdr(body_forms))) {
                body = car(body_forms);
            } else {
                body = cons(sym_begin, body_forms);
            }
            int val = make_closure(params, body, env);
            global_env = env_extend(sym, val, global_env);
            return val;
        }
        int val = eval(car(cdr(args)), env);
        if (catch_throwing) return NIL_VAL;
        global_env = env_extend(first, val, global_env);
        return val;
    }

    /* set! — mutate existing binding */
    if (head == sym_set) {
        int sym = car(args);
        int val = eval(car(cdr(args)), env);
        if (catch_throwing) return NIL_VAL;
        /* Search local env first */
        int e = env;
        while (!IS_NIL(e)) {
            int binding = car(e);
            if (car(binding) == sym) {
                heap_cdr[PTR_IDX(binding)] = val;
                return val;
            }
            e = cdr(e);
        }
        /* Then global env */
        e = global_env;
        while (!IS_NIL(e)) {
            int binding = car(e);
            if (car(binding) == sym) {
                heap_cdr[PTR_IDX(binding)] = val;
                return val;
            }
            e = cdr(e);
        }
        puts_str("ERR:set!-unbound ");
        print_val(sym);
        putc_uart(10);
        return NIL_VAL;
    }

    /* lambda — supports multiple body forms via implicit begin */
    if (head == sym_lambda) {
        int params = car(args);
        int body_forms = cdr(args);
        int body;
        if (IS_NIL(cdr(body_forms))) {
            body = car(body_forms);  /* single body */
        } else {
            body = cons(sym_begin, body_forms);  /* wrap in begin */
        }
        return make_closure(params, body, env);
    }

    /* defmacro: (defmacro name (params) body...) — supports multi-body */
    if (head == sym_defmacro) {
        int name = car(args);
        int mparams = car(cdr(args));
        int mbody_forms = cdr(cdr(args));
        int mbody;
        if (IS_NIL(cdr(mbody_forms))) {
            mbody = car(mbody_forms);
        } else {
            mbody = cons(sym_begin, mbody_forms);
        }
        int mac = make_macro(mparams, mbody, env);
        global_env = env_extend(name, mac, global_env);
        return mac;
    }

    /* catch — (catch tag-expr body-expr): non-local exit target */
    if (head == sym_catch) {
        int tag = eval(car(args), env);
        if (catch_throwing) return NIL_VAL;
        if (catch_depth >= MAX_CATCH_DEPTH) {
            puts_str("ERR:catch-overflow\n");
            return NIL_VAL;
        }
        catch_tags[catch_depth] = tag;
        catch_vals[catch_depth] = NIL_VAL;
        int saved_depth = catch_depth;
        catch_depth = catch_depth + 1;
        int result = eval(car(cdr(args)), env);
        catch_depth = saved_depth;
        if (catch_throwing && catch_target == saved_depth) {
            catch_throwing = 0;
            return catch_vals[saved_depth];
        }
        if (catch_throwing) return NIL_VAL;  /* propagate unwind */
        return result;
    }

    /* begin — eval all but last, tail call last */
    if (head == sym_begin) {
        if (IS_NIL(args)) return NIL_VAL;
        while (!IS_NIL(cdr(args))) {
            eval(car(args), env);
            if (catch_throwing) return NIL_VAL;
            args = cdr(args);
        }
        expr = car(args);
        continue;
    }

    /* Function call: eval head */
    int fn = eval(head, env);
    if (catch_throwing) return NIL_VAL;

    /* Macro expansion — tail call the expanded form */
    if (IS_EXTENDED(fn)) {
        if (ext_type(fn) == ETYPE_MACRO) {
            int mac_env = env_bind(closure_params(fn), args, closure_env(fn));
            int expanded = eval(closure_body(fn), mac_env);
            if (catch_throwing) return NIL_VAL;
            expr = expanded;
            continue;
        }
    }

    /* Evaluate arguments */
    int evaled_args = eval_list(args, env);
    if (catch_throwing) return NIL_VAL;

    /* Closure application — tail call the body */
    if (IS_EXTENDED(fn)) {
        int fn_type = ext_type(fn);
        if (fn_type == ETYPE_CLOSURE) {
            int cl_params = closure_params(fn);
            int cl_body = closure_body(fn);
            int cl_env = closure_env(fn);
            env = env_bind(cl_params, evaled_args, cl_env);
            expr = cl_body;
            continue;
        }
        if (fn_type == ETYPE_PRIMITIVE) {
            int id = FIXNUM_VAL(ext_data(fn));
            return apply_primitive(id, evaled_args);
        }
    }
    puts_str("ERR:not-fn\n");
    return NIL_VAL;
  }
}

int apply_fn(int fn, int args) {
    if (IS_EXTENDED(fn)) {
        int fn_type = ext_type(fn);
        if (fn_type == ETYPE_CLOSURE) {
            int bound_env = env_bind(closure_params(fn), args, closure_env(fn));
            return eval(closure_body(fn), bound_env);
        }
        if (fn_type == ETYPE_PRIMITIVE) {
            int id = FIXNUM_VAL(ext_data(fn));
            return apply_primitive(id, args);
        }
    }
    puts_str("ERR:not-fn\n");
    return NIL_VAL;
}

/* --- Initialization --- */

void register_prim(char *name, int id) {
    int sym = intern(name);
    int prim = make_primitive(id);
    global_env = env_extend(sym, prim, global_env);
}

void eval_init() {
    global_env = NIL_VAL;

    sym_quote = intern("quote");
    sym_if = intern("if");
    sym_define = intern("define");
    sym_lambda = intern("lambda");
    sym_defmacro = intern("defmacro");
    sym_begin = intern("begin");
    sym_quasiquote = intern("quasiquote");
    sym_unquote = intern("unquote");
    sym_unquote_splicing = intern("unquote-splicing");
    sym_set = intern("set!");
    sym_catch = intern("catch");
    sym_asm = intern("asm");

    /* Initialize catch/throw state */
    catch_depth = 0;
    catch_throwing = 0;
    wind_depth = 0;

    register_prim("+", PRIM_ADD);
    register_prim("-", PRIM_SUB);
    register_prim("*", PRIM_MUL);
    register_prim("/", PRIM_DIV);
    register_prim("%", PRIM_MOD);
    register_prim("<", PRIM_LT);
    register_prim("=", PRIM_EQ_NUM);
    register_prim("cons", PRIM_CONS);
    register_prim("car", PRIM_CAR);
    register_prim("cdr", PRIM_CDR);
    register_prim("list", PRIM_LIST);
    register_prim("null?", PRIM_NULLP);
    register_prim("pair?", PRIM_PAIRP);
    register_prim("atom?", PRIM_ATOMP);
    register_prim("eq?", PRIM_EQP);
    register_prim("not", PRIM_NOT);
    register_prim("print", PRIM_PRINT);
    register_prim("number?", PRIM_NUMP);
    register_prim("exit", PRIM_EXIT);
    register_prim("peek", PRIM_PEEK);
    register_prim("poke", PRIM_POKE);
    register_prim("delay", PRIM_DELAY);
    register_prim("println", PRIM_PRINTLN);
    register_prim("newline", PRIM_NEWLINE);
    register_prim("apply", PRIM_APPLY);
    register_prim("string-length", PRIM_STR_LEN);
    register_prim("string-ref", PRIM_STR_REF);
    register_prim("string-append", PRIM_STR_APPEND);
    register_prim("string=?", PRIM_STR_EQ);
    register_prim("string?", PRIM_STRINGP);
    register_prim("display", PRIM_DISPLAY);
    register_prim("gc", PRIM_GC);
    register_prim("heap-used", PRIM_HEAP_USED);
    register_prim("heap-size", PRIM_HEAP_SIZE);
    register_prim("number->string", PRIM_NUM_TO_STR);
    register_prim("fn?", PRIM_FNQP);
    register_prim("eval", PRIM_EVAL);
    register_prim("macroexpand-1", PRIM_MACEXPAND);
    register_prim("gensym", PRIM_GENSYM);
    register_prim("symbol->string", PRIM_SYM_TO_STR);
    register_prim("string->symbol", PRIM_STR_TO_SYM);
    register_prim("throw", PRIM_THROW);
    register_prim("dynamic-wind", PRIM_DYN_WIND);
    register_prim("symbol?", PRIM_SYMBOLP);
    register_prim("substring", PRIM_SUBSTR);
    register_prim("format", PRIM_FORMAT);
    register_prim("char->integer", PRIM_CHR_TO_INT);
    register_prim("integer->char", PRIM_INT_TO_CHR);
    register_prim("string-index", PRIM_STR_INDEX);
    register_prim("string-contains?", PRIM_STR_CONTAINS);
    gensym_counter = 0;
}
