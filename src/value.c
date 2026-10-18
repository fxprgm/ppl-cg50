/* value.c -- reference-counted objects and the text of values. */
#include "vm.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ refcount */

static void obj_free(value *v);

void v_incref(const value *v)
{
    switch (v->t) {
    case T_STR: v->u.s->h.rc++; break;
    case T_LIST: v->u.l->h.rc++; break;
    case T_MAT: v->u.m->h.rc++; break;
    case T_SYM: v->u.y->h.rc++; break;
    default: break;
    }
}

void v_decref(value *v)
{
    obj *o = NULL;
    switch (v->t) {
    case T_STR: o = &v->u.s->h; break;
    case T_LIST: o = &v->u.l->h; break;
    case T_MAT: o = &v->u.m->h; break;
    case T_SYM: o = &v->u.y->h; break;
    default: return;
    }
    if (--o->rc == 0) obj_free(v);
    v->t = T_NONE;
}

static void obj_free(value *v)
{
    uint32_t i;
    switch (v->t) {
    case T_STR: free(v->u.s); break;
    case T_LIST:
        for (i = 0; i < v->u.l->n; i++) v_decref(&v->u.l->v[i]);
        free(v->u.l->v); free(v->u.l); break;
    case T_MAT:
        free(v->u.m->v); free(v->u.m); break;
    case T_SYM: {
        value s; s.t = T_STR; s.u.s = v->u.y->text;
        v_decref(&s); free(v->u.y); break;
    }
    default: break;
    }
}

void v_set(value *dst, const value *src)
{
    value old = *dst;
    *dst = *src;
    v_incref(dst);
    v_decref(&old);
}

void v_real(value *v, const real *r) { v->t = T_REAL; v->ib = v->iw = v->x = 0; v->u.r = *r; }

void v_int32(value *v, int32_t n) { real r; dec_from_int(&r, n); v_real(v, &r); }

void v_none(value *v) { memset(v, 0, sizeof *v); v->t = T_NONE; }

/* ------------------------------------------------------------- strings */

pstr *str_new(uint32_t len)
{
    pstr *s = (pstr *)malloc(sizeof(pstr) + (len ? len - 1 : 0) * sizeof(uint16_t) + sizeof(uint16_t));
    if (!s) return NULL;
    s->h.rc = 1;
    s->len = len;
    return s;
}

uint32_t utf8_to_u16(uint16_t *out, const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s, *e = p + n;
    uint32_t k = 0;
    while (p < e) {
        uint32_t c = *p++;
        if (c >= 0xC0 && c < 0xE0 && p < e) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        else if (c >= 0xE0 && c < 0xF0 && p + 1 < e) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        else if (c >= 0xF0 && p + 2 < e) {
            c = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
            c -= 0x10000;
            if (out) { out[k] = (uint16_t)(0xD800 + (c >> 10)); out[k + 1] = (uint16_t)(0xDC00 + (c & 0x3FF)); }
            k += 2;
            continue;
        }
        if (out) out[k] = (uint16_t)c;
        k++;
    }
    return k;
}

size_t u16_to_utf8(char *out, const uint16_t *s, uint32_t n)
{
    size_t k = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        if (c < 0x80) { if (out) out[k] = (char)c; k++; }
        else if (c < 0x800) { if (out) { out[k] = (char)(0xC0 | (c >> 6)); out[k + 1] = (char)(0x80 | (c & 0x3F)); } k += 2; }
        else if (c < 0x10000) {
            if (out) { out[k] = (char)(0xE0 | (c >> 12)); out[k + 1] = (char)(0x80 | ((c >> 6) & 0x3F)); out[k + 2] = (char)(0x80 | (c & 0x3F)); }
            k += 3;
        } else {
            if (out) { out[k] = (char)(0xF0 | (c >> 18)); out[k + 1] = (char)(0x80 | ((c >> 12) & 0x3F)); out[k + 2] = (char)(0x80 | ((c >> 6) & 0x3F)); out[k + 3] = (char)(0x80 | (c & 0x3F)); }
            k += 4;
        }
    }
    return k;
}

int v_str_utf8(value *v, const char *s, size_t n)
{
    uint32_t len = utf8_to_u16(NULL, s, n);
    pstr *p = str_new(len);
    if (!p) return E_MEM;
    utf8_to_u16(p->c, s, n);
    v_none(v);
    v->t = T_STR; v->u.s = p;
    return 0;
}

int v_str_u16(value *v, const uint16_t *s, uint32_t n)
{
    pstr *p = str_new(n);
    if (!p) return E_MEM;
    if (n) memcpy(p->c, s, n * sizeof(uint16_t));
    v_none(v);
    v->t = T_STR; v->u.s = p;
    return 0;
}

int str_eq_ascii(const pstr *s, const char *a)
{
    uint32_t i;
    for (i = 0; i < s->len; i++) if (!a[i] || s->c[i] != (unsigned char)a[i]) return 0;
    return a[i] == 0;
}

pstr *str_own(value *v)
{
    if (v->u.s->h.rc > 1) {
        pstr *n = str_new(v->u.s->len);
        if (!n) return NULL;
        memcpy(n->c, v->u.s->c, v->u.s->len * sizeof(uint16_t));
        v->u.s->h.rc--;
        v->u.s = n;
    }
    return v->u.s;
}

/* --------------------------------------------------------------- lists */

plist *list_new(uint32_t n)
{
    plist *l = (plist *)malloc(sizeof *l);
    uint32_t i;
    if (!l) return NULL;
    l->h.rc = 1;
    l->n = n;
    l->cap = n < 4 ? 4 : n;
    l->v = (value *)malloc(l->cap * sizeof(value));
    if (!l->v) { free(l); return NULL; }
    for (i = 0; i < n; i++) v_int32(&l->v[i], 0);
    return l;
}

int list_push(plist *l, const value *v)
{
    if (l->n == l->cap) {
        uint32_t nc = l->cap * 2;
        value *nv = (value *)realloc(l->v, nc * sizeof(value));
        if (!nv) return E_MEM;
        l->v = nv; l->cap = nc;
    }
    v_copy(&l->v[l->n++], v);
    return 0;
}

int v_list(value *v, uint32_t n)
{
    plist *l = list_new(n);
    if (!l) return E_MEM;
    v_none(v);
    v->t = T_LIST; v->u.l = l;
    return 0;
}

plist *list_own(value *v)
{
    plist *o = v->u.l;
    if (o->h.rc > 1) {
        plist *n = list_new(0);
        uint32_t i;
        if (!n) return NULL;
        for (i = 0; i < o->n; i++) if (list_push(n, &o->v[i])) return NULL;
        o->h.rc--;
        v->u.l = n;
    }
    return v->u.l;
}

/* ------------------------------------------------------------ matrices */

pmat *mat_new(int rows, int cols, int vec)
{
    pmat *m = (pmat *)malloc(sizeof *m);
    int i, n = (vec ? 1 : rows) * cols;
    if (!m) return NULL;
    m->h.rc = 1;
    m->rows = (uint16_t)(vec ? 1 : rows);
    m->cols = (uint16_t)cols;
    m->vec = (uint8_t)(vec != 0);
    m->v = (value *)malloc((n ? n : 1) * sizeof(value));
    if (!m->v) { free(m); return NULL; }
    for (i = 0; i < n; i++) v_int32(&m->v[i], 0);
    return m;
}

int v_mat(value *v, int rows, int cols, int vec)
{
    pmat *m = mat_new(rows, cols, vec);
    if (!m) return E_MEM;
    v_none(v);
    v->t = T_MAT; v->u.m = m;
    return 0;
}

pmat *mat_own(value *v)
{
    pmat *o = v->u.m;
    if (o->h.rc > 1) {
        pmat *n = mat_new(o->rows, o->cols, o->vec);
        if (!n) return NULL;
        memcpy(n->v, o->v, (size_t)o->rows * o->cols * sizeof(value));
        o->h.rc--;
        v->u.m = n;
    }
    return v->u.m;
}

/* ------------------------------------------------------------ equality */

int v_equal(const value *a, const value *b)
{
    uint32_t i;
    if (a->t != b->t) {
        real x, y;
        if ((a->t == T_REAL || a->t == T_INT) && (b->t == T_REAL || b->t == T_INT)
            && !to_real(a, &x) && !to_real(b, &y))
            return dec_cmp(&x, &y) == 0;
        return 0;
    }
    switch (a->t) {
    case T_REAL: return dec_cmp(&a->u.r, &b->u.r) == 0;
    case T_INT: return a->u.i.hi == b->u.i.hi && a->u.i.lo == b->u.i.lo;
    case T_CPLX: return dec_cmp(&a->u.c.re, &b->u.c.re) == 0 && dec_cmp(&a->u.c.im, &b->u.c.im) == 0;
    case T_STR:
        if (a->u.s->len != b->u.s->len) return 0;
        return !memcmp(a->u.s->c, b->u.s->c, a->u.s->len * sizeof(uint16_t));
    case T_LIST:
        if (a->u.l->n != b->u.l->n) return 0;
        for (i = 0; i < a->u.l->n; i++) if (!v_equal(&a->u.l->v[i], &b->u.l->v[i])) return 0;
        return 1;
    case T_MAT: {
        uint32_t n = (uint32_t)a->u.m->rows * a->u.m->cols;
        if (a->u.m->rows != b->u.m->rows || a->u.m->cols != b->u.m->cols || a->u.m->vec != b->u.m->vec) return 0;
        for (i = 0; i < n; i++) if (!v_equal(&a->u.m->v[i], &b->u.m->v[i])) return 0;
        return 1;
    }
    case T_SYM: {
        value x, y;
        x.t = y.t = T_STR; x.u.s = a->u.y->text; y.u.s = b->u.y->text;
        return v_equal(&x, &y);
    }
    default: return 0;
    }
}

int v_truth(const value *v, int *t)
{
    switch (v->t) {
    case T_REAL: *t = !dec_is_zero(&v->u.r); return 0;
    case T_INT: *t = v->u.i.hi || v->u.i.lo; return 0;
    case T_CPLX: *t = !dec_is_zero(&v->u.c.re) || !dec_is_zero(&v->u.c.im); return 0;
    default: return E_BADTYPE;
    }
}

/* ------------------------------------------------------------- sbuf */

void sb_init(sbuf *b) { b->p = NULL; b->n = b->cap = 0; }

void sb_put(sbuf *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        char *np;
        while (nc < b->n + n + 1) nc *= 2;
        np = (char *)realloc(b->p, nc);
        if (!np) return;
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

void sb_puts(sbuf *b, const char *s) { sb_put(b, s, strlen(s)); }
void sb_free(sbuf *b) { free(b->p); sb_init(b); }

/* -------------------------------------------------------------- text */

static void real_text(ppl_vm *V, sbuf *b, const real *r)
{
    char buf[64];
    int n = dec_format(buf, r, V->set.format, V->set.digits, 0);
    sb_put(b, buf, (size_t)n);
}

/* degrees-minutes-seconds: 1.5 -> 1°30′00″ */
static void dms_text(ppl_vm *V, sbuf *b, const real *r)
{
    real x, d, m, s, f, sixty;
    char buf[64];
    int n;
    int32_t di, mi;
    dec_abs(&x, r);
    dec_from_int(&sixty, 60);
    dec_ip(&d, &x);
    dec_sub(&f, &x, &d); dec_mul(&f, &f, &sixty);
    dec_ip(&m, &f);
    dec_sub(&s, &f, &m); dec_mul(&s, &s, &sixty);
    dec_round_places(&s, &s, 6, 0);
    if (dec_cmp(&s, &sixty) >= 0) { real one; dec_from_int(&one, 1); dec_sub(&s, &s, &sixty); dec_add(&m, &m, &one); }
    if (dec_cmp(&m, &sixty) >= 0) { real one; dec_from_int(&one, 1); dec_sub(&m, &m, &sixty); dec_add(&d, &d, &one); }
    if (r->s && !dec_is_zero(r)) sb_puts(b, "\xE2\x88\x92");
    n = dec_format(buf, &d, FMT_STD, 0, 0); sb_put(b, buf, (size_t)n);
    sb_puts(b, "\xC2\xB0");
    dec_to_int32(&m, &mi);
    buf[0] = (char)('0' + mi / 10); buf[1] = (char)('0' + mi % 10); sb_put(b, buf, 2);
    sb_puts(b, "\xE2\x80\xB2");
    dec_ip(&f, &s);
    dec_to_int32(&f, &di);
    buf[0] = (char)('0' + di / 10); buf[1] = (char)('0' + di % 10); sb_put(b, buf, 2);
    dec_sub(&f, &s, &f);
    if (!dec_is_zero(&f)) {
        n = dec_format(buf, &f, FMT_STD, 0, 0);
        sb_put(b, buf + 1, (size_t)(n - 1));   /* ".5" */
    }
    sb_puts(b, "\xE2\x80\xB3");
    (void)V;
}

static int sys_base(const ppl_vm *V)
{
    switch (V->set.base) { case 0: return B_BIN; case 1: return B_OCT; case 2: return B_DEC; default: return B_HEX; }
}

void int_mask(const ppl_vm *V, value *v)
{
    int w = v->iw ? v->iw : V->set.bits;
    if (w >= 64) return;
    if (w >= 32) v->u.i.hi &= (w == 32) ? 0u : ((1u << (w - 32)) - 1u);
    else { v->u.i.hi = 0; v->u.i.lo &= (1u << w) - 1u; }
}

static void int_text(ppl_vm *V, sbuf *b, const value *v)
{
    value m = *v;
    char t[80];
    int n = 0, base = v->ib ? v->ib : sys_base(V), radix;
    uint32_t hi, lo;
    static const char suf[] = "?bodh";
    int_mask(V, &m);
    hi = m.u.i.hi; lo = m.u.i.lo;
    radix = base == B_BIN ? 2 : base == B_OCT ? 8 : base == B_HEX ? 16 : 10;
    do {
        /* (hi:lo) /= radix */
        uint32_t r, qh = hi / (uint32_t)radix, rem = hi % (uint32_t)radix;
        uint32_t t1 = (rem << 16) | (lo >> 16), q1 = t1 / (uint32_t)radix;
        uint32_t t2;
        rem = t1 % (uint32_t)radix;
        t2 = (rem << 16) | (lo & 0xFFFFu);
        r = t2 % (uint32_t)radix;
        lo = (q1 << 16) | (t2 / (uint32_t)radix);
        hi = qh;
        t[n++] = "0123456789ABCDEF"[r];
    } while (hi || lo);
    sb_puts(b, "#");
    while (n) { char c = t[--n]; sb_put(b, &c, 1); }
    if (v->iw && v->iw != V->set.bits) {
        char w[8];
        int k = 0, x = v->iw;
        w[k++] = ':';
        if (x >= 10) w[k++] = (char)('0' + x / 10);
        w[k++] = (char)('0' + x % 10);
        sb_put(b, w, (size_t)k);
    }
    sb_put(b, &suf[base], 1);
}

static void str_text(sbuf *b, const pstr *s, int quote)
{
    size_t n = u16_to_utf8(NULL, s->c, s->len);
    char *tmp = (char *)malloc(n + 1);
    if (quote) sb_puts(b, "\"");
    if (tmp) { u16_to_utf8(tmp, s->c, s->len); sb_put(b, tmp, n); free(tmp); }
    if (quote) sb_puts(b, "\"");
}

/* as the Prime shows them: 2+3*i, i, -i, 2-i; a zero imaginary part shows the real part only */
static void cplx_text(ppl_vm *V, sbuf *b, const cplx *c)
{
    real im = c->im, one;
    int neg = im.s;
    if (dec_is_zero(&im)) { real_text(V, b, &c->re); return; }
    if (!dec_is_zero(&c->re)) {
        real_text(V, b, &c->re);
        /* the operator between the parts is a plain '-' (recorded STRINGs: 3-4*i,
           −0.552220307199-1.1052954935*i); a sign is U+2212 */
        sb_puts(b, neg ? "-" : "+");
    } else if (neg) {
        sb_puts(b, "\xE2\x88\x92");
    }
    im.s = 0;
    dec_from_int(&one, 1);
    if (dec_cmp(&im, &one) != 0) {
        real_text(V, b, &im);
        sb_puts(b, "*");
    }
    sb_puts(b, "\xEE\x80\x83");   /* U+E003: the Prime's imaginary unit */
}

void v_to_text(ppl_vm *V, sbuf *b, const value *v, int quote)
{
    uint32_t i;
    switch (v->t) {
    case T_REAL:
        if (v->x == 1) dms_text(V, b, &v->u.r);
        else real_text(V, b, &v->u.r);
        break;
    case T_INT: int_text(V, b, v); break;
    case T_STR: str_text(b, v->u.s, quote); break;
    case T_CPLX: cplx_text(V, b, &v->u.c); break;
    case T_LIST:
        sb_puts(b, "{");
        for (i = 0; i < v->u.l->n; i++) {
            if (i) sb_puts(b, ",");
            v_to_text(V, b, &v->u.l->v[i], 1);
        }
        sb_puts(b, "}");
        break;
    case T_MAT: {
        pmat *m = v->u.m;
        int r, c;
        if (m->vec) {
            sb_puts(b, "[");
            for (c = 0; c < m->cols; c++) { if (c) sb_puts(b, ","); v_to_text(V, b, &m->v[c], 1); }
            sb_puts(b, "]");
        } else {
            sb_puts(b, "[");
            for (r = 0; r < m->rows; r++) {
                if (r) sb_puts(b, ",");
                sb_puts(b, "[");
                for (c = 0; c < m->cols; c++) {
                    if (c) sb_puts(b, ",");
                    v_to_text(V, b, &m->v[r * m->cols + c], 1);
                }
                sb_puts(b, "]");
            }
            sb_puts(b, "]");
        }
        break;
    }
    case T_SYM: str_text(b, v->u.y->text, 0); break;
    case T_GROB: { char g[3] = { 'G', (char)('0' + v->ib), 0 }; sb_puts(b, g); break; }
    default: break;
    }
}

const char *ppl_errmsg(int e)
{
    static const char *m[E_COUNT] = {
        "OK", "Syntax error", "Bad argument type", "Bad argument value",
        "Invalid dimension", "Undefined name", "Division by 0",
        "Index outside range", "Insufficient memory", "Infinite result",
        "Non-real result", "Wrong number of arguments", "Not supported",
        "Program stopped", "Undefined result", "Insufficient statistics data",
        "Not implemented yet", "Break"
    };
    return (e >= 0 && e < E_COUNT) ? m[e] : "Error";
}
