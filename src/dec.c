/* dec.c -- extended decimal engine (xn) and the 12-digit real operations.
 * See dec.h and decx.h.  Rounding is half away from zero everywhere. */
#include "decx.h"
#include <string.h>

const uint16_t xn_pow10[5] = { 1, 10, 100, 1000, 10000 };

/* Tie rule: 0 = half away from zero (default), 1 = half to even.  Which one
 * the Prime uses is not settled yet (see notes/ppl.md). */
int dec_round_mode = 0;

/* ------------------------------------------------------------------ xn */

static void xn_trim(xn *x)
{
    while (x->n && x->d[x->n - 1] == 0) x->n--;
    if (!x->n) { x->e = 0; x->s = 0; }
}

void xn_zero(xn *x) { x->e = 0; x->s = 0; x->n = 0; x->k = DEC_FINITE; }

int xn_is_zero(const xn *x) { return x->n == 0; }

void xn_from_int(xn *x, int32_t v)
{
    uint32_t u;
    xn_zero(x);
    if (v < 0) { x->s = 1; u = (uint32_t)(-(v + 1)) + 1u; } else u = (uint32_t)v;
    while (u) { x->d[x->n++] = (uint16_t)(u % 10000u); u /= 10000u; }
}

static int limb_digits(unsigned t)
{
    return t >= 1000 ? 4 : t >= 100 ? 3 : t >= 10 ? 2 : 1;
}

int xn_ndig(const xn *x)
{
    if (!x->n) return 0;
    return 4 * (x->n - 1) + limb_digits(x->d[x->n - 1]);
}

int xn_top(const xn *x) { return x->e + xn_ndig(x) - 1; }

int xn_digit(const xn *x, int pos)
{
    int rel = pos - x->e;
    if (rel < 0 || rel >= 4 * x->n) return 0;
    return (x->d[rel >> 2] / xn_pow10[rel & 3]) % 10;
}

/* coefficient *= 10^k, e -= k.  Returns -1 when out of capacity. */
static int xn_shl(xn *x, int k)
{
    int a, b, i;
    uint32_t carry;
    if (k <= 0 || !x->n) { if (!x->n) x->e = 0; return 0; }
    a = k >> 2; b = k & 3;
    if (x->n + a + 1 > XN_LIMBS) return -1;
    if (b) {
        uint32_t p = xn_pow10[b];
        carry = 0;
        for (i = 0; i < x->n; i++) {
            uint32_t t = (uint32_t)x->d[i] * p + carry;
            x->d[i] = (uint16_t)(t % 10000u);
            carry = t / 10000u;
        }
        if (carry) x->d[x->n++] = (uint16_t)carry;
    }
    if (a) {
        for (i = x->n - 1; i >= 0; i--) x->d[i + a] = x->d[i];
        for (i = 0; i < a; i++) x->d[i] = 0;
        x->n = (uint8_t)(x->n + a);
    }
    x->e -= k;
    return 0;
}

/* coefficient /= 10^k (truncating), e += k.  *rd gets the first discarded
 * digit, *st whether anything below it was non-zero. */
static void xn_shr(xn *x, int k, int *rd, int *st)
{
    int a, b, i, rel;
    int r = 0, s = 0;
    if (k <= 0) { if (rd) *rd = 0; if (st) *st = 0; return; }
    if (!x->n) { if (rd) *rd = 0; if (st) *st = 0; return; }
    rel = k - 1;
    if (rel < 4 * x->n) {
        unsigned limb = x->d[rel >> 2];
        r = (limb / xn_pow10[rel & 3]) % 10;
        if (limb % xn_pow10[rel & 3]) s = 1;
        for (i = 0; i < (rel >> 2) && !s; i++) if (x->d[i]) s = 1;
    } else {
        s = 1;   /* the whole value is below the cut */
        r = 0;
    }
    if (rd) *rd = r;
    if (st) *st = s;
    a = k >> 2; b = k & 3;
    if (a >= x->n) { x->n = 0; x->e += k; xn_trim(x); x->e = 0; return; }
    if (a) {
        for (i = 0; i + a < x->n; i++) x->d[i] = x->d[i + a];
        x->n = (uint8_t)(x->n - a);
    }
    if (b) {
        uint32_t p = xn_pow10[b], rem = 0;
        for (i = x->n - 1; i >= 0; i--) {
            uint32_t cur = rem * 10000u + x->d[i];
            x->d[i] = (uint16_t)(cur / p);
            rem = cur % p;
        }
    }
    x->e += k;
    {
        int32_t e = x->e;
        xn_trim(x);
        if (x->n) x->e = e;
    }
}

static void xn_inc(xn *x)   /* coefficient += 1 */
{
    int i = 0;
    for (;;) {
        if (i == x->n) { x->d[x->n++] = 1; return; }
        if (++x->d[i] < 10000) return;
        x->d[i++] = 0;
    }
}

void xn_round(xn *x, int P)
{
    int nd = xn_ndig(x), rd, st;
    if (nd <= P) return;
    xn_shr(x, nd - P, &rd, &st);
    if (rd > 5 || (rd == 5 && (dec_round_mode == 0 || st || (x->n && (x->d[0] & 1))))) {
        xn_inc(x);
        if (xn_ndig(x) > P) xn_shr(x, 1, NULL, NULL);
    }
}

void xn_trunc_int(xn *x)
{
    if (x->n && x->e < 0) {
        int s = x->s;
        xn_shr(x, -x->e, NULL, NULL);
        if (x->n) { x->s = (uint8_t)s; }
    }
}

int xn_is_int(const xn *x)
{
    int i, rel;
    if (!x->n || x->e >= 0) return 1;
    rel = -x->e;                 /* number of fraction digits */
    if (rel >= 4 * x->n) return 0;
    for (i = 0; i < (rel >> 2); i++) if (x->d[i]) return 0;
    return x->d[rel >> 2] % xn_pow10[rel & 3] == 0;
}

int xn_cmpmag(const xn *a, const xn *b)
{
    int ta, tb, i;
    xn A, B;
    if (!a->n) return b->n ? -1 : 0;
    if (!b->n) return 1;
    ta = xn_top(a); tb = xn_top(b);
    if (ta != tb) return ta > tb ? 1 : -1;
    /* same leading position: align and compare limbs */
    A = *a; B = *b;
    if (A.e > B.e) { if (xn_shl(&A, A.e - B.e)) return 0; }
    else if (B.e > A.e) { if (xn_shl(&B, B.e - A.e)) return 0; }
    if (A.n != B.n) return A.n > B.n ? 1 : -1;
    for (i = A.n - 1; i >= 0; i--)
        if (A.d[i] != B.d[i]) return A.d[i] > B.d[i] ? 1 : -1;
    return 0;
}

int xn_cmp(const xn *a, const xn *b)
{
    int sa = a->n ? (a->s ? -1 : 1) : 0, sb = b->n ? (b->s ? -1 : 1) : 0, c;
    if (sa != sb) return sa > sb ? 1 : -1;
    if (!sa) return 0;
    c = xn_cmpmag(a, b);
    return sa > 0 ? c : -c;
}

/* digits of B below position L collapse into one sticky digit at L-1 */
static void xn_jam(xn *b, int L)
{
    int rd, st, s = b->s;
    if (!b->n || b->e >= L) return;
    if (xn_top(b) < L) {
        b->n = 1; b->d[0] = 1; b->e = L - 1;
        return;
    }
    xn_shr(b, L - b->e, &rd, &st);
    b->s = (uint8_t)s;
    if (rd || st) {
        if (!b->n) { b->n = 1; b->d[0] = 1; b->e = L - 1; return; }
        xn_shl(b, 1);
        b->d[0] = (uint16_t)(b->d[0] + 1);
    }
}

static void mag_add(xn *r, const xn *a, const xn *b)   /* same e */
{
    int n = a->n > b->n ? a->n : b->n, i;
    uint32_t c = 0;
    for (i = 0; i < n; i++) {
        uint32_t t = c + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
        if (t >= 10000) { r->d[i] = (uint16_t)(t - 10000); c = 1; }
        else { r->d[i] = (uint16_t)t; c = 0; }
    }
    r->n = (uint8_t)n;
    if (c) r->d[r->n++] = 1;
}

static void mag_sub(xn *r, const xn *a, const xn *b)   /* |a|>=|b|, same e */
{
    int i;
    int32_t c = 0;
    for (i = 0; i < a->n; i++) {
        int32_t t = (int32_t)a->d[i] - (i < b->n ? b->d[i] : 0) - c;
        if (t < 0) { t += 10000; c = 1; } else c = 0;
        r->d[i] = (uint16_t)t;
    }
    r->n = a->n;
}

void xn_add(xn *r, const xn *a, const xn *b, int P)
{
    xn A, B;
    int ta, tb, L, c;
    int32_t e;
    if (!a->n) { *r = *b; xn_round(r, P); return; }
    if (!b->n) { *r = *a; xn_round(r, P); return; }
    ta = xn_top(a); tb = xn_top(b);
    if (ta >= tb) { A = *a; B = *b; } else { A = *b; B = *a; int t = ta; ta = tb; tb = t; }
    L = ta - P - 3;
    if (A.e < L) L = A.e;
    xn_jam(&B, L);
    e = A.e < B.e ? A.e : B.e;
    xn_shl(&A, A.e - e);
    xn_shl(&B, B.e - e);
    r->k = DEC_FINITE;
    if (A.s == B.s) {
        mag_add(r, &A, &B);
        r->s = A.s;
    } else {
        c = xn_cmpmag(&A, &B);
        if (c == 0) { xn_zero(r); return; }
        if (c > 0) { mag_sub(r, &A, &B); r->s = A.s; }
        else { mag_sub(r, &B, &A); r->s = B.s; }
    }
    r->e = e;
    xn_trim(r);
    if (r->n) r->e = e;
    xn_round(r, P);
}

void xn_sub(xn *r, const xn *a, const xn *b, int P)
{
    xn nb = *b;
    if (nb.n) nb.s ^= 1;
    xn_add(r, a, &nb, P);
}

void xn_mul(xn *r, const xn *a, const xn *b, int P)
{
    uint32_t t[XN_LIMBS + 1];
    xn A = *a, B = *b;
    int i, j, n;
    if (!A.n || !B.n) { xn_zero(r); return; }
    while (A.n + B.n > XN_LIMBS) {
        /* too long to multiply exactly: shorten the longer operand */
        if (A.n >= B.n) xn_round(&A, 4 * (XN_LIMBS / 2) - 4);
        else xn_round(&B, 4 * (XN_LIMBS / 2) - 4);
        if (A.n + B.n > XN_LIMBS) {
            xn_round(&A, 4 * (XN_LIMBS / 2) - 4);
            xn_round(&B, 4 * (XN_LIMBS / 2) - 4);
        }
    }
    n = A.n + B.n;
    for (i = 0; i < n; i++) t[i] = 0;
    for (i = 0; i < A.n; i++) {
        uint32_t c = 0, ai = A.d[i];
        if (!ai) continue;
        for (j = 0; j < B.n; j++) {
            uint32_t v = t[i + j] + ai * B.d[j] + c;
            t[i + j] = v % 10000u;
            c = v / 10000u;
        }
        t[i + B.n] += c;
    }
    r->k = DEC_FINITE;
    r->s = A.s ^ B.s;
    r->e = A.e + B.e;
    r->n = (uint8_t)n;
    for (i = 0; i < n; i++) r->d[i] = (uint16_t)t[i];
    {
        int32_t e = r->e; uint8_t s = r->s;
        xn_trim(r);
        if (r->n) { r->e = e; r->s = s; }
    }
    xn_round(r, P);
}

void xn_mul_int(xn *r, const xn *a, int32_t m, int P)
{
    xn b;
    xn_from_int(&b, m);
    xn_mul(r, a, &b, P);
}

static int coef_less(const xn *a, const xn *b)  /* coefficient a < b */
{
    int i;
    if (a->n != b->n) return a->n < b->n;
    for (i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i];
    return 0;
}

/* Knuth's algorithm D in base 10000 on coefficients. */
int xn_idivmod(xn *q, xn *rem, const xn *a, const xn *b)
{
    uint16_t u[XN_LIMBS + 2], v[XN_LIMBS + 1];
    int n = b->n, m, i, j;
    xn Q;
    if (!n) return -1;
    xn_zero(&Q);
    if (coef_less(a, b)) {
        if (rem) { *rem = *a; rem->e = 0; rem->s = 0; }
        *q = Q;
        return 0;
    }
    m = a->n - n;
    if (n == 1) {
        uint32_t vv = b->d[0], r = 0;
        for (i = a->n - 1; i >= 0; i--) {
            uint32_t cur = r * 10000u + a->d[i];
            Q.d[i] = (uint16_t)(cur / vv);
            r = cur % vv;
        }
        Q.n = a->n;
        xn_trim(&Q);
        *q = Q;
        if (rem) { xn_from_int(rem, (int32_t)r); }
        return 0;
    }
    {
        uint32_t f = 10000u / ((uint32_t)b->d[n - 1] + 1u), c = 0;
        for (i = 0; i < a->n; i++) {
            uint32_t t = a->d[i] * f + c;
            u[i] = (uint16_t)(t % 10000u); c = t / 10000u;
        }
        u[a->n] = (uint16_t)c;
        c = 0;
        for (i = 0; i < n; i++) {
            uint32_t t = b->d[i] * f + c;
            v[i] = (uint16_t)(t % 10000u); c = t / 10000u;
        }
        for (j = m; j >= 0; j--) {
            uint32_t num = (uint32_t)u[j + n] * 10000u + u[j + n - 1];
            uint32_t qh = num / v[n - 1], rh = num % v[n - 1];
            int32_t borrow, t;
            uint32_t carry;
            while (qh >= 10000u || qh * v[n - 2] > rh * 10000u + u[j + n - 2]) {
                qh--; rh += v[n - 1];
                if (rh >= 10000u) break;
            }
            borrow = 0; carry = 0;
            for (i = 0; i < n; i++) {
                uint32_t p = qh * v[i] + carry;
                carry = p / 10000u;
                t = (int32_t)u[i + j] - (int32_t)(p % 10000u) - borrow;
                if (t < 0) { t += 10000; borrow = 1; } else borrow = 0;
                u[i + j] = (uint16_t)t;
            }
            t = (int32_t)u[j + n] - (int32_t)carry - borrow;
            if (t < 0) {
                /* add back */
                uint32_t c2 = 0;
                u[j + n] = (uint16_t)(t + 10000);
                qh--;
                for (i = 0; i < n; i++) {
                    uint32_t s = (uint32_t)u[i + j] + v[i] + c2;
                    if (s >= 10000u) { u[i + j] = (uint16_t)(s - 10000u); c2 = 1; }
                    else { u[i + j] = (uint16_t)s; c2 = 0; }
                }
                u[j + n] = (uint16_t)((u[j + n] + c2) % 10000u);
            } else u[j + n] = (uint16_t)t;
            Q.d[j] = (uint16_t)qh;
        }
        Q.n = (uint8_t)(m + 1);
        xn_trim(&Q);
        *q = Q;
        if (rem) {
            uint32_t r = 0;
            xn R;
            xn_zero(&R);
            for (i = n - 1; i >= 0; i--) {
                uint32_t cur = r * 10000u + u[i];
                R.d[i] = (uint16_t)(cur / f);
                r = cur % f;
            }
            R.n = (uint8_t)n;
            xn_trim(&R);
            *rem = R;
        }
    }
    return 0;
}

int xn_div(xn *r, const xn *a, const xn *b, int P)
{
    xn A = *a, B = *b, Q;
    int s, lim = 4 * (XN_LIMBS / 2) - 4;
    int32_t e;
    uint8_t sg;
    if (!B.n) return DEC_EDIVZERO;
    if (!A.n) { xn_zero(r); return 0; }
    if (xn_ndig(&A) > lim) xn_round(&A, lim);
    if (xn_ndig(&B) > lim) xn_round(&B, lim);
    s = P + 2 + xn_ndig(&B) - xn_ndig(&A);
    if (s < 0) s = 0;
    xn_shl(&A, s);
    e = A.e - B.e;
    sg = A.s ^ B.s;
    {
        xn R;
        xn_idivmod(&Q, &R, &A, &B);
        if (R.n && dec_round_mode) {   /* sticky digit for ties-to-even */
            xn_shl(&Q, 1);
            Q.e = 0;
            if (!Q.n) Q.n = 1;
            Q.d[0] = (uint16_t)(Q.d[0] + 1);
            e -= 1;
        }
    }
    Q.e = e; Q.s = sg; Q.k = DEC_FINITE;
    *r = Q;
    xn_round(r, P);
    return 0;
}

int xn_div_int(xn *r, const xn *a, int32_t m, int P)
{
    xn b;
    xn_from_int(&b, m);
    return xn_div(r, a, &b, P);
}

/* floor(sqrt) of integer coefficients by Newton from above, rounded to P. */
int xn_sqrt(xn *r, const xn *a, int P)
{
    xn N, y, t, q;
    int s, nd, half;
    int32_t e;
    if (a->s && a->n) return DEC_EDOMAIN;
    if (!a->n) { xn_zero(r); return 0; }
    N = *a;
    nd = xn_ndig(&N);
    s = 2 * (P + 2) - nd;
    if (s < 0) s = 0;
    if ((N.e - s) & 1) s++;
    xn_shl(&N, s);
    e = (N.e) / 2;   /* N.e is even now */
    N.e = 0;
    nd = xn_ndig(&N);
    half = (nd + 1) / 2;
    /* seed: an over-estimate from the leading digits */
    {
        /* take the top 8 (or 7) digits so that the exponent parity matches */
        xn top = N;
        int cut = nd - 8;
        uint32_t v = 0, g;
        int i;
        if ((cut & 1)) cut++;
        if (cut < 0) cut = 0;
        xn_shr(&top, cut, NULL, NULL);
        for (i = top.n - 1; i >= 0; i--) v = v * 10000u + top.d[i];
        /* integer sqrt of v (v < 10^9) by bit method */
        {
            uint32_t res = 0, bit = 1u << 30, x = v;
            while (bit > x) bit >>= 2;
            while (bit) {
                if (x >= res + bit) { x -= res + bit; res = (res >> 1) + bit; }
                else res >>= 1;
                bit >>= 2;
            }
            g = res + 2;
        }
        xn_from_int(&y, (int32_t)g);
        xn_shl(&y, cut / 2);
        y.e = 0;
        (void)half;
    }
    /* Newton from above: y' = (y + N/y) / 2 until it stops decreasing */
    for (;;) {
        xn_idivmod(&q, NULL, &N, &y);
        q.e = 0; q.s = 0;
        mag_add(&t, &y, &q);  /* both e == 0 */
        t.e = 0; t.s = 0; t.k = DEC_FINITE;
        xn_trim(&t);
        {
            /* halve */
            uint32_t rr = 0;
            int i;
            for (i = t.n - 1; i >= 0; i--) {
                uint32_t cur = rr * 10000u + t.d[i];
                t.d[i] = (uint16_t)(cur / 2u);
                rr = cur % 2u;
            }
            xn_trim(&t);
        }
        if (xn_cmpmag(&t, &y) >= 0) break;
        y = t;
    }
    y.e = e; y.s = 0; y.k = DEC_FINITE;
    *r = y;
    xn_round(r, P);
    return 0;
}

void xn_scale10(xn *x, int k) { if (x->n) x->e += k; }

int xn_to_int32(const xn *x, int32_t *v)
{
    xn t = *x;
    uint32_t acc = 0;
    int i;
    if (!xn_is_int(&t)) return DEC_ERANGE;
    if (!t.n) { *v = 0; return 0; }
    if (xn_top(&t) > 9) return DEC_ERANGE;
    if (t.e > 0) xn_shl(&t, t.e);
    else if (t.e < 0) xn_shr(&t, -t.e, NULL, NULL);
    for (i = t.n - 1; i >= 0; i--) acc = acc * 10000u + t.d[i];
    if (acc > 2147483647u) return DEC_ERANGE;
    *v = x->s ? -(int32_t)acc : (int32_t)acc;
    return 0;
}

int xn_from_str(xn *x, const char *s)
{
    char dig[120];
    int nd = 0, frac = 0, seen_point = 0, ex = 0, exs = 0, i;
    uint8_t sg = 0;
    xn_zero(x);
    if (*s == '-') { sg = 1; s++; } else if (*s == '+') s++;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            if (!nd && *s == '0') { if (seen_point) frac++; continue; }
            if (nd < (int)sizeof dig) { dig[nd++] = *s; if (seen_point) frac++; }
            else if (!seen_point) ex++;      /* dropped integer digit */
        } else if (*s == '.' && !seen_point) seen_point = 1;
        else break;
    }
    if (*s == 'E' || *s == 'e') {
        int ev = 0;
        s++;
        if (*s == '-') { exs = 1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') { ev = ev * 10 + (*s - '0'); if (ev > 100000) ev = 100000; s++; }
        ex += exs ? -ev : ev;
    }
    for (i = nd; i > 0; i -= 4) {
        int j, start = i - 4 < 0 ? 0 : i - 4;
        unsigned v = 0;
        for (j = start; j < i; j++) v = v * 10 + (unsigned)(dig[j] - '0');
        x->d[x->n++] = (uint16_t)v;
    }
    {
        int32_t e = ex - frac;
        xn_trim(x);
        if (x->n) { x->e = e; x->s = sg; }
    }
    return 0;
}

void xn_from_real(xn *x, const real *r)
{
    x->k = r->k;
    x->s = r->s;
    if (r->m[0] == 0) { xn_zero(x); x->k = r->k; return; }
    x->d[0] = r->m[2]; x->d[1] = r->m[1]; x->d[2] = r->m[0];
    x->n = 3;
    x->e = r->e - 11;
}

int xn_to_real(real *r, const xn *x)
{
    xn t = *x;
    int nd;
    int32_t E;
    r->k = DEC_FINITE;
    xn_round(&t, DEC_DIGITS);
    if (!t.n) { dec_zero(r); return 0; }
    nd = xn_ndig(&t);
    if (nd < DEC_DIGITS) xn_shl(&t, DEC_DIGITS - nd);
    E = t.e + 11;
    r->s = t.s;
    if (E > DEC_EMAX) {   /* the Prime clamps silently to +-MAXREAL */
        r->m[0] = 9999; r->m[1] = 9999; r->m[2] = 9999; r->e = DEC_EMAX;
        return DEC_EOVERFLOW;
    }
    if (E < DEC_EMIN) { dec_zero(r); return DEC_EUNDERFLOW; }
    r->m[0] = t.d[2]; r->m[1] = t.d[1]; r->m[2] = t.d[0];
    r->e = (int16_t)E;
    return 0;
}

/* ------------------------------------------------------------- reals */

void dec_zero(real *r) { memset(r, 0, sizeof *r); }

int dec_maxreal(real *r, int neg)
{
    r->m[0] = 9999; r->m[1] = 9999; r->m[2] = 9999; r->e = DEC_EMAX;
    r->s = (uint8_t)(neg != 0); r->k = DEC_FINITE;
    return DEC_EOVERFLOW;
}

int dec_is_zero(const real *x) { return x->k == DEC_FINITE && x->m[0] == 0; }

void dec_from_int(real *r, int32_t v)
{
    xn x;
    xn_from_int(&x, v);
    xn_to_real(r, &x);
}

int dec_from_str(real *r, const char *s, const char **end)
{
    const char *p = s;
    int any = 0;
    xn x;
    if (*p == '-' || *p == '+') p++;
    while (*p >= '0' && *p <= '9') { p++; any = 1; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { p++; any = 1; } }
    if (!any) { if (end) *end = s; return DEC_ESYNTAX; }
    if (*p == 'E' || *p == 'e') {
        const char *q = p + 1;
        if (*q == '-' || *q == '+') q++;
        if (*q >= '0' && *q <= '9') { while (*q >= '0' && *q <= '9') q++; p = q; }
    }
    if (end) *end = p;
    {
        char buf[160];
        size_t n = (size_t)(p - s);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, s, n); buf[n] = 0;
        /* cut off an exponent marker without digits */
        xn_from_str(&x, buf);
    }
    return xn_to_real(r, &x);
}

int dec_is_int(const real *x)
{
    xn t;
    if (x->k != DEC_FINITE) return 0;
    xn_from_real(&t, x);
    return xn_is_int(&t);
}

int dec_to_int32(const real *x, int32_t *v)
{
    xn t;
    if (x->k != DEC_FINITE) return DEC_ERANGE;
    xn_from_real(&t, x);
    return xn_to_int32(&t, v);
}

int dec_to_int32_trunc(const real *x, int32_t *v)
{
    xn t;
    if (x->k != DEC_FINITE) return DEC_ERANGE;
    xn_from_real(&t, x);
    xn_trunc_int(&t);
    return xn_to_int32(&t, v);
}

int dec_to_u64(const real *x, uint32_t *hi, uint32_t *lo)
{
    /* integer part of |x|, which must be < 10^12 (it always is below 2^40) */
    xn t;
    uint32_t h = 0, l = 0;
    int i;
    if (x->k != DEC_FINITE) return DEC_ERANGE;
    xn_from_real(&t, x);
    xn_trunc_int(&t);
    if (t.n && xn_top(&t) > 19) return DEC_ERANGE;
    if (t.n && t.e > 0) xn_shl(&t, t.e);
    for (i = t.n - 1; i >= 0; i--) {
        /* (h:l) = (h:l) * 10000 + d */
        uint32_t lo_lo = (l & 0xFFFFu) * 10000u + t.d[i];
        uint32_t lo_hi = (l >> 16) * 10000u + (lo_lo >> 16);
        uint32_t nh = h * 10000u + (lo_hi >> 16);
        l = (lo_hi << 16) | (lo_lo & 0xFFFFu);
        h = nh;
    }
    *hi = h; *lo = l;
    return 0;
}

void dec_from_u64(real *r, uint32_t hi, uint32_t lo)
{
    /* repeated division of (hi:lo) by 10000 */
    xn x;
    xn_zero(&x);
    while (hi || lo) {
        uint32_t rem, qh, ql, t;
        qh = hi / 10000u; rem = hi % 10000u;
        t = (rem << 16) | (lo >> 16);
        ql = (t / 10000u) << 16; rem = t % 10000u;
        t = (rem << 16) | (lo & 0xFFFFu);
        ql |= t / 10000u; rem = t % 10000u;
        x.d[x.n++] = (uint16_t)rem;
        hi = qh; lo = ql;
    }
    xn_to_real(r, &x);
}

static int special2(real *r, const real *a, const real *b)
{
    if (a->k == DEC_NAN || b->k == DEC_NAN) { dec_zero(r); r->k = DEC_NAN; return 1; }
    return 0;
}

int dec_add(real *r, const real *a, const real *b)
{
    xn x, y, z;
    if (a->k | b->k) {
        if (special2(r, a, b)) return 0;
        if (a->k == DEC_INF && b->k == DEC_INF && a->s != b->s) { dec_zero(r); r->k = DEC_NAN; return 0; }
        *r = a->k == DEC_INF ? *a : *b;
        return 0;
    }
    xn_from_real(&x, a); xn_from_real(&y, b);
    xn_add(&z, &x, &y, DEC_DIGITS);
    return xn_to_real(r, &z);
}

int dec_sub(real *r, const real *a, const real *b)
{
    real nb = *b;
    if (nb.m[0] || nb.k) nb.s ^= 1;
    return dec_add(r, a, &nb);
}

int dec_mul(real *r, const real *a, const real *b)
{
    xn x, y, z;
    if (a->k | b->k) {
        if (special2(r, a, b)) return 0;
        if (dec_is_zero(a) || dec_is_zero(b)) { dec_zero(r); r->k = DEC_NAN; return 0; }
        dec_zero(r); r->k = DEC_INF; r->s = a->s ^ b->s; return 0;
    }
    xn_from_real(&x, a); xn_from_real(&y, b);
    xn_mul(&z, &x, &y, DEC_DIGITS);
    return xn_to_real(r, &z);
}

int dec_div(real *r, const real *a, const real *b)
{
    xn x, y, z;
    int st;
    if (a->k | b->k) {
        if (special2(r, a, b)) return 0;
        if (a->k == DEC_INF && b->k == DEC_INF) { dec_zero(r); r->k = DEC_NAN; return 0; }
        if (b->k == DEC_INF) { dec_zero(r); return 0; }
        *r = *a; r->s = a->s ^ b->s; return 0;
    }
    if (dec_is_zero(b)) return DEC_EDIVZERO;
    xn_from_real(&x, a); xn_from_real(&y, b);
    st = xn_div(&z, &x, &y, DEC_DIGITS);
    if (st) return st;
    return xn_to_real(r, &z);
}

void dec_neg(real *r, const real *a)
{
    *r = *a;
    if (r->m[0] || r->k) r->s ^= 1;
}

void dec_abs(real *r, const real *a) { *r = *a; r->s = 0; }

int dec_cmp(const real *a, const real *b)
{
    int sa, sb, i, c = 0;
    if (a->k == DEC_NAN || b->k == DEC_NAN) return 2;
    if (a->k == DEC_INF || b->k == DEC_INF) {
        int va = a->k == DEC_INF ? (a->s ? -2 : 2) : 0;
        int vb = b->k == DEC_INF ? (b->s ? -2 : 2) : 0;
        if (va == vb) { if (va) return 0; }
        if (va || vb) {
            if (!va) va = dec_is_zero(a) ? 0 : (a->s ? -1 : 1);
            if (!vb) vb = dec_is_zero(b) ? 0 : (b->s ? -1 : 1);
            return va < vb ? -1 : va > vb ? 1 : 0;
        }
    }
    sa = a->m[0] ? (a->s ? -1 : 1) : 0;
    sb = b->m[0] ? (b->s ? -1 : 1) : 0;
    if (sa != sb) return sa < sb ? -1 : 1;
    if (!sa) return 0;
    if (a->e != b->e) c = a->e > b->e ? 1 : -1;
    else for (i = 0; i < 3 && !c; i++) if (a->m[i] != b->m[i]) c = a->m[i] > b->m[i] ? 1 : -1;
    return sa > 0 ? c : -c;
}

int dec_ip(real *r, const real *x)
{
    xn t;
    if (x->k) { *r = *x; return 0; }
    xn_from_real(&t, x);
    xn_trunc_int(&t);
    return xn_to_real(r, &t);
}

int dec_fp(real *r, const real *x)
{
    real i;
    if (x->k) { dec_zero(r); r->k = DEC_NAN; return 0; }
    dec_ip(&i, x);
    return dec_sub(r, x, &i);
}

int dec_floor(real *r, const real *x)
{
    real i, one;
    if (x->k) { *r = *x; return 0; }
    dec_ip(&i, x);
    if (x->s && dec_cmp(&i, x) != 0) { dec_from_int(&one, 1); return dec_sub(r, &i, &one); }
    *r = i;
    return 0;
}

int dec_ceil(real *r, const real *x)
{
    real i, one;
    if (x->k) { *r = *x; return 0; }
    dec_ip(&i, x);
    if (!x->s && dec_cmp(&i, x) != 0) { dec_from_int(&one, 1); return dec_add(r, &i, &one); }
    *r = i;
    return 0;
}

int dec_round_places(real *r, const real *x, int places, int truncate)
{
    xn t;
    int rd, st, s;
    if (x->k) { *r = *x; return 0; }
    xn_from_real(&t, x);
    s = t.s;
    if (places >= 0) {
        if (places > 12) places = 12;
        if (t.n && t.e < -places) {
            xn_shr(&t, -places - t.e, &rd, &st);
            if (!truncate && rd >= 5) {
                if (!t.n) { t.n = 1; t.d[0] = 1; t.e = -places; }
                else xn_inc(&t);
            }
            if (t.n) t.s = (uint8_t)s;
        }
    } else {
        int P = -places;
        if (P > 12) P = 12;
        if (P < 1) P = 1;
        if (truncate) {
            int nd = xn_ndig(&t);
            if (nd > P) { xn_shr(&t, nd - P, NULL, NULL); t.s = (uint8_t)s; }
        } else xn_round(&t, P);
    }
    return xn_to_real(r, &t);
}

int dec_mod(real *r, const real *a, const real *b)
{
    xn A, B, Q, R;
    int32_t e;
    if (a->k | b->k) { dec_zero(r); r->k = DEC_NAN; return 0; }
    if (dec_is_zero(b)) { *r = *a; return 0; }   /* x MOD 0 = x */
    if (dec_is_zero(a)) { dec_zero(r); return 0; }
    xn_from_real(&A, a); xn_from_real(&B, b);
    e = A.e < B.e ? A.e : B.e;
    if (A.e - e > 80 || B.e - e > 80) {
        /* |a| hugely larger than |b|: no digit of the remainder survives */
        if (xn_top(&A) > xn_top(&B)) { dec_zero(r); return 0; }
        /* |a| much smaller than |b|: floor(a/b) is 0 or -1 */
        if (a->s == b->s) { *r = *a; return 0; }
        return dec_add(r, a, b);
    }
    xn_shl(&A, A.e - e); xn_shl(&B, B.e - e);
    {
        uint8_t sa = A.s, sb = B.s;
        A.s = B.s = 0; A.e = B.e = 0;
        xn_idivmod(&Q, &R, &A, &B);
        R.e = e;
        if (R.n) {
            R.s = sa;
            if (sa != sb) {   /* floor: the remainder takes the divisor's sign */
                xn bb = B; bb.e = e; bb.s = sb;
                xn_add(&R, &R, &bb, XN_MAXP);
            }
        } else xn_zero(&R);
    }
    return xn_to_real(r, &R);
}

int dec_xpon(const real *x) { return dec_is_zero(x) ? 0 : x->e; }

void dec_mant(real *r, const real *x)
{
    *r = *x;
    if (!dec_is_zero(x)) r->e = 0;
}
