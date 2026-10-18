/* bi_misc.c -- # integers, program and system built-ins, I/O, and the
 * plug-in point for graphics (stubs until the gint front end exists). */
#include "bi.h"
#include "decx.h"
#include <string.h>

/* ------------------------------------------------------------ integers */

static uint64_t u64of(const value *v)
{
    if (v->t == T_INT) return ((uint64_t)v->u.i.hi << 32) | v->u.i.lo;
    {
        uint32_t hi = 0, lo = 0;
        dec_to_u64(&v->u.r, &hi, &lo);
        return ((uint64_t)hi << 32) | lo;
    }
}

static void set_int(value *out, uint64_t x, int base, int bits)
{
    v_none(out);
    out->t = T_INT; out->ib = (uint8_t)base; out->iw = (uint8_t)bits;
    out->u.i.hi = (uint32_t)(x >> 32); out->u.i.lo = (uint32_t)x;
}

static void set_real_u64(value *out, uint64_t x)
{
    real r;
    dec_from_u64(&r, (uint32_t)(x >> 32), (uint32_t)x);
    v_real(out, &r);
}

/* bit operations: on # integers within their word size; on reals within 39
 * bits (BITNOT(47) is 549755813840 on the Prime: 2^39-1-47) */
#define REALBITS 39

static int bitw(ppl_vm *V, const value *v) { return v->t == T_INT ? (v->iw ? v->iw : V->set.bits) : REALBITS; }
static uint64_t wmask(int w) { return w >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1); }

static int bit_result(ppl_vm *V, const value *proto, uint64_t x, value *out)
{
    int w = bitw(V, proto);
    x &= wmask(w);
    if (proto->t == T_INT) { set_int(out, x, proto->ib, proto->iw); return 0; }
    set_real_u64(out, x);
    return 0;
}

static int check_bitarg(const value *v)
{
    if (v->t == T_INT) return 0;
    if (v->t != T_REAL) return E_BADTYPE;
    if (v->u.r.s && !dec_is_zero(&v->u.r)) return E_BADVAL;
    return 0;
}

static int bitop(ppl_vm *V, int argc, value *a, value *out, int op)
{
    uint64_t x;
    int i;
    for (i = 0; i < argc; i++) if (check_bitarg(&a[i])) return E_BADTYPE;
    x = u64of(&a[0]);
    for (i = 1; i < argc; i++) {
        uint64_t y = u64of(&a[i]);
        x = op == 0 ? (x & y) : op == 1 ? (x | y) : (x ^ y);
    }
    return bit_result(V, &a[0], x, out);
}
static BI(b_bitand) { return bitop(V, argc, a, out, 0); }
static BI(b_bitor) { return bitop(V, argc, a, out, 1); }
static BI(b_bitxor) { return bitop(V, argc, a, out, 2); }
static BI(b_bitnot) { (void)argc; if (check_bitarg(&a[0])) return E_BADTYPE; return bit_result(V, &a[0], ~u64of(&a[0]), out); }
static int shift(ppl_vm *V, int argc, value *a, value *out, int left)
{
    int32_t n = 1;
    uint64_t x;
    if (check_bitarg(&a[0])) return E_BADTYPE;
    if (argc > 1 && bi_int(&a[1], &n)) return E_BADVAL;
    x = u64of(&a[0]);
    if (n < 0) { n = -n; left = !left; }
    if (n >= 64) x = 0; else x = left ? x << n : x >> n;
    return bit_result(V, &a[0], x, out);
}
static BI(b_bitsl) { return shift(V, argc, a, out, 1); }
static BI(b_bitsr) { return shift(V, argc, a, out, 0); }

static BI(b_rtob)
{
    real r;
    uint64_t x;
    int w;
    (void)argc;
    if (a[0].t == T_INT) { v_copy(out, &a[0]); return 0; }
    if (bi_real(&a[0], &r)) return E_BADTYPE;
    {
        uint32_t hi, lo;
        real t;
        dec_abs(&t, &r);
        if (dec_to_u64(&t, &hi, &lo)) return E_BADVAL;
        x = ((uint64_t)hi << 32) | lo;
        if (r.s) x = (uint64_t)0 - x;
    }
    w = V->set.bits;
    if (w < 64 && x > wmask(w) && !r.s) x = wmask(w);   /* saturates: R->B(300) in 8 bits is #FFh */
    set_int(out, x, B_DEFAULT, 0);
    int_mask(V, out);
    return 0;
}
static BI(b_btor)
{
    value m;
    (void)argc;
    if (a[0].t != T_INT) { if (a[0].t == T_REAL) { v_copy(out, &a[0]); return 0; } return E_BADTYPE; }
    m = a[0];
    int_mask(V, &m);
    set_real_u64(out, ((uint64_t)m.u.i.hi << 32) | m.u.i.lo);
    return 0;
}
static BI(b_setbase)
{
    int32_t b;
    (void)argc;
    if (bi_int(&a[1], &b) || b < 0 || b > 4) return E_BADVAL;
    if (a[0].t == T_INT) { *out = a[0]; out->ib = (uint8_t)b; return 0; }
    {
        int st = b_rtob(V, 1, a, out);
        if (!st) out->ib = (uint8_t)b;
        return st;
    }
}
static BI(b_getbase)
{
    (void)argc;
    if (a[0].t != T_INT) return E_BADTYPE;
    {
        int b = a[0].ib;
        if (!b) b = V->set.base + 1;
        set_int(out, (uint64_t)b, a[0].ib, 0);
    }
    return 0;
}
static BI(b_setbits)
{
    int32_t n = V->set.bits;
    if (argc > 1 && (bi_int(&a[1], &n) || n < -63 || n > 64)) return E_BADVAL;
    if (n < 0) n = -n;
    if (n == 0) n = V->set.bits;
    if (a[0].t == T_INT) *out = a[0];
    else { int st = b_rtob(V, 1, a, out); if (st) return st; }
    out->iw = (uint8_t)n;
    int_mask(V, out);
    return 0;
}
static BI(b_getbits)
{
    (void)argc;
    if (a[0].t != T_INT) return E_BADTYPE;
    v_int32(out, a[0].iw ? a[0].iw : V->set.bits);
    return 0;
}

/* ------------------------------------------------------------ program */

static BI(b_type)
{
    (void)V; (void)argc;
    v_int32(out, a[0].t < T_THUNK ? a[0].t : 0);
    return 0;
}

static BI(b_ticks)
{
    (void)argc; (void)a;
    v_int32(out, V->ticks ? (int32_t)(V->ticks() & 0x7FFFFFFF) : 0);
    return 0;
}

static BI(b_wait)
{
    (void)argc; (void)a; (void)V;
    v_int32(out, 0);
    return 0;
}

static BI(b_getkey)
{
    (void)argc; (void)a;
    v_int32(out, V->getkey ? V->getkey() : -1);
    return 0;
}

static BI(b_iskeydown) { (void)V; (void)argc; (void)a; v_int32(out, 0); return 0; }

static BI(b_print)
{
    if (argc && V->print) {
        sbuf s;
        sb_init(&s);
        v_to_text(V, &s, &a[0], 0);
        V->print(V, s.p ? s.p : "");
        sb_free(&s);
    } else if (!argc && V->print) V->print(V, NULL);   /* PRINT(): clear */
    v_int32(out, 0);
    return 0;
}

static BI(b_msgbox)
{
    int st = b_print(V, argc ? 1 : 0, a, out);
    v_int32(out, 1);
    return st;
}

static BI(b_unsupported) { (void)V; (void)argc; (void)a; (void)out; return E_UNSUPPORTED; }

static BI(b_ifte)
{
    int t, st;
    (void)V; (void)argc;
    if ((st = v_truth(&a[0], &t))) return st;
    v_copy(out, t ? &a[1] : &a[2]);
    return 0;
}

static BI(b_piecewise)
{
    int i, t, st;
    (void)V;
    for (i = 0; i + 1 < argc; i += 2) {
        if ((st = v_truth(&a[i], &t))) return st;
        if (t) { v_copy(out, &a[i + 1]); return 0; }
    }
    if (argc & 1) { v_copy(out, &a[argc - 1]); return 0; }
    return E_UNDEFRES;
}

static BI(b_eval) { (void)V; (void)argc; v_copy(out, &a[0]); return 0; }

static BI(b_version)
{
    (void)V; (void)argc; (void)a;
    return v_str_utf8(out, "PPL for fx-CG50 0.1", 19);
}

/* CAS: the hook for a future giac library (IDEAS.md idea 14).  Integer
 * helpers that programs use most are done natively in bi_list.c. */
static BI(b_cas) { (void)V; (void)argc; (void)a; (void)out; return E_UNSUPPORTED; }

/* FNROOT(expr, var, guess): secant method on the expression */
static BI(b_fnroot)
{
    value x0v, x1v, f0v, f1v, saved;
    real x0, x1, f0, f1, t, d;
    int st, it;
    if (argc < 3) return E_ARGC;
    if (bi_real(&a[2], &x0)) return E_BADTYPE;
    vm_load_ref(V, &a[1], &saved);
    {
        real h, one;
        dec_from_str(&h, "0.001", NULL);
        dec_from_int(&one, 1);
        dec_abs(&t, &x0);
        if (dec_cmp(&t, &one) > 0) dec_mul(&h, &h, &t);
        dec_add(&x1, &x0, &h);
    }
    v_real(&x0v, &x0);
    if ((st = vm_store_ref(V, &a[1], &x0v))) goto done;
    if ((st = vm_run_thunk(V, &a[0], &f0v))) goto done;
    if ((st = bi_real(&f0v, &f0))) { v_decref(&f0v); goto done; }
    for (it = 0; it < 200; it++) {
        v_real(&x1v, &x1);
        if ((st = vm_store_ref(V, &a[1], &x1v))) goto done;
        if ((st = vm_run_thunk(V, &a[0], &f1v))) goto done;
        if ((st = bi_real(&f1v, &f1))) { v_decref(&f1v); goto done; }
        if (dec_is_zero(&f1)) break;
        dec_sub(&d, &f1, &f0);
        if (dec_is_zero(&d)) break;
        dec_sub(&t, &x1, &x0);
        dec_mul(&t, &t, &f1);
        dec_div(&t, &t, &d);
        x0 = x1; f0 = f1;
        dec_sub(&x1, &x1, &t);
        if (dec_cmp(&x1, &x0) == 0) break;
    }
    v_real(out, &x1);
    st = 0;
done:
    vm_store_ref(V, &a[1], &saved);
    v_decref(&saved);
    return st;
}

/* ------------------------------------------------------------ dates */

static int days_from_civil(int y, int m, int d)
{
    int era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int z, int *y, int *m, int *d)
{
    int era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y += *m <= 2;
}

/* YYYY.MMDD -> day number */
static int date_days(const value *v, int *days)
{
    real r, t, h;
    int32_t ymd;
    if (bi_real(v, &r)) return E_BADTYPE;
    dec_from_int(&h, 10000);
    dec_mul(&t, &r, &h);
    if (dec_to_int32(&t, &ymd)) return E_BADVAL;
    {
        int y = ymd / 10000, m = (ymd / 100) % 100, d = ymd % 100;
        if (m < 1 || m > 12 || d < 1 || d > 31) return E_BADVAL;
        *days = days_from_civil(y, m, d);
    }
    return 0;
}

static BI(b_ddays)
{
    int d1, d2, st;
    (void)V; (void)argc;
    if ((st = date_days(&a[0], &d1)) || (st = date_days(&a[1], &d2))) return st;
    v_int32(out, d2 - d1);
    return 0;
}

static BI(b_dateadd)
{
    int d, st, y, m, dd;
    int32_t n;
    real r, h;
    (void)V; (void)argc;
    if ((st = date_days(&a[0], &d))) return st;
    if (bi_int(&a[1], &n)) return E_BADVAL;
    civil_from_days(d + n, &y, &m, &dd);
    dec_from_int(&r, y * 10000 + m * 100 + dd);
    dec_from_int(&h, 10000);
    dec_div(&r, &r, &h);
    v_real(out, &r);
    return 0;
}

static BI(b_dayofweek)
{
    int d, st, w;
    (void)V; (void)argc;
    if ((st = date_days(&a[0], &d))) return st;
    /* 1970-01-01 was a Thursday; the Prime numbers Monday 1 .. Sunday 7 */
    w = ((d % 7) + 7 + 3) % 7 + 1;
    v_int32(out, w);
    return 0;
}

/* ------------------------------------------------------------ graphics */

/* Drawing commands answer 1 on the Prime.  Until the gint front end
 * supplies a drawing back end they do nothing here and answer 1, so that
 * computing programs run unchanged. */
static BI(b_draw1) { (void)V; (void)argc; (void)a; v_int32(out, 1); return 0; }

static BI(b_rgb)
{
    int32_t r, g, b, al = 0;
    (void)V;
    if (bi_int(&a[0], &r) || bi_int(&a[1], &g) || bi_int(&a[2], &b)) return E_BADVAL;
    if (argc > 3 && bi_int(&a[3], &al)) return E_BADVAL;
    set_int(out, ((uint64_t)(al & 255) << 24) | ((uint64_t)(r & 255) << 16) | ((uint64_t)(g & 255) << 8) | (uint64_t)(b & 255), B_HEX, 0);
    out->ib = B_DEFAULT;
    return 0;
}

/* C->PX and PX->C use the Function app's default window: x from -16,
 * y down from 10.9, 0.1 per pixel (C->PX(0,0) is {160,109} on the Prime) */
static int plot_xy(const value *a, int argc, real *x, real *y)
{
    if (argc < 2) { dec_zero(x); dec_zero(y); return 1; }
    if (bi_real(&a[0], x) || bi_real(&a[1], y)) return -1;
    return 0;
}
static BI(b_ctopx)
{
    real x, y, t, k;
    int st = plot_xy(a, argc, &x, &y);
    (void)V;
    if (st < 0) return E_BADTYPE;
    if ((st = v_list(out, 2))) return st;
    if (argc < 2) return 0;
    dec_from_int(&k, 16); dec_add(&t, &x, &k);
    dec_from_int(&k, 10); dec_mul(&t, &t, &k); dec_floor(&t, &t);
    v_real(&out->u.l->v[0], &t);
    dec_from_str(&k, "10.9", NULL); dec_sub(&t, &k, &y);
    dec_from_int(&k, 10); dec_mul(&t, &t, &k); dec_floor(&t, &t);
    v_real(&out->u.l->v[1], &t);
    return 0;
}
static BI(b_pxtoc)
{
    real x, y, t, k;
    int st = plot_xy(a, argc, &x, &y);
    (void)V;
    if (st < 0) return E_BADTYPE;
    if ((st = v_list(out, 2))) return st;
    dec_from_int(&k, 10);
    dec_div(&t, &x, &k); dec_from_int(&k, 16); dec_sub(&t, &t, &k);
    v_real(&out->u.l->v[0], &t);
    dec_from_int(&k, 10);
    dec_div(&t, &y, &k); dec_from_str(&k, "10.9", NULL); dec_sub(&t, &k, &t);
    v_real(&out->u.l->v[1], &t);
    return 0;
}

static BI(b_heaviside)
{
    real x;
    (void)V; (void)argc;
    if (bi_real(&a[0], &x)) return E_BADTYPE;
    v_int32(out, (x.s && !dec_is_zero(&x)) ? 0 : 1);
    return 0;
}

static BI(b_mouse)
{
    value l;
    int st;
    (void)V; (void)argc; (void)a;
    if ((st = v_list(out, 2))) return st;
    v_list(&l, 0); out->u.l->v[0] = l;
    v_list(&l, 0); out->u.l->v[1] = l;
    return 0;
}

const builtin bi_misc_table[] = {
    { "BITAND", b_bitand, 2, -1, 0 }, { "BITOR", b_bitor, 2, -1, 0 }, { "BITXOR", b_bitxor, 2, -1, 0 },
    { "BITNOT", b_bitnot, 1, 1, 0 }, { "BITSL", b_bitsl, 1, 2, 0 }, { "BITSR", b_bitsr, 1, 2, 0 },
    { "R\xE2\x86\x92" "B", b_rtob, 1, 1, 0 }, { "B\xE2\x86\x92R", b_btor, 1, 1, 0 },
    { "SETBASE", b_setbase, 2, 2, 0 }, { "GETBASE", b_getbase, 1, 1, 0 },
    { "SETBITS", b_setbits, 1, 2, 0 }, { "GETBITS", b_getbits, 1, 1, 0 },
    { "TYPE", b_type, 1, 1, 0 }, { "TICKS", b_ticks, 0, 0, BF_NOPAREN },
    { "WAIT", b_wait, 0, 1, 0 }, { "GETKEY", b_getkey, 0, 0, BF_NOPAREN },
    { "ISKEYDOWN", b_iskeydown, 1, 1, 0 }, { "PRINT", b_print, 0, 1, 0 },
    { "MSGBOX", b_msgbox, 1, 3, 0 },
    { "INPUT", b_unsupported, 1, -1, 0 }, { "CHOOSE", b_unsupported, 2, -1, 0 },
    { "EDITLIST", b_unsupported, 1, 1, 0 }, { "EDITMAT", b_unsupported, 1, 3, 0 },
    { "STARTAPP", b_unsupported, 1, 1, 0 }, { "STARTVIEW", b_unsupported, 1, 2, 0 },
    { "IFTE", b_ifte, 3, 3, 0 }, { "PIECEWISE", b_piecewise, 1, -1, 0 },
    { "EVAL", b_eval, 1, 1, 0 }, { "VERSION", b_version, 0, 0, BF_NOPAREN },
    { "CAS", b_cas, 1, -1, 0 },
    { "FNROOT", b_fnroot, 3, 3, BF_QUOTE1 | BF_QUOTE2 },
    { "DDAYS", b_ddays, 2, 2, 0 }, { "DATEADD", b_dateadd, 2, 2, 0 }, { "DAYOFWEEK", b_dayofweek, 1, 1, 0 },
    { "RGB", b_rgb, 3, 4, 0 },
    { "C\xE2\x86\x92PX", b_ctopx, 0, 2, 0 }, { "PX\xE2\x86\x92" "C", b_pxtoc, 0, 2, 0 },
    { "Heaviside", b_heaviside, 1, 1, 0 }, { "MOUSE", b_mouse, 0, 1, BF_NOPAREN },
    { "FREEZE", b_draw1, 0, 0, BF_NOPAREN }, { "DRAWMENU", b_draw1, 0, -1, 0 },
    { "RECT", b_draw1, 0, -1, 0 }, { "RECT_P", b_draw1, 0, -1, 0 },
    { "LINE", b_draw1, 0, -1, 0 }, { "LINE_P", b_draw1, 0, -1, 0 },
    { "ARC", b_draw1, 0, -1, 0 }, { "ARC_P", b_draw1, 0, -1, 0 },
    { "PIXON", b_draw1, 0, -1, 0 }, { "PIXON_P", b_draw1, 0, -1, 0 },
    { "PIXOFF", b_draw1, 0, -1, 0 }, { "PIXOFF_P", b_draw1, 0, -1, 0 },
    { "TEXTOUT", b_draw1, 0, -1, 0 }, { "TEXTOUT_P", b_draw1, 0, -1, 0 },
    { "BLIT", b_draw1, 0, -1, 0 }, { "BLIT_P", b_draw1, 0, -1, 0 },
    { "DIMGROB", b_draw1, 0, -1, 0 }, { "DIMGROB_P", b_draw1, 0, -1, 0 },
    { "FILLPOLY", b_draw1, 0, -1, 0 }, { "FILLPOLY_P", b_draw1, 0, -1, 0 },
    { "TRIANGLE", b_draw1, 0, -1, 0 }, { "TRIANGLE_P", b_draw1, 0, -1, 0 },
    { "INVERT", b_draw1, 0, -1, 0 }, { "INVERT_P", b_draw1, 0, -1, 0 },
    { NULL, NULL, 0, 0, 0 }
};
