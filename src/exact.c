/* exact.c -- exact(x) of a Home real as the HP Prime's CAS computes it.
 *
 * The one file of the core with floating point (`make floatcheck` keeps it so): the
 * Prime's answer comes from binary doubles, so only doubles give the same fractions.
 * Measured on the Virtual Calculator 2026-10-23 (notes/ppl.md, "Built-ins for the corpus
 * programs"): the Home real becomes a double whose mantissa is truncated to 48 bits, then
 * giac's exact_double runs on it with eps = 1E-12:
 *   exact(33.3333333334) = 100/3, exact(99.9999999997) = 100,
 *   exact(0.333333333) = 333332121/999996364 (the odd last term is the 48-bit truncation).
 * A few dozen double operations a call: libgcc's soft-float on the SH-4 (no FPU), IEEE
 * exact there and on the Mac (built with -ffp-contract=off, so no fused multiply-add).
 *
 * GPL-3.0-or-later.
 */
#include <string.h>
#include "exact.h"

_Static_assert(sizeof(double) == 8, "exact.c needs 64-bit doubles (-m4-nofpu, not -m4-single-only)");

#define BIG (1 << 30)
#define EPS 1e-12

/* the Prime's double of |x| for 1E-10 <= |x| < 1E10: correctly rounded (10^k is exact up
 * to 10^22), then the mantissa cut to 48 of its 53 bits */
static double to_double48(const real *x)
{
    uint64_t M = (uint64_t)x->m[0] * 100000000u + (uint64_t)x->m[1] * 10000u + x->m[2], b;
    double p10 = 1, d;
    int k;
    for (k = 0; k < 11 - x->e; k++) p10 *= 10;
    d = (double)M / p10;
    memcpy(&b, &d, sizeof b);
    b &= ~(uint64_t)0x1F;
    memcpy(&d, &b, sizeof d);
    return d;
}

/* giac's exact_double for 1 <= d <= 2^30 (kti89.cc in giac): continued-fraction terms
 * while the rest is above eps, eps growing with the square of each reciprocal; the
 * convergent p/q of the terms */
static int cfrac(double d, uint64_t *p, uint64_t *q)
{
    double eps = EPS, eps1 = 1 + EPS;
    uint64_t h1 = 1, h2 = 0, k1 = 0, k2 = 1;
    for (;;) {
        uint64_t a = (uint64_t)(int32_t)(d * eps1), h, k;
        if (a && (h1 > (UINT64_MAX - h2) / a || k1 > (UINT64_MAX - k2) / a)) return EX_BIG;
        h = a * h1 + h2; k = a * k1 + k2;
        h2 = h1; h1 = h; k2 = k1; k1 = k;
        d = d - (double)(int32_t)(d * eps1);
        if (d <= eps) break;
        d = 1 / d;
        if (d > BIG) break;
        eps = eps * d * d;
    }
    *p = h1; *q = k1;
    return EX_FRAC;
}

int exact_frac(const real *x, uint64_t *p, uint64_t *q)
{
    double d;
    *p = 0; *q = 1;
    if (dec_is_zero(x)) return EX_FRAC;
    /* far from 1 the answer needs no doubles (and 10^k would not be exact) */
    if (x->e >= 10) return EX_FLOOR;
    if (x->e <= -11) return EX_INVFLOOR;
    d = to_double48(x);
    if (d > BIG) return EX_FLOOR;
    if (d >= 1) return cfrac(d, p, q);
    d = 1 / d;
    if (d > BIG) return EX_INVFLOOR;
    return cfrac(d, q, p);
}
