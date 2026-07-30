#pragma once

/* prelude-scheme.h -- Scheme-flavored prelude.
 * R7RS-inspired naming conventions on tml24c. */

void load_prelude() {
    /* Core list ops — tail-recursive where possible */
    eval_str("(define (reverse-acc lst acc) (if (null? lst) acc (reverse-acc (cdr lst) (cons (car lst) acc))))");
    eval_str("(define (reverse lst) (reverse-acc lst nil))");
    eval_str("(define (map-acc f lst acc) (if (null? lst) (reverse acc) (map-acc f (cdr lst) (cons (f (car lst)) acc))))");
    eval_str("(define (map f lst) (map-acc f lst nil))");
    eval_str("(define (filter-acc p lst acc) (if (null? lst) (reverse acc) (if (p (car lst)) (filter-acc p (cdr lst) (cons (car lst) acc)) (filter-acc p (cdr lst) acc))))");
    eval_str("(define (filter p lst) (filter-acc p lst nil))");
    eval_str("(define for-each (lambda (f lst) (if (null? lst) nil (begin (f (car lst)) (for-each f (cdr lst))))))");
    eval_str("(define (length-acc lst n) (if (null? lst) n (length-acc (cdr lst) (+ n 1))))");
    eval_str("(define (length lst) (length-acc lst 0))");
    eval_str("(define (append-acc ra b) (if (null? ra) b (append-acc (cdr ra) (cons (car ra) b))))");
    eval_str("(define (append a b) (append-acc (reverse a) b))");
    eval_str("(define (list-ref lst n) (if (= n 0) (car lst) (list-ref (cdr lst) (- n 1))))");
    eval_str("(define reduce (lambda (f init lst) (if (null? lst) init (reduce f (f init (car lst)) (cdr lst)))))");

    /* Accessors */
    eval_str("(define cadr (lambda (x) (car (cdr x))))");
    eval_str("(define caar (lambda (x) (car (car x))))");
    eval_str("(define cdar (lambda (x) (cdr (car x))))");
    eval_str("(define caddr (lambda (x) (car (cdr (cdr x)))))");

    /* Scheme predicates */
    eval_str("(define boolean? (lambda (x) (if (eq? x t) t (null? x))))");
    eval_str("(define list? (lambda (x) (if (null? x) t (if (pair? x) (list? (cdr x)) nil))))");
    eval_str("(define equal? (lambda (a b) (if (eq? a b) t (if (pair? a) (if (pair? b) (if (equal? (car a) (car b)) (equal? (cdr a) (cdr b)) nil) nil) (if (string? a) (if (string? b) (string=? a b) nil) nil)))))");
    eval_str("(define zero? (lambda (n) (= n 0)))");
    eval_str("(define even? (lambda (n) (= (% n 2) 0)))");
    eval_str("(define odd? (lambda (n) (not (= (% n 2) 0))))");
    eval_str("(define positive? (lambda (n) (< 0 n)))");
    eval_str("(define negative? (lambda (n) (< n 0)))");

    /* Numeric */
    eval_str("(define abs (lambda (n) (if (< n 0) (- 0 n) n)))");
    eval_str("(define max (lambda (a b) (if (< a b) b a)))");
    eval_str("(define min (lambda (a b) (if (< a b) a b)))");
    eval_str("(define modulo %)");
    eval_str("(define remainder %)");

    /* Scheme macros */
    eval_str("(define (let-expand first rest) (if (pair? first) `((lambda ,(map car first) ,@rest) ,@(map cadr first)) `((lambda (,first) (set! ,first (lambda ,(map car (car rest)) ,@(cdr rest))) (,first ,@(map cadr (car rest)))) nil)))");
    eval_str("(defmacro let (first . rest) (let-expand first rest))");
    eval_str("(define (let*-expand bindings body) (if (null? (cdr bindings)) `(let (,(car bindings)) ,@body) `(let (,(car bindings)) ,(let*-expand (cdr bindings) body))))");
    eval_str("(defmacro let* (bindings . body) (let*-expand bindings body))");
    eval_str("(define cond-expand (lambda (clauses) (if (null? clauses) nil (if (eq? (caar clauses) 'else) (cadr (car clauses)) `(if ,(caar clauses) ,(cadr (car clauses)) ,(cond-expand (cdr clauses)))))))");
    eval_str("(defmacro cond clauses (cond-expand clauses))");
    eval_str("(define (case-match-datums key datums) (if (null? datums) nil (if (eq? key (car datums)) t (case-match-datums key (cdr datums)))))");
    eval_str("(define (case-expand-clauses key clauses) (if (null? clauses) nil (if (eq? (caar clauses) 'else) (cadr (car clauses)) `(if (case-match-datums ,key ',(caar clauses)) ,(cadr (car clauses)) ,(case-expand-clauses key (cdr clauses))))))");
    eval_str("(defmacro case (expr . clauses) `(let ((_k_ ,expr)) ,(case-expand-clauses '_k_ clauses)))");
    eval_str("(define (letrec-sets bindings) (if (null? bindings) nil (cons `(set! ,(caar bindings) ,(cadr (car bindings))) (letrec-sets (cdr bindings)))))");
    eval_str("(defmacro letrec (bindings . body) `((lambda ,(map car bindings) ,@(letrec-sets bindings) ,@body) ,@(map (lambda (b) nil) bindings)))");
    eval_str("(defmacro do (clauses test . body) `(let _do_ ,(map (lambda (c) (list (car c) (cadr c))) clauses) (if ,(car test) ,(if (null? (cdr test)) nil (cadr test)) (begin ,@body (_do_ ,@(map caddr clauses))))))");
    eval_str("(defmacro and (a b) `(if ,a ,b nil))");
    eval_str("(defmacro or (a b) `(if ,a ,a ,b))");
    eval_str("(defmacro when (test . body) `(if ,test (begin ,@body) nil))");
    eval_str("(defmacro unless (test . body) `(if ,test nil (begin ,@body)))");

    /* Multiple return values */
    eval_str("(define values list)");
    eval_str("(define (call-with-values producer consumer) (apply consumer (producer)))");

    /* define-fn: (define-fn (f x) body) shorthand */
    eval_str("(defmacro define-fn (sig body) `(define ,(car sig) (lambda ,(cdr sig) ,body)))");

    /* Combinators */
    eval_str("(define identity (lambda (x) x))");
    eval_str("(define compose (lambda (f g) (lambda (x) (f (g x)))))");

    /* String utilities */
    eval_str("(define ->str (lambda (x) (cond ((string? x) x) ((number? x) (number->string x)) (else \"\"))))");
    eval_str("(define str2 (lambda (a b) (string-append (->str a) (->str b))))");
    eval_str("(define str (lambda args (reduce str2 \"\" args)))");

    /* Metaprogramming */
    eval_str("(define macroexpand (lambda (form) (let ((expanded (macroexpand-1 form))) (if (eq? expanded form) form (macroexpand expanded)))))");

    /* Comments */
    eval_str("(defmacro comment rest nil)");

    /* Escape continuations */
    eval_str("(define (call/ec proc) (let ((tag (gensym))) (catch tag (proc (lambda (val) (throw tag val))))))");
    eval_str("(define call-with-escape-continuation call/ec)");

    /* Error handling */
    eval_str("(define *error-tag* (gensym))");
    eval_str("(define *error-handler* nil)");
    eval_str("(define (raise obj) (if (null? *error-handler*) (begin (display \"ERROR: \") (println obj) (exit)) (*error-handler* obj)))");
    eval_str("(define (with-exception-handler handler thunk) (let ((saved *error-handler*)) (begin (set! *error-handler* (lambda (e) (throw *error-tag* (handler e)))) (let ((result (catch *error-tag* (thunk)))) (begin (set! *error-handler* saved) result)))))");
    eval_str("(define with-handler with-exception-handler)");
    eval_str("(define (error msg) (raise msg))");
    eval_str("(define (guard-clauses var clauses) (if (null? clauses) '(raise e) (let ((clause (car clauses))) (if (eq? (car clause) 'else) (cadr clause) `(if ,(car clause) ,(cadr clause) ,(guard-clauses var (cdr clauses)))))))");
    eval_str("(defmacro guard (binding . body) `(with-handler (lambda (,(car binding)) ,(guard-clauses (car binding) (cdr binding))) (lambda () ,@body)))");
    /* dynamic-wind is available as a primitive; no unwind-protect (CL name) */

    /* Dynamic parameters */
    eval_str("(define (make-parameter init) (let ((val init)) (lambda args (if (null? args) val (set! val (car args))))))");
    eval_str("(define (call-with-parameterize param new-val thunk) (let ((saved (param))) (dynamic-wind (lambda () (param new-val)) thunk (lambda () (param saved)))))");
    eval_str("(defmacro parameterize (bindings body) `(call-with-parameterize ,(caar bindings) ,(cadr (car bindings)) (lambda () ,body)))");

    /* Association lists */
    eval_str("(define assoc (lambda (key alist) (if (null? alist) nil (if (eq? key (caar alist)) (car alist) (assoc key (cdr alist))))))");
    eval_str("(define get (lambda (key alist default) (if (null? alist) default (if (eq? key (caar alist)) (cdar alist) (get key (cdr alist) default)))))");
}
