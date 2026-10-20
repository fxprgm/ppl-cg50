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

static BI(b_gcd)
{
    int32_t v[16], g;
    int i, st;
    (void)V;
    if (argc == 1 && a[0].t == T_LIST) return E_UNSUPPORTED;
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
    (void)V;
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
    { "gcd", b_gcd, 2, 16, 0 }, { "lcm", b_lcm, 2, 16, 0 },
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
    { NULL, NULL, 0, 0, 0 }
};
