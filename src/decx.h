/* decx.h -- the extended-precision decimal engine behind dec.c/decfn.c.
 *
 * An xn is (-1)^s * C * 10^e where the integer coefficient C is held in
 * little-endian base-10000 limbs d[0..n-1] (d[n-1] != 0; n == 0 is zero).
 * Operations take a precision P (significant digits) and round half away
 * from zero, the Prime's rule.  P up to XN_MAXP digits.
 */
#ifndef PPL_DECX_H
#define PPL_DECX_H

#include "dec.h"

#define XN_LIMBS 28              /* capacity: 112 digits */
#define XN_MAXP  48              /* largest precision an operation accepts */
#define XN_WP    26              /* working precision of elementary functions */

typedef struct {
    int32_t  e;
    uint8_t  s;
    uint8_t  n;
    uint8_t  k;                  /* DEC_FINITE / DEC_INF / DEC_NAN */
    uint16_t d[XN_LIMBS];
} xn;

extern const uint16_t xn_pow10[5];   /* 1, 10, 100, 1000, 10000 */

void xn_zero(xn *x);
void xn_from_int(xn *x, int32_t v);
void xn_from_real(xn *x, const real *r);
int  xn_to_real(real *r, const xn *x);   /* rounds to 12 digits, range check */
int  xn_from_str(xn *x, const char *s); /* constants: "[-]ddd.ddd[E[-]dd]" */
int  xn_ndig(const xn *x);
int  xn_top(const xn *x);                /* exponent of the leading digit */
int  xn_is_zero(const xn *x);
void xn_round(xn *x, int P);
void xn_trunc_int(xn *x);                /* drop the fraction (toward 0) */
int  xn_is_int(const xn *x);
int  xn_cmpmag(const xn *a, const xn *b);
int  xn_cmp(const xn *a, const xn *b);
void xn_add(xn *r, const xn *a, const xn *b, int P);
void xn_sub(xn *r, const xn *a, const xn *b, int P);
void xn_mul(xn *r, const xn *a, const xn *b, int P);
int  xn_div(xn *r, const xn *a, const xn *b, int P);
void xn_mul_int(xn *r, const xn *a, int32_t m, int P);
int  xn_div_int(xn *r, const xn *a, int32_t m, int P);
int  xn_sqrt(xn *r, const xn *a, int P);
int  xn_exp(xn *r, const xn *x, int P);  /* decfn.c; DEC_EOVERFLOW / DEC_EUNDERFLOW */
void xn_scale10(xn *x, int k);           /* multiply by 10^k (exponent only) */
int  xn_to_int32(const xn *x, int32_t *v);   /* integer valued, |v| < 2^31 */
int  xn_digit(const xn *x, int pos);     /* digit at absolute position 10^pos */
/* integer division of coefficients: q = floor(A/B), rem = A - qB (both >=0,
 * integer valued xn with e == 0).  Returns 0, or -1 on capacity overflow. */
int  xn_idivmod(xn *q, xn *rem, const xn *a, const xn *b);

#endif
