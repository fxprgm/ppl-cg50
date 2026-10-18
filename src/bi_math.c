/* bi_math.c -- numeric built-ins: elementary functions, parts of numbers,
 * complex parts, combinatorics, random numbers. */
#include "bi.h"
#include "decx.h"
#include <string.h>

int bi_fin(value *out, const real *r, int st)
{
    if (st && !DEC_SOFT(st)) return dec_err(st);
    v_real(out, r);
    return 0;
}

int bi_real(const value *v, real *r) { return to_real(v, r) ? E_BADTYPE : 0; }

int bi_int(const value *v, int32_t *n)
{
    if (v->t != T_REAL && v->t != T_INT) return E_BADTYPE;
    return to_int(v, n) ? E_BADVAL : 0;
}

/* f over a pair, broadcasting lists */
int bi_map2(ppl_vm *V, const value *a, const value *b,
            int (*f)(ppl_vm *, const value *, const value *, value *), value *out)
{
    if (a->t == T_LIST || b->t == T_LIST) {
        uint32_t n, i;
        value r;
        int st;
        if (a->t == T_LIST && b->t == T_LIST && a->u.l->n != b->u.l->n) return E_DIM;
        n = a->t == T_LIST ? a->u.l->n : b->u.l->n;
        if ((st = v_list(&r, n))) return st;
        for (i = 0; i < n; i++) {
            st = bi_map2(V, a->t == T_LIST ? &a->u.l->v[i] : a, b->t == T_LIST ? &b->u.l->v[i] : b, f, &r.u.l->v[i]);
            if (st) { v_decref(&r); return st; }
        }
        *out = r;
        return 0;
    }
    return f(V, a, b, out);
}

/* ---- unary real functions, element-wise over lists ---- */

#define RF(nm, call) \
    static int r_##nm(ppl_vm *V, const real *x, value *out) { real r; int st; (void)V; dec_zero(&r); st = call; return bi_fin(out, &r, st); } \
    static BI(b_##nm) { (void)argc; return ar_map1(V, &a[0], r_##nm, NULL, out); }

RF(asinh, dec_asinh(&r, x))
RF(acosh, dec_acosh(&r, x))
RF(atanh, dec_atanh(&r, x))
RF(sinh, dec_sinh(&r, x))
RF(cosh, dec_cosh(&r, x))
RF(tanh, dec_tanh(&r, x))
RF(expm1, dec_expm1(&r, x))
RF(lnp1, dec_lnp1(&r, x))
RF(alog, dec_alog(&r, x))
RF(ip, dec_ip(&r, x))
RF(fp, dec_fp(&r, x))
RF(floor, dec_floor(&r, x))
RF(ceiling, dec_ceil(&r, x))
RF(gamma, dec_gamma(&r, x))
RF(mant, (dec_mant(&r, x), 0))
RF(xpon, (dec_from_int(&r, dec_xpon(x)), 0))

static int r_sin(ppl_vm *V, const real *x, value *out) { real r; return bi_fin(out, &r, dec_sin(&r, x, V->set.angle)); }
static int r_cos(ppl_vm *V, const real *x, value *out) { real r; return bi_fin(out, &r, dec_cos(&r, x, V->set.angle)); }
static int r_tan(ppl_vm *V, const real *x, value *out)
{
    real r;
    int st = dec_tan(&r, x, V->set.angle);
    if (st == DEC_EDOMAIN) return E_INFRES;
    return bi_fin(out, &r, st);
}
static int r_atan(ppl_vm *V, const real *x, value *out) { real r; return bi_fin(out, &r, dec_atan(&r, x, V->set.angle)); }
static int r_asin(ppl_vm *V, const real *x, value *out) { real r; int st = dec_asin(&r, x, V->set.angle); return st == DEC_EDOMAIN ? E_NONREAL : bi_fin(out, &r, st); }
static int r_acos(ppl_vm *V, const real *x, value *out) { real r; int st = dec_acos(&r, x, V->set.angle); return st == DEC_EDOMAIN ? E_NONREAL : bi_fin(out, &r, st); }

/* reciprocal trig: computed as 1/f with f rounded first, like the Prime */
static int recip(value *out, real *r, int st)
{
    real one, q = {{0, 0, 0}, 0, 0, 0};
    if (st && !DEC_SOFT(st)) return dec_err(st);
    if (dec_is_zero(r)) return E_INFRES;
    dec_from_int(&one, 1);
    return bi_fin(out, &q, dec_div(&q, &one, r));
}
static int r_csc(ppl_vm *V, const real *x, value *out) { real r; int st = dec_sin(&r, x, V->set.angle); return recip(out, &r, st); }
static int r_sec(ppl_vm *V, const real *x, value *out) { real r; int st = dec_cos(&r, x, V->set.angle); return recip(out, &r, st); }
static int r_cot(ppl_vm *V, const real *x, value *out)
{
    real s, c, q = {{0, 0, 0}, 0, 0, 0};
    int st = dec_sin(&s, x, V->set.angle);
    if (st && !DEC_SOFT(st)) return dec_err(st);
    dec_cos(&c, x, V->set.angle);
    if (dec_is_zero(&s)) return E_INFRES;
    return bi_fin(out, &q, dec_div(&q, &c, &s));
}
static int inv_of(const real *x, real *y)
{
    real one;
    if (dec_is_zero(x)) return E_INFRES;
    dec_from_int(&one, 1);
    return dec_err(dec_div(y, &one, x));
}
static int r_acsc(ppl_vm *V, const real *x, value *out) { real y; int e = inv_of(x, &y); return e ? e : r_asin(V, &y, out); }
static int r_asec(ppl_vm *V, const real *x, value *out) { real y; int e = inv_of(x, &y); return e ? e : r_acos(V, &y, out); }
static int r_acot(ppl_vm *V, const real *x, value *out)
{
    real y;
    if (dec_is_zero(x)) {   /* ACOT(0) = quarter turn */
        real one; dec_from_int(&one, 1);
        y = one; y.k = DEC_INF;
        return r_atan(V, &y, out);
    }
    inv_of(x, &y);
    return r_atan(V, &y, out);
}

/* complex-capable ones */
static int r_exp(ppl_vm *V, const real *x, value *out) { real r; (void)V; return bi_fin(out, &r, dec_exp(&r, x)); }
static int c_expv(ppl_vm *V, const cplx *x, value *out) { cplx z; int e; (void)V; if ((e = c_exp(&z, x))) return e; return c_make(out, &z.re, &z.im); }
static int c_lnv(ppl_vm *V, const cplx *x, value *out) { cplx z; int e; (void)V; if ((e = c_ln(&z, x))) return e; return c_make(out, &z.re, &z.im); }
static int r_ln(ppl_vm *V, const real *x, value *out)
{
    real r;
    if (dec_is_zero(x)) return E_BADVAL;
    if (x->s) {
        cplx c;
        if (!V->set.complex_ok) return E_NONREAL;
        c.re = *x; dec_zero(&c.im);
        return c_lnv(V, &c, out);
    }
    return bi_fin(out, &r, dec_ln(&r, x));
}
static int r_log(ppl_vm *V, const real *x, value *out)
{
    real r;
    if (dec_is_zero(x)) return E_BADVAL;
    if (x->s) return V->set.complex_ok ? E_UNSUPPORTED : E_NONREAL;
    return bi_fin(out, &r, dec_log10(&r, x));
}
static int r_abs(ppl_vm *V, const real *x, value *out) { real r; (void)V; dec_abs(&r, x); v_real(out, &r); return 0; }
static int c_absv(ppl_vm *V, const cplx *x, value *out) { real r; (void)V; c_abs(&r, x); v_real(out, &r); return 0; }
static int r_sign(ppl_vm *V, const real *x, value *out) { (void)V; v_int32(out, dec_is_zero(x) ? 0 : x->s ? -1 : 1); return 0; }
static int c_signv(ppl_vm *V, const cplx *x, value *out)
{
    real m; cplx z, mm;
    (void)V;
    c_abs(&m, x);
    mm.re = m; dec_zero(&mm.im);
    c_div(&z, x, &mm);
    return c_make(out, &z.re, &z.im);
}
static int r_re(ppl_vm *V, const real *x, value *out) { (void)V; v_real(out, x); return 0; }
static int c_rev(ppl_vm *V, const cplx *x, value *out) { (void)V; v_real(out, &x->re); return 0; }
static int r_im(ppl_vm *V, const real *x, value *out) { (void)V; (void)x; v_int32(out, 0); return 0; }
static int c_imv(ppl_vm *V, const cplx *x, value *out) { (void)V; v_real(out, &x->im); return 0; }
static int c_conjv(ppl_vm *V, const cplx *x, value *out) { real im; (void)V; dec_neg(&im, &x->im); return c_make(out, &x->re, &im); }
static int r_arg(ppl_vm *V, const real *x, value *out)
{
    real r, z;
    dec_zero(&z);
    return bi_fin(out, &r, dec_atan2(&r, &z, x, V->set.angle));
}
static int c_argv(ppl_vm *V, const cplx *x, value *out) { real r; int e = c_arg(V, &r, x); if (e) return e; v_real(out, &r); return 0; }

static BI(b_sin) { (void)argc; return ar_map1(V, &a[0], r_sin, NULL, out); }
static BI(b_cos) { (void)argc; return ar_map1(V, &a[0], r_cos, NULL, out); }
static BI(b_tan) { (void)argc; return ar_map1(V, &a[0], r_tan, NULL, out); }
static BI(b_asin) { (void)argc; return ar_map1(V, &a[0], r_asin, NULL, out); }
static BI(b_acos) { (void)argc; return ar_map1(V, &a[0], r_acos, NULL, out); }
static BI(b_atan) { (void)argc; return ar_map1(V, &a[0], r_atan, NULL, out); }
static BI(b_csc) { (void)argc; return ar_map1(V, &a[0], r_csc, NULL, out); }
static BI(b_sec) { (void)argc; return ar_map1(V, &a[0], r_sec, NULL, out); }
static BI(b_cot) { (void)argc; return ar_map1(V, &a[0], r_cot, NULL, out); }
static BI(b_acsc) { (void)argc; return ar_map1(V, &a[0], r_acsc, NULL, out); }
static BI(b_asec) { (void)argc; return ar_map1(V, &a[0], r_asec, NULL, out); }
static BI(b_acot) { (void)argc; return ar_map1(V, &a[0], r_acot, NULL, out); }
static BI(b_exp) { (void)argc; return ar_map1(V, &a[0], r_exp, c_expv, out); }
static BI(b_ln) { (void)argc; return ar_map1(V, &a[0], r_ln, c_lnv, out); }
static BI(b_sign) { (void)argc; return ar_map1(V, &a[0], r_sign, c_signv, out); }
static BI(b_re) { (void)argc; return ar_map1(V, &a[0], r_re, c_rev, out); }
static BI(b_im) { (void)argc; return ar_map1(V, &a[0], r_im, c_imv, out); }
static BI(b_conj) { (void)argc; return ar_map1(V, &a[0], r_re, c_conjv, out); }
static BI(b_arg) { (void)argc; return ar_map1(V, &a[0], r_arg, c_argv, out); }

static int logb2(ppl_vm *V, const value *x, const value *b, value *out)
{
    real X, B, r = {{0, 0, 0}, 0, 0, 0};
    (void)V;
    if (bi_real(x, &X) || bi_real(b, &B)) return E_BADTYPE;
    return bi_fin(out, &r, dec_logb(&r, &X, &B));
}
static BI(b_log)
{
    if (argc == 2) return bi_map2(V, &a[0], &a[1], logb2, out);
    return ar_map1(V, &a[0], r_log, NULL, out);
}

static BI(b_abs)
{
    (void)argc;
    if (a[0].t == T_MAT) {
        /* Frobenius norm */
        pmat *m = a[0].u.m;
        xn acc, t, e;
        int i, n = m->rows * m->cols;
        real r;
        xn_zero(&acc);
        for (i = 0; i < n; i++) {
            if (m->v[i].t == T_CPLX) {
                xn_from_real(&e, &m->v[i].u.c.re); xn_mul(&t, &e, &e, 30); xn_add(&acc, &acc, &t, 30);
                xn_from_real(&e, &m->v[i].u.c.im);
            } else xn_from_real(&e, &m->v[i].u.r);
            xn_mul(&t, &e, &e, 30);
            xn_add(&acc, &acc, &t, 30);
        }
        xn_sqrt(&acc, &acc, DEC_DIGITS);
        return bi_fin(out, &r, xn_to_real(&r, &acc));
    }
    if (a[0].t == T_INT) { v_copy(out, &a[0]); return 0; }
    return ar_map1(V, &a[0], r_abs, c_absv, out);
}

/* ---- two-argument numeric ---- */

static int round2(ppl_vm *V, const value *x, const value *n, value *out, int trunc)
{
    real X, r = {{0, 0, 0}, 0, 0, 0};
    int32_t k;
    (void)V;
    if (x->t == T_CPLX) {
        value re, im; cplx c = x->u.c;
        value vr, vi;
        v_real(&vr, &c.re); v_real(&vi, &c.im);
        if (round2(V, &vr, n, &re, trunc) || round2(V, &vi, n, &im, trunc)) return E_BADTYPE;
        return c_make(out, &re.u.r, &im.u.r);
    }
    if (bi_real(x, &X)) return E_BADTYPE;
    if (bi_int(n, &k)) return E_BADTYPE;
    if (k < -12 || k > 12) return E_BADVAL;
    return bi_fin(out, &r, dec_round_places(&r, &X, k, trunc));
}
static int round2r(ppl_vm *V, const value *x, const value *n, value *out) { return round2(V, x, n, out, 0); }
static int round2t(ppl_vm *V, const value *x, const value *n, value *out) { return round2(V, x, n, out, 1); }
static BI(b_round)
{
    value z;
    if (argc == 1) { v_int32(&z, 0); return bi_map2(V, &a[0], &z, round2r, out); }
    return bi_map2(V, &a[0], &a[1], round2r, out);
}
static BI(b_truncate)
{
    value z;
    if (argc == 1) { v_int32(&z, 0); return bi_map2(V, &a[0], &z, round2t, out); }
    return bi_map2(V, &a[0], &a[1], round2t, out);
}

static int maxmin2(ppl_vm *V, const value *x, const value *y, value *out, int ismax)
{
    real X, Y;
    (void)V;
    if (bi_real(x, &X) || bi_real(y, &Y)) return E_BADTYPE;
    v_copy(out, (dec_cmp(&X, &Y) >= 0) == ismax ? x : y);
    return 0;
}
static int max2(ppl_vm *V, const value *x, const value *y, value *out) { return maxmin2(V, x, y, out, 1); }
static int min2(ppl_vm *V, const value *x, const value *y, value *out) { return maxmin2(V, x, y, out, 0); }
static int maxmin(ppl_vm *V, int argc, value *a, value *out, int ismax)
{
    if (argc == 1) {
        uint32_t i;
        value best;
        if (a[0].t != T_LIST) { v_copy(out, &a[0]); return 0; }
        if (!a[0].u.l->n) return E_DIM;
        v_copy(&best, &a[0].u.l->v[0]);
        for (i = 1; i < a[0].u.l->n; i++) {
            value t;
            int st = maxmin2(V, &best, &a[0].u.l->v[i], &t, ismax);
            v_decref(&best);
            if (st) return st;
            best = t;
        }
        *out = best;
        return 0;
    }
    return bi_map2(V, &a[0], &a[1], ismax ? max2 : min2, out);
}
static BI(b_max) { return maxmin(V, argc, a, out, 1); }
static BI(b_min) { return maxmin(V, argc, a, out, 0); }

static int pct(ppl_vm *V, const value *x, const value *y, value *out)
{
    real X, Y, t, h;
    (void)V;
    if (bi_real(x, &X) || bi_real(y, &Y)) return E_BADTYPE;
    dec_from_int(&h, 100);
    dec_mul(&t, &X, &Y);
    return bi_fin(out, &t, dec_div(&t, &t, &h));
}
static BI(b_percent) { (void)argc; return bi_map2(V, &a[0], &a[1], pct, out); }
static BI(b_pctchange)
{
    real x, y, t, h;
    (void)argc; (void)V;
    if (bi_real(&a[0], &x) || bi_real(&a[1], &y)) return E_BADTYPE;
    if (dec_is_zero(&x)) return E_DIV0;
    dec_from_int(&h, 100);
    dec_sub(&t, &y, &x); dec_mul(&t, &t, &h);
    return bi_fin(out, &t, dec_div(&t, &t, &x));
}
static BI(b_pcttotal)
{
    real x, y, t, h;
    (void)argc; (void)V;
    if (bi_real(&a[0], &x) || bi_real(&a[1], &y)) return E_BADTYPE;
    if (dec_is_zero(&x)) return E_DIV0;
    dec_from_int(&h, 100);
    dec_mul(&t, &y, &h);
    return bi_fin(out, &t, dec_div(&t, &t, &x));
}

/* COMB and PERM: exact products with guard digits */
static int combperm(const value *nv, const value *kv, value *out, int comb)
{
    int32_t n, k, i;
    xn acc, t;
    real r;
    if (bi_int(nv, &n) || bi_int(kv, &k)) return E_BADVAL;
    if (n < 0 || k < 0) return E_BADVAL;
    if (k > n) { v_int32(out, 0); return 0; }
    if (comb && k > n - k) k = n - k;
    xn_from_int(&acc, 1);
    for (i = 0; i < k; i++) {
        xn_from_int(&t, n - i);
        xn_mul(&acc, &acc, &t, 40);
        if (comb) xn_div_int(&acc, &acc, i + 1, 40);
        if (xn_top(&acc) > 600) break;
    }
    return bi_fin(out, &r, xn_to_real(&r, &acc));
}
static int comb2(ppl_vm *V, const value *n, const value *k, value *out) { (void)V; return combperm(n, k, out, 1); }
static int perm2(ppl_vm *V, const value *n, const value *k, value *out) { (void)V; return combperm(n, k, out, 0); }
static BI(b_comb) { (void)argc; return bi_map2(V, &a[0], &a[1], comb2, out); }
static BI(b_perm) { (void)argc; return bi_map2(V, &a[0], &a[1], perm2, out); }

/* ---- random numbers: xoshiro128** ---- */

static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
static uint32_t rnd32(ppl_vm *V)
{
    uint32_t *s = V->rng, r = rotl(s[1] * 5, 7) * 9, t = s[1] << 9;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 11);
    return r;
}
static void rnd_unit(ppl_vm *V, real *r)
{
    /* 12 random digits: 0.dddddddddddd */
    xn x;
    uint32_t a = rnd32(V) % 1000000u, b = rnd32(V) % 1000000u;
    xn_zero(&x);
    x.d[0] = (uint16_t)(b % 10000u);
    x.d[1] = (uint16_t)((b / 10000u) + (a % 100u) * 100u);
    x.d[2] = (uint16_t)(a / 100u);
    x.n = 3;
    while (x.n && !x.d[x.n - 1]) x.n--;
    x.e = -12;
    if (!x.n) { dec_zero(r); return; }
    xn_to_real(r, &x);
}
static BI(b_random)
{
    real u, lo, hi, t;
    rnd_unit(V, &u);
    if (argc == 0) { v_real(out, &u); return 0; }
    if (argc == 1) { dec_zero(&lo); if (bi_real(&a[0], &hi)) return E_BADTYPE; }
    else if (bi_real(&a[0], &lo) || bi_real(&a[1], &hi)) return E_BADTYPE;
    dec_sub(&t, &hi, &lo); dec_mul(&t, &t, &u); dec_add(&t, &t, &lo);
    v_real(out, &t);
    return 0;
}
static BI(b_randint)
{
    int32_t lo = 0, hi;
    uint32_t span;
    if (argc == 1) { if (bi_int(&a[0], &hi)) return E_BADVAL; }
    else if (bi_int(&a[0], &lo) || bi_int(&a[1], &hi)) return E_BADVAL;
    if (hi < lo) { int32_t t = hi; hi = lo; lo = t; }
    span = (uint32_t)(hi - lo) + 1u;
    v_int32(out, lo + (int32_t)(span ? rnd32(V) % span : rnd32(V)));
    return 0;
}
static BI(b_randseed)
{
    int32_t s = 0;
    int i;
    if (argc) { real r; if (bi_real(&a[0], &r)) return E_BADTYPE; dec_to_int32_trunc(&r, &s); }
    V->rng[0] = (uint32_t)s ^ 0x9E3779B9u; V->rng[1] = 0x85EBCA6Bu; V->rng[2] = 0xC2B2AE35u ^ ((uint32_t)s << 7); V->rng[3] = 0x27D4EB2Fu;
    for (i = 0; i < 8; i++) rnd32(V);
    v_int32(out, 0);
    return 0;
}
static BI(b_randnorm)
{
    /* Box-Muller */
    real u1, u2, t, m, s, two, l;
    dec_zero(&m); dec_from_int(&s, 1);
    if (argc >= 1 && bi_real(&a[0], &m)) return E_BADTYPE;
    if (argc >= 2 && bi_real(&a[1], &s)) return E_BADTYPE;
    do rnd_unit(V, &u1); while (dec_is_zero(&u1));
    rnd_unit(V, &u2);
    dec_ln(&l, &u1);
    dec_from_int(&two, -2);
    dec_mul(&l, &l, &two);
    dec_sqrt(&l, &l);
    dec_from_int(&two, 360);
    dec_mul(&u2, &u2, &two);
    dec_cos(&t, &u2, ANG_DEG);
    dec_mul(&t, &t, &l);
    dec_mul(&t, &t, &s);
    dec_add(&t, &t, &m);
    v_real(out, &t);
    return 0;
}

static BI(b_pi) { real r; (void)V; (void)argc; (void)a; dec_pi(&r); v_real(out, &r); return 0; }
static BI(b_maxreal) { real r; (void)V; (void)argc; (void)a; dec_maxreal(&r, 0); v_real(out, &r); return 0; }
static BI(b_minreal) { real r; (void)V; (void)argc; (void)a; dec_from_str(&r, "1E-499", NULL); v_real(out, &r); return 0; }

static BI(b_tohms)
{
    (void)V; (void)argc;
    if (a[0].t != T_REAL) return E_BADTYPE;
    *out = a[0]; out->x = 1;
    return 0;
}
static BI(b_hmsto)
{
    (void)V; (void)argc;
    if (a[0].t != T_REAL) return E_BADTYPE;
    *out = a[0]; out->x = 0;
    return 0;
}

/* table entries exported to builtins.c */
const builtin bi_math_table[] = {
    { "SIN", b_sin, 1, 1, 0 }, { "COS", b_cos, 1, 1, 0 }, { "TAN", b_tan, 1, 1, 0 },
    { "ASIN", b_asin, 1, 1, 0 }, { "ACOS", b_acos, 1, 1, 0 }, { "ATAN", b_atan, 1, 1, 0 },
    { "CSC", b_csc, 1, 1, 0 }, { "SEC", b_sec, 1, 1, 0 }, { "COT", b_cot, 1, 1, 0 },
    { "ACSC", b_acsc, 1, 1, 0 }, { "ASEC", b_asec, 1, 1, 0 }, { "ACOT", b_acot, 1, 1, 0 },
    { "SINH", b_sinh, 1, 1, 0 }, { "COSH", b_cosh, 1, 1, 0 }, { "TANH", b_tanh, 1, 1, 0 },
    { "ASINH", b_asinh, 1, 1, 0 }, { "ACOSH", b_acosh, 1, 1, 0 }, { "ATANH", b_atanh, 1, 1, 0 },
    { "EXP", b_exp, 1, 1, 0 }, { "LN", b_ln, 1, 1, 0 }, { "LOG", b_log, 1, 2, 0 },
    { "ALOG", b_alog, 1, 1, 0 }, { "EXPM1", b_expm1, 1, 1, 0 }, { "LNP1", b_lnp1, 1, 1, 0 },
    { "ABS", b_abs, 1, 1, 0 }, { "SIGN", b_sign, 1, 1, 0 },
    { "IP", b_ip, 1, 1, 0 }, { "FP", b_fp, 1, 1, 0 }, { "FLOOR", b_floor, 1, 1, 0 },
    { "CEILING", b_ceiling, 1, 1, 0 }, { "ROUND", b_round, 1, 2, 0 }, { "TRUNCATE", b_truncate, 1, 2, 0 },
    { "MANT", b_mant, 1, 1, 0 }, { "XPON", b_xpon, 1, 1, 0 },
    { "RE", b_re, 1, 1, 0 }, { "IM", b_im, 1, 1, 0 }, { "CONJ", b_conj, 1, 1, 0 }, { "ARG", b_arg, 1, 1, 0 },
    { "MAX", b_max, 1, 2, 0 }, { "MIN", b_min, 1, 2, 0 },
    { "%", b_percent, 2, 2, 0 }, { "%CHANGE", b_pctchange, 2, 2, 0 }, { "%TOTAL", b_pcttotal, 2, 2, 0 },
    { "COMB", b_comb, 2, 2, 0 }, { "PERM", b_perm, 2, 2, 0 },
    { "Gamma", b_gamma, 1, 1, 0 },
    { "RANDOM", b_random, 0, 2, BF_NOPAREN }, { "RANDINT", b_randint, 1, 2, 0 },
    { "RANDSEED", b_randseed, 0, 1, 0 }, { "RANDNORM", b_randnorm, 0, 2, 0 },
    { "PI", b_pi, 0, 0, BF_NOPAREN }, { "MAXREAL", b_maxreal, 0, 0, BF_NOPAREN },
    { "MINREAL", b_minreal, 0, 0, BF_NOPAREN },
    { "\xE2\x86\x92HMS", b_tohms, 1, 1, 0 }, { "HMS\xE2\x86\x92", b_hmsto, 1, 1, 0 },
    { NULL, NULL, 0, 0, 0 }
};
