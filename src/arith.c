/* arith.c -- PPL operators over reals, # integers, complex numbers,
 * strings, lists (element-wise) and matrices. */
#include "vm.h"
#include "decx.h"
#include <stdlib.h>
#include <string.h>

int dec_err(int st)
{
    switch (st) {
    case DEC_OK: case DEC_EOVERFLOW: case DEC_EUNDERFLOW: return 0;
    case DEC_EDIVZERO: return E_DIV0;
    default: return E_BADVAL;
    }
}

int to_real(const value *v, real *r)
{
    if (v->t == T_REAL) { *r = v->u.r; return 0; }
    if (v->t == T_INT) { dec_from_u64(r, v->u.i.hi, v->u.i.lo); return 0; }
    return E_BADTYPE;
}

int to_int(const value *v, int32_t *n)
{
    if (v->t == T_REAL) return dec_to_int32(&v->u.r, n) ? E_BADVAL : 0;
    if (v->t == T_INT) {
        if (v->u.i.hi || v->u.i.lo > 0x7FFFFFFFu) return E_BADVAL;
        *n = (int32_t)v->u.i.lo;
        return 0;
    }
    return E_BADTYPE;
}

int to_index(const value *v, int32_t *n)
{
    if (v->t == T_REAL) {
        real f;
        dec_floor(&f, &v->u.r);     /* the Prime truncates fractional indices */
        return dec_to_int32_trunc(&v->u.r, n) ? E_INDEX : 0;
    }
    return to_int(v, n);
}

static void v_bool(value *v, int b) { v_int32(v, b ? 1 : 0); }

int c_make(value *v, const real *re, const real *im)
{
    v_none(v);
    if (dec_is_zero(im)) { v_real(v, re); return 0; }
    v->t = T_CPLX;
    v->u.c.re = *re;
    v->u.c.im = *im;
    return 0;
}

static int to_cplx(const value *v, cplx *c)
{
    if (v->t == T_CPLX) { *c = v->u.c; return 0; }
    dec_zero(&c->im);
    return to_real(v, &c->re);
}

/* ------------------------------------------------------------ complex */

#define CP (XN_WP + 4)

static int xr(real *r, const xn *x) { int st = xn_to_real(r, x); return dec_err(st); }

int c_mul(cplx *r, const cplx *a, const cplx *b)
{
    xn ar, ai, br, bi, t1, t2, re, im;
    real R, I;
    xn_from_real(&ar, &a->re); xn_from_real(&ai, &a->im);
    xn_from_real(&br, &b->re); xn_from_real(&bi, &b->im);
    xn_mul(&t1, &ar, &br, CP); xn_mul(&t2, &ai, &bi, CP); xn_sub(&re, &t1, &t2, CP);
    xn_mul(&t1, &ar, &bi, CP); xn_mul(&t2, &ai, &br, CP); xn_add(&im, &t1, &t2, CP);
    xr(&R, &re); xr(&I, &im);
    r->re = R; r->im = I;
    return 0;
}

int c_div(cplx *r, const cplx *a, const cplx *b)
{
    xn ar, ai, br, bi, t1, t2, d, re, im;
    xn_from_real(&ar, &a->re); xn_from_real(&ai, &a->im);
    xn_from_real(&br, &b->re); xn_from_real(&bi, &b->im);
    xn_mul(&t1, &br, &br, CP); xn_mul(&t2, &bi, &bi, CP); xn_add(&d, &t1, &t2, CP);
    if (xn_is_zero(&d)) return E_DIV0;
    xn_mul(&t1, &ar, &br, CP); xn_mul(&t2, &ai, &bi, CP); xn_add(&re, &t1, &t2, CP);
    xn_mul(&t1, &ai, &br, CP); xn_mul(&t2, &ar, &bi, CP); xn_sub(&im, &t1, &t2, CP);
    xn_div(&re, &re, &d, CP); xn_div(&im, &im, &d, CP);
    xr(&r->re, &re); xr(&r->im, &im);
    return 0;
}

int c_abs(real *r, const cplx *a)
{
    xn x, y, t1, t2;
    xn_from_real(&x, &a->re); xn_from_real(&y, &a->im);
    xn_mul(&t1, &x, &x, CP); xn_mul(&t2, &y, &y, CP); xn_add(&t1, &t1, &t2, CP);
    xn_sqrt(&t1, &t1, DEC_DIGITS);
    return xr(r, &t1);
}

int c_arg(ppl_vm *V, real *r, const cplx *a)
{
    return dec_err(dec_atan2(r, &a->im, &a->re, V->set.angle));
}

int c_sqrt(cplx *r, const cplx *a)
{
    xn x, y, m, t1, t2, re, im;
    xn_from_real(&x, &a->re); xn_from_real(&y, &a->im);
    xn_mul(&t1, &x, &x, CP); xn_mul(&t2, &y, &y, CP); xn_add(&m, &t1, &t2, CP);
    xn_sqrt(&m, &m, CP);
    xn_add(&t1, &m, &x, CP); xn_div_int(&t1, &t1, 2, CP); xn_sqrt(&re, &t1, CP);
    xn_sub(&t2, &m, &x, CP); xn_div_int(&t2, &t2, 2, CP);
    if (t2.s) xn_zero(&t2);
    xn_sqrt(&im, &t2, CP);
    if (y.s && im.n) im.s = 1;
    xr(&r->re, &re); xr(&r->im, &im);
    return 0;
}

int c_ln(cplx *r, const cplx *a)
{
    real m;
    int st;
    if (dec_is_zero(&a->re) && dec_is_zero(&a->im)) return E_BADVAL;
    c_abs(&m, a);
    st = dec_ln(&r->re, &m);
    if (st && !DEC_SOFT(st)) return dec_err(st);
    return dec_err(dec_atan2(&r->im, &a->im, &a->re, ANG_RAD));
}

int c_exp(cplx *r, const cplx *a)
{
    real e, c, s;
    int st = dec_exp(&e, &a->re);
    if (st && !DEC_SOFT(st)) return dec_err(st);
    dec_cos(&c, &a->im, ANG_RAD); dec_sin(&s, &a->im, ANG_RAD);
    dec_mul(&r->re, &e, &c); dec_mul(&r->im, &e, &s);
    return 0;
}

int c_pow(cplx *r, const cplx *a, const cplx *b)
{
    cplx l, t;
    int st;
    /* integer exponents: exact repeated multiplication */
    if (dec_is_zero(&b->im) && dec_is_int(&b->re)) {
        int32_t n;
        if (!dec_to_int32(&b->re, &n) && n > -1000000 && n < 1000000) {
            uint32_t k = (uint32_t)(n < 0 ? -n : n);
            cplx acc, base = *a;
            dec_from_int(&acc.re, 1); dec_zero(&acc.im);
            while (k) {
                if (k & 1) c_mul(&acc, &acc, &base);
                k >>= 1;
                if (k) c_mul(&base, &base, &base);
            }
            if (n < 0) {
                cplx one; dec_from_int(&one.re, 1); dec_zero(&one.im);
                return c_div(r, &one, &acc);
            }
            *r = acc;
            return 0;
        }
    }
    if (dec_is_zero(&a->re) && dec_is_zero(&a->im)) { dec_zero(&r->re); dec_zero(&r->im); return 0; }
    /* square root exactly */
    if (dec_is_zero(&b->im) && b->re.m[0] == 5000 && !b->re.m[1] && !b->re.m[2] && b->re.e == -1 && !b->re.s)
        return c_sqrt(r, a);
    st = c_ln(&l, a);
    if (st) return st;
    c_mul(&t, &l, b);
    return c_exp(r, &t);
}

/* -------------------------------------------------------------- ints */

static uint64_t iv(const value *v) { return ((uint64_t)v->u.i.hi << 32) | v->u.i.lo; }

static void set_iv(value *v, uint64_t x) { v->u.i.hi = (uint32_t)(x >> 32); v->u.i.lo = (uint32_t)x; }

static int64_t sext(uint64_t x, int w)
{
    if (w >= 64) return (int64_t)x;
    if (x & ((uint64_t)1 << (w - 1))) return (int64_t)(x | (~(uint64_t)0 << w));
    return (int64_t)x;
}

static int int_op(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    uint64_t x, y, r = 0;
    int w = a->iw ? a->iw : V->set.bits, sg = V->set.is_signed;
    value A = *a, B = *b;
    int_mask(V, &A); int_mask(V, &B);
    x = iv(&A); y = iv(&B);
    switch (op) {
    case OP_ADD: r = x + y; break;
    case OP_SUB: r = x - y; break;
    case OP_MUL: r = x * y; break;
    case OP_DIV: case OP_IQUO:
        if (!y) return E_DIV0;
        if (sg) r = (uint64_t)(sext(x, w) / sext(y, w)); else r = x / y;
        break;
    case OP_MOD:
        if (!y) { r = x; break; }
        if (sg) r = (uint64_t)(sext(x, w) % sext(y, w)); else r = x % y;
        break;
    case OP_POW: {
        uint64_t e = y; r = 1;
        while (e) { if (e & 1) r *= x; x *= x; e >>= 1; }
        break;
    }
    case OP_EQ: v_bool(out, x == y); return 0;
    case OP_NE: v_bool(out, x != y); return 0;
    case OP_LT: v_bool(out, sg ? sext(x, w) < sext(y, w) : x < y); return 0;
    case OP_LE: v_bool(out, sg ? sext(x, w) <= sext(y, w) : x <= y); return 0;
    case OP_GT: v_bool(out, sg ? sext(x, w) > sext(y, w) : x > y); return 0;
    case OP_GE: v_bool(out, sg ? sext(x, w) >= sext(y, w) : x >= y); return 0;
    default: return E_BADTYPE;
    }
    v_none(out);
    out->t = T_INT; out->ib = a->ib; out->iw = a->iw;
    set_iv(out, r);
    int_mask(V, out);
    return 0;
}

/* ------------------------------------------------------------- reals */

static int real_op(ppl_vm *V, int op, const real *x, const real *y, value *out)
{
    real r;
    int st = 0, c;
    switch (op) {
    case OP_ADD: st = dec_add(&r, x, y); break;
    case OP_SUB: st = dec_sub(&r, x, y); break;
    case OP_MUL: st = dec_mul(&r, x, y); break;
    case OP_DIV: st = dec_div(&r, x, y); break;
    case OP_MOD: st = dec_mod(&r, x, y); break;
    case OP_IQUO: {
        real q;
        st = dec_div(&q, x, y);
        if (!st) st = dec_floor(&r, &q);
        break;
    }
    case OP_POW:
        if (dec_is_zero(x) && y->s && !dec_is_zero(y)) return E_DIV0;
        st = dec_pow(&r, x, y);
        if (st == DEC_EDOMAIN && x->s) {
            cplx a, b, z;
            if (!V->set.complex_ok) return E_NONREAL;
            a.re = *x; dec_zero(&a.im); b.re = *y; dec_zero(&b.im);
            st = c_pow(&z, &a, &b);
            if (st) return st;
            return c_make(out, &z.re, &z.im);
        }
        break;
    case OP_NTHROOT:
        st = dec_nthroot(&r, x, y);
        if (st == DEC_EDOMAIN && y->s && !dec_is_zero(x)) {
            cplx a, b, z;
            real one;
            if (!V->set.complex_ok) return E_NONREAL;
            dec_from_int(&one, 1);
            a.re = *y; dec_zero(&a.im);
            dec_div(&b.re, &one, x); dec_zero(&b.im);
            st = c_pow(&z, &a, &b);
            if (st) return st;
            return c_make(out, &z.re, &z.im);
        }
        break;
    case OP_EQ: v_bool(out, dec_cmp(x, y) == 0); return 0;
    case OP_NE: v_bool(out, dec_cmp(x, y) != 0); return 0;
    case OP_LT: c = dec_cmp(x, y); v_bool(out, c == -1); return 0;
    case OP_LE: c = dec_cmp(x, y); v_bool(out, c == -1 || c == 0); return 0;
    case OP_GT: c = dec_cmp(x, y); v_bool(out, c == 1); return 0;
    case OP_GE: c = dec_cmp(x, y); v_bool(out, c == 1 || c == 0); return 0;
    case OP_AND: v_bool(out, !dec_is_zero(x) && !dec_is_zero(y)); return 0;
    case OP_OR: v_bool(out, !dec_is_zero(x) || !dec_is_zero(y)); return 0;
    case OP_XOR: v_bool(out, !dec_is_zero(x) != !dec_is_zero(y)); return 0;
    default: return E_BADTYPE;
    }
    if (st && !DEC_SOFT(st)) return dec_err(st);
    v_real(out, &r);
    return 0;
}

static int cplx_op(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    cplx x, y, z;
    int st = 0, t;
    if (to_cplx(a, &x) || to_cplx(b, &y)) return E_BADTYPE;
    switch (op) {
    case OP_ADD: dec_add(&z.re, &x.re, &y.re); dec_add(&z.im, &x.im, &y.im); break;
    case OP_SUB: dec_sub(&z.re, &x.re, &y.re); dec_sub(&z.im, &x.im, &y.im); break;
    case OP_MUL: st = c_mul(&z, &x, &y); break;
    case OP_DIV: st = c_div(&z, &x, &y); break;
    case OP_POW: st = c_pow(&z, &x, &y); break;
    case OP_EQ: v_bool(out, !dec_cmp(&x.re, &y.re) && !dec_cmp(&x.im, &y.im)); return 0;
    case OP_NE: v_bool(out, dec_cmp(&x.re, &y.re) || dec_cmp(&x.im, &y.im)); return 0;
    case OP_AND: case OP_OR: case OP_XOR: {
        int p, q;
        v_truth(a, &p); v_truth(b, &q);
        t = op == OP_AND ? (p && q) : op == OP_OR ? (p || q) : (!p != !q);
        v_bool(out, t);
        return 0;
    }
    default: return E_BADTYPE;
    }
    (void)V;
    if (st) return st;
    return c_make(out, &z.re, &z.im);
}

/* ------------------------------------------------------------- strings */

static int str_op(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    if (op == OP_ADD) {
        sbuf s;
        int st;
        sb_init(&s);
        v_to_text(V, &s, a, 0);
        v_to_text(V, &s, b, 0);
        st = v_str_utf8(out, s.p ? s.p : "", s.n);
        sb_free(&s);
        return st;
    }
    if (op == OP_EQ) { v_bool(out, v_equal(a, b)); return 0; }
    if (op == OP_NE) { v_bool(out, !v_equal(a, b)); return 0; }
    if (a->t == T_STR && b->t == T_STR && op >= OP_LT && op <= OP_GE) {
        const pstr *x = a->u.s, *y = b->u.s;
        uint32_t i, n = x->len < y->len ? x->len : y->len;
        int c = 0;
        for (i = 0; i < n && !c; i++) if (x->c[i] != y->c[i]) c = x->c[i] < y->c[i] ? -1 : 1;
        if (!c) c = x->len < y->len ? -1 : x->len > y->len ? 1 : 0;
        switch (op) {
        case OP_LT: v_bool(out, c < 0); break;
        case OP_LE: v_bool(out, c <= 0); break;
        case OP_GT: v_bool(out, c > 0); break;
        default: v_bool(out, c >= 0); break;
        }
        return 0;
    }
    return E_BADTYPE;
}

/* ------------------------------------------------------------- lists */

static int list_op(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    uint32_t n, i;
    value r;
    int st;
    if (op == OP_EQ || op == OP_NE) {
        /* == on two lists of different sizes: not equal */
        if (a->t == T_LIST && b->t == T_LIST && a->u.l->n != b->u.l->n) { v_bool(out, op == OP_NE); return 0; }
    }
    if (a->t == T_LIST && b->t == T_LIST) {
        if (a->u.l->n != b->u.l->n) return E_DIM;
        n = a->u.l->n;
    } else n = a->t == T_LIST ? a->u.l->n : b->u.l->n;
    if ((st = v_list(&r, n))) return st;
    for (i = 0; i < n; i++) {
        const value *x = a->t == T_LIST ? &a->u.l->v[i] : a;
        const value *y = b->t == T_LIST ? &b->u.l->v[i] : b;
        value e;
        st = ar_binary(V, op, x, y, &e);
        if (st) { v_decref(&r); return st; }
        r.u.l->v[i] = e;
    }
    *out = r;
    return 0;
}

/* ------------------------------------------------------------ matrices */

static value *M_AT(pmat *m, int r, int c) { return &m->v[r * m->cols + c]; }

static int mat_map2(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    /* element-wise; a scalar on either side is broadcast */
    const pmat *ma = a->t == T_MAT ? a->u.m : NULL, *mb = b->t == T_MAT ? b->u.m : NULL;
    const pmat *s = ma ? ma : mb;
    int n, i, st;
    value r;
    if (ma && mb && (ma->rows != mb->rows || ma->cols != mb->cols || ma->vec != mb->vec)) return E_DIM;
    if ((st = v_mat(&r, s->rows, s->cols, s->vec))) return st;
    n = s->rows * s->cols;
    for (i = 0; i < n; i++) {
        value e;
        st = ar_binary(V, op, ma ? &ma->v[i] : a, mb ? &mb->v[i] : b, &e);
        if (st) { v_decref(&r); return st; }
        if (e.t != T_REAL && e.t != T_CPLX) { v_decref(&e); v_decref(&r); return E_BADTYPE; }
        r.u.m->v[i] = e;
    }
    *out = r;
    return 0;
}

int mat_mul(ppl_vm *V, const value *a, const value *b, value *out)
{
    const pmat *A = a->u.m, *B = b->u.m;
    int ar = A->vec ? 1 : A->rows, ac = A->cols, br, bc, i, j, k, st, vec = 0;
    value r;
    if (B->vec) { br = B->cols; bc = 1; } else { br = B->rows; bc = B->cols; }
    if (A->vec && B->vec) {
        /* vector . vector: dot product */
        value acc, t, u;
        if (A->cols != B->cols) return E_DIM;
        v_int32(&acc, 0);
        for (i = 0; i < A->cols; i++) {
            if ((st = ar_binary(V, OP_MUL, &A->v[i], &B->v[i], &t))) return st;
            if ((st = ar_binary(V, OP_ADD, &acc, &t, &u))) return st;
            acc = u;
        }
        *out = acc;
        return 0;
    }
    if (A->vec) { /* row vector times matrix */
        vec = 1;
    } else if (B->vec) vec = 1;
    if (ac != br) return E_DIM;
    if (vec) st = v_mat(&r, 1, A->vec ? bc : ar, 1);
    else st = v_mat(&r, ar, bc, 0);
    if (st) return st;
    for (i = 0; i < ar; i++)
        for (j = 0; j < bc; j++) {
            value acc, t, u;
            v_int32(&acc, 0);
            for (k = 0; k < ac; k++) {
                const value *x = A->vec ? &A->v[k] : &A->v[i * ac + k];
                const value *y = B->vec ? &B->v[k] : &B->v[k * bc + j];
                if ((st = ar_binary(V, OP_MUL, x, y, &t))) { v_decref(&r); return st; }
                if ((st = ar_binary(V, OP_ADD, &acc, &t, &u))) { v_decref(&r); return st; }
                acc = u;
            }
            if (vec) r.u.m->v[A->vec ? j : i] = acc;
            else r.u.m->v[i * bc + j] = acc;
        }
    *out = r;
    return 0;
}

static int v_absval(ppl_vm *V, const value *v, real *r)
{
    (void)V;
    if (v->t == T_CPLX) return c_abs(r, &v->u.c);
    if (to_real(v, r)) return E_BADTYPE;
    r->s = 0;
    return 0;
}

/* Gauss-Jordan on real matrices in the xn engine: 26 digits, one rounding
 * per element at the end, so [[1,2],[3,4]]^-1 is exactly [[-2,1],[1.5,-0.5]] */
static int mat_inv_real(const pmat *A, value *out, value *det)
{
    int n = A->rows, i, j, k, sign = 0, st;
    const int P = XN_WP;
    xn *w = (xn *)malloc((size_t)n * (size_t)(2 * n) * sizeof(xn)), d;
    real r;
    if (!w) return E_MEM;
#define W(i, j) w[(i) * 2 * n + (j)]
    for (i = 0; i < n; i++)
        for (j = 0; j < 2 * n; j++) {
            if (j < n) xn_from_real(&W(i, j), &A->v[i * n + j].u.r);
            else xn_from_int(&W(i, j), i == j - n);
        }
    xn_from_int(&d, 1);
    for (k = 0; k < n; k++) {
        int p = -1;
        for (i = k; i < n; i++)
            if (W(i, k).n && (p < 0 || xn_cmpmag(&W(i, k), &W(p, k)) > 0)) p = i;
        if (p < 0) {
            free(w);
            if (det) { v_int32(det, 0); return out ? E_BADVAL : 0; }
            return E_BADVAL;
        }
        if (p != k) {
            for (j = 0; j < 2 * n; j++) { xn t = W(k, j); W(k, j) = W(p, j); W(p, j) = t; }
            sign ^= 1;
        }
        {
            xn piv = W(k, k);
            xn_mul(&d, &d, &piv, P);
            for (j = k; j < 2 * n; j++) xn_div(&W(k, j), &W(k, j), &piv, P);
        }
        for (i = 0; i < n; i++) {
            xn f, t;
            if (i == k || !W(i, k).n) continue;
            f = W(i, k);
            for (j = k; j < 2 * n; j++) {
                if (!W(k, j).n) continue;
                xn_mul(&t, &f, &W(k, j), P);
                xn_sub(&W(i, j), &W(i, j), &t, P);
            }
        }
    }
    if (det) {
        if (sign && d.n) d.s ^= 1;
        xn_to_real(&r, &d);
        v_real(det, &r);
    }
    if (out) {
        if ((st = v_mat(out, n, n, 0))) { free(w); return st; }
        for (i = 0; i < n; i++)
            for (j = 0; j < n; j++) {
                xn_to_real(&r, &W(i, n + j));
                v_real(&out->u.m->v[i * n + j], &r);
            }
    }
#undef W
    free(w);
    return 0;
}

/* Gauss-Jordan with partial pivoting; det may be NULL */
int mat_inv(ppl_vm *V, const value *a, value *out, value *det)
{
    const pmat *A = a->u.m;
    int n = A->rows, i, j, k, st, sign = 0;
    value w, inv, d;
    if (A->vec || A->rows != A->cols) return E_DIM;
    {
        int allreal = 1;
        for (i = 0; i < n * n; i++) if (A->v[i].t != T_REAL) allreal = 0;
        if (allreal) return mat_inv_real(A, out, det);
    }
    if ((st = v_mat(&w, n, n, 0))) return st;
    memcpy(w.u.m->v, A->v, (size_t)n * n * sizeof(value));
    if ((st = v_mat(&inv, n, n, 0))) { v_decref(&w); return st; }
    for (i = 0; i < n; i++) v_int32(M_AT(inv.u.m, i, i), 1);
    v_int32(&d, 1);
    for (k = 0; k < n; k++) {
        int p = -1;
        real best, t;
        dec_zero(&best);
        for (i = k; i < n; i++) {
            v_absval(V, M_AT(w.u.m, i, k), &t);
            if (!dec_is_zero(&t) && (p < 0 || dec_cmp(&t, &best) > 0)) { best = t; p = i; }
        }
        if (p < 0) {
            v_decref(&w); v_decref(&inv);
            if (det) { v_int32(det, 0); return out ? E_BADVAL : 0; }
            return E_BADVAL;
        }
        if (p != k) {
            for (j = 0; j < n; j++) {
                value t1 = *M_AT(w.u.m, k, j); *M_AT(w.u.m, k, j) = *M_AT(w.u.m, p, j); *M_AT(w.u.m, p, j) = t1;
                t1 = *M_AT(inv.u.m, k, j); *M_AT(inv.u.m, k, j) = *M_AT(inv.u.m, p, j); *M_AT(inv.u.m, p, j) = t1;
            }
            sign ^= 1;
        }
        {
            value piv = *M_AT(w.u.m, k, k), t1;
            ar_binary(V, OP_MUL, &d, &piv, &t1); d = t1;
            for (j = 0; j < n; j++) {
                ar_binary(V, OP_DIV, M_AT(w.u.m, k, j), &piv, &t1); *M_AT(w.u.m, k, j) = t1;
                ar_binary(V, OP_DIV, M_AT(inv.u.m, k, j), &piv, &t1); *M_AT(inv.u.m, k, j) = t1;
            }
        }
        for (i = 0; i < n; i++) {
            value f, t1, t2;
            if (i == k) continue;
            f = *M_AT(w.u.m, i, k);
            if (f.t == T_REAL && dec_is_zero(&f.u.r)) continue;
            for (j = 0; j < n; j++) {
                ar_binary(V, OP_MUL, &f, M_AT(w.u.m, k, j), &t1);
                ar_binary(V, OP_SUB, M_AT(w.u.m, i, j), &t1, &t2); *M_AT(w.u.m, i, j) = t2;
                ar_binary(V, OP_MUL, &f, M_AT(inv.u.m, k, j), &t1);
                ar_binary(V, OP_SUB, M_AT(inv.u.m, i, j), &t1, &t2); *M_AT(inv.u.m, i, j) = t2;
            }
        }
    }
    v_decref(&w);
    if (det) { if (sign) ar_neg(V, &d, det); else *det = d; }
    if (out) *out = inv; else v_decref(&inv);
    return 0;
}

static int mat_op(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    int st;
    if (op == OP_EQ) { v_bool(out, v_equal(a, b)); return 0; }
    if (op == OP_NE) { v_bool(out, !v_equal(a, b)); return 0; }
    if (a->t == T_STR || b->t == T_STR) return E_BADTYPE;
    switch (op) {
    case OP_ADD: case OP_SUB:
        if (a->t != T_MAT || b->t != T_MAT) return E_BADTYPE;
        return mat_map2(V, op, a, b, out);
    case OP_MUL:
        if (a->t == T_MAT && b->t == T_MAT) return mat_mul(V, a, b, out);
        return mat_map2(V, op, a, b, out);
    case OP_DIV:
        if (b->t != T_MAT) return mat_map2(V, op, a, b, out);
        {
            value inv;
            if ((st = mat_inv(V, b, &inv, NULL))) return st;
            if (a->t == T_MAT) st = mat_mul(V, a, &inv, out);
            else st = mat_map2(V, OP_MUL, a, &inv, out);
            v_decref(&inv);
            return st;
        }
    case OP_POW: {
        int32_t n;
        value acc, base;
        if (a->t != T_MAT || to_int(b, &n)) return E_BADTYPE;
        if (a->u.m->vec || a->u.m->rows != a->u.m->cols) return E_DIM;
        if (n < 0) { if ((st = mat_inv(V, a, &base, NULL))) return st; n = -n; }
        else v_copy(&base, a);
        if ((st = v_mat(&acc, a->u.m->rows, a->u.m->cols, 0))) { v_decref(&base); return st; }
        { int i; for (i = 0; i < a->u.m->rows; i++) v_int32(M_AT(acc.u.m, i, i), 1); }
        while (n) {
            value t;
            if (n & 1) { mat_mul(V, &acc, &base, &t); v_decref(&acc); acc = t; }
            n >>= 1;
            if (n) { mat_mul(V, &base, &base, &t); v_decref(&base); base = t; }
        }
        v_decref(&base);
        *out = acc;
        return 0;
    }
    default: return E_BADTYPE;
    }
}

/* ------------------------------------------------------------ dispatch */

int ar_binary(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    real x, y;
    if (a->t == T_LIST || b->t == T_LIST) return list_op(V, op, a, b, out);
    if (a->t == T_STR || b->t == T_STR) return str_op(V, op, a, b, out);
    if (a->t == T_MAT || b->t == T_MAT) return mat_op(V, op, a, b, out);
    if (a->t == T_CPLX || b->t == T_CPLX) return cplx_op(V, op, a, b, out);
    if (a->t == T_INT && b->t == T_INT) return int_op(V, op, a, b, out);
    if (a->t == T_SYM || b->t == T_SYM) return E_UNSUPPORTED;
    if (to_real(a, &x) || to_real(b, &y)) return E_BADTYPE;
    return real_op(V, op, &x, &y, out);
}

int ar_neg(ppl_vm *V, const value *a, value *out)
{
    switch (a->t) {
    case T_REAL: v_none(out); out->t = T_REAL; dec_neg(&out->u.r, &a->u.r); return 0;
    case T_INT: {
        *out = *a;
        set_iv(out, (uint64_t)0 - iv(a));
        int_mask(V, out);
        return 0;
    }
    case T_CPLX: {
        real re, im;
        dec_neg(&re, &a->u.c.re); dec_neg(&im, &a->u.c.im);
        return c_make(out, &re, &im);
    }
    case T_LIST: case T_MAT: {
        value m1;
        v_int32(&m1, -1);
        return ar_binary(V, OP_MUL, a, &m1, out);
    }
    default: return E_BADTYPE;
    }
}

int ar_not(ppl_vm *V, const value *a, value *out)
{
    int t, st;
    if (a->t == T_LIST) {
        uint32_t i;
        value r;
        if ((st = v_list(&r, a->u.l->n))) return st;
        for (i = 0; i < a->u.l->n; i++)
            if ((st = ar_not(V, &a->u.l->v[i], &r.u.l->v[i]))) { v_decref(&r); return st; }
        *out = r;
        return 0;
    }
    if ((st = v_truth(a, &t))) return st;
    v_bool(out, !t);
    return 0;
}

int ar_map1(ppl_vm *V, const value *a, rfn1 rf, cfn1 cf, value *out)
{
    int st;
    switch (a->t) {
    case T_REAL: return rf(V, &a->u.r, out);
    case T_INT: { real r; to_real(a, &r); return rf(V, &r, out); }
    case T_CPLX: return cf ? cf(V, &a->u.c, out) : E_BADTYPE;
    case T_LIST: {
        uint32_t i;
        value r;
        if ((st = v_list(&r, a->u.l->n))) return st;
        for (i = 0; i < a->u.l->n; i++)
            if ((st = ar_map1(V, &a->u.l->v[i], rf, cf, &r.u.l->v[i]))) { v_decref(&r); return st; }
        *out = r;
        return 0;
    }
    case T_MAT: {
        int i, n = a->u.m->rows * a->u.m->cols;
        value r;
        if ((st = v_mat(&r, a->u.m->rows, a->u.m->cols, a->u.m->vec))) return st;
        for (i = 0; i < n; i++)
            if ((st = ar_map1(V, &a->u.m->v[i], rf, cf, &r.u.m->v[i]))) { v_decref(&r); return st; }
        *out = r;
        return 0;
    }
    default: return E_BADTYPE;
    }
}
