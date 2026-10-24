/* exact.h -- exact(x) of a Home real as the HP Prime's CAS computes it.
 *
 * GPL-3.0-or-later.  See exact.c, the one file of the core that uses floating point.
 */
#ifndef PPL_EXACT_H
#define PPL_EXACT_H
#include <stdint.h>
#include "dec.h"

enum {
    EX_FRAC,        /* |x| is p/q (q >= 1) */
    EX_FLOOR,       /* |x| above 2^30: the answer is floor(|x|) */
    EX_INVFLOOR,    /* |x| below 2^-30: the answer is 1/floor(1/|x|) */
    EX_BIG          /* p or q would not fit 64 bits */
};

/* the fraction the Prime's exact() gives for |x| (the caller puts back the sign) */
int exact_frac(const real *x, uint64_t *p, uint64_t *q);

#endif
