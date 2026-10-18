/* vm.h -- PPL values, objects, programs and the virtual machine.
 *
 * Values carry the Prime's own TYPE codes as tags.  Strings, lists and
 * matrices are reference-counted heap objects with copy-on-write, which
 * gives PPL's value semantics (L2 := L1 copies) without copying.
 */
#ifndef PPL_VM_H
#define PPL_VM_H

#include <stdint.h>
#include <stddef.h>
#include "dec.h"

/* ---- value types: TYPE() codes ---- */
enum {
    T_REAL = 0, T_INT = 1, T_STR = 2, T_CPLX = 3, T_MAT = 4, T_ERRV = 5,
    T_LIST = 6, T_SYM = 8, T_UNIT = 9,
    /* internal kinds, never seen by programs */
    T_THUNK = 100, T_VREF = 101, T_NONE = 102
};

/* integer bases, as GETBASE answers them */
enum { B_DEFAULT = 0, B_BIN = 1, B_OCT = 2, B_DEC = 3, B_HEX = 4 };

typedef struct obj { uint32_t rc; } obj;
struct pstr; struct plist; struct pmat; struct psym;

typedef struct { real re, im; } cplx;

typedef struct value {
    uint8_t t;          /* T_* */
    uint8_t ib;         /* T_INT: base B_*; T_VREF: variable kind */
    uint8_t iw;         /* T_INT: word size 1..64, 0 = system Bits */
    uint8_t x;          /* T_REAL: 1 = shown as degrees-minutes-seconds */
    union {
        real r;
        cplx c;
        struct { uint32_t hi, lo; } i;        /* T_INT: 64-bit two's compl. */
        struct pstr *s;
        struct plist *l;
        struct pmat *m;
        struct psym *y;
        struct { uint32_t pc; uint16_t idx; } th;  /* T_THUNK / T_VREF */
    } u;
} value;

typedef struct pstr  { obj h; uint32_t len; uint16_t c[1]; } pstr;      /* UTF-16 */
typedef struct plist { obj h; uint32_t n, cap; value *v; } plist;
/* matrix: rows x cols values (reals or complex), row-major; a vector has
 * rows == 0 marker vec=1 and cols elements */
typedef struct pmat  { obj h; uint16_t rows, cols; uint8_t vec; value *v; } pmat;
typedef struct psym  { obj h; pstr *text; } psym;   /* a symbolic/CAS object */

/* ---- errors ---- */
enum {
    E_OK = 0,
    E_SYNTAX = 1,       /* Syntax error */
    E_BADTYPE,          /* Bad argument type */
    E_BADVAL,           /* Bad argument value */
    E_DIM,              /* Invalid dimension */
    E_UNDEF,            /* Undefined name */
    E_DIV0,             /* Division by 0 */
    E_INDEX,            /* Index outside range */
    E_MEM,              /* Insufficient memory */
    E_INFRES,           /* Infinite result */
    E_NONREAL,          /* Non-real result */
    E_ARGC,             /* Wrong number of arguments */
    E_UNSUPPORTED,      /* Not supported here (graphics, CAS, apps...) */
    E_KILL,             /* KILL: stop the program */
    E_UNDEFRES,         /* Undefined result */
    E_STATS,            /* Insufficient statistics data */
    E_NOTIMPL,          /* recognised name, not implemented yet */
    E_COUNT
};
const char *ppl_errmsg(int e);

/* ---- system state (Home settings and variables) ---- */
typedef struct ppl_settings {
    int angle;          /* HAngle */
    int format;         /* HFormat: FMT_* */
    int digits;         /* HDigits */
    int complex_ok;     /* HComplex */
    int base;           /* Base: 0 bin 1 oct 2 dec 3 hex */
    int bits;           /* Bits: integer word size */
    int is_signed;      /* Signed */
    int entry, language, separator;
} ppl_settings;

/* ---- compiled code ---- */
typedef struct pfunc {
    char *name;
    uint8_t nparams, nlocals, exported, defined;
    uint8_t *code; uint32_t ncode, capcode;
    value *k; uint16_t nk, capk;       /* constants */
    char **names; uint16_t nnames, capnames;   /* run-time resolved names */
    struct pprog *prog;
    int line;
    uint8_t home;                      /* Home code: := creates variables */
} pfunc;

typedef struct pvar { char *name; value v; uint8_t exported; } pvar;

typedef struct pprog {
    char *name;
    pfunc **f; int nf, capf;
    pvar *g; int ng, capg;            /* program-level variables */
    struct pprog *next;
} pprog;

/* a user (Home) variable created by := in Home */
typedef struct hvar { char *name; value v; struct hvar *next; } hvar;

typedef struct frame {
    pfunc *fn;
    uint32_t pc;
    uint32_t bp;                       /* stack index of local 0 */
    uint32_t sp0;                      /* stack height to restore on return */
    value last;                        /* the value a function answers */
    uint8_t thunk;                     /* an expression run for a builtin */
    int line;
} frame;

typedef struct handler { uint32_t frame_depth, sp, pc; } handler;

#define VM_STACK 4096
#define VM_FRAMES 256
#define VM_HANDLERS 64

typedef struct ppl_vm {
    value stack[VM_STACK]; uint32_t sp;
    frame frames[VM_FRAMES]; int nframes;
    handler handlers[VM_HANDLERS]; int nhandlers;
    ppl_settings set;
    value sysvars[80];                 /* A-Z, L0-L9, M0-M9, Z0-Z9, Ans... */
    pprog *progs;
    hvar *hvars;
    uint32_t rng[4];
    int err; int errline; char errname[48];
    uint32_t ticks0;
    void (*print)(struct ppl_vm *, const char *utf8);   /* PRINT output */
    uint32_t (*ticks)(void);           /* milliseconds, for TICKS */
    int (*getkey)(void);               /* GETKEY: -1 when none */
    void (*now)(int *ymdhms);          /* host clock: y, mo, d, h, mi, s */
    void *user;
    int depth;                         /* re-entrant exec depth */
} ppl_vm;

/* ---- value helpers (value.c) ---- */
void v_incref(const value *v);
void v_decref(value *v);
static inline void v_copy(value *dst, const value *src) { *dst = *src; v_incref(dst); }
void v_set(value *dst, const value *src);       /* release dst, copy src */
void v_real(value *v, const real *r);
void v_int32(value *v, int32_t n);
void v_none(value *v);
int  v_str_utf8(value *v, const char *s, size_t n);
int  v_str_u16(value *v, const uint16_t *s, uint32_t n);
pstr *str_new(uint32_t len);
plist *list_new(uint32_t n);
int  list_push(plist *l, const value *v);       /* takes a new ref */
pmat *mat_new(int rows, int cols, int vec);
int  v_list(value *v, uint32_t n);              /* a list of n zeros */
int  v_mat(value *v, int rows, int cols, int vec);
plist *list_own(value *v);                      /* copy-on-write */
pmat  *mat_own(value *v);
pstr  *str_own(value *v);
int  v_equal(const value *a, const value *b);
int  v_truth(const value *v, int *t);
/* text of a value as STRING() writes it (UTF-8) into a growable buffer */
typedef struct sbuf { char *p; size_t n, cap; } sbuf;
void sb_init(sbuf *b);
void sb_put(sbuf *b, const char *s, size_t n);
void sb_puts(sbuf *b, const char *s);
void sb_free(sbuf *b);
void v_to_text(ppl_vm *V, sbuf *b, const value *v, int quote_strings);
size_t u16_to_utf8(char *out, const uint16_t *s, uint32_t n);  /* out may be NULL */
uint32_t utf8_to_u16(uint16_t *out, const char *s, size_t n);  /* out may be NULL */
int str_eq_ascii(const pstr *s, const char *a);
void int_mask(const ppl_vm *V, value *v);   /* wrap an integer to its word size */

/* ---- arithmetic (arith.c) ---- */
enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_MOD, OP_NTHROOT,
       OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_AND, OP_OR, OP_XOR,
       OP_IQUO };
int  ar_binary(ppl_vm *V, int op, const value *a, const value *b, value *out);
int  ar_neg(ppl_vm *V, const value *a, value *out);
int  ar_not(ppl_vm *V, const value *a, value *out);
int  dec_err(int st);            /* DEC_* status -> E_* (soft ones -> 0) */
int  to_real(const value *v, real *r);   /* real or integer -> real */
int  to_int(const value *v, int32_t *n); /* integer-valued -> int32 */
int  to_index(const value *v, int32_t *n); /* truncating */

/* apply a real->real function element-wise over lists/matrices; complex
 * arguments go to cf (may be NULL: Bad argument type) */
typedef int (*rfn1)(ppl_vm *, const real *, value *out);
typedef int (*cfn1)(ppl_vm *, const cplx *, value *out);
int  ar_map1(ppl_vm *V, const value *a, rfn1 rf, cfn1 cf, value *out);
int  c_make(value *v, const real *re, const real *im);   /* complex or real */

int c_mul(cplx *r, const cplx *a, const cplx *b);
int c_div(cplx *r, const cplx *a, const cplx *b);
int c_abs(real *r, const cplx *a);
int c_arg(ppl_vm *V, real *r, const cplx *a);
int c_sqrt(cplx *r, const cplx *a);
int c_ln(cplx *r, const cplx *a);
int c_exp(cplx *r, const cplx *a);
int c_pow(cplx *r, const cplx *a, const cplx *b);
int mat_mul(ppl_vm *V, const value *a, const value *b, value *out);
int mat_inv(ppl_vm *V, const value *a, value *out, value *det);

/* ---- built-ins (builtins.c) ---- */
typedef int (*bfn)(ppl_vm *V, int argc, value *argv, value *out);
typedef struct builtin {
    const char *name;
    bfn fn;
    int8_t minargs, maxargs;   /* maxargs -1: any */
    uint8_t flags;
} builtin;
#define BF_NOPAREN 1   /* may be called without parentheses (GETKEY, PI...) */
#define BF_QUOTE1  2   /* arg 1 is an expression evaluated by the builtin */
#define BF_QUOTE2  4   /* arg 2 is a variable reference */
#define BF_LIST    8   /* map over list arguments */
#define BF_VAR     16  /* a system variable read as name */
const builtin *bi_lookup(const char *name);
const builtin *bi_get(int id);
int bi_id(const builtin *b);
int bi_count(void);

/* ---- VM (vm.c) ---- */
void ppl_init(ppl_vm *V);
void ppl_free(ppl_vm *V);
int  ppl_load(ppl_vm *V, const char *name, const char *src, size_t n, char *msg, size_t msgn);
int  ppl_call(ppl_vm *V, const char *fname, int argc, const value *argv, value *out);
int  ppl_eval(ppl_vm *V, const char *src, value *out);   /* Home expression(s) */
int  vm_run_thunk(ppl_vm *V, const value *thunk, value *out);
int  vm_store_ref(ppl_vm *V, const value *ref, const value *v);
int  vm_load_ref(ppl_vm *V, const value *ref, value *out);
int  vm_call_value(ppl_vm *V, const char *name, int argc, value *argv, value *out);
int  vm_eval_string(ppl_vm *V, const char *utf8, size_t n, value *out);
int  vm_index(ppl_vm *V, const value *c, int argc, const value *idx, value *out);
int  vm_setting_get(ppl_vm *V, const char *name, value *out);   /* -1: no such */
int  vm_setting_set(ppl_vm *V, const char *name, const value *v);
pfunc *vm_find_func(ppl_vm *V, const char *name, int exported_only);

/* system variable ids (sysvars[]) */
enum { SV_A = 0, SV_Z = 25, SV_L0 = 26, SV_M0 = 36, SV_Z0 = 46, SV_ANS = 56, SV_G0 = 57, SV_COUNT = 67 };

#endif
