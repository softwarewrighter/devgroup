#pragma once

/* tml.h -- Tiny Macro Lisp core types and macros
 *
 * All values are a single COR24 int (24-bit).
 * Low 2 bits = tag, upper bits = payload.
 *
 *   [23..2] payload  [1:0] tag
 *   00  Fixnum   -- 22-bit signed integer
 *   01  Cons     -- heap cell index
 *   10  Symbol   -- symbol table index
 *   11  Extended -- heap cell index (closure, macro, primitive, string)
 */

/* Tags */
#define TAG_FIXNUM   0
#define TAG_CONS     1
#define TAG_SYMBOL   2
#define TAG_EXTENDED 3
#define TAG_MASK     3

/* Tag checks */
#define IS_FIXNUM(v)   (((v) & TAG_MASK) == TAG_FIXNUM)
#define IS_CONS(v)     (((v) & TAG_MASK) == TAG_CONS)
#define IS_SYMBOL(v)   (((v) & TAG_MASK) == TAG_SYMBOL)
#define IS_EXTENDED(v) (((v) & TAG_MASK) == TAG_EXTENDED)

/* Fixnum encode/decode */
#define MAKE_FIXNUM(n)  (((n) << 2) | TAG_FIXNUM)
#define FIXNUM_VAL(v)   ((v) >> 2)

/* Pointer (index) encode/decode */
#define MAKE_CONS(idx)     (((idx) << 2) | TAG_CONS)
#define MAKE_SYMBOL(idx)   (((idx) << 2) | TAG_SYMBOL)
#define MAKE_EXTENDED(idx) (((idx) << 2) | TAG_EXTENDED)
#define PTR_IDX(v)         ((v) >> 2)

/* Special values: nil = symbol 0, t = symbol 1 */
#define NIL_VAL   MAKE_SYMBOL(0)
#define T_VAL     MAKE_SYMBOL(1)
#define IS_NIL(v) ((v) == NIL_VAL)

#define HEAP_SIZE 32768

/* Extended object type tags */
#define ETYPE_CLOSURE   MAKE_FIXNUM(1)
#define ETYPE_MACRO     MAKE_FIXNUM(2)
#define ETYPE_PRIMITIVE MAKE_FIXNUM(3)
#define ETYPE_STRING    MAKE_FIXNUM(4)
