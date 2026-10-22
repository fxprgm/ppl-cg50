/* bi_cas.c -- the CAS hook.  `CAS.name(args)` and Home's lower-case CAS
 * names first try the native integer helpers below (what PPL programs use
 * most); anything else goes to V->cas, a function a front end can set to a
 * giac library loaded at run time (IDEAS.md idea 14).  Without one the
 * call fails with "Not supported". */
#include "bi.h"
#include "decx.h"
#include <string.h>

static int int_args(int argc, value *a, int32_t *v)
{
    int i;
    for (i = 0; i < argc; i++) if (bi_int(&a[i], &v[i])) return E_BADVAL;
    return 0;
}

static int32_t gcd32(int32_t x, int32_t y)
{
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y) { int32_t t = x % y; x = y; y = t; }
    return x;
}

/* gcd/lcm with one argument (giac): of a list's or vector's elements; any other
 * value is answered as it is (gaussjordan.ppl divides a pivot by gcd(pivot);
 * assumed) */
static int one_arg(ppl_vm *V, value *a, value *out, int (*f)(ppl_vm *, int, value *, value *))
{
    const value *el;
    int n;
    value tmp[16];
    if (a[0].t == T_LIST) { el = a[0].u.l->v; n = (int)a[0].u.l->n; }
    else if (a[0].t == T_MAT && a[0].u.m->vec) { el = a[0].u.m->v; n = a[0].u.m->cols; }
    else { v_copy(out, &a[0]); return 0; }
    if (n < 1 || n > 16) return n ? E_BADVAL : E_DIM;
    memcpy(tmp, el, (size_t)n * sizeof(value));
    if (n == 1) { int32_t x; if (bi_int(&tmp[0], &x)) return E_BADVAL; v_int32(out, x < 0 ? -x : x); return 0; }
    return f(V, n, tmp, out);
}

static BI(b_gcd)
{
    int32_t v[16], g;
    int i, st;
    if (argc == 1) return one_arg(V, a, out, b_gcd);
    if (argc > 16 || (st = int_args(argc, a, v))) return E_BADVAL;
    g = v[0];
    for (i = 1; i < argc; i++) g = gcd32(g, v[i]);
    v_int32(out, g < 0 ? -g : g);
    return 0;
}

static BI(b_lcm)
{
    int32_t v[16];
    int i, st;
    real acc, t, g;
    if (argc == 1) return one_arg(V, a, out, b_lcm);
    if (argc > 16 || (st = int_args(argc, a, v))) return E_BADVAL;
    dec_from_int(&acc, v[0] < 0 ? -v[0] : v[0]);
    for (i = 1; i < argc; i++) {
        int32_t cur, gg;
        if (dec_to_int32(&acc, &cur)) return E_BADVAL;
        gg = gcd32(cur, v[i]);
        if (!gg) { dec_zero(&acc); continue; }
        dec_from_int(&t, v[i] < 0 ? -v[i] : v[i]);
        dec_from_int(&g, gg);
        dec_div(&t, &t, &g);
        dec_mul(&acc, &acc, &t);
    }
    v_real(out, &acc);
    return 0;
}

static BI(b_irem)
{
    int32_t v[2];
    (void)V; (void)argc;
    if (int_args(2, a, v)) return E_BADVAL;
    if (!v[1]) return E_DIV0;
    v_int32(out, v[0] % v[1]);   /* sign of the dividend, as giac */
    return 0;
}

static BI(b_iquo)
{
    int32_t v[2];
    (void)V; (void)argc;
    if (int_args(2, a, v)) return E_BADVAL;
    if (!v[1]) return E_DIV0;
    {
        int32_t q = v[0] / v[1];
        if ((v[0] % v[1]) && ((v[0] < 0) != (v[1] < 0)) && v[0] < 0) q -= v[1] > 0 ? 1 : -1;
        v_int32(out, q);
    }
    return 0;
}

static int is_prime32(uint32_t n)
{
    uint32_t d;
    if (n < 2) return 0;
    if (n < 4) return 1;
    if (!(n & 1) || !(n % 3)) return 0;
    for (d = 5; d <= n / d; d += 6) if (!(n % d) || !(n % (d + 2))) return 0;
    return 1;
}

static BI(b_isprime)
{
    int32_t n;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n)) return E_BADVAL;
    v_int32(out, n > 0 && is_prime32((uint32_t)n));
    return 0;
}

static BI(b_nextprime)
{
    int32_t n;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n)) return E_BADVAL;
    if (n < 2) n = 1;
    do n++; while (!is_prime32((uint32_t)n) && n < 2147483647);
    v_int32(out, n);
    return 0;
}

static BI(b_prevprime)
{
    int32_t n;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n)) return E_BADVAL;
    if (n <= 2) return E_BADVAL;
    do n--; while (n > 2 && !is_prime32((uint32_t)n));
    v_int32(out, n);
    return 0;
}

static BI(b_powmod)
{
    int32_t v[3];
    uint32_t b, e, m, r = 1;
    (void)V; (void)argc;
    if (int_args(3, a, v) || v[1] < 0 || v[2] <= 0) return E_BADVAL;
    m = (uint32_t)v[2];
    b = (uint32_t)(((v[0] % v[2]) + v[2]) % v[2]);
    e = (uint32_t)v[1];
    while (e) {
        if (e & 1) r = (uint32_t)(((uint64_t)r * b) % m);
        b = (uint32_t)(((uint64_t)b * b) % m);
        e >>= 1;
    }
    v_int32(out, (int32_t)r);
    return 0;
}

const builtin bi_cas_table[] = {
    { "gcd", b_gcd, 1, 16, 0 }, { "lcm", b_lcm, 1, 16, 0 },
    { "irem", b_irem, 2, 2, 0 }, { "iquo", b_iquo, 2, 2, 0 },
    { "isPrime", b_isprime, 1, 1, 0 }, { "isprime", b_isprime, 1, 1, 0 },
    { "nextprime", b_nextprime, 1, 1, 0 }, { "prevprime", b_prevprime, 1, 1, 0 },
    { "powmod", b_powmod, 3, 3, 0 },
    { NULL, NULL, 0, 0, 0 }
};

/* ---- CAS-side names that programs call directly ---- */

static BI(b_sqrtf)
{
    value two;
    (void)argc;
    v_int32(&two, 2);
    if (a[0].t == T_CPLX || (a[0].t == T_REAL && a[0].u.r.s && !dec_is_zero(&a[0].u.r) && V->set.complex_ok))
        return vm_sqrt(V, &a[0], out);   /* as √: sqrt((1,2)) = 1.27201964951+0.786151377757*i */
    if (a[0].t == T_REAL && a[0].u.r.s && !dec_is_zero(&a[0].u.r) && !V->set.complex_ok) return E_NONREAL;
    return ar_binary(V, OP_NTHROOT, &two, &a[0], out);
}

static BI(b_true) { (void)V; (void)argc; (void)a; v_int32(out, 1); return 0; }
static BI(b_false) { (void)V; (void)argc; (void)a; v_int32(out, 0); return 0; }

static BI(b_member)
{
    uint32_t i;
    (void)V; (void)argc;
    if (a[1].t != T_LIST) return E_BADTYPE;
    for (i = 0; i < a[1].u.l->n; i++) if (v_equal(&a[1].u.l->v[i], &a[0])) { v_int32(out, (int32_t)i + 1); return 0; }
    v_int32(out, 0);
    return 0;
}

static BI(b_approx) { (void)V; (void)argc; v_copy(out, &a[0]); return 0; }

static BI(b_cat)
{
    int i, st;
    value acc;
    v_str_utf8(&acc, "", 0);
    for (i = 0; i < argc; i++) {
        value t;
        st = ar_binary(V, OP_ADD, &acc, &a[i], &t);
        v_decref(&acc);
        if (st) return st;
        acc = t;
    }
    *out = acc;
    return 0;
}

static BI(b_rowcol)
{
    (void)argc;
    return vm_index(V, &a[0], 1, &a[1], out);
}

static BI(b_col)
{
    int32_t j;
    int i, st;
    const pmat *m;
    (void)V; (void)argc;
    if (a[0].t != T_MAT || a[0].u.m->vec) return E_BADTYPE;
    if (bi_int(&a[1], &j)) return E_BADVAL;
    m = a[0].u.m;
    if (j < 1 || j > m->cols) return E_INDEX;
    if ((st = v_mat(out, 1, m->rows, 1))) return st;
    for (i = 0; i < m->rows; i++) out->u.m->v[i] = m->v[i * m->cols + j - 1];
    return 0;
}

static BI(b_idivis)
{
    int32_t n, d;
    int st;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n) || n < 1) return E_BADVAL;
    if ((st = v_list(out, 0))) return st;
    for (d = 1; d <= n / d; d++) {
        if (n % d) continue;
        { value v; v_int32(&v, d); list_push(out->u.l, &v); }
    }
    for (d = (int32_t)out->u.l->n - 1; d >= 0; d--) {
        int32_t q;
        value v;
        to_int(&out->u.l->v[d], &q);
        if (q * q == n) continue;
        v_int32(&v, n / q);
        list_push(out->u.l, &v);
    }
    return 0;
}

static BI(b_euler)
{
    int32_t n, r, p;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n) || n < 1) return E_BADVAL;
    r = n;
    for (p = 2; p <= n / p; p++)
        if (!(n % p)) { while (!(n % p)) n /= p; r -= r / p; }
    if (n > 1) r -= r / n;
    v_int32(out, r);
    return 0;
}

/* ∫(expr, var, a, b): 10-point Gauss-Legendre on 1, 2, 4 ... 64 panels
 * until two successive results agree to 12 digits; sums in the xn engine */
static int gl_panels(ppl_vm *V, value *a, const real *lo, const real *hi, int panels, xn *res)
{
    static const char *xs[5] = { "0.1488743389816312108848260", "0.4333953941292471907992659",
                                 "0.6794095682990244062343274", "0.8650633666889845107320967",
                                 "0.9739065285171717200779640" };
    static const char *ws[5] = { "0.2955242247147528701738930", "0.2692667193099963550912269",
                                 "0.2190863625159820439955349", "0.1494513491505805931457763",
                                 "0.0666713443086881375935688" };
    xn L, H, h, half, sum, mid, t, x, w;
    int k, i, sg, st;
    xn_from_real(&L, lo); xn_from_real(&H, hi);
    xn_sub(&h, &H, &L, 30); xn_div_int(&h, &h, panels, 30);
    xn_div_int(&half, &h, 2, 30);
    xn_zero(&sum);
    for (k = 0; k < panels; k++) {
        xn_mul_int(&mid, &h, k, 30); xn_add(&mid, &mid, &L, 30); xn_add(&mid, &mid, &half, 30);
        for (i = 0; i < 5; i++)
            for (sg = -1; sg <= 1; sg += 2) {
                real xr, fr;
                value xv, fv;
                xn_from_str(&x, xs[i]); xn_from_str(&w, ws[i]);
                if (sg < 0) x.s = 1;
                xn_mul(&x, &x, &half, 30); xn_add(&x, &x, &mid, 30);
                xn_to_real(&xr, &x);
                v_real(&xv, &xr);
                if ((st = vm_store_ref(V, &a[1], &xv))) return st;
                if ((st = vm_run_thunk(V, &a[0], &fv))) return st;
                if ((st = bi_real(&fv, &fr))) { v_decref(&fv); return st; }
                xn_from_real(&t, &fr);
                xn_mul(&t, &t, &w, 30); xn_mul(&t, &t, &half, 30);
                xn_add(&sum, &sum, &t, 30);
            }
    }
    *res = sum;
    return 0;
}

static BI(b_integral)
{
    real lo, hi, r = {{0, 0, 0}, 0, 0, 0}, prev;
    value saved;
    int st = 0, p;
    xn cur;
    (void)argc;
    if (bi_real(&a[2], &lo) || bi_real(&a[3], &hi)) return E_BADTYPE;
    vm_load_ref(V, &a[1], &saved);
    dec_zero(&prev);
    for (p = 1; p <= 64; p *= 2) {
        if ((st = gl_panels(V, a, &lo, &hi, p, &cur))) break;
        xn_to_real(&r, &cur);
        if (p > 1 && dec_cmp(&r, &prev) == 0) break;
        prev = r;
    }
    vm_store_ref(V, &a[1], &saved);
    v_decref(&saved);
    if (st) return st;
    v_real(out, &r);
    return 0;
}

/* ---- exact rationals of reals: numer, denom, exact ---- */

/* the simplest fraction p/q (a continued-fraction convergent) that rounds to x
 * at 12 digits; p carries the sign.  Returns 1 when x is out of reach (its
 * denominator would pass 10^18): then p = x, q = 1. */
static int real_frac(const real *x, real *p, real *q)
{
    uint64_t M = (uint64_t)x->m[0] * 100000000u + (uint64_t)x->m[1] * 10000u + x->m[2], N, D = 1;
    uint64_t h1 = 1, h2 = 0, k1 = 0, k2 = 1, n, d;
    int e = x->e - 11, i;
    real ax;
    dec_abs(&ax, x);
    dec_from_int(q, 1);
    if (dec_is_zero(x) || x->k != DEC_FINITE) { *p = *x; return 0; }
    while (M % 10 == 0) { M /= 10; e++; }
    if (e >= 0) { *p = *x; return 0; }            /* an integer */
    if (e < -18) { *p = *x; return 1; }
    for (i = 0; i < -e; i++) D *= 10;
    N = M;
    n = N; d = D;
    while (d) {
        uint64_t aq = n / d, h = aq * h1 + h2, k = aq * k1 + k2, r = n - aq * d;
        real rh, rk, t;
        h2 = h1; h1 = h; k2 = k1; k1 = k;
        n = d; d = r;
        dec_from_u64(&rh, (uint32_t)(h >> 32), (uint32_t)h);
        dec_from_u64(&rk, (uint32_t)(k >> 32), (uint32_t)k);
        if (!d || (!dec_div(&t, &rh, &rk) && !dec_cmp(&t, &ax))) { *p = rh; *q = rk; break; }
    }
    p->s = x->s;
    return 0;
}

/* numer/denom of a real: of its simplest fraction (bernoulli.ppl builds exact
 * fractions with numer(A/B), denom(A/B) from Home reals; the Prime's CAS sees
 * them exactly, assumed) */
static int frac_part(const value *v, value *out, int den)
{
    real x, p, q;
    if (v->t == T_LIST) {
        uint32_t i;
        int st;
        if ((st = v_list(out, v->u.l->n))) return st;
        for (i = 0; i < v->u.l->n; i++) if ((st = frac_part(&v->u.l->v[i], &out->u.l->v[i], den))) { v_decref(out); return st; }
        return 0;
    }
    if (bi_real(v, &x)) return E_BADTYPE;
    real_frac(&x, &p, &q);
    v_real(out, den ? &q : &p);
    return 0;
}
static BI(b_numer) { (void)V; (void)argc; return frac_part(&a[0], out, 0); }
static BI(b_denom) { (void)V; (void)argc; return frac_part(&a[0], out, 1); }

/* exact(x): the simplest fraction, shown as a real here (gradian.ppl calls
 * approx(exact(...)); User Guide: exact(1.4141) = 14141/10000) */
static int r_exact(ppl_vm *V, const real *x, value *out)
{
    real p, q, r = {{0, 0, 0}, 0, 0, 0};
    (void)V;
    real_frac(x, &p, &q);
    return bi_fin(out, &r, dec_div(&r, &p, &q));
}
static BI(b_exact) { (void)argc; if (a[0].t == T_CPLX) { v_copy(out, &a[0]); return 0; } return ar_map1(V, &a[0], r_exact, NULL, out); }

/* ifactors(n) (giac): [p1, e1, p2, e2, ...] as a vector, primes increasing;
 * ifactors(1) = []; |n| below 10^12 */
static BI(b_ifactors)
{
    real x;
    uint32_t hi, lo;
    uint64_t n, p;
    value f, t;
    int st;
    (void)V; (void)argc;
    if (bi_real(&a[0], &x) || !dec_is_int(&x) || dec_is_zero(&x)) return E_BADVAL;
    x.s = 0;
    if (dec_to_u64(&x, &hi, &lo)) return E_BADVAL;
    n = ((uint64_t)hi << 32) | lo;
    if ((st = v_list(&f, 0))) return st;
    for (p = 2; p * p <= n; p += (p == 2 ? 1 : 2)) {
        int32_t e = 0;
        while (n % p == 0) { n /= p; e++; }
        if (e) {
            real r;
            dec_from_u64(&r, (uint32_t)(p >> 32), (uint32_t)p);
            v_real(&t, &r); list_push(f.u.l, &t);
            v_int32(&t, e); list_push(f.u.l, &t);
        }
    }
    if (n > 1) {
        real r;
        dec_from_u64(&r, (uint32_t)(n >> 32), (uint32_t)n);
        v_real(&t, &r); list_push(f.u.l, &t);
        v_int32(&t, 1); list_push(f.u.l, &t);
    }
    {
        uint32_t i, k = f.u.l->n;
        if ((st = v_mat(out, 1, (int)k, 1))) { v_decref(&f); return st; }
        for (i = 0; i < k; i++) out->u.m->v[i] = f.u.l->v[i];
    }
    v_decref(&f);
    return 0;
}

/* product(list, vector or matrix) and product(expr, var, from, to [, step]):
 * the product of the values, or of expr over var (User Guide:
 * product(n,n,1,10,2) = 945); a matrix gives the product of each column
 * (assumed, as the statistics functions) */
static BI(b_product)
{
    value v, acc;
    int st = 0;
    if (argc == 1) {
        const value *el;
        uint32_t n, i, cols = 1;
        if ((st = vm_run_thunk(V, &a[0], &v))) return st;
        if (v.t == T_LIST) { el = v.u.l->v; n = v.u.l->n; }
        else if (v.t == T_MAT) { el = v.u.m->v; cols = v.u.m->vec ? (uint32_t)v.u.m->cols : 1; n = (uint32_t)v.u.m->cols; if (!v.u.m->vec) { cols = n; n = (uint32_t)v.u.m->rows; } }
        else { *out = v; return 0; }
        if (v.t == T_MAT && !v.u.m->vec) {
            uint32_t j;
            if ((st = v_mat(out, 1, (int)cols, 1))) { v_decref(&v); return st; }
            for (j = 0; j < cols && !st; j++) {
                value c;
                v_int32(&c, 1);
                for (i = 0; i < n && !st; i++) {
                    value t;
                    if (!(st = ar_binary(V, OP_MUL, &c, &el[i * cols + j], &t))) { v_decref(&c); c = t; }
                }
                out->u.m->v[j] = c;
            }
            v_decref(&v);
            if (st) v_decref(out);
            return st;
        }
        if (v.t == T_MAT) n = cols;
        v_int32(&acc, 1);
        for (i = 0; i < n; i++) {
            value t;
            if ((st = ar_binary(V, OP_MUL, &acc, &el[i], &t))) break;
            v_decref(&acc);
            acc = t;
        }
        v_decref(&v);
        if (st) { v_decref(&acc); return st; }
        *out = acc;
        return 0;
    }
    if (argc < 4) return E_ARGC;
    {
        real x, end, step, z;
        value saved, xv;
        int up;
        uint32_t guard = 0;
        if (bi_real(&a[2], &x) || bi_real(&a[3], &end)) return E_BADTYPE;
        if (argc > 4) { if (bi_real(&a[4], &step)) return E_BADTYPE; } else dec_from_int(&step, 1);
        if (dec_is_zero(&step)) return E_BADVAL;
        dec_zero(&z);
        up = dec_cmp(&step, &z) > 0;
        vm_load_ref(V, &a[1], &saved);
        v_int32(&acc, 1);
        while (up ? dec_cmp(&x, &end) <= 0 : dec_cmp(&x, &end) >= 0) {
            value e, t;
            v_real(&xv, &x);
            if ((st = vm_store_ref(V, &a[1], &xv))) break;
            if ((st = vm_run_thunk(V, &a[0], &e))) break;
            st = ar_binary(V, OP_MUL, &acc, &e, &t);
            v_decref(&e);
            if (st) break;
            v_decref(&acc);
            acc = t;
            dec_add(&x, &x, &step);
            if (++guard > 1000000u) { st = E_MEM; break; }
        }
        vm_store_ref(V, &a[1], &saved);
        v_decref(&saved);
        if (st) { v_decref(&acc); return st; }
        *out = acc;
        return 0;
    }
}

/* giac's type numbers, which programs compare with TYPE(): listapi.ppl tests
 * TYPE(x) == DOM_FLOAT-1 (a real) and DOM_LIST-1 (a list) */
static BI(b_dom_int) { (void)V; (void)argc; (void)a; v_int32(out, 0); return 0; }
static BI(b_dom_float) { (void)V; (void)argc; (void)a; v_int32(out, 1); return 0; }
static BI(b_dom_complex) { (void)V; (void)argc; (void)a; v_int32(out, 4); return 0; }
static BI(b_dom_ident) { (void)V; (void)argc; (void)a; v_int32(out, 6); return 0; }
static BI(b_dom_list) { (void)V; (void)argc; (void)a; v_int32(out, 7); return 0; }
static BI(b_dom_symbolic) { (void)V; (void)argc; (void)a; v_int32(out, 8); return 0; }
static BI(b_dom_rat) { (void)V; (void)argc; (void)a; v_int32(out, 10); return 0; }
static BI(b_dom_string) { (void)V; (void)argc; (void)a; v_int32(out, 12); return 0; }
static BI(b_dom_func) { (void)V; (void)argc; (void)a; v_int32(out, 13); return 0; }

/* the two coordinates of a point given as a complex, a real, a 2-vector or two
 * reals */
static int point2(int argc, const value *a, real *x, real *y)
{
    if (argc == 2) return (bi_real(&a[0], x) || bi_real(&a[1], y)) ? E_BADTYPE : 0;
    if (a[0].t == T_CPLX) { *x = a[0].u.c.re; *y = a[0].u.c.im; return 0; }
    if (a[0].t == T_MAT && a[0].u.m->vec && a[0].u.m->cols == 2)
        return (bi_real(&a[0].u.m->v[0], x) || bi_real(&a[0].u.m->v[1], y)) ? E_BADTYPE : 0;
    if (bi_real(&a[0], x)) return E_BADTYPE;
    dec_zero(y);
    return 0;
}

static int vec2(value *out, const real *x, const real *y)
{
    int st;
    if ((st = v_mat(out, 1, 2, 1))) return st;
    v_real(&out->u.m->v[0], x);
    v_real(&out->u.m->v[1], y);
    return 0;
}

/* polar_coordinates(z), (x, y) or ([x, y]): [r, θ], θ in the current angle mode
 * (conformance: polar_coordinates(point(3,4)) = [5,0.927295218002] in radians;
 * mathlib.ppl expects degrees in degree mode) */
static BI(b_polar)
{
    real x, y, r, t, th;
    int st;
    if ((st = point2(argc, a, &x, &y))) return st;
    if ((st = dec_err(dec_mul(&r, &x, &x))) || (st = dec_err(dec_mul(&t, &y, &y))) ||
        (st = dec_err(dec_add(&r, &r, &t))) || (st = dec_err(dec_sqrt(&r, &r)))) return st;
    if (dec_is_zero(&x) && dec_is_zero(&y)) dec_zero(&th);
    else if ((st = dec_err(dec_atan2(&th, &y, &x, V->set.angle)))) return st;
    return vec2(out, &r, &th);
}

/* rectangular_coordinates([r, θ]) or (r, θ): [x, y]; a complex is already a
 * point: [re, im] (deltawye.ppl passes impedances) */
static BI(b_rect)
{
    real r, th, x, y;
    int st;
    if (argc == 1 && a[0].t == T_CPLX) return vec2(out, &a[0].u.c.re, &a[0].u.c.im);
    if ((st = point2(argc, a, &r, &th))) return st;
    if ((st = dec_err(dec_cos(&x, &th, V->set.angle))) || (st = dec_err(dec_sin(&y, &th, V->set.angle))) ||
        (st = dec_err(dec_mul(&x, &x, &r))) || (st = dec_err(dec_mul(&y, &y, &r)))) return st;
    return vec2(out, &x, &y);
}

/* append(list or vector, x): x added at the end; head/tail: the first element,
 * all but the first (giac; cashflow.ppl) */
static BI(b_append)
{
    int st;
    (void)V; (void)argc;
    if (a[0].t == T_LIST) {
        plist *l;
        v_copy(out, &a[0]);
        if (!(l = list_own(out))) { v_decref(out); return E_MEM; }
        if ((st = list_push(l, &a[1]))) { v_decref(out); return st; }
        return 0;
    }
    if (a[0].t == T_MAT && a[0].u.m->vec) {
        int i, n = a[0].u.m->cols;
        if (a[1].t != T_REAL && a[1].t != T_CPLX && a[1].t != T_INT) return E_BADTYPE;
        if ((st = v_mat(out, 1, n + 1, 1))) return st;
        for (i = 0; i < n; i++) out->u.m->v[i] = a[0].u.m->v[i];
        v_copy(&out->u.m->v[n], &a[1]);
        if (a[1].t == T_INT) { real x; to_real(&a[1], &x); v_real(&out->u.m->v[n], &x); }
        return 0;
    }
    if (a[0].t == T_MAT && a[1].t == T_MAT && a[1].u.m->vec) {
        /* a vector appended to a matrix is a new row (giac; truss: append(intfor,d[1])) */
        const pmat *m = a[0].u.m;
        int n = m->rows * m->cols;
        if (a[1].u.m->cols != m->cols) return E_DIM;
        if ((st = v_mat(out, m->rows + 1, m->cols, 0))) return st;
        memcpy(out->u.m->v, m->v, (size_t)n * sizeof(value));
        memcpy(out->u.m->v + n, a[1].u.m->v, (size_t)m->cols * sizeof(value));
        return 0;
    }
    return E_BADTYPE;
}

static int seq_elems(const value *v, const value **el, uint32_t *n)
{
    if (v->t == T_LIST) { *el = v->u.l->v; *n = v->u.l->n; return 0; }
    if (v->t == T_MAT && v->u.m->vec) { *el = v->u.m->v; *n = (uint32_t)v->u.m->cols; return 0; }
    return E_BADTYPE;
}

static BI(b_head)
{
    const value *el;
    uint32_t n;
    (void)V; (void)argc;
    if (a[0].t == T_STR) { if (!a[0].u.s->len) return E_DIM; return v_str_u16(out, a[0].u.s->c, 1); }
    if (seq_elems(&a[0], &el, &n)) return E_BADTYPE;
    if (!n) return E_DIM;
    v_copy(out, &el[0]);
    return 0;
}

static BI(b_tail)
{
    const value *el;
    uint32_t n, i;
    int st;
    (void)V; (void)argc;
    if (a[0].t == T_STR) return v_str_u16(out, a[0].u.s->c + (a[0].u.s->len ? 1 : 0), a[0].u.s->len ? a[0].u.s->len - 1 : 0);
    if (seq_elems(&a[0], &el, &n)) return E_BADTYPE;
    if (!n) return E_DIM;
    if (a[0].t == T_LIST) {
        if ((st = v_list(out, 0))) return st;
        for (i = 1; i < n; i++) if ((st = list_push(out->u.l, &el[i]))) { v_decref(out); return st; }
        return 0;
    }
    if ((st = v_mat(out, 1, (int)n - 1, 1))) return st;
    for (i = 1; i < n; i++) out->u.m->v[i - 1] = el[i];
    return 0;
}

/* sto(value, var): stores the value, answers it (giac; cashflow.ppl) */
static BI(b_sto)
{
    int st;
    (void)argc;
    if ((st = vm_store_ref(V, &a[1], &a[0]))) return st;
    v_copy(out, &a[0]);
    return 0;
}

extern int bi_fnroot_impl(ppl_vm *V, int argc, value *a, value *out);

const builtin bi_cas2_table[] = {
    { "sqrt", b_sqrtf, 1, 1, 0 }, { "SQRT", b_sqrtf, 1, 1, 0 },
    { "TRUE", b_true, 0, 0, BF_NOPAREN }, { "true", b_true, 0, 0, BF_NOPAREN },
    { "FALSE", b_false, 0, 0, BF_NOPAREN }, { "false", b_false, 0, 0, BF_NOPAREN },
    { "member", b_member, 2, 2, 0 }, { "approx", b_approx, 1, 1, 0 }, { "evalf", b_approx, 1, 2, 0 },
    { "cat", b_cat, 1, -1, 0 }, { "row", b_rowcol, 2, 2, 0 }, { "col", b_col, 2, 2, 0 },
    { "idivis", b_idivis, 1, 1, 0 }, { "euler", b_euler, 1, 1, 0 },
    { "\xE2\x88\xAB", b_integral, 4, 4, BF_QUOTE1 | BF_QUOTE2 },     /* ∫ */
    { "fsolve", bi_fnroot_impl, 3, 3, BF_QUOTE1 | BF_QUOTE2 },
    { "int", b_integral, 4, 4, BF_QUOTE1 | BF_QUOTE2 },              /* CAS integrate */
    { "numer", b_numer, 1, 1, 0 }, { "denom", b_denom, 1, 1, 0 }, { "exact", b_exact, 1, 1, 0 },
    { "ifactors", b_ifactors, 1, 1, 0 },
    { "product", b_product, 1, 5, BF_QUOTE1 | BF_QUOTE2 },
    { "polar_coordinates", b_polar, 1, 2, 0 }, { "rectangular_coordinates", b_rect, 1, 2, 0 },
    { "DOM_INT", b_dom_int, 0, 0, BF_NOPAREN }, { "DOM_FLOAT", b_dom_float, 0, 0, BF_NOPAREN },
    { "DOM_COMPLEX", b_dom_complex, 0, 0, BF_NOPAREN }, { "DOM_IDENT", b_dom_ident, 0, 0, BF_NOPAREN },
    { "DOM_LIST", b_dom_list, 0, 0, BF_NOPAREN }, { "DOM_SYMBOLIC", b_dom_symbolic, 0, 0, BF_NOPAREN },
    { "DOM_RAT", b_dom_rat, 0, 0, BF_NOPAREN }, { "DOM_STRING", b_dom_string, 0, 0, BF_NOPAREN },
    { "DOM_FUNC", b_dom_func, 0, 0, BF_NOPAREN },
    { "append", b_append, 2, 2, 0 }, { "head", b_head, 1, 1, 0 }, { "tail", b_tail, 1, 1, 0 },
    { "sto", b_sto, 2, 2, BF_QUOTE2 },
    { NULL, NULL, 0, 0, 0 }
};
