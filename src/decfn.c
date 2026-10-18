/* decfn.c -- elementary functions on 12-digit reals.
 *
 * Method: every function is evaluated in the xn engine with XN_WP (26)
 * significant digits -- 14 guard digits -- and rounded once to 12 digits,
 * half away from zero.  With that many guard digits the 12-digit result is
 * the correctly rounded value except when the exact result lies within about
 * 1e-24 (relative) of a rounding midpoint, which in practice only happens
 * for exact results (powers, roots of perfect powers), and those are
 * computed exactly or land on the exact value after rounding.
 *
 * Argument reduction uses 54-digit constants (pi, ln 2, ln 10); trig in
 * degrees and gradians reduces the angle exactly in decimal first, so that
 * SIN(180) is exactly 0 in degrees.
 */
#include "decx.h"
#include <string.h>

#define WP XN_WP            /* working precision */
#define GP (XN_WP + 8)      /* precision for reductions */

static const char S_PI[]   = "3.14159265358979323846264338327950288419716939937510582";
static const char S_LN2[]  = "0.693147180559945309417232121458176568075500134360255254";
static const char S_LN10[] = "2.30258509299404568401799145468436420760110148862877298";

static int k_ready;
static xn K_PI, K_PI2, K_LN2, K_LN10, K_ONE, K_HALF, K_TWO, K_180, K_200;

static void init_consts(void)
{
    if (k_ready) return;
    xn_from_str(&K_PI, S_PI);
    xn_div_int(&K_PI2, &K_PI, 2, XN_MAXP);
    xn_from_str(&K_LN2, S_LN2);
    xn_from_str(&K_LN10, S_LN10);
    xn_from_int(&K_ONE, 1);
    xn_from_int(&K_TWO, 2);
    xn_from_str(&K_HALF, "0.5");
    xn_from_int(&K_180, 180);
    xn_from_int(&K_200, 200);
    k_ready = 1;
}

/* ---------------------------------------------------------- helpers */

static int finish(real *r, const xn *x) { return xn_to_real(r, x); }

static int nan_result(real *r) { dec_zero(r); r->k = DEC_NAN; return DEC_EDOMAIN; }

/* x - k*c for an integer k (as xn) and a constant c, at precision P */
static void reduce_by(xn *r, const xn *x, const xn *k, const xn *c, int P)
{
    xn t;
    xn_mul(&t, k, c, XN_MAXP);
    xn_sub(r, x, &t, P);
}

/* nearest integer of an xn (half away from zero) */
static void xn_nint(xn *r, const xn *x)
{
    xn t = *x;
    int s = t.s;
    t.s = 0;
    xn_add(&t, &t, &K_HALF, XN_MAXP);
    xn_trunc_int(&t);
    if (t.n) t.s = (uint8_t)s;
    *r = t;
}

/* ------------------------------------------------------------- exp */

/* expm1(x) for |x| <= ~1.2: halve 10 times, series, then u <- u(u+2). */
static void x_expm1_small(xn *r, const xn *x, int P)
{
    xn y, term, sum, t;
    int k, Q = P + 6;
    /* y = x / 1024 is exact in decimal (1/1024 = 0.0009765625) */
    xn_from_str(&t, "0.0009765625");
    xn_mul(&y, x, &t, XN_MAXP);
    xn_round(&y, Q);
    sum = y;
    term = y;
    for (k = 2; k < 60; k++) {
        xn_mul(&term, &term, &y, Q);
        xn_div_int(&term, &term, k, Q);
        if (!term.n || xn_top(&term) < xn_top(&sum) - Q - 1) break;
        xn_add(&sum, &sum, &term, Q);
    }
    for (k = 0; k < 10; k++) {
        xn_add(&t, &sum, &K_TWO, Q);
        xn_mul(&sum, &sum, &t, Q);
    }
    *r = sum;
    xn_round(r, P);
}

/* exp(x) = 10^k * (1 + expm1(x - k ln10)).  Returns overflow/underflow. */
static int x_exp(xn *r, const xn *x, int P)
{
    xn k, rr, u;
    int32_t ki;
    init_consts();
    if (!x->n) { *r = K_ONE; return 0; }
    if (xn_top(x) >= 4) {   /* |x| >= 10000 */
        if (x->s) { xn_zero(r); return DEC_EUNDERFLOW; }
        return DEC_EOVERFLOW;
    }
    xn_div(&k, x, &K_LN10, 20);
    xn_nint(&k, &k);
    xn_to_int32(&k, &ki);
    reduce_by(&rr, x, &k, &K_LN10, P + 8);
    x_expm1_small(&u, &rr, P + 2);
    xn_add(r, &u, &K_ONE, P);
    if (r->n) r->e += ki;
    return 0;
}

static int x_expm1(xn *r, const xn *x, int P)
{
    xn one;
    init_consts();
    if (!x->n) { xn_zero(r); return 0; }
    if (xn_top(x) < 0 || (xn_top(x) == 0 && xn_digit(x, 0) <= 1)) {
        /* |x| < 2: direct, no cancellation */
        x_expm1_small(r, x, P);
        return 0;
    }
    {
        int st = x_exp(r, x, P + 4);
        if (st == DEC_EOVERFLOW) return st;
        one = K_ONE;
        xn_sub(r, r, &one, P);
    }
    return 0;
}

/* -------------------------------------------------------------- ln */

/* ln(g) for g near 1 (0.7 <= g <= 1.5): 2 atanh((g-1)/(g+1)) */
static void x_ln_near1(xn *r, const xn *gm1, int P)
{
    /* gm1 = g - 1, exact */
    xn z, z2, num, den, term, sum;
    int k, Q = P + 4;
    num = *gm1;
    xn_add(&den, gm1, &K_TWO, XN_MAXP);
    xn_div(&z, &num, &den, Q);
    if (!z.n) { xn_zero(r); return; }
    xn_mul(&z2, &z, &z, Q);
    sum = z;
    term = z;
    for (k = 3; k < 400; k += 2) {
        xn t;
        xn_mul(&term, &term, &z2, Q);
        xn_div_int(&t, &term, k, Q);
        if (!t.n || xn_top(&t) < xn_top(&sum) - Q - 1) break;
        xn_add(&sum, &sum, &t, Q);
    }
    xn_mul_int(r, &sum, 2, P);
}

static int x_ln(xn *r, const xn *x, int P)
{
    xn f, gm1, t, acc;
    int32_t E;
    int j;
    init_consts();
    if (x->s || !x->n) return DEC_EDOMAIN;
    /* near 1: direct */
    xn_sub(&gm1, x, &K_ONE, XN_MAXP);
    if (!gm1.n) { xn_zero(r); return 0; }
    {
        xn lo, hi;
        xn_from_str(&lo, "0.7"); xn_from_str(&hi, "1.5");
        if (xn_cmp(x, &lo) >= 0 && xn_cmp(x, &hi) <= 0) {
            x_ln_near1(r, &gm1, P);
            return 0;
        }
    }
    /* x = f * 10^E, 1 <= f < 10 */
    f = *x;
    E = xn_top(&f);
    f.e -= E;
    /* f = 2^j * g with g in [0.75, 1.5) */
    j = 0;
    {
        xn lim;
        xn_from_str(&lim, "1.5");
        while (xn_cmp(&f, &lim) >= 0) { xn_div_int(&f, &f, 2, XN_MAXP); j++; }
    }
    /* f/2^j is exact in decimal (powers of 1/2 terminate) */
    xn_sub(&gm1, &f, &K_ONE, XN_MAXP);
    x_ln_near1(&acc, &gm1, P + 4);
    if (j) { xn_mul_int(&t, &K_LN2, j, GP); xn_add(&acc, &acc, &t, P + 4); }
    if (E) {
        xn ee;
        xn_from_int(&ee, E);
        xn_mul(&t, &ee, &K_LN10, GP);
        xn_add(&acc, &acc, &t, P + 4);
    }
    *r = acc;
    xn_round(r, P);
    return 0;
}

/* ------------------------------------------------------- real wrappers */

static int load(xn *x, const real *a)
{
    if (a->k) return a->k;
    xn_from_real(x, a);
    return 0;
}

int dec_exp(real *r, const real *a)
{
    xn x, y;
    int st;
    if (a->k == DEC_NAN) return nan_result(r);
    if (a->k == DEC_INF) { dec_zero(r); if (!a->s) { r->k = DEC_INF; } return 0; }
    load(&x, a);
    st = x_exp(&y, &x, WP);
    if (st == DEC_EUNDERFLOW) { dec_zero(r); return 0; }
    if (st == DEC_EOVERFLOW) return dec_maxreal(r, 0);
    st = finish(r, &y);
    if (st == DEC_EUNDERFLOW) return 0;
    return st;
}

int dec_expm1(real *r, const real *a)
{
    xn x, y;
    int st;
    if (a->k) return nan_result(r);
    load(&x, a);
    st = x_expm1(&y, &x, WP);
    if (st == DEC_EOVERFLOW) return dec_maxreal(r, 0);
    if (st) return st;
    return finish(r, &y);
}

int dec_ln(real *r, const real *a)
{
    xn x, y;
    int st;
    if (a->k) return nan_result(r);
    if (dec_is_zero(a)) return DEC_EDOMAIN;   /* -infinity */
    if (a->s) return DEC_EDOMAIN;             /* complex: caller decides */
    load(&x, a);
    st = x_ln(&y, &x, WP);
    if (st) return st;
    return finish(r, &y);
}

int dec_lnp1(real *r, const real *a)
{
    xn x, y, t;
    if (a->k) return nan_result(r);
    load(&x, a);
    init_consts();
    {
        xn m1;
        xn_from_int(&m1, -1);
        if (xn_cmp(&x, &m1) <= 0) return DEC_EDOMAIN;
    }
    {
        xn lo, hi;
        xn_from_str(&lo, "-0.3"); xn_from_str(&hi, "0.5");
        if (xn_cmp(&x, &lo) >= 0 && xn_cmp(&x, &hi) <= 0) {
            x_ln_near1(&y, &x, WP);
            return finish(r, &y);
        }
    }
    xn_add(&t, &x, &K_ONE, XN_MAXP);
    x_ln(&y, &t, WP);
    return finish(r, &y);
}

int dec_log10(real *r, const real *a)
{
    xn x, y;
    int st;
    if (a->k) return nan_result(r);
    if (dec_is_zero(a) || a->s) return DEC_EDOMAIN;
    load(&x, a);
    /* exact for powers of ten */
    if (a->m[0] == 1000 && a->m[1] == 0 && a->m[2] == 0) { dec_from_int(r, a->e); return 0; }
    st = x_ln(&y, &x, WP + 2);
    if (st) return st;
    xn_div(&y, &y, &K_LN10, WP);
    return finish(r, &y);
}

/* log of x to base b, both with guard digits: LOG(8,2) is exactly 3 */
int dec_logb(real *r, const real *a, const real *b)
{
    xn x, y, t;
    int st;
    if (a->k || b->k) return nan_result(r);
    if (dec_is_zero(a) || a->s || dec_is_zero(b) || b->s) return DEC_EDOMAIN;
    xn_from_real(&x, a); xn_from_real(&y, b);
    st = x_ln(&t, &x, WP + 2);
    if (st) return st;
    st = x_ln(&y, &y, WP + 2);
    if (st) return st;
    if (!y.n) return DEC_EDIVZERO;
    xn_div(&t, &t, &y, WP);
    return finish(r, &t);
}

int dec_alog(real *r, const real *a)
{
    xn x, y;
    int st;
    if (a->k) return nan_result(r);
    load(&x, a);
    if (xn_is_int(&x)) {
        int32_t v;
        if (xn_to_int32(&x, &v) || v > 10000 || v < -10000) {
            if (a->s) { dec_zero(r); return 0; }
            return dec_maxreal(r, 0);
        }
        xn_from_int(&y, 1);
        y.e = v;
        st = finish(r, &y);
        return st == DEC_EUNDERFLOW ? 0 : st;
    }
    init_consts();
    xn_mul(&y, &x, &K_LN10, GP);
    st = x_exp(&y, &y, WP);
    if (st == DEC_EUNDERFLOW) { dec_zero(r); return 0; }
    if (st == DEC_EOVERFLOW) return dec_maxreal(r, 0);
    st = finish(r, &y);
    return st == DEC_EUNDERFLOW ? 0 : st;
}

int dec_sqrt(real *r, const real *a)
{
    xn x, y;
    if (a->k == DEC_INF && !a->s) { *r = *a; return 0; }
    if (a->k) return nan_result(r);
    if (a->s && !dec_is_zero(a)) return DEC_EDOMAIN;
    load(&x, a);
    xn_sqrt(&y, &x, DEC_DIGITS + 2);
    /* xn_sqrt is truncated-then-rounded at 14 digits; recompute at 12 from
     * the truncated root to keep a single rounding */
    xn_sqrt(&y, &x, DEC_DIGITS);
    return finish(r, &y);
}

/* integer power by squaring, at precision P */
static void x_ipow(xn *r, const xn *x, uint32_t n, int P)
{
    xn b = *x, acc = K_ONE;
    init_consts();
    acc = K_ONE;
    while (n) {
        if (n & 1) xn_mul(&acc, &acc, &b, P);
        n >>= 1;
        if (n) xn_mul(&b, &b, &b, P);
    }
    *r = acc;
}

int dec_pow(real *r, const real *a, const real *b)
{
    xn x, y, t;
    int st;
    init_consts();
    if (a->k || b->k) {
        /* minimal handling of infinities */
        return nan_result(r);
    }
    load(&x, a); load(&y, b);
    if (!y.n) { dec_from_int(r, 1); return 0; }       /* x^0 = 1 */
    if (!x.n) {
        if (y.s) return DEC_EDIVZERO;
        dec_zero(r); return 0;
    }
    if (xn_is_int(&y)) {
        int32_t n;
        if (!xn_to_int32(&y, &n) && n > -100000000 && n < 100000000) {
            /* overflow estimate: |log10 x| * n */
            int32_t tx = xn_top(&x);
            uint32_t an = n < 0 ? (uint32_t)(-n) : (uint32_t)n;
            int neg = x.s && (an & 1);
            xn ax = x;
            ax.s = 0;
            if (tx != 0 && tx != -1) {
                /* magnitude ~ 10^(tx*n): bail out early when far off range */
                int32_t lim = 2000 / (tx < 0 ? -tx : tx);
                if ((int32_t)an > lim + 1) {
                    int big = (tx > 0) == (n > 0);
                    if (big) return dec_maxreal(r, neg);
                    dec_zero(r); return 0;
                }
            }
            if (an > 100000) {
                /* large exponents of numbers near 1: use exp/ln */
                goto general;
            }
            x_ipow(&t, &ax, an, WP + 4);
            if (n < 0) {
                st = xn_div(&t, &K_ONE, &t, WP);
                if (st) return st;
            }
            if (neg && t.n) t.s = 1;
            st = finish(r, &t);
            if (st == DEC_EUNDERFLOW) return 0;
            return st;
        }
    }
general:
    if (x.s) {
        /* negative base: real only for integer exponent (handled above) */
        if (xn_is_int(&y)) {
            int odd = xn_digit(&y, 0) & 1;
            xn ax = x; ax.s = 0;
            st = x_ln(&t, &ax, GP);
            if (st) return st;
            xn_mul(&t, &t, &y, GP);
            st = x_exp(&t, &t, WP);
            if (st == DEC_EUNDERFLOW) { dec_zero(r); return 0; }
            if (st == DEC_EOVERFLOW) return dec_maxreal(r, odd);
            if (odd && t.n) t.s = 1;
            return finish(r, &t);
        }
        return DEC_EDOMAIN;
    }
    st = x_ln(&t, &x, GP);
    if (st) return st;
    xn_mul(&t, &t, &y, GP);
    st = x_exp(&t, &t, WP);
    if (st == DEC_EUNDERFLOW) { dec_zero(r); return 0; }
    if (st == DEC_EOVERFLOW) return dec_maxreal(r, 0);
    st = finish(r, &t);
    if (st == DEC_EUNDERFLOW) return 0;
    return st;
}

/* n NTHROOT x: the real n-th root; for x < 0 only odd integer n. */
int dec_nthroot(real *r, const real *n, const real *a)
{
    xn x, nn, t;
    int st, neg = 0;
    init_consts();
    if (n->k || a->k) return nan_result(r);
    if (dec_is_zero(n)) return DEC_EDOMAIN;
    load(&x, a); load(&nn, n);
    if (!x.n) {
        if (nn.s) return DEC_EDIVZERO;
        dec_zero(r); return 0;
    }
    if (x.s) {
        int32_t iv;
        if (!xn_is_int(&nn) || xn_to_int32(&nn, &iv) || !(iv & 1)) return DEC_EDOMAIN;
        neg = 1;
        x.s = 0;
    }
    if (n->m[0] == 2000 && !n->m[1] && !n->m[2] && n->e == 0 && !n->s) {
        xn_sqrt(&t, &x, DEC_DIGITS);
        if (neg) t.s = 1;
        return finish(r, &t);
    }
    st = x_ln(&t, &x, GP);
    if (st) return st;
    st = xn_div(&t, &t, &nn, GP);
    if (st) return st;
    st = x_exp(&t, &t, WP);
    if (st == DEC_EUNDERFLOW) { dec_zero(r); return 0; }
    if (st) return st;
    if (neg && t.n) t.s = 1;
    return finish(r, &t);
}

/* ------------------------------------------------------------ trig */

/* sin and cos of r, |r| <= pi/4 (radians), at precision P */
static void x_sincos(xn *s, xn *c, const xn *r, int P)
{
    xn r2, term, sum;
    int k, Q = P + 4;
    if (!r->n) { if (s) xn_zero(s); if (c) *c = K_ONE; return; }
    xn_mul(&r2, r, r, Q);
    if (s) {
        sum = *r; term = *r;
        for (k = 2; k < 200; k += 2) {
            xn_mul(&term, &term, &r2, Q);
            xn_div_int(&term, &term, k * (k + 1), Q);
            if (!term.n || xn_top(&term) < xn_top(&sum) - Q - 1) break;
            term.s ^= 1;
            xn_add(&sum, &sum, &term, Q);
        }
        *s = sum; xn_round(s, P);
    }
    if (c) {
        sum = K_ONE; term = K_ONE;
        for (k = 1; k < 200; k += 2) {
            xn_mul(&term, &term, &r2, Q);
            xn_div_int(&term, &term, k * (k + 1), Q);
            if (!term.n || xn_top(&term) < -Q - 1) break;
            term.s ^= 1;
            xn_add(&sum, &sum, &term, Q);
        }
        *c = sum; xn_round(c, P);
    }
}

/* Reduce an angle to an octant: returns quadrant q (0..3) and the remainder
 * in radians, |rem| <= pi/4. */
static void reduce_angle(xn *rem, int *q, const xn *x, int ang)
{
    xn k, t;
    int32_t qi = 0;
    init_consts();
    if (ang == ANG_DEG || ang == ANG_GRAD) {
        /* exact decimal reduction: quarter = 90 or 100 */
        xn quarter, full, r;
        xn_from_int(&quarter, ang == ANG_DEG ? 90 : 100);
        xn_from_int(&full, ang == ANG_DEG ? 360 : 400);
        /* r = x mod full, exactly */
        {
            real rx, rf, rr;
            xn_to_real(&rx, x);   /* x came from a real: exact */
            xn_to_real(&rf, &full);
            dec_mod(&rr, &rx, &rf);
            xn_from_real(&r, &rr);
        }
        /* q = nearest quarter */
        xn_div(&k, &r, &quarter, 20);
        xn_nint(&k, &k);
        xn_to_int32(&k, &qi);
        xn_mul(&t, &k, &quarter, XN_MAXP);
        xn_sub(&r, &r, &t, XN_MAXP);       /* exact, |r| <= quarter/2 */
        /* to radians */
        if (r.n) {
            xn_mul(&r, &r, &K_PI, GP);
            xn_div_int(&r, &r, ang == ANG_DEG ? 180 : 200, GP);
        }
        *rem = r;
        *q = (int)(((qi % 4) + 4) % 4);
        return;
    }
    /* radians: k = nearest integer of x / (pi/2) */
    xn_div(&k, x, &K_PI2, 30);
    xn_nint(&k, &k);
    if (!k.n) { *rem = *x; *q = 0; return; }
    reduce_by(rem, x, &k, &K_PI2, GP);
    {
        /* quadrant = k mod 4: from the last two digits of k */
        int d0 = xn_digit(&k, 0), d1 = xn_digit(&k, 1);
        int m = (d1 * 10 + d0) % 4;
        if (k.s) m = (4 - m) % 4;
        *q = m;
    }
}

static int trig_load(xn *x, const real *a)
{
    if (a->k) return DEC_EDOMAIN;
    xn_from_real(x, a);
    return 0;
}

int dec_sin(real *r, const real *a, int ang)
{
    xn x, rem, s, c;
    int q;
    if (trig_load(&x, a)) return nan_result(r);
    reduce_angle(&rem, &q, &x, ang);
    x_sincos((q & 1) ? NULL : &s, (q & 1) ? &c : NULL, &rem, WP);
    switch (q) {
    case 0: return finish(r, &s);
    case 1: return finish(r, &c);
    case 2: if (s.n) s.s ^= 1; return finish(r, &s);
    default: if (c.n) c.s ^= 1; return finish(r, &c);
    }
}

int dec_cos(real *r, const real *a, int ang)
{
    xn x, rem, s, c;
    int q;
    if (trig_load(&x, a)) return nan_result(r);
    reduce_angle(&rem, &q, &x, ang);
    x_sincos((q & 1) ? &s : NULL, (q & 1) ? NULL : &c, &rem, WP);
    switch (q) {
    case 0: return finish(r, &c);
    case 1: if (s.n) s.s ^= 1; return finish(r, &s);
    case 2: if (c.n) c.s ^= 1; return finish(r, &c);
    default: return finish(r, &s);
    }
}

int dec_tan(real *r, const real *a, int ang)
{
    xn x, rem, s, c, t;
    int q, st;
    if (trig_load(&x, a)) return nan_result(r);
    reduce_angle(&rem, &q, &x, ang);
    x_sincos(&s, &c, &rem, WP + 2);
    if (q & 1) {
        /* tan = -cos/sin */
        if (!s.n) return DEC_EDOMAIN;     /* infinite result */
        st = xn_div(&t, &c, &s, WP);
        if (st) return st;
        if (t.n) t.s ^= 1;
    } else {
        st = xn_div(&t, &s, &c, WP);
        if (st) return st;
    }
    return finish(r, &t);
}

/* atan of x in radians at precision P */
static void x_atan(xn *r, const xn *x, int P)
{
    xn y, t, y2, term, sum;
    int inv = 0, k, Q = P + 4, sg = x->s;
    init_consts();
    if (!x->n) { xn_zero(r); return; }
    y = *x; y.s = 0;
    if (xn_cmpmag(&y, &K_ONE) > 0) {
        inv = 1;
        xn_div(&y, &K_ONE, &y, Q);
    }
    /* two angle halvings: y <- y / (1 + sqrt(1 + y^2)) */
    for (k = 0; k < 2; k++) {
        xn_mul(&t, &y, &y, Q);
        xn_add(&t, &t, &K_ONE, Q);
        xn_sqrt(&t, &t, Q);
        xn_add(&t, &t, &K_ONE, Q);
        xn_div(&y, &y, &t, Q);
    }
    xn_mul(&y2, &y, &y, Q);
    sum = y; term = y;
    for (k = 3; k < 600; k += 2) {
        xn tk;
        xn_mul(&term, &term, &y2, Q);
        xn_div_int(&tk, &term, k, Q);
        if (!tk.n || xn_top(&tk) < xn_top(&sum) - Q - 1) break;
        if ((k >> 1) & 1) tk.s ^= 1;
        xn_add(&sum, &sum, &tk, Q);
    }
    xn_mul_int(&sum, &sum, 4, Q);
    if (inv) xn_sub(&sum, &K_PI2, &sum, Q);
    if (sum.n) sum.s = (uint8_t)sg;
    *r = sum;
    xn_round(r, P);
}

static int to_angle(real *r, xn *rad, int ang)
{
    init_consts();
    if (ang == ANG_DEG) {
        xn_mul(rad, rad, &K_180, GP);
        xn_div(rad, rad, &K_PI, WP);
    } else if (ang == ANG_GRAD) {
        xn_mul(rad, rad, &K_200, GP);
        xn_div(rad, rad, &K_PI, WP);
    }
    return finish(r, rad);
}

int dec_atan(real *r, const real *a, int ang)
{
    xn x, t;
    if (a->k == DEC_INF) {
        init_consts();
        t = K_PI2; t.s = a->s;
        return to_angle(r, &t, ang);
    }
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    x_atan(&t, &x, WP + 2);
    return to_angle(r, &t, ang);
}

/* asin in radians for |x| <= 1 */
static void x_asin(xn *r, const xn *x, int P)
{
    xn t, u;
    init_consts();
    if (xn_cmpmag(x, &K_ONE) == 0) { *r = K_PI2; r->s = x->s; xn_round(r, P); return; }
    /* asin x = atan(x / sqrt((1-x)(1+x))) */
    xn_sub(&t, &K_ONE, x, XN_MAXP);
    xn_add(&u, &K_ONE, x, XN_MAXP);
    xn_mul(&t, &t, &u, P + 4);
    xn_sqrt(&t, &t, P + 4);
    xn_div(&t, x, &t, P + 4);
    x_atan(r, &t, P);
}

int dec_asin(real *r, const real *a, int ang)
{
    xn x, t;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (xn_cmpmag(&x, &K_ONE) > 0) return DEC_EDOMAIN;
    x_asin(&t, &x, WP + 2);
    return to_angle(r, &t, ang);
}

int dec_acos(real *r, const real *a, int ang)
{
    xn x, t, h;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (xn_cmpmag(&x, &K_ONE) > 0) return DEC_EDOMAIN;
    if (xn_cmpmag(&x, &K_HALF) <= 0) {
        x_asin(&t, &x, WP + 4);
        xn_sub(&t, &K_PI2, &t, WP + 2);
    } else {
        /* acos x = 2 asin(sqrt((1-|x|)/2)), and pi - that for x < 0 */
        h = x; h.s = 0;
        xn_sub(&t, &K_ONE, &h, XN_MAXP);
        xn_div_int(&t, &t, 2, XN_MAXP);
        xn_sqrt(&t, &t, WP + 4);
        x_asin(&t, &t, WP + 4);
        xn_mul_int(&t, &t, 2, WP + 4);
        if (x.s) xn_sub(&t, &K_PI, &t, WP + 2);
    }
    return to_angle(r, &t, ang);
}

int dec_atan2(real *r, const real *y, const real *x, int ang)
{
    /* angle of the point (x, y), in (-pi, pi] */
    xn X, Y, t;
    init_consts();
    if (y->k || x->k) return nan_result(r);
    xn_from_real(&X, x); xn_from_real(&Y, y);
    if (!X.n) {
        if (!Y.n) { dec_zero(r); return 0; }
        t = K_PI2; t.s = Y.s;
        return to_angle(r, &t, ang);
    }
    xn_div(&t, &Y, &X, WP + 4);
    x_atan(&t, &t, WP + 4);
    if (X.s) {
        if (Y.s) xn_sub(&t, &t, &K_PI, WP + 2);
        else xn_add(&t, &t, &K_PI, WP + 2);
    }
    return to_angle(r, &t, ang);
}

int dec_angle_to_rad(real *r, const real *a, int ang)
{
    xn x;
    init_consts();
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    if (ang == ANG_DEG) { xn_mul(&x, &x, &K_PI, GP); xn_div_int(&x, &x, 180, WP); }
    else if (ang == ANG_GRAD) { xn_mul(&x, &x, &K_PI, GP); xn_div_int(&x, &x, 200, WP); }
    return finish(r, &x);
}

int dec_angle_from_rad(real *r, const real *a, int ang)
{
    xn x;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    return to_angle(r, &x, ang);
}

/* ------------------------------------------------------- hyperbolic */

int dec_sinh(real *r, const real *a)
{
    xn x, u, t;
    int st;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (!x.n) { dec_zero(r); return 0; }
    /* sinh = (u + u/(u+1)) / 2 with u = expm1(x) */
    st = x_expm1(&u, &x, WP + 4);
    if (st == DEC_EOVERFLOW) return dec_maxreal(r, 0);
    if (st) return st;
    if (x.s && xn_top(&x) >= 4) return dec_maxreal(r, 1);
    xn_add(&t, &u, &K_ONE, WP + 4);
    xn_div(&t, &u, &t, WP + 4);
    xn_add(&t, &u, &t, WP + 2);
    xn_div_int(&t, &t, 2, WP);
    return finish(r, &t);
}

int dec_cosh(real *r, const real *a)
{
    xn x, e, t;
    int st;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    x.s = 0;
    st = x_exp(&e, &x, WP + 2);
    if (st == DEC_EOVERFLOW) return dec_maxreal(r, 0);
    if (st) return st;
    xn_div(&t, &K_ONE, &e, WP + 2);
    xn_add(&t, &e, &t, WP + 2);
    xn_div_int(&t, &t, 2, WP);
    return finish(r, &t);
}

int dec_tanh(real *r, const real *a)
{
    xn x, u, t;
    int st;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (!x.n) { dec_zero(r); return 0; }
    if (xn_top(&x) >= 2) { dec_from_int(r, x.s ? -1 : 1); return 0; }
    /* tanh = u/(u+2), u = expm1(2x) */
    xn_mul_int(&t, &x, 2, XN_MAXP);
    st = x_expm1(&u, &t, WP + 4);
    if (st) return st;
    xn_add(&t, &u, &K_TWO, WP + 4);
    xn_div(&t, &u, &t, WP);
    return finish(r, &t);
}

int dec_asinh(real *r, const real *a)
{
    xn x, t, s;
    int sg;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (!x.n) { dec_zero(r); return 0; }
    sg = x.s; x.s = 0;
    /* asinh x = ln1p(x + x^2/(1 + sqrt(1+x^2))) */
    xn_mul(&t, &x, &x, WP + 6);
    xn_add(&s, &t, &K_ONE, WP + 6);
    xn_sqrt(&s, &s, WP + 6);
    xn_add(&s, &s, &K_ONE, WP + 6);
    xn_div(&t, &t, &s, WP + 6);
    xn_add(&t, &t, &x, WP + 6);
    {
        xn lim;
        xn_from_str(&lim, "0.5");
        if (xn_cmp(&t, &lim) <= 0) x_ln_near1(&s, &t, WP);
        else { xn_add(&t, &t, &K_ONE, WP + 6); x_ln(&s, &t, WP); }
    }
    if (s.n) s.s = (uint8_t)sg;
    return finish(r, &s);
}

int dec_acosh(real *r, const real *a)
{
    xn x, t, s;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (xn_cmp(&x, &K_ONE) < 0) return DEC_EDOMAIN;
    /* acosh x = ln1p(y + sqrt(y(y+2))), y = x - 1 */
    xn_sub(&t, &x, &K_ONE, XN_MAXP);
    xn_add(&s, &t, &K_TWO, XN_MAXP);
    xn_mul(&s, &s, &t, WP + 6);
    xn_sqrt(&s, &s, WP + 6);
    xn_add(&t, &t, &s, WP + 6);
    {
        xn lim;
        xn_from_str(&lim, "0.5");
        if (xn_cmp(&t, &lim) <= 0) x_ln_near1(&s, &t, WP);
        else { xn_add(&t, &t, &K_ONE, WP + 6); x_ln(&s, &t, WP); }
    }
    return finish(r, &s);
}

int dec_atanh(real *r, const real *a)
{
    xn x, t, s;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (xn_cmpmag(&x, &K_ONE) >= 0) return DEC_EDOMAIN;
    /* atanh x = ln1p(2x/(1-x)) / 2 */
    xn_sub(&s, &K_ONE, &x, XN_MAXP);
    xn_mul_int(&t, &x, 2, XN_MAXP);
    xn_div(&t, &t, &s, WP + 6);
    {
        xn lo, hi;
        xn_from_str(&lo, "-0.3"); xn_from_str(&hi, "0.5");
        if (xn_cmp(&t, &lo) >= 0 && xn_cmp(&t, &hi) <= 0) x_ln_near1(&s, &t, WP + 2);
        else { xn_add(&t, &t, &K_ONE, WP + 6); x_ln(&s, &t, WP + 2); }
    }
    xn_div_int(&s, &s, 2, WP);
    return finish(r, &s);
}

/* ------------------------------------------------------------ gamma */

/* ln Gamma(z) for z >= 30 by Stirling's series */
static void x_lgamma_big(xn *r, const xn *z, int P)
{
    static const int32_t bn[10] = { 1, -1, 1, -1, 5, -691, 7, -3617, 43867, -174611 };
    static const int32_t bd[10] = { 6, 30, 42, 30, 66, 2730, 6, 510, 798, 330 };
    xn t, lnz, sum, zp, z2, term, h;
    int k, Q = P + 4;
    init_consts();
    x_ln(&lnz, z, Q);
    xn_sub(&h, z, &K_HALF, Q);
    xn_mul(&sum, &h, &lnz, Q);
    xn_sub(&sum, &sum, z, Q);
    /* + ln(2 pi)/2 */
    xn_mul_int(&t, &K_PI, 2, Q);
    x_ln(&t, &t, Q);
    xn_div_int(&t, &t, 2, Q);
    xn_add(&sum, &sum, &t, Q);
    zp = *z;
    xn_mul(&z2, z, z, Q);
    for (k = 1; k <= 10; k++) {
        xn num;
        xn_from_int(&num, bn[k - 1]);
        xn_div_int(&term, &num, bd[k - 1] * (2 * k) * (2 * k - 1), Q);
        xn_div(&term, &term, &zp, Q);
        xn_add(&sum, &sum, &term, Q);
        xn_mul(&zp, &zp, &z2, Q);
    }
    *r = sum;
    xn_round(r, P);
}

/* Gamma(x) for x not a non-positive integer */
static int x_gamma(xn *r, const xn *x, int P)
{
    xn z, prod, t, lg;
    int st, Q = P + 4;
    init_consts();
    if (x->s || xn_cmp(x, &K_HALF) < 0) {
        /* reflection: Gamma(x) = pi / (sin(pi x) Gamma(1-x)) */
        xn s, g, omx;
        real rx, rs;
        if (xn_is_int(x)) return DEC_EDOMAIN;
        xn_sub(&omx, &K_ONE, x, XN_MAXP);
        st = x_gamma(&g, &omx, Q);
        if (st) return st;
        /* sin(pi x): reduce x mod 2 exactly via degrees: pi x rad = 180x deg */
        xn_mul_int(&t, x, 180, XN_MAXP);
        xn_to_real(&rx, &t);
        {
            xn tt; xn_from_real(&tt, &rx);
            if (xn_cmp(&tt, &t) == 0) {
                dec_sin(&rs, &rx, ANG_DEG);
                xn_from_real(&s, &rs);
                /* only 12 digits; good enough unless near a pole */
            } else {
                xn rem; int q;
                xn_mul(&t, x, &K_PI, GP);
                reduce_angle(&rem, &q, &t, ANG_RAD);
                {
                    xn ss, cc;
                    x_sincos(&ss, &cc, &rem, Q);
                    s = (q == 0) ? ss : (q == 1) ? cc : (q == 2) ? ss : cc;
                    if (q == 2 || q == 3) s.s ^= 1;
                }
            }
        }
        xn_mul(&t, &s, &g, Q);
        if (!t.n) return DEC_EDOMAIN;
        return xn_div(r, &K_PI, &t, P);
    }
    /* shift up to z >= 30 */
    z = *x;
    prod = K_ONE;
    {
        xn lim;
        xn_from_int(&lim, 30);
        while (xn_cmp(&z, &lim) < 0) {
            xn_mul(&prod, &prod, &z, Q);
            xn_add(&z, &z, &K_ONE, XN_MAXP);
        }
    }
    x_lgamma_big(&lg, &z, Q);
    st = x_exp(&t, &lg, Q);
    if (st) return st;
    return xn_div(r, &t, &prod, P);
}

int dec_gamma(real *r, const real *a)
{
    xn x, t;
    int st;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (xn_is_int(&x)) {
        int32_t n;
        if (x.s || !x.n) return DEC_EDOMAIN;
        if (!xn_to_int32(&x, &n) && n <= 300) {
            int i;
            t = K_ONE;
            for (i = 2; i < n; i++) xn_mul_int(&t, &t, i, WP + 4);
            return finish(r, &t);
        }
        if (n > 300) return dec_maxreal(r, 0);
    }
    st = x_gamma(&t, &x, WP);
    if (st) return st;
    return finish(r, &t);
}

int dec_lgamma(real *r, const real *a)
{
    xn x, t;
    if (a->k) return nan_result(r);
    xn_from_real(&x, a);
    init_consts();
    if (x.s || !x.n) return DEC_EDOMAIN;
    {
        xn z = x, prod = K_ONE, lim, lp;
        xn_from_int(&lim, 30);
        while (xn_cmp(&z, &lim) < 0) {
            xn_mul(&prod, &prod, &z, WP + 4);
            xn_add(&z, &z, &K_ONE, XN_MAXP);
        }
        x_lgamma_big(&t, &z, WP + 4);
        x_ln(&lp, &prod, WP + 4);
        xn_sub(&t, &t, &lp, WP);
    }
    return finish(r, &t);
}

void dec_pi(real *r)
{
    init_consts();
    xn_to_real(r, &K_PI);
}

void dec_e(real *r)
{
    xn one, e;
    init_consts();
    xn_from_int(&one, 1);
    x_exp(&e, &one, WP);
    xn_to_real(r, &e);
}
