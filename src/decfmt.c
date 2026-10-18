/* decfmt.c -- reals to text in the Prime's Standard/Fixed/Scientific/
 * Engineering formats.
 *
 * Standard: up to 12 significant digits, trailing zeros dropped.  Positional
 * notation when it fits in 12 digit positions (1E11 -> 100000000000,
 * 1E-12 -> 0.000000000001, 1.234E-9 -> 0.000000001234), otherwise
 * scientific (1E12 -> 1E12, 1.2345E-9).  The Prime writes a negative sign as
 * U+2212 and the exponent mark as U+1D07 (small capital E); DECF_ASCII gives
 * '-' and 'E' instead.
 */
#include "decx.h"
#include <string.h>

static char *put_minus(char *p, int ascii)
{
    if (ascii) *p++ = '-';
    else { *p++ = (char)0xE2; *p++ = (char)0x88; *p++ = (char)0x92; }  /* U+2212 */
    return p;
}

static char *put_emark(char *p, int ascii)
{
    if (ascii) *p++ = 'E';
    else { *p++ = (char)0xE1; *p++ = (char)0xB4; *p++ = (char)0x87; }  /* U+1D07 */
    return p;
}

static char *put_int(char *p, int v, int ascii)
{
    char t[12];
    int n = 0;
    if (v < 0) { p = put_minus(p, ascii); v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    return p;
}

/* the 12 digits of x as characters */
static void digits12(char *d, const real *x)
{
    int i, j;
    for (i = 0; i < 3; i++) {
        unsigned v = x->m[i];
        for (j = 3; j >= 0; j--) { d[i * 4 + j] = (char)('0' + v % 10); v /= 10; }
    }
}

static int sig_count(const char *d)
{
    int n = 12;
    while (n > 1 && d[n - 1] == '0') n--;
    return n;
}

/* scientific with nd significant digits (1..12); keep_zeros: show exactly
 * nd digits, else trim trailing zeros. */
static char *put_sci(char *p, const real *x, int nd, int keep_zeros, int ascii, int eng)
{
    real y;
    char d[12];
    int n, e, lead = 1, i;
    if (nd < 1) nd = 1;
    if (nd > 12) nd = 12;
    dec_round_places(&y, x, -nd, 0);
    digits12(d, &y);
    e = y.e;
    if (eng) {
        int m = ((e % 3) + 3) % 3;
        lead = 1 + m;
        e -= m;
        if (nd < lead) nd = lead;
    }
    n = keep_zeros ? nd : sig_count(d);
    if (n < lead) n = lead;
    for (i = 0; i < lead; i++) *p++ = d[i];
    if (n > lead) {
        *p++ = '.';
        for (i = lead; i < n; i++) *p++ = d[i];
    }
    p = put_emark(p, ascii);
    p = put_int(p, e, ascii);
    return p;
}

int dec_format(char *buf, const real *x, int fmt, int digits, int flags)
{
    char *p = buf;
    int ascii = flags & DECF_ASCII;
    char d[12];
    int n, e, i;

    if (x->k == DEC_NAN) { strcpy(buf, "NaN"); return 3; }
    if (x->k == DEC_INF) {
        strcpy(buf, x->s ? "-Inf" : "+Inf");
        return 4;
    }
    if (dec_is_zero(x)) {
        if (fmt == FMT_FIX && digits > 0) {
            *p++ = '0'; *p++ = '.';
            for (i = 0; i < digits; i++) *p++ = '0';
            *p = 0;
            return (int)(p - buf);
        }
        if ((fmt == FMT_SCI || fmt == FMT_ENG)) {
            *p++ = '0';
            if (digits > 0) { *p++ = '.'; for (i = 0; i < digits; i++) *p++ = '0'; }
            p = put_emark(p, ascii); *p++ = '0'; *p = 0;
            return (int)(p - buf);
        }
        buf[0] = '0'; buf[1] = 0;
        return 1;
    }
    if (x->s) p = put_minus(p, ascii);

    if (fmt == FMT_SCI || fmt == FMT_ENG) {
        p = put_sci(p, x, digits + 1, 1, ascii, fmt == FMT_ENG);
        *p = 0;
        return (int)(p - buf);
    }

    if (fmt == FMT_FIX) {
        real y, ax;
        dec_abs(&ax, x);
        dec_round_places(&y, &ax, digits, 0);
        if (dec_is_zero(&y) || y.e >= 12) {
            /* too small to show or too large: scientific */
            p = put_sci(p, &ax, digits + 1, 1, ascii, 0);
            *p = 0;
            return (int)(p - buf);
        }
        digits12(d, &y);
        e = y.e;
        if (e >= 0) {
            for (i = 0; i <= e; i++) *p++ = d[i];
            if (digits > 0) {
                *p++ = '.';
                for (i = 0; i < digits; i++) *p++ = (e + 1 + i < 12) ? d[e + 1 + i] : '0';
            }
        } else {
            *p++ = '0';
            if (digits > 0) {
                *p++ = '.';
                for (i = 0; i < digits; i++) {
                    int k = i - (-e - 1);      /* index into d */
                    *p++ = (k >= 0 && k < 12) ? d[k] : '0';
                }
            }
        }
        *p = 0;
        return (int)(p - buf);
    }

    /* Standard */
    digits12(d, x);
    n = sig_count(d);
    e = x->e;
    if (e >= 0 && e < 12) {
        for (i = 0; i <= e; i++) *p++ = i < n ? d[i] : '0';
        if (n > e + 1) {
            *p++ = '.';
            for (i = e + 1; i < n; i++) *p++ = d[i];
        }
    } else if (e < 0 && n - e - 1 <= 12) {
        *p++ = '0'; *p++ = '.';
        for (i = 0; i < -e - 1; i++) *p++ = '0';
        for (i = 0; i < n; i++) *p++ = d[i];
    } else {
        real ax;
        dec_abs(&ax, x);
        p = put_sci(p, &ax, 12, 0, ascii, 0);
    }
    *p = 0;
    return (int)(p - buf);
}
