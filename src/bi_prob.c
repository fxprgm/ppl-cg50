/* bi_prob.c -- probability distributions and special functions.
 *
 * Everything is computed in the xn engine with QP (34) significant digits
 * and rounded once to 12 digits at the end.  The Prime answers some of the
 * special functions symbolically for exact arguments (Zeta(2) -> pi^2/6,
 * erf(1) -> erf(1)); here they always answer a number.
 *
 * Distribution conventions (HP Prime User Guide, chapter 23):
 *   NORMALD([mu,sigma,]x)       STUDENT(n,x)   CHISQUARE(n,x)   FISHER(n,d,x)
 *   BINOMIAL(n,p,k)             POISSON(mu,k)  GEOMETRIC(p,x) = p(1-p)^(x-1)
 *   _CDF: lower tail; an extra bound gives the probability between the two
 *   (binomial and Poisson: k1..k2 inclusive; continuous and geometric:
 *   CDF(x2)-CDF(x1)).  _ICDF of a discrete law: the smallest k whose CDF
 *   reaches p.
 */
#include "bi.h"
#include "decx.h"
#include <string.h>
#include <stdlib.h>

#define QP 34

static const char S_PI[]   = "3.14159265358979323846264338327950288419716939937510582";
static const char S_LN2[]  = "0.693147180559945309417232121458176568075500134360255254";
static const char S_LN10[] = "2.30258509299404568401799145468436420760110148862877298";

static int k_ok;
static xn K_PI, K_LN2, K_LN10, K_ONE, K_TWO, K_HALF, K_SQRT2PI, K_SQRTPI;

/* Bernoulli numbers B2..B20 as fractions */
static const int32_t BN[10] = { 1, -1, 1, -1, 5, -691, 7, -3617, 43867, -174611 };
static const int32_t BD[10] = { 6, 30, 42, 30, 66, 2730, 6, 510, 798, 330 };

static void kinit(void)
{
    xn t;
    if (k_ok) return;
    xn_from_str(&K_PI, S_PI);
    xn_from_str(&K_LN2, S_LN2);
    xn_from_str(&K_LN10, S_LN10);
    xn_from_int(&K_ONE, 1);
    xn_from_int(&K_TWO, 2);
    xn_from_str(&K_HALF, "0.5");
    xn_mul_int(&t, &K_PI, 2, XN_MAXP);
    xn_sqrt(&K_SQRT2PI, &t, QP + 4);
    xn_sqrt(&K_SQRTPI, &K_PI, QP + 4);
    k_ok = 1;
}

/* ------------------------------------------------------------ xn helpers */

static void xadd(xn *r, const xn *a, const xn *b) { xn_add(r, a, b, QP); }
static void xsub(xn *r, const xn *a, const xn *b) { xn_sub(r, a, b, QP); }
static void xmul(xn *r, const xn *a, const xn *b) { xn_mul(r, a, b, QP); }
static void xdiv(xn *r, const xn *a, const xn *b) { xn_div(r, a, b, QP); }

static void xnint(xn *r, const xn *x)
{
    xn t = *x;
    int s = t.s;
    t.s = 0;
    xn_add(&t, &t, &K_HALF, XN_MAXP);
    xn_trunc_int(&t);
    if (t.n) t.s = (uint8_t)s;
    *r = t;
}

/* e^x - 1 for |x| <= ~1.2: x/1024, series, then u <- u(u+2) ten times */
static void p_expm1_small(xn *r, const xn *x)
{
    xn y, term, sum, t;
    int k, Q = QP + 6;
    xn_from_str(&t, "0.0009765625");
    xn_mul(&y, x, &t, XN_MAXP);
    xn_round(&y, Q);
    sum = y; term = y;
    for (k = 2; k < 80; k++) {
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
    xn_round(r, QP + 2);
}

/* e^x with no range limit (the xn exponent is 32-bit) */
static void p_exp(xn *r, const xn *x)
{
    xn k, rr, t, u;
    int32_t ki = 0;
    kinit();
    if (!x->n) { *r = K_ONE; return; }
    if (xn_top(x) > 8) {   /* |x| > 1e9: far beyond any 12-digit result */
        if (x->s) { xn_zero(r); return; }
        *r = K_ONE; r->e = 1000000000; return;
    }
    xn_div(&k, x, &K_LN10, 20);
    xnint(&k, &k);
    xn_to_int32(&k, &ki);
    xn_mul(&t, &k, &K_LN10, XN_MAXP);
    xn_sub(&rr, x, &t, QP + 10);
    p_expm1_small(&u, &rr);
    xn_add(r, &u, &K_ONE, QP);
    if (r->n) r->e += ki;
}

/* ln g for g near 1, from gm1 = g - 1 (exact): 2 atanh(gm1/(gm1+2)) */
static void p_ln_near1(xn *r, const xn *gm1)
{
    xn z, z2, den, term, sum, t;
    int k, Q = QP + 4;
    xn_add(&den, gm1, &K_TWO, XN_MAXP);
    xn_div(&z, gm1, &den, Q);
    if (!z.n) { xn_zero(r); return; }
    xn_mul(&z2, &z, &z, Q);
    sum = z; term = z;
    for (k = 3; k < 600; k += 2) {
        xn_mul(&term, &term, &z2, Q);
        xn_div_int(&t, &term, k, Q);
        if (!t.n || xn_top(&t) < xn_top(&sum) - Q - 1) break;
        xn_add(&sum, &sum, &t, Q);
    }
    xn_mul_int(r, &sum, 2, QP + 2);
}

/* ln x, x > 0 */
static void p_ln(xn *r, const xn *x)
{
    xn f, gm1, t, acc, lim;
    int32_t E;
    int j = 0;
    kinit();
    xn_sub(&gm1, x, &K_ONE, XN_MAXP);
    if (!gm1.n) { xn_zero(r); return; }
    {
        xn lo, hi;
        xn_from_str(&lo, "0.7"); xn_from_str(&hi, "1.5");
        if (xn_cmp(x, &lo) >= 0 && xn_cmp(x, &hi) <= 0) { p_ln_near1(r, &gm1); return; }
    }
    f = *x;
    E = xn_top(&f);
    f.e -= E;
    xn_from_str(&lim, "1.5");
    while (xn_cmp(&f, &lim) >= 0) { xn_div_int(&f, &f, 2, XN_MAXP); j++; }
    xn_sub(&gm1, &f, &K_ONE, XN_MAXP);
    p_ln_near1(&acc, &gm1);
    if (j) { xn_mul_int(&t, &K_LN2, j, XN_MAXP); xn_add(&acc, &acc, &t, QP + 4); }
    if (E) {
        xn ee;
        xn_from_int(&ee, E);
        xn_mul(&t, &ee, &K_LN10, XN_MAXP);
        xn_add(&acc, &acc, &t, QP + 4);
    }
    *r = acc;
    xn_round(r, QP + 2);
}

/* x^y = e^(y ln x), x > 0 */
static void p_pow(xn *r, const xn *x, const xn *y)
{
    xn t;
    if (!y->n) { *r = K_ONE; return; }
    if (!x->n) { xn_zero(r); return; }
    p_ln(&t, x);
    xn_mul(&t, &t, y, QP + 4);
    p_exp(r, &t);
}

/* x^n for an integer n >= 0, exact up to QP digits */
static void p_ipow(xn *r, const xn *x, uint32_t n)
{
    xn b = *x, acc = K_ONE;
    while (n) {
        if (n & 1) xn_mul(&acc, &acc, &b, QP + 2);
        n >>= 1;
        if (n) xn_mul(&b, &b, &b, QP + 2);
    }
    *r = acc;
}

/* ln Gamma(z), z > 0: shift up to z >= 40, then Stirling's series */
static void p_lgamma(xn *r, const xn *z0)
{
    xn z = *z0, prod = K_ONE, lim, t, lnz, sum, zp, z2, term, h, lp;
    int k, Q = QP + 4, shifted = 0;
    kinit();
    xn_from_int(&lim, 40);
    while (xn_cmp(&z, &lim) < 0) {
        xn_mul(&prod, &prod, &z, Q);
        xn_add(&z, &z, &K_ONE, XN_MAXP);
        shifted = 1;
    }
    p_ln(&lnz, &z);
    xn_sub(&h, &z, &K_HALF, Q);
    xn_mul(&sum, &h, &lnz, Q);
    xn_sub(&sum, &sum, &z, Q);
    p_ln(&t, &K_SQRT2PI);
    xn_add(&sum, &sum, &t, Q);
    zp = z;
    xn_mul(&z2, &z, &z, Q);
    for (k = 1; k <= 10; k++) {
        xn num;
        xn_from_int(&num, BN[k - 1]);
        xn_div_int(&term, &num, BD[k - 1] * (2 * k) * (2 * k - 1), Q);
        xn_div(&term, &term, &zp, Q);
        xn_add(&sum, &sum, &term, Q);
        xn_mul(&zp, &zp, &z2, Q);
    }
    if (shifted) { p_ln(&lp, &prod); xn_sub(&sum, &sum, &lp, Q); }
    *r = sum;
    xn_round(r, QP + 2);
}

/* ------------------------------------------------------------ special */

/* erf(x) for x >= 0 by the positive series
 *   erf x = 2/sqrt(pi) e^(-x^2) sum 2^n x^(2n+1) / (1*3*...*(2n+1)) */
static void p_erf_series(xn *r, const xn *x)
{
    xn x2, term, sum, t;
    int n, Q = QP + 4;
    kinit();
    if (!x->n) { xn_zero(r); return; }
    xn_mul(&x2, x, x, Q);
    xn_mul_int(&t, &x2, 2, Q);
    sum = *x; term = *x;
    for (n = 1; n < 5000; n++) {
        xn_mul(&term, &term, &t, Q);
        xn_div_int(&term, &term, 2 * n + 1, Q);
        if (xn_top(&term) < xn_top(&sum) - Q - 1) break;
        xn_add(&sum, &sum, &term, Q);
    }
    x2.s ^= 1;
    p_exp(&t, &x2);
    xn_mul(&sum, &sum, &t, Q);
    xn_mul_int(&sum, &sum, 2, Q);
    xn_div(r, &sum, &K_SQRTPI, QP + 2);
}

/* erfc(x) for x >= 3 by the continued fraction
 *   erfc x = e^(-x^2)/sqrt(pi) / (x + (1/2)/(x + 1/(x + (3/2)/(x + ...)))) */
static void p_erfc_cf(xn *r, const xn *x)
{
    xn t, k2, x2;
    int k, Q = QP + 4;
    t = *x;
    for (k = 400; k >= 1; k--) {
        xn_from_int(&k2, k);
        xn_div_int(&k2, &k2, 2, Q);
        xn_div(&k2, &k2, &t, Q);
        xn_add(&t, x, &k2, Q);
    }
    xn_mul(&x2, x, x, Q);
    x2.s ^= 1;
    p_exp(&x2, &x2);
    xn_div(&x2, &x2, &K_SQRTPI, Q);
    xn_div(r, &x2, &t, QP + 2);
}

/* erfc for any x */
static void p_erfc(xn *r, const xn *x)
{
    xn ax = *x, t, three;
    kinit();
    ax.s = 0;
    xn_from_int(&three, 3);
    if (xn_cmp(&ax, &three) >= 0) p_erfc_cf(&t, &ax);
    else { p_erf_series(&t, &ax); xn_sub(&t, &K_ONE, &t, QP + 2); }
    if (x->s && x->n) xn_sub(&t, &K_TWO, &t, QP + 2);
    *r = t;
}

static void p_erf(xn *r, const xn *x)
{
    xn ax = *x, three;
    kinit();
    ax.s = 0;
    xn_from_int(&three, 3);
    if (xn_cmp(&ax, &three) >= 0) { xn t; p_erfc_cf(&t, &ax); xn_sub(r, &K_ONE, &t, QP + 2); }
    else p_erf_series(r, &ax);
    if (x->s && r->n) r->s = 1;
}

/* regularized lower incomplete gamma P(a, x), a > 0, x >= 0 */
static void p_gammp(xn *r, const xn *a, const xn *x)
{
    xn pre, t, lg, ap1;
    int Q = QP + 4;
    kinit();
    if (!x->n || x->s) { xn_zero(r); return; }
    /* prefactor e^(a ln x - x - lnGamma(a)) */
    p_ln(&t, x);
    xn_mul(&pre, &t, a, Q);
    xn_sub(&pre, &pre, x, Q);
    p_lgamma(&lg, a);
    xn_sub(&pre, &pre, &lg, Q);
    p_exp(&pre, &pre);
    xn_add(&ap1, a, &K_ONE, Q);
    if (xn_cmp(x, &ap1) < 0) {
        /* series: sum x^n / (a (a+1) ... (a+n)) */
        xn sum, term, ak;
        int n;
        xn_div(&term, &K_ONE, a, Q);
        sum = term; ak = *a;
        for (n = 1; n < 10000; n++) {
            xn_add(&ak, &ak, &K_ONE, Q);
            xn_mul(&term, &term, x, Q);
            xn_div(&term, &term, &ak, Q);
            if (xn_top(&term) < xn_top(&sum) - Q - 1) break;
            xn_add(&sum, &sum, &term, Q);
        }
        xn_mul(r, &sum, &pre, QP + 2);
    } else {
        /* Q(a,x) by Lentz's continued fraction; P = 1 - Q */
        xn b, c, d, h, an, del, tiny;
        int i;
        xn_from_str(&tiny, "1E-200");
        xn_add(&b, x, &K_ONE, Q); xn_sub(&b, &b, a, Q);
        xn_div(&c, &K_ONE, &tiny, Q);
        xn_div(&d, &K_ONE, &b, Q);
        h = d;
        for (i = 1; i < 10000; i++) {
            xn ii, aim;
            xn_from_int(&ii, i);
            xn_sub(&aim, a, &ii, Q);
            xn_mul(&an, &ii, &aim, Q); an.s ^= an.n ? 1 : 0;   /* -i (i - a) */
            xn_add(&b, &b, &K_TWO, Q);
            xn_mul(&d, &an, &d, Q); xn_add(&d, &d, &b, Q);
            if (!d.n) d = tiny;
            xn_div(&c, &an, &c, Q); xn_add(&c, &b, &c, Q);
            if (!c.n) c = tiny;
            xn_div(&d, &K_ONE, &d, Q);
            xn_mul(&del, &d, &c, Q);
            xn_mul(&h, &h, &del, Q);
            xn_sub(&t, &del, &K_ONE, Q);
            if (!t.n || xn_top(&t) < -Q) break;
        }
        xn_mul(&t, &h, &pre, Q);
        xn_sub(r, &K_ONE, &t, QP + 2);
    }
}

/* continued fraction of the incomplete beta (Lentz) */
static void p_betacf(xn *r, const xn *a, const xn *b, const xn *x)
{
    xn qab, qap, qam, c, d, h, aa, del, tiny, t, mm, m2;
    int m, Q = QP + 4;
    xn_from_str(&tiny, "1E-200");
    xn_add(&qab, a, b, Q);
    xn_add(&qap, a, &K_ONE, Q);
    xn_sub(&qam, a, &K_ONE, Q);
    c = K_ONE;
    xn_mul(&t, &qab, x, Q); xn_div(&t, &t, &qap, Q);
    xn_sub(&d, &K_ONE, &t, Q);
    if (!d.n) d = tiny;
    xn_div(&d, &K_ONE, &d, Q);
    h = d;
    for (m = 1; m < 10000; m++) {
        xn_from_int(&mm, m);
        xn_from_int(&m2, 2 * m);
        /* even step: m (b - m) x / ((qam + 2m)(a + 2m)) */
        xn_sub(&t, b, &mm, Q); xn_mul(&t, &t, &mm, Q); xn_mul(&aa, &t, x, Q);
        xn_add(&t, &qam, &m2, Q); xn_div(&aa, &aa, &t, Q);
        xn_add(&t, a, &m2, Q); xn_div(&aa, &aa, &t, Q);
        xn_mul(&d, &aa, &d, Q); xn_add(&d, &d, &K_ONE, Q); if (!d.n) d = tiny;
        xn_div(&c, &aa, &c, Q); xn_add(&c, &c, &K_ONE, Q); if (!c.n) c = tiny;
        xn_div(&d, &K_ONE, &d, Q);
        xn_mul(&h, &h, &d, Q); xn_mul(&h, &h, &c, Q);
        /* odd step: -(a + m)(qab + m) x / ((a + 2m)(qap + 2m)) */
        xn_add(&t, a, &mm, Q); xn_add(&aa, &qab, &mm, Q); xn_mul(&aa, &aa, &t, Q); xn_mul(&aa, &aa, x, Q);
        xn_add(&t, a, &m2, Q); xn_div(&aa, &aa, &t, Q);
        xn_add(&t, &qap, &m2, Q); xn_div(&aa, &aa, &t, Q);
        if (aa.n) aa.s ^= 1;
        xn_mul(&d, &aa, &d, Q); xn_add(&d, &d, &K_ONE, Q); if (!d.n) d = tiny;
        xn_div(&c, &aa, &c, Q); xn_add(&c, &c, &K_ONE, Q); if (!c.n) c = tiny;
        xn_div(&d, &K_ONE, &d, Q);
        xn_mul(&del, &d, &c, Q);
        xn_mul(&h, &h, &del, Q);
        xn_sub(&t, &del, &K_ONE, Q);
        if (!t.n || xn_top(&t) < -Q) break;
    }
    *r = h;
}

/* regularized incomplete beta I_x(a, b), 0 <= x <= 1 */
static void p_betai(xn *r, const xn *a, const xn *b, const xn *x)
{
    xn bt, t, u, lg, omx, lim;
    int Q = QP + 4;
    kinit();
    if (!x->n || x->s) { xn_zero(r); return; }
    xn_sub(&omx, &K_ONE, x, XN_MAXP);
    if (!omx.n || omx.s) { *r = K_ONE; return; }
    /* bt = e^(lnG(a+b) - lnG(a) - lnG(b) + a ln x + b ln(1-x)) */
    xn_add(&t, a, b, Q); p_lgamma(&bt, &t);
    p_lgamma(&lg, a); xn_sub(&bt, &bt, &lg, Q);
    p_lgamma(&lg, b); xn_sub(&bt, &bt, &lg, Q);
    p_ln(&t, x); xn_mul(&t, &t, a, Q); xn_add(&bt, &bt, &t, Q);
    p_ln(&t, &omx); xn_mul(&t, &t, b, Q); xn_add(&bt, &bt, &t, Q);
    p_exp(&bt, &bt);
    /* use the fraction directly when x < (a+1)/(a+b+2) */
    xn_add(&lim, a, &K_ONE, Q);
    xn_add(&t, a, b, Q); xn_add(&t, &t, &K_TWO, Q);
    xn_div(&lim, &lim, &t, Q);
    if (xn_cmp(x, &lim) < 0) {
        p_betacf(&u, a, b, x);
        xn_mul(&u, &u, &bt, Q);
        xn_div(r, &u, a, QP + 2);
    } else {
        p_betacf(&u, b, a, &omx);
        xn_mul(&u, &u, &bt, Q);
        xn_div(&u, &u, b, Q);
        xn_sub(r, &K_ONE, &u, QP + 2);
    }
}

/* ------------------------------------------------------------ laws */

enum { D_NORMAL, D_STUDENT, D_CHISQ, D_FISHER };

typedef struct law { int kind; xn p1, p2; } law;

/* density and lower-tail probability of a continuous law at x */
static int law_pdf(const law *L, const xn *x, xn *r)
{
    xn t, u, v, h;
    int Q = QP + 4;
    kinit();
    switch (L->kind) {
    case D_NORMAL:
        xn_mul(&t, x, x, Q);
        xn_div_int(&t, &t, 2, Q);
        t.s ^= t.n ? 1 : 0;
        p_exp(&t, &t);
        xn_div(r, &t, &K_SQRT2PI, QP + 2);
        return 0;
    case D_STUDENT:
        /* Gamma((n+1)/2) / (sqrt(n pi) Gamma(n/2)) (1 + x^2/n)^(-(n+1)/2) */
        xn_add(&h, &L->p1, &K_ONE, Q); xn_div_int(&h, &h, 2, Q);
        p_lgamma(&t, &h);
        xn_div_int(&u, &L->p1, 2, Q); p_lgamma(&u, &u);
        xn_sub(&t, &t, &u, Q);
        xn_mul(&u, x, x, Q); xn_div(&u, &u, &L->p1, Q); xn_add(&u, &u, &K_ONE, Q);
        p_ln(&u, &u); xn_mul(&u, &u, &h, Q);
        xn_sub(&t, &t, &u, Q);
        p_exp(&t, &t);
        xn_mul(&v, &L->p1, &K_PI, Q); xn_sqrt(&v, &v, Q);
        xn_div(r, &t, &v, QP + 2);
        return 0;
    case D_CHISQ:
        /* x^(n/2-1) e^(-x/2) / (2^(n/2) Gamma(n/2)) */
        if (x->s || !x->n) { xn_zero(r); return 0; }
        xn_div_int(&h, &L->p1, 2, Q);
        xn_sub(&t, &h, &K_ONE, Q);
        p_ln(&u, x); xn_mul(&t, &t, &u, Q);
        xn_div_int(&u, x, 2, Q); xn_sub(&t, &t, &u, Q);
        xn_mul(&u, &h, &K_LN2, Q); xn_sub(&t, &t, &u, Q);
        p_lgamma(&u, &h); xn_sub(&t, &t, &u, Q);
        p_exp(r, &t);
        return 0;
    default: {
        /* F: sqrt((n x)^n d^d / (n x + d)^(n+d)) / (x B(n/2, d/2)) */
        xn n = L->p1, d = L->p2, nx, s;
        if (x->s || !x->n) { xn_zero(r); return 0; }
        xn_mul(&nx, &n, x, Q);
        p_ln(&t, &nx); xn_mul(&t, &t, &n, Q);
        p_ln(&u, &d); xn_mul(&u, &u, &d, Q); xn_add(&t, &t, &u, Q);
        xn_add(&s, &nx, &d, Q); p_ln(&u, &s); xn_add(&v, &n, &d, Q); xn_mul(&u, &u, &v, Q);
        xn_sub(&t, &t, &u, Q);
        xn_div_int(&t, &t, 2, Q);
        /* - ln x - ln B(n/2, d/2) */
        p_ln(&u, x); xn_sub(&t, &t, &u, Q);
        xn_div_int(&h, &n, 2, Q); p_lgamma(&u, &h); xn_sub(&t, &t, &u, Q);
        xn_div_int(&h, &d, 2, Q); p_lgamma(&u, &h); xn_sub(&t, &t, &u, Q);
        xn_add(&h, &n, &d, Q); xn_div_int(&h, &h, 2, Q); p_lgamma(&u, &h); xn_add(&t, &t, &u, Q);
        p_exp(r, &t);
        return 0;
    }
    }
}

static int law_cdf(const law *L, const xn *x, xn *r)
{
    xn t, u, h;
    int Q = QP + 4;
    kinit();
    switch (L->kind) {
    case D_NORMAL:
        /* Phi(x) = erfc(-x/sqrt 2)/2 */
        xn_sqrt(&t, &K_TWO, Q);
        xn_div(&t, x, &t, Q);
        t.s ^= t.n ? 1 : 0;
        p_erfc(&t, &t);
        xn_div_int(r, &t, 2, QP + 2);
        return 0;
    case D_STUDENT: {
        /* I_{n/(n+x^2)}(n/2, 1/2) / 2 is the far tail */
        xn n = L->p1;
        if (!x->n) { *r = K_HALF; return 0; }
        xn_mul(&t, x, x, Q); xn_add(&t, &t, &n, Q); xn_div(&t, &n, &t, Q);
        xn_div_int(&h, &n, 2, Q);
        p_betai(&u, &h, &K_HALF, &t);
        xn_div_int(&u, &u, 2, Q);
        if (x->s) *r = u; else xn_sub(r, &K_ONE, &u, QP + 2);
        return 0;
    }
    case D_CHISQ:
        if (x->s || !x->n) { xn_zero(r); return 0; }
        xn_div_int(&h, &L->p1, 2, Q);
        xn_div_int(&t, x, 2, Q);
        p_gammp(r, &h, &t);
        return 0;
    default: {
        xn n = L->p1, d = L->p2, nx, hb;
        if (x->s || !x->n) { xn_zero(r); return 0; }
        xn_mul(&nx, &n, x, Q);
        xn_add(&t, &nx, &d, Q);
        xn_div(&t, &nx, &t, Q);
        xn_div_int(&h, &n, 2, Q);
        xn_div_int(&hb, &d, 2, Q);
        p_betai(r, &h, &hb, &t);
        return 0;
    }
    }
}

/* the x where the lower tail reaches p: bracket, then Newton steps kept
 * inside the bracket (bisection when a step leaves it) */
static int law_icdf(const law *L, const xn *p, xn *r)
{
    xn lo, hi, x, f, d, step, t;
    int i, positive = L->kind == D_CHISQ || L->kind == D_FISHER;
    kinit();
    if (p->s || !p->n || xn_cmp(p, &K_ONE) >= 0) return E_BADVAL;
    /* starting point: the median of the symmetric laws, else 1 */
    if (positive) { xn_zero(&lo); x = K_ONE; xn_from_int(&hi, 2); }
    else { xn_zero(&x); xn_from_int(&lo, -1); xn_from_int(&hi, 1); }
    for (i = 0; i < 200; i++) {   /* widen the bracket */
        law_cdf(L, &hi, &t);
        if (xn_cmp(&t, p) >= 0) break;
        lo = hi;
        xn_mul_int(&hi, &hi, 2, QP);
    }
    if (!positive)
        for (i = 0; i < 200; i++) {
            law_cdf(L, &lo, &t);
            if (xn_cmp(&t, p) <= 0) break;
            hi = lo;
            xn_mul_int(&lo, &lo, 2, QP);
        }
    if (xn_cmp(&x, &lo) <= 0 || xn_cmp(&x, &hi) >= 0) { xadd(&x, &lo, &hi); xn_div_int(&x, &x, 2, QP); }
    for (i = 0; i < 300; i++) {
        law_cdf(L, &x, &f);
        xsub(&f, &f, p);
        if (!f.n) break;
        if (f.s) lo = x; else hi = x;
        law_pdf(L, &x, &d);
        if (d.n) {
            xdiv(&step, &f, &d);
            xsub(&t, &x, &step);
        }
        if (!d.n || xn_cmp(&t, &lo) <= 0 || xn_cmp(&t, &hi) >= 0) {
            xadd(&t, &lo, &hi);
            xn_div_int(&t, &t, 2, QP);
            xsub(&step, &x, &t);
        }
        x = t;
        if (!step.n || (x.n && xn_top(&step) < xn_top(&x) - 26)) break;
        {
            xn w;
            xsub(&w, &hi, &lo);
            if (!w.n || (x.n && xn_top(&w) < xn_top(&x) - 30)) break;
        }
    }
    *r = x;
    return 0;
}

/* ------------------------------------------------------------ arguments */

static int argx(const value *v, xn *x)
{
    real r;
    if (to_real(v, &r)) return E_BADTYPE;
    xn_from_real(x, &r);
    return 0;
}

static int done(value *out, const xn *x)
{
    real r;
    int st = xn_to_real(&r, x);
    return bi_fin(out, &r, st);
}

/* normal with mu, sigma: x -> (x - mu)/sigma */
static int normal_args(int argc, value *a, int nx, xn *mu, xn *sg)
{
    kinit();
    xn_zero(mu); *sg = K_ONE;
    if (argc >= nx + 2) {
        if (argx(&a[0], mu) || argx(&a[1], sg)) return E_BADTYPE;
        if (sg->s || !sg->n) return E_BADVAL;
    } else if (argc != nx) return E_ARGC;
    return 0;
}

static void zscore(xn *z, const xn *x, const xn *mu, const xn *sg)
{
    xsub(z, x, mu);
    xdiv(z, z, sg);
}

BI(b_normald)
{
    xn mu, sg, x, z, r;
    int st;
    (void)V;
    if ((st = normal_args(argc, a, 1, &mu, &sg))) return st;
    if ((st = argx(&a[argc - 1], &x))) return st;
    {
        law L; L.kind = D_NORMAL;
        zscore(&z, &x, &mu, &sg);
        law_pdf(&L, &z, &r);
        xdiv(&r, &r, &sg);
    }
    return done(out, &r);
}

BI(b_normald_cdf)
{
    xn mu, sg, x, z, r, r2;
    law L;
    int st;
    (void)V;
    L.kind = D_NORMAL;
    if (argc == 2 || argc == 4) {
        /* the probability between two bounds */
        xn x2;
        if ((st = normal_args(argc, a, 2, &mu, &sg))) return st;
        if ((st = argx(&a[argc - 2], &x)) || (st = argx(&a[argc - 1], &x2))) return st;
        zscore(&z, &x, &mu, &sg); law_cdf(&L, &z, &r);
        zscore(&z, &x2, &mu, &sg); law_cdf(&L, &z, &r2);
        xsub(&r, &r2, &r);
        return done(out, &r);
    }
    if ((st = normal_args(argc, a, 1, &mu, &sg))) return st;
    if ((st = argx(&a[argc - 1], &x))) return st;
    zscore(&z, &x, &mu, &sg);
    law_cdf(&L, &z, &r);
    return done(out, &r);
}

BI(b_normald_icdf)
{
    xn mu, sg, p, r;
    law L;
    int st;
    (void)V;
    L.kind = D_NORMAL;
    if ((st = normal_args(argc, a, 1, &mu, &sg))) return st;
    if ((st = argx(&a[argc - 1], &p))) return st;
    if ((st = law_icdf(&L, &p, &r))) return st;
    xmul(&r, &r, &sg);
    xadd(&r, &r, &mu);
    return done(out, &r);
}

/* continuous laws with degrees of freedom: STUDENT(n,x), CHISQUARE(n,x),
 * FISHER(n,d,x) */
static int law_args(int kind, int argc, value *a, law *L, int *used)
{
    int np = kind == D_FISHER ? 2 : 1, st;
    L->kind = kind;
    if (argc < np + 1) return E_ARGC;
    if ((st = argx(&a[0], &L->p1))) return st;
    if (L->p1.s || !L->p1.n) return E_BADVAL;
    if (np == 2) {
        if ((st = argx(&a[1], &L->p2))) return st;
        if (L->p2.s || !L->p2.n) return E_BADVAL;
    }
    *used = np;
    return 0;
}

static int dist_pdf(int kind, int argc, value *a, value *out)
{
    law L; xn x, r; int st, np;
    if ((st = law_args(kind, argc, a, &L, &np))) return st;
    if (argc != np + 1) return E_ARGC;
    if ((st = argx(&a[np], &x))) return st;
    law_pdf(&L, &x, &r);
    return done(out, &r);
}

static int dist_cdf(int kind, int argc, value *a, value *out)
{
    law L; xn x, r; int st, np;
    if ((st = law_args(kind, argc, a, &L, &np))) return st;
    if ((st = argx(&a[np], &x))) return st;
    law_cdf(&L, &x, &r);
    if (argc == np + 2) {
        xn x2, r2;
        if ((st = argx(&a[np + 1], &x2))) return st;
        law_cdf(&L, &x2, &r2);
        xsub(&r, &r2, &r);
    } else if (argc != np + 1) return E_ARGC;
    return done(out, &r);
}

static int dist_icdf(int kind, int argc, value *a, value *out)
{
    law L; xn p, r; int st, np;
    if ((st = law_args(kind, argc, a, &L, &np))) return st;
    if (argc != np + 1) return E_ARGC;
    if ((st = argx(&a[np], &p))) return st;
    if ((st = law_icdf(&L, &p, &r))) return st;
    return done(out, &r);
}

BI(b_student) { (void)V; return dist_pdf(D_STUDENT, argc, a, out); }
BI(b_student_cdf) { (void)V; return dist_cdf(D_STUDENT, argc, a, out); }
BI(b_student_icdf) { (void)V; return dist_icdf(D_STUDENT, argc, a, out); }
BI(b_chisquare) { (void)V; return dist_pdf(D_CHISQ, argc, a, out); }
BI(b_chisquare_cdf) { (void)V; return dist_cdf(D_CHISQ, argc, a, out); }
BI(b_chisquare_icdf) { (void)V; return dist_icdf(D_CHISQ, argc, a, out); }
BI(b_fisher) { (void)V; return dist_pdf(D_FISHER, argc, a, out); }
BI(b_fisher_cdf) { (void)V; return dist_cdf(D_FISHER, argc, a, out); }
BI(b_fisher_icdf) { (void)V; return dist_icdf(D_FISHER, argc, a, out); }

/* ------------------------------------------------------------ discrete */

static int int_arg(const value *v, int32_t *n)
{
    real r;
    if (to_real(v, &r)) return E_BADTYPE;
    if (!dec_is_int(&r) || dec_to_int32(&r, n)) return E_BADVAL;
    return 0;
}

static int prob_arg(const value *v, xn *p)
{
    int st = argx(v, p);
    if (st) return st;
    if (p->s || xn_cmp(p, &K_ONE) > 0) return E_BADVAL;
    return 0;
}

/* binomial term C(n,k) p^k (1-p)^(n-k) */
static void binom_term(xn *r, int32_t n, int32_t k, const xn *p, const xn *q)
{
    xn c, t;
    int32_t i, kk = k > n - k ? n - k : k;
    c = K_ONE;
    for (i = 0; i < kk; i++) {
        xn_mul_int(&c, &c, n - i, QP + 4);
        xn_div_int(&c, &c, i + 1, QP + 4);
    }
    if (n > 2000) {
        /* large n: powers through logarithms */
        xn kk2, nk;
        xn_from_int(&kk2, k); xn_from_int(&nk, n - k);
        if (p->n) p_pow(&t, p, &kk2); else t = k ? (xn){0} : K_ONE;
        xn_mul(&c, &c, &t, QP + 4);
        if (q->n) p_pow(&t, q, &nk); else t = (n - k) ? (xn){0} : K_ONE;
    } else {
        p_ipow(&t, p, (uint32_t)k);
        xn_mul(&c, &c, &t, QP + 4);
        p_ipow(&t, q, (uint32_t)(n - k));
    }
    xn_mul(r, &c, &t, QP + 2);
}

static int binom_args(int argc, value *a, int32_t *n, xn *p, xn *q)
{
    int st;
    kinit();
    (void)argc;
    if ((st = int_arg(&a[0], n))) return st;
    if (*n < 0) return E_BADVAL;
    if ((st = prob_arg(&a[1], p))) return st;
    xn_sub(q, &K_ONE, p, XN_MAXP);
    return 0;
}

/* sum of terms for k in [k1, k2] */
static void binom_sum(xn *r, int32_t n, int32_t k1, int32_t k2, const xn *p, const xn *q)
{
    xn s, t;
    int32_t k;
    xn_zero(&s);
    if (k1 < 0) k1 = 0;
    if (k2 > n) k2 = n;
    for (k = k1; k <= k2; k++) { binom_term(&t, n, k, p, q); xn_add(&s, &s, &t, QP + 2); }
    *r = s;
}

BI(b_binomial)
{
    int32_t n, k;
    xn p, q, r;
    int st;
    (void)V;
    if (argc == 2) {   /* BINOMIAL(n, k): COMB(n, k) */
        if ((st = int_arg(&a[0], &n)) || (st = int_arg(&a[1], &k))) return st;
        kinit();
        xn_from_int(&p, 1); xn_from_int(&q, 1);
        binom_term(&r, n, k, &p, &q);
        return done(out, &r);
    }
    if ((st = binom_args(argc, a, &n, &p, &q))) return st;
    if ((st = int_arg(&a[2], &k))) return st;
    if (k < 0 || k > n) { v_int32(out, 0); return 0; }
    binom_term(&r, n, k, &p, &q);
    return done(out, &r);
}

BI(b_binomial_cdf)
{
    int32_t n, k1 = 0, k2;
    xn p, q, r;
    int st;
    (void)V;
    if ((st = binom_args(argc, a, &n, &p, &q))) return st;
    if ((st = int_arg(&a[2], &k2))) return st;
    if (argc > 3) { k1 = k2; if ((st = int_arg(&a[3], &k2))) return st; }
    binom_sum(&r, n, k1, k2, &p, &q);
    return done(out, &r);
}

BI(b_binomial_icdf)
{
    int32_t n, k;
    xn p, q, target, s, t;
    int st;
    (void)V; (void)argc;
    if ((st = binom_args(argc, a, &n, &p, &q))) return st;
    if ((st = prob_arg(&a[2], &target))) return st;
    xn_zero(&s);
    for (k = 0; k < n; k++) {
        binom_term(&t, n, k, &p, &q);
        xn_add(&s, &s, &t, QP + 2);
        if (xn_cmp(&s, &target) >= 0) break;
    }
    v_int32(out, k);
    return 0;
}

static int pois_args(value *a, xn *mu)
{
    int st;
    kinit();
    if ((st = argx(&a[0], mu))) return st;
    if (mu->s) return E_BADVAL;
    return 0;
}

/* e^-mu mu^k / k! */
static void pois_term(xn *r, const xn *mu, int32_t k)
{
    xn t, lf;
    int32_t i;
    xn m = *mu;
    if (!mu->n) { if (k == 0) *r = K_ONE; else xn_zero(r); return; }
    if (k <= 200) {
        p_ipow(&t, mu, (uint32_t)k);
        lf = K_ONE;
        for (i = 2; i <= k; i++) xn_mul_int(&lf, &lf, i, QP + 4);
        xn_div(&t, &t, &lf, QP + 4);
        m.s = 1;
        p_exp(&lf, &m);
        xn_mul(r, &t, &lf, QP + 2);
        return;
    }
    /* k ln mu - mu - lnGamma(k+1) */
    {
        xn kk, s;
        xn_from_int(&kk, k);
        p_ln(&t, mu); xn_mul(&s, &t, &kk, QP + 4);
        xn_sub(&s, &s, mu, QP + 4);
        xn_add(&kk, &kk, &K_ONE, QP + 4);
        p_lgamma(&t, &kk);
        xn_sub(&s, &s, &t, QP + 4);
        p_exp(r, &s);
    }
}

BI(b_poisson)
{
    xn mu, r;
    int32_t k;
    int st;
    (void)V; (void)argc;
    if ((st = pois_args(a, &mu))) return st;
    if ((st = int_arg(&a[1], &k))) return st;
    if (k < 0) { v_int32(out, 0); return 0; }
    pois_term(&r, &mu, k);
    return done(out, &r);
}

BI(b_poisson_cdf)
{
    xn mu, r, t;
    int32_t k1 = 0, k2, k;
    int st;
    (void)V;
    if ((st = pois_args(a, &mu))) return st;
    if ((st = int_arg(&a[1], &k2))) return st;
    if (argc > 2) { k1 = k2; if ((st = int_arg(&a[2], &k2))) return st; }
    if (k1 < 0) k1 = 0;
    xn_zero(&r);
    for (k = k1; k <= k2; k++) { pois_term(&t, &mu, k); xn_add(&r, &r, &t, QP + 2); }
    return done(out, &r);
}

BI(b_poisson_icdf)
{
    xn mu, target, s, t;
    int32_t k;
    int st;
    (void)V; (void)argc;
    if ((st = pois_args(a, &mu))) return st;
    if ((st = prob_arg(&a[1], &target))) return st;
    xn_zero(&s);
    for (k = 0; k < 1000000; k++) {
        pois_term(&t, &mu, k);
        xn_add(&s, &s, &t, QP + 2);
        if (xn_cmp(&s, &target) >= 0) break;
        if (!t.n && k > 10) break;
    }
    v_int32(out, k);
    return 0;
}

/* GEOMETRIC(p, x) = p (1-p)^(x-1); CDF(p, x) = 1 - (1-p)^x */
static void geo_cdf(xn *r, const xn *q, int32_t x)
{
    xn t;
    if (x <= 0) { xn_zero(r); return; }
    p_ipow(&t, q, (uint32_t)x);
    xn_sub(r, &K_ONE, &t, QP + 2);
}

BI(b_geometric)
{
    xn p, q, t;
    int32_t x;
    int st;
    (void)V; (void)argc;
    kinit();
    if ((st = prob_arg(&a[0], &p))) return st;
    if ((st = int_arg(&a[1], &x))) return st;
    if (x < 1) { v_int32(out, 0); return 0; }
    xn_sub(&q, &K_ONE, &p, XN_MAXP);
    p_ipow(&t, &q, (uint32_t)(x - 1));
    xn_mul(&t, &t, &p, QP + 2);
    return done(out, &t);
}

BI(b_geometric_cdf)
{
    xn p, q, r, r2;
    int32_t x, x2;
    int st;
    (void)V;
    kinit();
    if ((st = prob_arg(&a[0], &p))) return st;
    if ((st = int_arg(&a[1], &x))) return st;
    xn_sub(&q, &K_ONE, &p, XN_MAXP);
    geo_cdf(&r, &q, x);
    if (argc > 2) {
        if ((st = int_arg(&a[2], &x2))) return st;
        geo_cdf(&r2, &q, x2);
        xsub(&r, &r2, &r);
    }
    return done(out, &r);
}

BI(b_geometric_icdf)
{
    xn p, q, target, r;
    int32_t k;
    int st;
    (void)V; (void)argc;
    kinit();
    if ((st = prob_arg(&a[0], &p))) return st;
    if ((st = prob_arg(&a[1], &target))) return st;
    if (!p.n) return E_BADVAL;
    xn_sub(&q, &K_ONE, &p, XN_MAXP);
    for (k = 1; k < 100000000; k++) {
        geo_cdf(&r, &q, k);
        if (xn_cmp(&r, &target) >= 0) break;
        if (k > 1000) {
            /* jump close by logarithms: k ~ ln(1-target)/ln(1-p) */
            xn l1, l2, om;
            xn_sub(&om, &K_ONE, &target, XN_MAXP);
            if (!om.n || !q.n) break;
            p_ln(&l1, &om); p_ln(&l2, &q);
            xdiv(&l1, &l1, &l2);
            xn_trunc_int(&l1);
            {
                int32_t j;
                if (!xn_to_int32(&l1, &j) && j > k + 2) k = j - 2;
            }
        }
    }
    v_int32(out, k);
    return 0;
}

/* ------------------------------------------------------------ special */

/* Gamma(x) for real x not a non-positive integer, with guard digits */
static int p_gamma(xn *r, const xn *x)
{
    xn t;
    if (!x->s && x->n) { p_lgamma(&t, x); p_exp(r, &t); return 0; }
    if (xn_is_int(x)) return E_BADVAL;
    {
        /* reflection: Gamma(x) = pi / (sin(pi x) Gamma(1-x)) */
        xn omx, g, s;
        real rx, rs;
        xn_sub(&omx, &K_ONE, x, XN_MAXP);
        p_lgamma(&t, &omx); p_exp(&g, &t);
        xn_mul_int(&t, x, 180, XN_MAXP);
        xn_to_real(&rx, &t);
        dec_sin(&rs, &rx, ANG_DEG);
        xn_from_real(&s, &rs);
        xmul(&t, &s, &g);
        if (!t.n) return E_BADVAL;
        xdiv(r, &K_PI, &t);
    }
    return 0;
}

BI(b_beta)
{
    xn x, y, gx, gy, gs, s;
    int st;
    (void)V; (void)argc;
    kinit();
    if ((st = argx(&a[0], &x)) || (st = argx(&a[1], &y))) return st;
    if (!x.s && x.n && !y.s && y.n) {
        xn t;
        p_lgamma(&gx, &x); p_lgamma(&gy, &y);
        xadd(&s, &x, &y); p_lgamma(&gs, &s);
        xadd(&t, &gx, &gy); xsub(&t, &t, &gs);
        p_exp(&t, &t);
        return done(out, &t);
    }
    xadd(&s, &x, &y);
    if ((st = p_gamma(&gx, &x)) || (st = p_gamma(&gy, &y))) return st;
    if (xn_is_int(&s) && (s.s || !s.n)) { xn z; xn_zero(&z); return done(out, &z); }
    if ((st = p_gamma(&gs, &s))) return st;
    xmul(&gx, &gx, &gy);
    xdiv(&gx, &gx, &gs);
    return done(out, &gx);
}

BI(b_erf) { xn x, r; int st; (void)V; (void)argc; if ((st = argx(&a[0], &x))) return st; p_erf(&r, &x); return done(out, &r); }
BI(b_erfc) { xn x, r; int st; (void)V; (void)argc; if ((st = argx(&a[0], &x))) return st; p_erfc(&r, &x); return done(out, &r); }

/* Psi(a [, n]): the n-th derivative of the digamma function */
BI(b_psi)
{
    xn z, acc, t, nf, lim, z2, zp;
    int32_t n = 0, k;
    int st, Q = QP + 4;
    (void)V;
    kinit();
    if ((st = argx(&a[0], &z))) return st;
    if (argc > 1 && ((st = int_arg(&a[1], &n)) || n < 0 || n > 40)) return st ? st : E_BADVAL;
    if (xn_is_int(&z) && (z.s || !z.n)) return E_INFRES;
    nf = K_ONE;
    for (k = 2; k <= n; k++) xn_mul_int(&nf, &nf, k, Q);   /* n! */
    xn_zero(&acc);
    xn_from_int(&lim, 30 + n);
    /* recurrence: psi^(n)(z) = psi^(n)(z+1) + (-1)^(n+1) n! / z^(n+1) */
    while (xn_cmp(&z, &lim) < 0) {
        p_ipow(&t, &z, (uint32_t)(n + 1));
        xn_div(&t, &nf, &t, Q);
        if (!(n & 1)) t.s ^= t.n ? 1 : 0;
        xn_add(&acc, &acc, &t, Q);
        xn_add(&z, &z, &K_ONE, XN_MAXP);
    }
    xn_mul(&z2, &z, &z, Q);
    if (n == 0) {
        /* ln z - 1/(2z) - sum B2k / (2k z^2k) */
        xn s;
        p_ln(&s, &z);
        xn_div(&t, &K_ONE, &z, Q); xn_div_int(&t, &t, 2, Q); xn_sub(&s, &s, &t, Q);
        zp = z2;
        for (k = 1; k <= 10; k++) {
            xn b;
            xn_from_int(&b, BN[k - 1]);
            xn_div_int(&b, &b, BD[k - 1] * 2 * k, Q);
            xn_div(&b, &b, &zp, Q);
            xn_sub(&s, &s, &b, Q);
            xn_mul(&zp, &zp, &z2, Q);
        }
        xn_add(&acc, &acc, &s, Q);
    } else {
        /* (-1)^(n+1) [ (n-1)!/z^n + n!/(2 z^(n+1)) + sum B2k (2k+n-1)!/((2k)! z^(2k+n)) ] */
        xn s, f1, c;
        f1 = K_ONE;
        for (k = 2; k <= n - 1; k++) xn_mul_int(&f1, &f1, k, Q);   /* (n-1)! */
        p_ipow(&zp, &z, (uint32_t)n);
        xn_div(&s, &f1, &zp, Q);
        xn_mul(&zp, &zp, &z, Q);
        xn_div(&t, &nf, &zp, Q); xn_div_int(&t, &t, 2, Q);
        xn_add(&s, &s, &t, Q);
        /* c = (2k+n-1)!/(2k)!, from k=1: (n+1)!/2 */
        c = nf; xn_mul_int(&c, &c, n + 1, Q); xn_div_int(&c, &c, 2, Q);
        for (k = 1; k <= 10; k++) {
            xn b;
            xn_mul(&zp, &zp, &z, Q);         /* z^(2k+n) */
            xn_from_int(&b, BN[k - 1]);
            xn_div_int(&b, &b, BD[k - 1], Q);
            xn_mul(&b, &b, &c, Q);
            xn_div(&b, &b, &zp, Q);
            xn_add(&s, &s, &b, Q);
            xn_mul(&zp, &zp, &z, Q);
            /* next c: times (2k+n)(2k+n+1) / ((2k+1)(2k+2)) */
            xn_mul_int(&c, &c, (2 * k + n) * (2 * k + n + 1), Q);
            xn_div_int(&c, &c, (2 * k + 1) * (2 * k + 2), Q);
        }
        if (!(n & 1)) s.s ^= s.n ? 1 : 0;
        xn_add(&acc, &acc, &s, Q);
    }
    return done(out, &acc);
}

/* Zeta(s) by Euler-Maclaurin summation */
BI(b_zeta)
{
    xn s, sum, t, lnk, Nn, sm1, poch, Np, term;
    int32_t k, N, j;
    int st, Q = QP + 4;
    (void)V; (void)argc;
    kinit();
    if ((st = argx(&a[0], &s))) return st;
    if (!xn_cmp(&s, &K_ONE)) return E_INFRES;
    {
        int32_t si = 0;
        xn as = s;
        as.s = 0;
        xn_trunc_int(&as);
        xn_to_int32(&as, &si);
        N = 30 + (si > 200 ? 200 : si);
    }
    xn_zero(&sum);
    for (k = 2; k < N; k++) {
        xn kk;
        xn_from_int(&kk, k);
        p_ln(&lnk, &kk);
        xn_mul(&t, &lnk, &s, Q);
        t.s ^= t.n ? 1 : 0;
        p_exp(&t, &t);
        xn_add(&sum, &sum, &t, Q);
    }
    xn_add(&sum, &sum, &K_ONE, Q);
    xn_from_int(&Nn, N);
    p_ln(&lnk, &Nn);
    /* N^(1-s)/(s-1) + N^(-s)/2 */
    xn_sub(&sm1, &s, &K_ONE, Q);
    xn_mul(&t, &lnk, &sm1, Q); t.s ^= t.n ? 1 : 0;
    p_exp(&Np, &t);                    /* N^(1-s) */
    xn_div(&t, &Np, &sm1, Q);
    xn_add(&sum, &sum, &t, Q);
    xn_div(&Np, &Np, &Nn, Q);          /* N^(-s) */
    xn_div_int(&t, &Np, 2, Q);
    xn_add(&sum, &sum, &t, Q);
    /* sum_j B2j/(2j)! s(s+1)...(s+2j-2) N^(-s-2j+1) */
    poch = s;
    xn_div(&Np, &Np, &Nn, Q);          /* N^(-s-1) */
    for (j = 1; j <= 10; j++) {
        xn b;
        int32_t i;
        xn_from_int(&b, BN[j - 1]);
        xn_div_int(&b, &b, BD[j - 1], Q);
        for (i = 1; i <= 2 * j; i++) xn_div_int(&b, &b, i, Q);
        xn_mul(&term, &b, &poch, Q);
        xn_mul(&term, &term, &Np, Q);
        xn_add(&sum, &sum, &term, Q);
        if (!poch.n) break;
        /* poch *= (s+2j-1)(s+2j); Np /= N^2 */
        xn_from_int(&b, 2 * j - 1); xn_add(&b, &b, &s, Q); xn_mul(&poch, &poch, &b, Q);
        xn_from_int(&b, 2 * j); xn_add(&b, &b, &s, Q); xn_mul(&poch, &poch, &b, Q);
        xn_div(&Np, &Np, &Nn, Q);
        xn_div(&Np, &Np, &Nn, Q);
    }
    return done(out, &sum);
}

/* ------------------------------------------------------------ statistics */

/* values (and optional frequencies) of a list or vector, as xn */
static int stat_data(int argc, value *a, xn **vals, xn **freq, uint32_t *n)
{
    uint32_t i, m;
    const value *el, *fe = NULL;
    if (a[0].t == T_LIST) { m = a[0].u.l->n; el = a[0].u.l->v; }
    else if (a[0].t == T_MAT && a[0].u.m->vec) { m = a[0].u.m->cols; el = a[0].u.m->v; }
    else return E_BADTYPE;
    if (argc > 1) {
        if (a[1].t == T_LIST && a[1].u.l->n == m) fe = a[1].u.l->v;
        else if (a[1].t == T_MAT && a[1].u.m->vec && a[1].u.m->cols == m) fe = a[1].u.m->v;
        else return E_DIM;
    }
    if (!m) return E_DIM;
    *vals = (xn *)malloc(m * sizeof(xn));
    *freq = (xn *)malloc(m * sizeof(xn));
    if (!*vals || !*freq) { free(*vals); free(*freq); return E_MEM; }
    for (i = 0; i < m; i++) {
        if (argx(&el[i], &(*vals)[i])) { free(*vals); free(*freq); return E_BADTYPE; }
        if (fe) {
            if (argx(&fe[i], &(*freq)[i]) || (*freq)[i].s) { free(*vals); free(*freq); return E_BADVAL; }
        } else xn_from_int(&(*freq)[i], 1);
    }
    *n = m;
    return 0;
}

static int stat_moments(int argc, value *a, xn *mean, xn *var)
{
    xn *v, *f, sw, s, t, d;
    uint32_t n, i;
    int st;
    kinit();
    if ((st = stat_data(argc, a, &v, &f, &n))) return st;
    xn_zero(&sw); xn_zero(&s);
    for (i = 0; i < n; i++) {
        xadd(&sw, &sw, &f[i]);
        xmul(&t, &v[i], &f[i]);
        xadd(&s, &s, &t);
    }
    if (!sw.n) { free(v); free(f); return E_BADVAL; }
    xdiv(mean, &s, &sw);
    if (var) {
        /* population variance: sum f (x - mean)^2 / sum f */
        xn_zero(&s);
        for (i = 0; i < n; i++) {
            xsub(&d, &v[i], mean);
            xmul(&t, &d, &d);
            xmul(&t, &t, &f[i]);
            xadd(&s, &s, &t);
        }
        xdiv(var, &s, &sw);
    }
    free(v); free(f);
    return 0;
}

BI(b_mean) { xn m; int st; (void)V; if ((st = stat_moments(argc, a, &m, NULL))) return st; return done(out, &m); }
BI(b_variance) { xn m, s; int st; (void)V; if ((st = stat_moments(argc, a, &m, &s))) return st; return done(out, &s); }
BI(b_stddev)
{
    xn m, s;
    int st;
    (void)V;
    if ((st = stat_moments(argc, a, &m, &s))) return st;
    xn_sqrt(&s, &s, QP);
    return done(out, &s);
}

BI(b_median)
{
    xn *v, *f, half, cum, tot, t;
    uint32_t n, i, j;
    int st;
    (void)V;
    kinit();
    if ((st = stat_data(argc, a, &v, &f, &n))) return st;
    /* sort the values (with their frequencies) */
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && xn_cmp(&v[j - 1], &v[j]) > 0; j--) {
            xn x = v[j]; v[j] = v[j - 1]; v[j - 1] = x;
            x = f[j]; f[j] = f[j - 1]; f[j - 1] = x;
        }
    xn_zero(&tot);
    for (i = 0; i < n; i++) xadd(&tot, &tot, &f[i]);
    if (!tot.n) { free(v); free(f); return E_BADVAL; }
    xn_div_int(&half, &tot, 2, QP);
    xn_zero(&cum);
    for (i = 0; i < n; i++) {
        xadd(&cum, &cum, &f[i]);
        if (xn_cmp(&cum, &half) > 0) { t = v[i]; break; }
        if (!xn_cmp(&cum, &half)) {
            /* exactly half below: the mean of this value and the next */
            uint32_t k = i + 1;
            while (k < n && !f[k].n) k++;
            if (k < n) { xadd(&t, &v[i], &v[k]); xn_div_int(&t, &t, 2, QP); }
            else t = v[i];
            break;
        }
    }
    if (i == n) t = v[n - 1];
    free(v); free(f);
    return done(out, &t);
}
