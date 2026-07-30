#pragma once

/* compile.h -- Lisp to COR24 native code compiler
 *
 * Stack-based compiler: each expression leaves result in r0.
 * Emits COR24 assembly text to UART.
 *
 * Assumes runtime provides: _cons, _car, _cdr, _print_val.
 * Compiled code defines _cmain as entry point.
 *
 * Supported: fixnum, symbol, quote, if, begin, define, lambda,
 *   +, -, *, <, =, cons, car, cdr, null?, pair?, not, print,
 *   general function calls.
 *
 * Limitation: no closure capture (lambdas can only reference
 * their own parameters and globals).
 */

/* Label counter */
int clbl;

/* Pre-interned primitive symbols */
int cp_plus;
int cp_minus;
int cp_star;
int cp_lt;
int cp_eq;
int cp_cons_s;
int cp_car_s;
int cp_cdr_s;
int cp_nullp;
int cp_pairp;
int cp_not_s;
int cp_print_s;

/* Deferred function table */
#define CFMAX 32
int cf_lbl[CFMAX];
int cf_body[CFMAX];
int cf_env[CFMAX];
int cf_n;
int cf_done;

/* Global variable tracking */
#define CGMAX 64
int cg_sym[CGMAX];
int cg_n;

void compile_init() {
    clbl = 0;
    cf_n = 0;
    cf_done = 0;
    cg_n = 0;

    cp_plus = intern("+");
    cp_minus = intern("-");
    cp_star = intern("*");
    cp_lt = intern("<");
    cp_eq = intern("=");
    cp_cons_s = intern("cons");
    cp_car_s = intern("car");
    cp_cdr_s = intern("cdr");
    cp_nullp = intern("null?");
    cp_pairp = intern("pair?");
    cp_not_s = intern("not");
    cp_print_s = intern("print");
}

int clnew() {
    int l = clbl;
    clbl = clbl + 1;
    return l;
}

void cg_add(int sym) {
    int i = 0;
    while (i < cg_n) {
        if (cg_sym[i] == sym) return;
        i = i + 1;
    }
    cg_sym[cg_n] = sym;
    cg_n = cg_n + 1;
}

/* --- Emit helpers --- */

void ce(char *s) {
    puts_str("        ");
    puts_str(s);
    putc_uart(10);
}

void ce_ld(int l) {
    puts_str("_CL");
    print_int(l);
    putc_uart(':');
    putc_uart(10);
}

void ce_lr(int l) {
    puts_str("_CL");
    print_int(l);
}

void ce_gr(int sym) {
    puts_str("_G");
    print_int(PTR_IDX(sym));
}

void ce_li(int v) {
    if (v >= -128 && v <= 127) {
        puts_str("        lc      r0,");
    } else {
        puts_str("        la      r0,");
    }
    print_int(v);
    putc_uart(10);
}

void ce_li1(int v) {
    if (v >= -128 && v <= 127) {
        puts_str("        lc      r1,");
    } else {
        puts_str("        la      r1,");
    }
    print_int(v);
    putc_uart(10);
}

/* Forward declaration */
void cexpr(int expr, int env);

/* Compile quoted value: emit code to construct at runtime */
void cquote(int val) {
    if (IS_FIXNUM(val) || IS_SYMBOL(val)) {
        ce_li(val);
        return;
    }
    if (IS_CONS(val)) {
        cquote(cdr(val));
        ce("push    r0");
        cquote(car(val));
        ce("push    r0");
        puts_str("        la      r0,_cons\n");
        ce("jal     r1,(r0)");
        ce("add     sp,6");
        return;
    }
    ce_li(NIL_VAL);
}

/* Count list length */
int clen(int lst) {
    int n = 0;
    while (!IS_NIL(lst)) {
        n = n + 1;
        lst = cdr(lst);
    }
    return n;
}

/* Look up in compile-time env: returns frame offset or -9999 */
int cenv_get(int sym, int env) {
    while (!IS_NIL(env)) {
        if (car(car(env)) == sym) return FIXNUM_VAL(cdr(car(env)));
        env = cdr(env);
    }
    return -9999;
}

/* Extend compile-time env */
int cenv_put(int sym, int off, int env) {
    return cons(cons(sym, MAKE_FIXNUM(off)), env);
}

/* Build env for function params: first param at fp+9, then +3 each */
int cenv_params(int params) {
    int env = NIL_VAL;
    int p = params;
    int off = 9;
    while (!IS_NIL(p)) {
        env = cenv_put(car(p), off, env);
        p = cdr(p);
        off = off + 3;
    }
    return env;
}

/* Push args right-to-left via recursion */
void cpush_args(int args, int env) {
    if (IS_NIL(args)) return;
    cpush_args(cdr(args), env);
    cexpr(car(args), env);
    ce("push    r0");
}

/* --- Compile expression: result in r0 --- */

void cexpr(int expr, int env) {
    /* Fixnum: load tagged value */
    if (IS_FIXNUM(expr)) {
        ce_li(expr);
        return;
    }

    /* Nil */
    if (IS_NIL(expr)) {
        ce_li(NIL_VAL);
        return;
    }

    /* T */
    if (expr == T_VAL) {
        ce_li(T_VAL);
        return;
    }

    /* Symbol: variable reference */
    if (IS_SYMBOL(expr)) {
        int off = cenv_get(expr, env);
        if (off != -9999) {
            puts_str("        lw      r0,");
            print_int(off);
            puts_str("(fp)\n");
        } else {
            cg_add(expr);
            puts_str("        la      r1,");
            ce_gr(expr);
            putc_uart(10);
            ce("lw      r0,0(r1)");
        }
        return;
    }

    if (!IS_CONS(expr)) {
        ce_li(NIL_VAL);
        return;
    }

    int head = car(expr);
    int args = cdr(expr);

    /* --- Special forms --- */

    /* asm: emit raw assembly lines (strings verbatim, symbols as _G<n>)
     * Strings emit a trailing newline unless followed by a symbol.
     * Symbols emit their _G<n> label inline (no newline). */
    if (head == sym_asm) {
        int strs = args;
        while (!IS_NIL(strs)) {
            int s = car(strs);
            int next = cdr(strs);
            if (is_string(s)) {
                puts_str(string_data(s));
                /* newline unless next arg is a symbol (concatenation) */
                if (IS_NIL(next) || !IS_SYMBOL(car(next))) {
                    putc_uart(10);
                }
            } else if (IS_SYMBOL(s)) {
                cg_add(s);
                ce_gr(s);
                /* newline after symbol unless next is also a symbol */
                if (IS_NIL(next) || !IS_SYMBOL(car(next))) {
                    putc_uart(10);
                }
            } else {
                puts_str("ERR:asm-bad-arg\n");
                return;
            }
            strs = next;
        }
        return;
    }

    /* quote */
    if (head == sym_quote) {
        cquote(car(args));
        return;
    }

    /* if: compile test, branch on nil */
    if (head == sym_if) {
        int lt = clnew();
        int le = clnew();
        int lx = clnew();

        cexpr(car(args), env);
        ce_li1(NIL_VAL);
        ce("ceq     r0,r1");
        /* brf: branch if NOT equal to nil (test passed) → then */
        puts_str("        brf     ");
        ce_lr(lt);
        putc_uart(10);
        /* fall through: equal to nil → long jump to else */
        puts_str("        la      r2,");
        ce_lr(le);
        putc_uart(10);
        ce("jmp     (r2)");

        /* then */
        ce_ld(lt);
        cexpr(car(cdr(args)), env);
        puts_str("        la      r2,");
        ce_lr(lx);
        putc_uart(10);
        ce("jmp     (r2)");

        /* else */
        ce_ld(le);
        if (!IS_NIL(cdr(cdr(args)))) {
            cexpr(car(cdr(cdr(args))), env);
        } else {
            ce_li(NIL_VAL);
        }
        ce_ld(lx);
        return;
    }

    /* begin */
    if (head == sym_begin) {
        while (!IS_NIL(args)) {
            cexpr(car(args), env);
            args = cdr(args);
        }
        return;
    }

    /* define */
    if (head == sym_define) {
        int sym = car(args);
        int vexpr = car(cdr(args));
        cg_add(sym);

        /* Named function: (define name (lambda ...)) */
        if (IS_CONS(vexpr) && car(vexpr) == sym_lambda) {
            int fl = clnew();
            int fp_ = car(cdr(vexpr));
            int fb = car(cdr(cdr(vexpr)));
            int fe = cenv_params(fp_);

            cf_lbl[cf_n] = fl;
            cf_body[cf_n] = fb;
            cf_env[cf_n] = fe;
            cf_n = cf_n + 1;

            puts_str("        la      r0,");
            ce_lr(fl);
            putc_uart(10);
            puts_str("        la      r1,");
            ce_gr(sym);
            putc_uart(10);
            ce("sw      r0,0(r1)");
            return;
        }

        /* General define */
        cexpr(vexpr, env);
        puts_str("        la      r1,");
        ce_gr(sym);
        putc_uart(10);
        ce("sw      r0,0(r1)");
        return;
    }

    /* lambda (anonymous) */
    if (head == sym_lambda) {
        int fl = clnew();
        int fp_ = car(args);
        int fb = car(cdr(args));
        int fe = cenv_params(fp_);

        cf_lbl[cf_n] = fl;
        cf_body[cf_n] = fb;
        cf_env[cf_n] = fe;
        cf_n = cf_n + 1;

        puts_str("        la      r0,");
        ce_lr(fl);
        putc_uart(10);
        return;
    }

    /* --- Inlined primitives --- */
    if (IS_SYMBOL(head)) {
        int a1 = IS_NIL(args) ? NIL_VAL : car(args);
        int a2 = NIL_VAL;
        if (!IS_NIL(args) && !IS_NIL(cdr(args))) {
            a2 = car(cdr(args));
        }

        /* + : tagged fixnum add (tag 0 preserves) */
        if (head == cp_plus) {
            cexpr(a2, env);
            ce("push    r0");
            cexpr(a1, env);
            ce("pop     r1");
            ce("add     r0,r1");
            return;
        }

        /* - : tagged fixnum sub */
        if (head == cp_minus) {
            cexpr(a2, env);
            ce("push    r0");
            cexpr(a1, env);
            ce("pop     r1");
            ce("sub     r0,r1");
            return;
        }

        /* * : untag one operand, multiply */
        if (head == cp_star) {
            cexpr(a2, env);
            ce("push    r0");
            cexpr(a1, env);
            ce("pop     r1");
            ce("lc      r2,2");
            ce("sra     r1,r2");
            ce("mul     r0,r1");
            return;
        }

        /* < : signed compare on tagged values */
        if (head == cp_lt) {
            int lt = clnew();
            int lx = clnew();
            cexpr(a2, env);
            ce("push    r0");
            cexpr(a1, env);
            ce("pop     r1");
            ce("cls     r0,r1");
            puts_str("        brt     ");
            ce_lr(lt);
            putc_uart(10);
            ce_li(NIL_VAL);
            puts_str("        la      r2,");
            ce_lr(lx);
            putc_uart(10);
            ce("jmp     (r2)");
            ce_ld(lt);
            ce_li(T_VAL);
            ce_ld(lx);
            return;
        }

        /* = : equality compare */
        if (head == cp_eq) {
            int lt = clnew();
            int lx = clnew();
            cexpr(a2, env);
            ce("push    r0");
            cexpr(a1, env);
            ce("pop     r1");
            ce("ceq     r0,r1");
            puts_str("        brt     ");
            ce_lr(lt);
            putc_uart(10);
            ce_li(NIL_VAL);
            puts_str("        la      r2,");
            ce_lr(lx);
            putc_uart(10);
            ce("jmp     (r2)");
            ce_ld(lt);
            ce_li(T_VAL);
            ce_ld(lx);
            return;
        }

        /* cons: call runtime _cons */
        if (head == cp_cons_s) {
            cexpr(a2, env);
            ce("push    r0");
            cexpr(a1, env);
            ce("push    r0");
            puts_str("        la      r0,_cons\n");
            ce("jal     r1,(r0)");
            ce("add     sp,6");
            return;
        }

        /* car: call runtime _car */
        if (head == cp_car_s) {
            cexpr(a1, env);
            ce("push    r0");
            puts_str("        la      r0,_car\n");
            ce("jal     r1,(r0)");
            ce("add     sp,3");
            return;
        }

        /* cdr: call runtime _cdr */
        if (head == cp_cdr_s) {
            cexpr(a1, env);
            ce("push    r0");
            puts_str("        la      r0,_cdr\n");
            ce("jal     r1,(r0)");
            ce("add     sp,3");
            return;
        }

        /* null? */
        if (head == cp_nullp) {
            int lt = clnew();
            int lx = clnew();
            cexpr(a1, env);
            ce_li1(NIL_VAL);
            ce("ceq     r0,r1");
            puts_str("        brt     ");
            ce_lr(lt);
            putc_uart(10);
            ce_li(NIL_VAL);
            puts_str("        la      r2,");
            ce_lr(lx);
            putc_uart(10);
            ce("jmp     (r2)");
            ce_ld(lt);
            ce_li(T_VAL);
            ce_ld(lx);
            return;
        }

        /* pair? : check tag == TAG_CONS (1) */
        if (head == cp_pairp) {
            int lt = clnew();
            int lx = clnew();
            cexpr(a1, env);
            ce("lc      r1,3");
            ce("and     r0,r1");
            ce("lc      r1,1");
            ce("ceq     r0,r1");
            puts_str("        brt     ");
            ce_lr(lt);
            putc_uart(10);
            ce_li(NIL_VAL);
            puts_str("        la      r2,");
            ce_lr(lx);
            putc_uart(10);
            ce("jmp     (r2)");
            ce_ld(lt);
            ce_li(T_VAL);
            ce_ld(lx);
            return;
        }

        /* not */
        if (head == cp_not_s) {
            int lt = clnew();
            int lx = clnew();
            cexpr(a1, env);
            ce_li1(NIL_VAL);
            ce("ceq     r0,r1");
            puts_str("        brt     ");
            ce_lr(lt);
            putc_uart(10);
            ce_li(NIL_VAL);
            puts_str("        la      r2,");
            ce_lr(lx);
            putc_uart(10);
            ce("jmp     (r2)");
            ce_ld(lt);
            ce_li(T_VAL);
            ce_ld(lx);
            return;
        }

        /* print: call _print_val, return the printed value */
        if (head == cp_print_s) {
            cexpr(a1, env);
            ce("push    r0");
            ce("push    r0");
            puts_str("        la      r0,_print_val\n");
            ce("jal     r1,(r0)");
            ce("add     sp,3");
            ce("pop     r0");
            return;
        }
    }

    /* --- General function call --- */
    int nargs = clen(args);
    cpush_args(args, env);

    /* Load function address */
    if (IS_SYMBOL(head)) {
        int off = cenv_get(head, env);
        if (off != -9999) {
            puts_str("        lw      r0,");
            print_int(off);
            puts_str("(fp)\n");
        } else {
            cg_add(head);
            puts_str("        la      r1,");
            ce_gr(head);
            putc_uart(10);
            ce("lw      r0,0(r1)");
        }
    } else {
        cexpr(head, env);
    }

    ce("jal     r1,(r0)");
    if (nargs > 0) {
        puts_str("        add     sp,");
        print_int(nargs * 3);
        putc_uart(10);
    }
}

/* Emit a compiled function body */
void cemit_fn(int i) {
    ce_ld(cf_lbl[i]);
    ce("push    fp");
    ce("push    r2");
    ce("push    r1");
    ce("mov     fp,sp");
    cexpr(cf_body[i], cf_env[i]);
    ce("mov     sp,fp");
    ce("pop     r1");
    ce("pop     r2");
    ce("pop     fp");
    ce("jmp     (r1)");
}

/* Emit all deferred functions (may grow during emission) */
void cemit_fns() {
    while (cf_done < cf_n) {
        cemit_fn(cf_done);
        cf_done = cf_done + 1;
    }
}

/* Emit data section with global variables */
void cemit_data() {
    if (cg_n == 0) return;
    puts_str("        .data\n");
    int i = 0;
    while (i < cg_n) {
        ce_gr(cg_sym[i]);
        puts_str(":\n        .word   0\n");
        i = i + 1;
    }
}

/* Compile a list of top-level expressions into a program */
void compile_program(int exprs) {
    puts_str("; Generated by tml24c compiler\n");
    puts_str("        .text\n\n");

    puts_str("        .globl  _cmain\n");
    puts_str("_cmain:\n");
    ce("push    fp");
    ce("push    r2");
    ce("push    r1");
    ce("mov     fp,sp");

    while (!IS_NIL(exprs)) {
        cexpr(car(exprs), NIL_VAL);
        exprs = cdr(exprs);
    }

    ce("mov     sp,fp");
    ce("pop     r1");
    ce("pop     r2");
    ce("pop     fp");
    ce("jmp     (r1)");
    putc_uart(10);

    cemit_fns();
    putc_uart(10);

    cemit_data();
}
