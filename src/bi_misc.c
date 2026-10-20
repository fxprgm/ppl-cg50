/* bi_misc.c -- # integers, program and system built-ins, I/O.  The drawing
 * commands are in bi_gfx.c. */
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
    if (a[0].t == T_LIST) {
        /* element by element (slider: B→R(MOUSE()), a list of lists) */
        uint32_t k, n = a[0].u.l->n;
        int st;
        if ((st = v_list(out, n))) return st;
        for (k = 0; k < n; k++) {
            value r;
            if ((st = b_btor(V, 1, &a[0].u.l->v[k], &r))) { v_decref(out); return st; }
            v_decref(&out->u.l->v[k]);
            out->u.l->v[k] = r;
        }
        return 0;
    }
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
    if (argc < 2) b = 0;
    else if (bi_int(&a[1], &b) || b < 0 || b > 4) return E_BADVAL;
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
    v_int32(out, a[0].t < T_THUNK ? a[0].t : a[0].t == T_GROB ? 8 : 0);
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

/* STARTVIEW(n [,draw]): no app views here; answer 0 so the program goes on
 * (STARTVIEW(-1), Home, is what programs call most) */
static BI(b_startview) { (void)V; (void)argc; (void)a; v_int32(out, 0); return 0; }

/* EDITLIST/EDITMAT(var [,title [,read only]]): no editor in the core; answer the
 * list or matrix unchanged.  A front end with an editor replaces these entries
 * through bi_set_host_table() (same names). */
static BI(b_editlist)
{
    (void)V; (void)argc;
    if (a[0].t != T_LIST && !(a[0].t == T_MAT && a[0].u.m->vec)) return E_BADTYPE;
    v_copy(out, &a[0]);
    return 0;
}

static BI(b_editmat)
{
    (void)V; (void)argc;
    if (a[0].t != T_MAT) return E_BADTYPE;
    v_copy(out, &a[0]);
    return 0;
}

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
int bi_fnroot_impl(ppl_vm *V, int argc, value *a, value *out);
int bi_fnroot_impl(ppl_vm *V, int argc, value *a, value *out)
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
        static const uint8_t mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int y = ymd / 10000, m = (ymd / 100) % 100, d = ymd % 100;
        int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        /* a Gregorian date, 1582.1015 .. 9999.1231 (User Guide, date calculations);
         * 29 February only in a leap year */
        if (ymd < 15821015 || ymd > 99991231 || m < 1 || m > 12 || d < 1 || d > mdays[m - 1] ||
            (m == 2 && d == 29 && !leap)) return E_BADVAL;
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
    if (y > 9999 || y * 10000 + m * 100 + dd < 15821015) return E_BADVAL;
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
    /* an invalid date answers -1, not an error: calendar.ppl (jmollera) tests
     * DAYOFWEEK(y+0.0229) > 0 for a leap year and DAYOFWEEK(d) < 0 for an invalid
     * date (assumed: the exact negative value is not measured) */
    if ((st = date_days(&a[0], &d))) { if (st != E_BADVAL) return st; v_int32(out, -1); return 0; }
    /* 1970-01-01 was a Thursday; the Prime numbers Monday 1 .. Sunday 7 */
    w = ((d % 7) + 7 + 3) % 7 + 1;
    v_int32(out, w);
    return 0;
}

/* ------------------------------------------------------------ graphics */

/* CHECK/UNCHECK (menu items, the input side): answer 0 for now */
static BI(b_check) { (void)V; (void)argc; (void)a; v_int32(out, 0); return 0; }

static BI(b_lengthf)
{
    (void)V; (void)argc;
    if (a[0].t == T_LIST) { v_int32(out, (int32_t)a[0].u.l->n); return 0; }
    if (a[0].t == T_STR) { v_int32(out, (int32_t)a[0].u.s->len); return 0; }
    if (a[0].t == T_MAT) { v_int32(out, a[0].u.m->vec ? a[0].u.m->cols : a[0].u.m->rows); return 0; }
    v_int32(out, 1);
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

static BI(b_dirac)
{
    real x;
    (void)V; (void)argc;
    if (bi_real(&a[0], &x)) return E_BADTYPE;
    if (dec_is_zero(&x)) { dec_from_int(&x, 1); x.k = DEC_INF; v_real(out, &x); return 0; }
    v_int32(out, 0);
    return 0;
}

/* units (2_m, 1_(ft/s)) are parsed but not supported yet */
static BI(b_deg)
{
    real x, r = {{0, 0, 0}, 0, 0, 0};
    (void)argc;
    if (bi_real(&a[0], &x)) return E_BADTYPE;
    if (V->set.angle == ANG_DEG) { v_real(out, &x); return 0; }
    if (V->set.angle == ANG_RAD) return bi_fin(out, &r, dec_angle_to_rad(&r, &x, ANG_DEG));
    {   /* gradians: x * 10 / 9 */
        real k;
        dec_from_int(&k, 10); dec_mul(&x, &x, &k);
        dec_from_int(&k, 9);
        dec_zero(&r);
        return bi_fin(out, &r, dec_div(&r, &x, &k));
    }
}

static BI(b_unit) { (void)V; (void)argc; (void)a; (void)out; return E_UNSUPPORTED; }

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
    { "SETBASE", b_setbase, 1, 2, 0 }, { "GETBASE", b_getbase, 1, 1, 0 },
    { "SETBITS", b_setbits, 1, 2, 0 }, { "GETBITS", b_getbits, 1, 1, 0 },
    { "TYPE", b_type, 1, 1, 0 }, { "TICKS", b_ticks, 0, 0, BF_NOPAREN },
    { "WAIT", b_wait, 0, 1, 0 }, { "GETKEY", b_getkey, 0, 0, BF_NOPAREN },
    { "ISKEYDOWN", b_iskeydown, 1, 1, 0 }, { "PRINT", b_print, 0, 1, 0 },
    { "MSGBOX", b_msgbox, 1, 3, 0 },
    { "INPUT", b_unsupported, 1, -1, BF_REF1 }, { "CHOOSE", b_unsupported, 2, -1, BF_REF1 },
    { "EDITLIST", b_editlist, 1, 3, 0 }, { "EDITMAT", b_editmat, 1, 3, 0 },
    { "STARTAPP", b_unsupported, 1, 1, 0 }, { "STARTVIEW", b_startview, 1, 2, 0 },
    { "IFTE", b_ifte, 3, 3, 0 }, { "PIECEWISE", b_piecewise, 1, -1, 0 },
    { "EVAL", b_eval, 1, 1, 0 }, { "VERSION", b_version, 0, 0, BF_NOPAREN },
    { "CAS", b_cas, 1, -1, 0 },
    { "FNROOT", bi_fnroot_impl, 3, 3, BF_QUOTE1 | BF_QUOTE2 },
    { "DDAYS", b_ddays, 2, 2, 0 }, { "DATEADD", b_dateadd, 2, 2, 0 }, { "DAYOFWEEK", b_dayofweek, 1, 1, 0 },
    { "_UNIT", b_unit, 2, 2, 0 }, { "_DEG", b_deg, 1, 1, 0 }, { "Heaviside", b_heaviside, 1, 1, 0 }, { "Dirac", b_dirac, 1, 1, 0 }, { "MOUSE", b_mouse, 0, 1, BF_NOPAREN },
    { "CHECK", b_check, 0, -1, 0 }, { "UNCHECK", b_check, 0, -1, 0 },
    { "when", b_ifte, 3, 3, 0 }, { "length", b_lengthf, 1, 1, 0 },
    { NULL, NULL, 0, 0, 0 }
};
