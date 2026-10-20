/* units.h -- HP Prime measurements (TYPE 9): a real magnitude times a unit.
 *
 * A unit is a product of catalogue units, each with an SI prefix and an
 * integer power (`kgf*m^-1`), plus the text it was written with when it came
 * from a literal (`1_(kgf/m)` keeps "kgf/m" for display).  Values hold the
 * magnitude inline (value.u.un.r) and a reference-counted pun.
 */
#ifndef PPL_UNITS_H
#define PPL_UNITS_H

#include "vm.h"

#define UN_MAXF 8                  /* factors in one unit */

typedef struct ufac { uint8_t id, pre; int8_t pow; } ufac;   /* catalogue id, prefix (0 none), power */

typedef struct pun {
    obj h;
    uint8_t n;                     /* factors */
    ufac f[UN_MAXF];
    uint16_t tlen;                 /* length of text (0: rebuild the text from f) */
    char text[1];                  /* the unit as written, UTF-8, NUL-terminated */
} pun;

/* operators where either side is a measurement (from ar_binary) */
int  un_binary(ppl_vm *V, int op, const value *a, const value *b, value *out);
int  un_neg(const value *a, value *out);
void un_text(ppl_vm *V, sbuf *b, const value *v);
int  un_equal(const value *a, const value *b);

extern const builtin bi_unit_table[];

#endif
