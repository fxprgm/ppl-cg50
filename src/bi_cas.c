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
