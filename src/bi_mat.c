/* bi_mat.c -- matrix built-ins.
 *
 * Real matrices are processed in the xn engine with MP significant digits
 * (14 guard digits) and every element is rounded once to 12 digits at the
 * end, so DET([[1,2],[3,4]]) is exactly -2 and RREF gives exact decimals.
 * Complex elements go through the generic value arithmetic (ar_binary).
 * Eigenvalues come from the characteristic polynomial (Faddeev-LeVerrier)
 * solved by Durand-Kerner in complex xn arithmetic, which is also POLYROOT.
 * The Prime computes LQ/QR/SVD/SCHUR/EIGEN* and the lower-case names with
 * binary doubles in its CAS, so their last digits can differ from ours. */
#include "bi.h"
#include "decx.h"
#include <stdlib.h>
#include <string.h>

#define MP 26                     /* working precision */

/* ------------------------------------------------------------ helpers */

static int nrows(const pmat *m) { return m->vec ? 1 : m->rows; }
static value *EL(pmat *m, int i, int j) { return &m->v[i * m->cols + j]; }

static int need_mat(const value *v) { return v->t == T_MAT ? 0 : E_BADTYPE; }

static int need_square(const value *v)
{
    if (v->t != T_MAT) return E_BADTYPE;
    if (v->u.m->vec || v->u.m->rows != v->u.m->cols) return E_DIM;
    return 0;
}

/* apply fn to each matrix of a list argument */
static int listmap(ppl_vm *V, int argc, value *a, value *out, int (*fn)(ppl_vm *, int, value *, value *))
{
    uint32_t i;
    int st;
    value r;
    if (a[0].t != T_LIST) return fn(V, argc, a, out);
    if ((st = v_list(&r, a[0].u.l->n))) return st;
    for (i = 0; i < a[0].u.l->n; i++) {
        value args[4];
        int k;
        for (k = 0; k < argc && k < 4; k++) args[k] = a[k];
        args[0] = a[0].u.l->v[i];
        if ((st = fn(V, argc, args, &r.u.l->v[i]))) { v_decref(&r); return st; }
    }
    *out = r;
    return 0;
}

/* real matrices in xn */
typedef struct { int r, c; xn *e; } xm;

static xn *XE(xm *m, int i, int j) { return &m->e[i * m->c + j]; }

static int xm_new(xm *m, int r, int c)
{
    int i;
    m->r = r; m->c = c;
    m->e = (xn *)malloc((size_t)((r * c) > 0 ? r * c : 1) * sizeof(xn));
    if (!m->e) return E_MEM;
    for (i = 0; i < r * c; i++) xn_zero(&m->e[i]);
    return 0;
}

static void xm_free(xm *m) { free(m->e); m->e = NULL; }

/* E_BADTYPE when an element is not real */
static int xm_from(const value *v, xm *m)
{
    const pmat *p = v->u.m;
    int i, n = nrows(p) * p->cols, st;
    if ((st = xm_new(m, nrows(p), p->cols))) return st;
    for (i = 0; i < n; i++) {
        if (p->v[i].t != T_REAL) { xm_free(m); return E_BADTYPE; }
        xn_from_real(&m->e[i], &p->v[i].u.r);
    }
    return 0;
}

static int xm_to(xm *m, value *out, int vec)
{
    int i, st;
    if ((st = v_mat(out, m->r, m->c, vec))) return st;
    for (i = 0; i < m->r * m->c; i++) {
        real r;
        xn_to_real(&r, &m->e[i]);
        v_real(&out->u.m->v[i], &r);
    }
    return 0;
}

static void xn_absv(xn *r, const xn *a) { *r = *a; r->s = 0; }

static void xm_maxabs(xm *m, xn *mx);
static int negligible(const xn *x, const xn *scale);

/* entries that are rounding noise beside the largest one become 0 */
static void xm_clean(xm *m)
{
    xn mx;
    int i;
    xm_maxabs(m, &mx);
    for (i = 0; i < m->r * m->c; i++) if (negligible(&m->e[i], &mx)) xn_zero(&m->e[i]);
}

/* |x| below the noise floor of a computation on values of size `scale` */
static int negligible(const xn *x, const xn *scale)
{
    if (!x->n) return 1;
    if (!scale->n) return 0;
    return xn_top(x) < xn_top(scale) - (MP - 6);
}

static void xm_maxabs(xm *m, xn *mx)
{
    int i;
    xn_zero(mx);
    for (i = 0; i < m->r * m->c; i++) if (xn_cmpmag(&m->e[i], mx) > 0) xn_absv(mx, &m->e[i]);
}

/* Gauss-Jordan in place (reduced=1) or row echelon; returns the rank and
 * the determinant factor (product of pivots with sign) when det != NULL */
static int xm_eliminate(xm *m, int reduced, xn *det)
{
    int row = 0, col, i, j, k;
    xn scale, t;
    xm_maxabs(m, &scale);
    if (det) xn_from_int(det, 1);
    for (col = 0; col < m->c && row < m->r; col++) {
        int p = -1;
        for (i = row; i < m->r; i++)
            if (!negligible(XE(m, i, col), &scale) && (p < 0 || xn_cmpmag(XE(m, i, col), XE(m, p, col)) > 0)) p = i;
        if (p < 0) {
            for (i = row; i < m->r; i++) xn_zero(XE(m, i, col));
            if (det) xn_zero(det);
            continue;
        }
        if (p != row) {
            for (j = 0; j < m->c; j++) { t = *XE(m, p, j); *XE(m, p, j) = *XE(m, row, j); *XE(m, row, j) = t; }
            if (det && det->n) det->s ^= 1;
        }
        {
            xn piv = *XE(m, row, col);
            if (det) xn_mul(det, det, &piv, MP);
            if (reduced) for (j = col; j < m->c; j++) xn_div(XE(m, row, j), XE(m, row, j), &piv, MP);
            for (i = reduced ? 0 : row + 1; i < m->r; i++) {
                xn f;
                if (i == row || !XE(m, i, col)->n) continue;
                if (reduced) f = *XE(m, i, col);
                else xn_div(&f, XE(m, i, col), &piv, MP);
                for (k = col; k < m->c; k++) {
                    xn_mul(&t, &f, XE(m, row, k), MP);
                    xn_sub(XE(m, i, k), XE(m, i, k), &t, MP);
                    if (negligible(XE(m, i, k), &scale)) xn_zero(XE(m, i, k));
                }
                xn_zero(XE(m, i, col));
            }
        }
        row++;
    }
    if (det && row < m->c) xn_zero(det);
    return row;
}

static void xm_ident(xm *m)
{
    int i;
    for (i = 0; i < m->r * m->c; i++) xn_zero(&m->e[i]);
    for (i = 0; i < m->r && i < m->c; i++) xn_from_int(XE(m, i, i), 1);
}

static int xm_mul(xm *r, xm *a, xm *b)  /* r = a*b, r freshly allocated */
{
    int i, j, k, st;
    if ((st = xm_new(r, a->r, b->c))) return st;
    for (i = 0; i < a->r; i++)
        for (j = 0; j < b->c; j++) {
            xn acc, t;
            xn_zero(&acc);
            for (k = 0; k < a->c; k++) { xn_mul(&t, XE(a, i, k), XE(b, k, j), MP + 4); xn_add(&acc, &acc, &t, MP + 4); }
            xn_round(&acc, MP);
            *XE(r, i, j) = acc;
        }
    return 0;
}

static int xm_trn(xm *r, xm *a)
{
    int i, j, st;
    if ((st = xm_new(r, a->c, a->r))) return st;
    for (i = 0; i < a->r; i++) for (j = 0; j < a->c; j++) *XE(r, j, i) = *XE(a, i, j);
    return 0;
}

/* ------------------------------------------------------------ complex xn */

typedef struct { xn re, im; } cx;

static void cx_add(cx *r, const cx *a, const cx *b) { xn_add(&r->re, &a->re, &b->re, MP); xn_add(&r->im, &a->im, &b->im, MP); }
static void cx_sub(cx *r, const cx *a, const cx *b) { xn_sub(&r->re, &a->re, &b->re, MP); xn_sub(&r->im, &a->im, &b->im, MP); }
static void cx_mul(cx *r, const cx *a, const cx *b)
{
    xn t1, t2, re;
    xn_mul(&t1, &a->re, &b->re, MP + 2); xn_mul(&t2, &a->im, &b->im, MP + 2); xn_sub(&re, &t1, &t2, MP);
    xn_mul(&t1, &a->re, &b->im, MP + 2); xn_mul(&t2, &a->im, &b->re, MP + 2); xn_add(&r->im, &t1, &t2, MP);
    r->re = re;
}
static void cx_abs2(xn *r, const cx *a)
{
    xn t;
    xn_mul(r, &a->re, &a->re, MP + 2); xn_mul(&t, &a->im, &a->im, MP + 2); xn_add(r, r, &t, MP);
}
static int cx_div(cx *r, const cx *a, const cx *b)
{
    xn d, t1, t2, re;
    cx_abs2(&d, b);
    if (!d.n) return E_DIV0;
    xn_mul(&t1, &a->re, &b->re, MP + 2); xn_mul(&t2, &a->im, &b->im, MP + 2); xn_add(&re, &t1, &t2, MP + 2);
    xn_mul(&t1, &a->im, &b->re, MP + 2); xn_mul(&t2, &a->re, &b->im, MP + 2); xn_sub(&r->im, &t1, &t2, MP + 2);
    xn_div(&r->re, &re, &d, MP); xn_div(&r->im, &r->im, &d, MP);
    return 0;
}

static int cx_from(const value *v, cx *c)
{
    xn_zero(&c->im);
    if (v->t == T_CPLX) { xn_from_real(&c->re, &v->u.c.re); xn_from_real(&c->im, &v->u.c.im); return 0; }
    if (v->t == T_REAL) { xn_from_real(&c->re, &v->u.r); return 0; }
    if (v->t == T_INT) { real r; to_real(v, &r); xn_from_real(&c->re, &r); return 0; }
    return E_BADTYPE;
}

/* rounded to 12 digits; parts negligible beside the other become 0 */
static void cx_to(const cx *c, value *out)
{
    xn re = c->re, im = c->im;
    real R, I;
    if (negligible(&im, &re)) xn_zero(&im);
    if (negligible(&re, &im)) xn_zero(&re);
    xn_to_real(&R, &re); xn_to_real(&I, &im);
    c_make(out, &R, &I);
}

static void cluster_fix(cx *z, int n, const cx *a);

/* Durand-Kerner: the n roots of sum p[k] x^(n-k), p[0] != 0 */
static int dk_roots(const cx *p, int n, cx *z)
{
    cx *a = (cx *)malloc((size_t)(n + 1) * sizeof(cx)), w, t;
    int i, k, it;
    xn bound, tol;
    if (!a) return E_MEM;
    for (i = 0; i <= n; i++) cx_div(&a[i], &p[i], &p[0]);
    /* radius: 1 + max |a_i| */
    xn_from_int(&bound, 1);
    for (i = 1; i <= n; i++) {
        xn m;
        cx_abs2(&m, &a[i]);
        xn_sqrt(&m, &m, 10);
        if (xn_cmp(&m, &bound) > 0) bound = m;
    }
    xn_from_str(&w.re, "0.4"); xn_from_str(&w.im, "0.9");
    t.re = bound; xn_zero(&t.im);
    for (k = 0; k < n; k++) { z[k] = t; cx_mul(&t, &t, &w); }
    xn_from_str(&tol, "1E-24");
    for (it = 0; it < 800; it++) {
        int done = 1;
        for (k = 0; k < n; k++) {
            cx pv = a[0], den, d, step;
            xn m1, m2;
            for (i = 1; i <= n; i++) { cx_mul(&pv, &pv, &z[k]); cx_add(&pv, &pv, &a[i]); }
            xn_from_int(&den.re, 1); xn_zero(&den.im);
            for (i = 0; i < n; i++) if (i != k) { cx_sub(&d, &z[k], &z[i]); cx_mul(&den, &den, &d); }
            if (cx_div(&step, &pv, &den)) { xn_from_str(&step.re, "1E-6"); xn_zero(&step.im); }
            cx_sub(&z[k], &z[k], &step);
            cx_abs2(&m1, &step); cx_abs2(&m2, &z[k]);
            xn_mul(&m2, &m2, &tol, 8);
            if (m1.n && xn_cmp(&m1, &m2) > 0) done = 0;
        }
        if (done) break;
    }
    cluster_fix(z, n, a);
    free(a);
    return 0;
}

/* A k-fold root comes out of Durand-Kerner as k points spread around it
 * (error ~ eps^(1/k)); their mean is accurate to ~eps.  Roots closer than
 * 1e-5 (relative) are taken as one multiple root. */
/* Newton on the polynomial c (degree n, c[0] = 1) from z */
static void newton_polish(const cx *c, int n, cx *z)
{
    int it, i;
    for (it = 0; it < 40; it++) {
        cx p = c[0], d, step, k;
        xn_zero(&d.re); xn_zero(&d.im);
        for (i = 1; i <= n; i++) {
            cx_mul(&d, &d, z); cx_add(&d, &d, &p);       /* derivative by Horner */
            cx_mul(&p, &p, z); cx_add(&p, &p, &c[i]);
        }
        if (cx_div(&step, &p, &d)) return;
        cx_sub(z, z, &step);
        cx_abs2(&k.re, &step);
        if (!k.re.n) return;
        cx_abs2(&k.im, z);
        if (xn_top(&k.re) < xn_top(&k.im) - 2 * MP) return;
    }
}

static void cluster_fix(cx *z, int n, const cx *a)
{
    int i, j, *grp = (int *)malloc((size_t)n * sizeof(int));
    xn tol;
    if (!grp) return;
    xn_from_str(&tol, "1E-10");             /* squared distance, relative */
    for (i = 0; i < n; i++) grp[i] = i;
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++) {
            cx d;
            xn d2, m2, one;
            cx_sub(&d, &z[i], &z[j]);
            cx_abs2(&d2, &d);
            cx_abs2(&m2, &z[i]);
            xn_from_int(&one, 1);
            if (xn_cmp(&m2, &one) < 0) m2 = one;
            xn_mul(&m2, &m2, &tol, 10);
            if (xn_cmp(&d2, &m2) < 0) { int g = grp[j], k; for (k = 0; k < n; k++) if (grp[k] == g) grp[k] = grp[i]; }
        }
    for (i = 0; i < n; i++) {
        cx sum;
        int cnt = 0;
        if (grp[i] != i) continue;
        xn_zero(&sum.re); xn_zero(&sum.im);
        for (j = 0; j < n; j++) if (grp[j] == i) { cx_add(&sum, &sum, &z[j]); cnt++; }
        if (cnt < 2) continue;
        xn_div_int(&sum.re, &sum.re, cnt, MP); xn_div_int(&sum.im, &sum.im, cnt, MP);
        {
            /* a cnt-fold root is a simple root of the (cnt-1)-th derivative */
            cx *d = (cx *)malloc((size_t)(n + 1) * sizeof(cx));
            int deg = n, k, m;
            if (d) {
                for (k = 0; k <= n; k++) d[k] = a[k];
                for (m = 1; m < cnt; m++) {
                    for (k = 0; k < deg; k++) { xn_mul_int(&d[k].re, &d[k].re, deg - k, MP); xn_mul_int(&d[k].im, &d[k].im, deg - k, MP); }
                    deg--;
                }
                for (k = 1; k <= deg; k++) cx_div(&d[k], &d[k], &d[0]);
                xn_from_int(&d[0].re, 1); xn_zero(&d[0].im);
                newton_polish(d, deg, &sum);
                free(d);
            }
        }
        for (j = 0; j < n; j++) if (grp[j] == i) z[j] = sum;
    }
    free(grp);
}

static int cx_less(const cx *a, const cx *b)   /* by real part, then imaginary */
{
    xn d, ma, mb;
    cx_abs2(&ma, a); cx_abs2(&mb, b);
    if (xn_cmp(&mb, &ma) > 0) ma = mb;
    xn_sqrt(&ma, &ma, 10);
    xn_sub(&d, &a->re, &b->re, MP);
    if (!negligible(&d, &ma)) return d.s;
    xn_sub(&d, &a->im, &b->im, MP);
    if (!negligible(&d, &ma)) return d.s;
    return 0;
}

static void cx_sort(cx *z, int n, int descending)
{
    int i, j;
    for (i = 1; i < n; i++) {
        cx t = z[i];
        for (j = i; j > 0 && (descending ? cx_less(&z[j - 1], &t) : cx_less(&t, &z[j - 1])); j--) z[j] = z[j - 1];
        z[j] = t;
    }
}

/* characteristic polynomial of a real square matrix (Faddeev-LeVerrier):
 * c[0] = 1, det(xI - A) = sum c[k] x^(n-k) */
static int charpoly(xm *A, cx *c)
{
    int n = A->r, k, i, st;
    xm M, AM;
    if ((st = xm_new(&M, n, n))) return st;
    xn_from_int(&c[0].re, 1); xn_zero(&c[0].im);
    for (k = 1; k <= n; k++) {
        xn tr;
        for (i = 0; i < n; i++) xn_add(XE(&M, i, i), XE(&M, i, i), &c[k - 1].re, MP);   /* M += c I */
        if ((st = xm_mul(&AM, A, &M))) { xm_free(&M); return st; }
        xm_free(&M);
        M = AM;
        xn_zero(&tr);
        for (i = 0; i < n; i++) xn_add(&tr, &tr, XE(&M, i, i), MP);
        xn_div_int(&c[k].re, &tr, -k, MP);
        xn_zero(&c[k].im);
    }
    xm_free(&M);
    return 0;
}

/* eigenvalues of a real square matrix, sorted by decreasing real part */
static int eig_x(xm *A, cx **out)
{
    cx *c, *z;
    int st;
    c = (cx *)malloc((size_t)(A->r + 1) * sizeof(cx));
    z = (cx *)malloc((size_t)(A->r + 1) * sizeof(cx));
    if (!c || !z) { free(c); free(z); return E_MEM; }
    st = charpoly(A, c);
    if (!st) st = dk_roots(c, A->r, z);
    free(c);
    if (st) { free(z); return st; }
    {
        /* a real symmetric matrix has real eigenvalues: drop the imaginary noise
         * the polynomial roots can carry (factoranalysis.ppl takes RE() of them
         * and sorts them, and stores them in a list) */
        int i, j, sym = 1;
        for (i = 0; i < A->r && sym; i++)
            for (j = 0; j < i && sym; j++) if (xn_cmp(XE(A, i, j), XE(A, j, i))) sym = 0;
        if (sym) for (i = 0; i < A->r; i++) xn_zero(&z[i].im);
    }
    cx_sort(z, A->r, 1);
    *out = z;
    return 0;
}

static int eigenvalues(const value *a, cx **out, int *n)
{
    xm A;
    int st;
    if ((st = need_square(a))) return st;
    if ((st = xm_from(a, &A))) return st;
    *n = A.r;
    st = eig_x(&A, out);
    xm_free(&A);
    return st;
}

/* a unit null vector of (A - lambda I), last non-zero component positive */
static int eigenvector(xm *A, const cx *lam, cx *v)
{
    int n = A->r, i, j, k, row = 0, free_col = -1, *pc;
    cx *m = (cx *)malloc((size_t)(n * n) * sizeof(cx)), t;
    xn scale, nrm;
    if (!m) return E_MEM;
    pc = (int *)malloc((size_t)n * sizeof(int));
    if (!pc) { free(m); return E_MEM; }
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            m[i * n + j].re = *XE(A, i, j); xn_zero(&m[i * n + j].im);
            if (i == j) cx_sub(&m[i * n + j], &m[i * n + j], lam);
        }
    xm_maxabs(A, &scale);
    {
        xn l2;
        cx_abs2(&l2, lam); xn_sqrt(&l2, &l2, 10);
        if (xn_cmp(&l2, &scale) > 0) scale = l2;
    }
    for (j = 0; j < n; j++) pc[j] = -1;
    for (j = 0; j < n && row < n; j++) {
        int p = -1;
        xn best, mg;
        xn_zero(&best);
        for (i = row; i < n; i++) {
            cx_abs2(&mg, &m[i * n + j]);
            if (xn_cmp(&mg, &best) > 0) { best = mg; p = i; }
        }
        xn_sqrt(&best, &best, 10);
        if (p < 0 || xn_top(&best) < xn_top(&scale) - (MP / 2)) { if (free_col < 0) free_col = j; continue; }
        for (k = 0; k < n; k++) { t = m[p * n + k]; m[p * n + k] = m[row * n + k]; m[row * n + k] = t; }
        for (i = 0; i < n; i++) {
            cx f;
            if (i == row) continue;
            cx_div(&f, &m[i * n + j], &m[row * n + j]);
            for (k = j; k < n; k++) { cx_mul(&t, &f, &m[row * n + k]); cx_sub(&m[i * n + k], &m[i * n + k], &t); }
        }
        pc[j] = row;
        row++;
    }
    if (free_col < 0) free_col = n - 1;
    for (j = 0; j < n; j++) { xn_zero(&v[j].re); xn_zero(&v[j].im); }
    xn_from_int(&v[free_col].re, 1);
    for (j = 0; j < n; j++) {
        if (pc[j] < 0 || j == free_col) continue;
        /* v_j = -m[r][free] / m[r][j] */
        cx_div(&t, &m[pc[j] * n + free_col], &m[pc[j] * n + j]);
        if (t.re.n) t.re.s ^= 1;
        if (t.im.n) t.im.s ^= 1;
        v[j] = t;
    }
    free(m); free(pc);
    xn_zero(&nrm);
    for (j = 0; j < n; j++) { cx_abs2(&t.re, &v[j]); xn_add(&nrm, &nrm, &t.re, MP); }
    xn_sqrt(&nrm, &nrm, MP);
    for (j = n - 1; j >= 0 && !v[j].re.n && !v[j].im.n; j--) {}
    if (j >= 0 && v[j].re.s) nrm.s = 1;
    for (j = 0; j < n; j++) { xn_div(&v[j].re, &v[j].re, &nrm, MP); xn_div(&v[j].im, &v[j].im, &nrm, MP); }
    return 0;
}

/* ------------------------------------------------------------ creation */

static int store_ij(ppl_vm *V, int which, int32_t n)
{
    value ref, v;
    v_none(&ref);
    ref.t = T_VREF; ref.ib = 3 /* VK_SYS */; ref.u.th.idx = (uint16_t)(SV_A + which); ref.u.th.pc = 0;
    v_int32(&v, n);
    return vm_store_ref(V, &ref, &v);
}

BI(b_makemat)
{
    int32_t r, c, i, j;
    int st = 0, vec = argc == 2;
    value I0, J0, res;
    if (bi_int(&a[1], &r) || (!vec && bi_int(&a[2], &c))) return E_BADVAL;
    if (vec) { c = r; r = 1; }
    if (r < 1 || c < 1 || r > 2000 || c > 2000) return E_BADVAL;
    v_copy(&I0, &V->sysvars[SV_A + 8]);
    v_copy(&J0, &V->sysvars[SV_A + 9]);
    if ((st = v_mat(&res, r, c, vec))) goto done;
    for (i = 0; i < r && !st; i++)
        for (j = 0; j < c && !st; j++) {
            value e;
            if ((st = store_ij(V, 8, vec ? j + 1 : i + 1)) || (st = store_ij(V, 9, j + 1))) break;
            if ((st = vm_run_thunk(V, &a[0], &e))) break;
            if (e.t == T_INT) { real x; to_real(&e, &x); v_real(&e, &x); }
            if (e.t != T_REAL && e.t != T_CPLX) { v_decref(&e); st = E_BADTYPE; break; }
            res.u.m->v[i * c + j] = e;
        }
    if (st) v_decref(&res); else *out = res;
done:
    v_set(&V->sysvars[SV_A + 8], &I0); v_set(&V->sysvars[SV_A + 9], &J0);
    v_decref(&I0); v_decref(&J0);
    return st;
}

static int idenmat1(ppl_vm *V, int argc, value *a, value *out)
{
    int32_t n, i;
    int st;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n) || n < 1 || n > 2000) return E_BADVAL;
    if ((st = v_mat(out, n, n, 0))) return st;
    for (i = 0; i < n; i++) v_int32(EL(out->u.m, i, i), 1);
    return 0;
}
BI(b_idenmat) { return listmap(V, argc, a, out, idenmat1); }

static uint32_t rnd(ppl_vm *V)
{
    uint32_t x = V->rng[0] ^ (V->rng[0] << 11);
    V->rng[0] = V->rng[1]; V->rng[1] = V->rng[2]; V->rng[2] = V->rng[3];
    V->rng[3] = V->rng[3] ^ (V->rng[3] >> 19) ^ x ^ (x >> 8);
    return V->rng[3];
}

/* randperm(n): a random permutation of 1..n as a vector (giac counts from 1
 * in the Prime's mode, assumed: listapi.ppl's shuffle falls back to
 * MAKELIST(I,I,1,n)); randperm(list or vector): its elements shuffled */
BI(b_randperm)
{
    int32_t n, i;
    int st;
    (void)argc;
    if (a[0].t == T_LIST || (a[0].t == T_MAT && a[0].u.m->vec)) {
        const value *el = a[0].t == T_LIST ? a[0].u.l->v : a[0].u.m->v;
        n = a[0].t == T_LIST ? (int32_t)a[0].u.l->n : a[0].u.m->cols;
        if (a[0].t == T_LIST) {
            if ((st = v_list(out, (uint32_t)n))) return st;
            for (i = 0; i < n; i++) { v_decref(&out->u.l->v[i]); v_copy(&out->u.l->v[i], &el[i]); }
        } else {
            if ((st = v_mat(out, 1, n, 1))) return st;
            for (i = 0; i < n; i++) out->u.m->v[i] = el[i];
        }
    } else {
        if (bi_int(&a[0], &n) || n < 0 || n > 65535) return E_BADVAL;
        if ((st = v_mat(out, 1, n, 1))) return st;
        for (i = 0; i < n; i++) v_int32(&out->u.m->v[i], i + 1);
    }
    {
        value *v = out->t == T_LIST ? out->u.l->v : out->u.m->v;
        for (i = n - 1; i > 0; i--) {         /* Fisher-Yates */
            int32_t j = (int32_t)(rnd(V) % (uint32_t)(i + 1));
            value t = v[i]; v[i] = v[j]; v[j] = t;
        }
    }
    return 0;
}

BI(b_randmat)
{
    int32_t r = 1, c, lo = -99, hi = 99, i, n;
    int st, vec = 0, k = 0;
    if (a[0].t == T_MAT) { k = 1; }      /* RANDMAT(name, ...) */
    if (argc - k < 1) return E_ARGC;
    if (argc - k == 1) { vec = 1; if (bi_int(&a[k], &c)) return E_BADVAL; }
    else {
        if (bi_int(&a[k], &r) || bi_int(&a[k + 1], &c)) return E_BADVAL;
        if (argc - k >= 4 && (bi_int(&a[k + 2], &lo) || bi_int(&a[k + 3], &hi))) return E_BADVAL;
    }
    if (r < 1 || c < 1 || r > 2000 || c > 2000) return E_BADVAL;
    if (hi < lo) { int32_t t = lo; lo = hi; hi = t; }
    if ((st = v_mat(out, r, c, vec))) return st;
    n = (vec ? 1 : r) * c;
    for (i = 0; i < n; i++) v_int32(&out->u.m->v[i], lo + (int32_t)(rnd(V) % (uint32_t)(hi - lo + 1)));
    return 0;
}

BI(b_hilbert)
{
    int32_t n, i, j;
    int st;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n) || n < 1 || n > 500) return E_BADVAL;
    if ((st = v_mat(out, n, n, 0))) return st;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            real one, d, q;
            dec_from_int(&one, 1); dec_from_int(&d, i + j + 1);
            dec_div(&q, &one, &d);
            v_real(EL(out->u.m, i, j), &q);
        }
    return 0;
}

BI(b_jordanblock)
{
    int32_t n, i;
    int st;
    (void)V; (void)argc;
    if (bi_int(&a[1], &n) || n < 1 || n > 2000) return E_BADVAL;
    if (a[0].t != T_REAL && a[0].t != T_CPLX) return E_BADTYPE;
    if ((st = v_mat(out, n, n, 0))) return st;
    for (i = 0; i < n; i++) {
        *EL(out->u.m, i, i) = a[0];
        if (i + 1 < n) v_int32(EL(out->u.m, i, i + 1), 1);
    }
    return 0;
}

static int elems(const value *v, const value **e, int *n)
{
    if (v->t == T_MAT && v->u.m->vec) { *e = v->u.m->v; *n = v->u.m->cols; return 0; }
    if (v->t == T_LIST) { *e = v->u.l->v; *n = (int)v->u.l->n; return 0; }
    if (v->t == T_MAT) { *e = v->u.m->v; *n = v->u.m->rows * v->u.m->cols; return 0; }
    return E_BADTYPE;
}

BI(b_vandermonde)
{
    const value *e;
    int n, i, j, st;
    (void)argc;
    if (elems(&a[0], &e, &n)) return E_BADTYPE;
    if ((st = v_mat(out, n, n, 0))) return st;
    for (i = 0; i < n; i++) {
        value p;
        v_int32(&p, 1);
        for (j = 0; j < n; j++) {
            value q;
            *EL(out->u.m, i, j) = p;
            if ((st = ar_binary(V, OP_MUL, &p, &e[i], &q))) { v_decref(out); return st; }
            p = q;
        }
    }
    return 0;
}

BI(b_diag)
{
    int st, i, n;
    (void)V; (void)argc;
    if (a[0].t == T_MAT && !a[0].u.m->vec) {
        pmat *m = a[0].u.m;
        n = m->rows < m->cols ? m->rows : m->cols;
        if ((st = v_mat(out, 1, n, 1))) return st;
        for (i = 0; i < n; i++) out->u.m->v[i] = *EL(m, i, i);
        return 0;
    }
    {
        const value *e;
        if (elems(&a[0], &e, &n)) return E_BADTYPE;
        if ((st = v_mat(out, n, n, 0))) return st;
        for (i = 0; i < n; i++) {
            if (e[i].t != T_REAL && e[i].t != T_CPLX) { v_decref(out); return E_BADTYPE; }
            *EL(out->u.m, i, i) = e[i];
        }
    }
    return 0;
}

BI(b_mkisom)
{
    /* the plane isometry of angle t: a rotation (sign 1) or a reflection */
    real t, c, s, ns;
    int32_t sg;
    int st;
    (void)argc;
    if (a[0].t == T_MAT && a[0].u.m->vec) { if (a[0].u.m->v[0].t != T_REAL) return E_BADTYPE; t = a[0].u.m->v[0].u.r; }
    else if (bi_real(&a[0], &t)) return E_BADTYPE;
    if (bi_int(&a[1], &sg)) return E_BADVAL;
    dec_cos(&c, &t, V->set.angle); dec_sin(&s, &t, V->set.angle);
    dec_neg(&ns, &s);
    if ((st = v_mat(out, 2, 2, 0))) return st;
    v_real(EL(out->u.m, 0, 0), &c);
    v_real(EL(out->u.m, 1, 0), &s);
    if (sg >= 0) { v_real(EL(out->u.m, 0, 1), &ns); v_real(EL(out->u.m, 1, 1), &c); }
    else { real nc; dec_neg(&nc, &c); v_real(EL(out->u.m, 0, 1), &s); v_real(EL(out->u.m, 1, 1), &nc); }
    return 0;
}

/* ------------------------------------------------------------ editing */

static int trn1(ppl_vm *V, int argc, value *a, value *out)
{
    pmat *m;
    int i, j, st, r;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    m = a[0].u.m;
    r = nrows(m);
    if ((st = v_mat(out, m->cols, r, 0))) return st;
    for (i = 0; i < r; i++) for (j = 0; j < m->cols; j++) *EL(out->u.m, j, i) = *EL(m, i, j);
    return 0;
}
BI(b_trn) { return listmap(V, argc, a, out, trn1); }

static int row_arg(const value *v, int lo, int hi, int32_t *n)
{
    if (bi_int(v, n)) return E_BADVAL;
    if (*n < lo || *n > hi) return E_INDEX;
    return 0;
}

BI(b_addrow)
{
    pmat *m, *x;
    int32_t at;
    int i, j, st, r;
    (void)V; (void)argc;
    if (need_mat(&a[0]) || a[0].u.m->vec) return E_BADTYPE;
    if (a[1].t != T_MAT || !a[1].u.m->vec) return E_BADTYPE;
    m = a[0].u.m; x = a[1].u.m;
    if (x->cols != m->cols) return E_DIM;
    if ((st = row_arg(&a[2], 1, m->rows + 1, &at))) return st;
    if ((st = v_mat(out, m->rows + 1, m->cols, 0))) return st;
    for (i = 0, r = 0; i <= m->rows; i++) {
        for (j = 0; j < m->cols; j++) *EL(out->u.m, i, j) = (i == at - 1) ? x->v[j] : *EL(m, r, j);
        if (i != at - 1) r++;
    }
    return 0;
}

BI(b_addcol)
{
    pmat *m, *x;
    int32_t at;
    int i, j, st, c;
    (void)V; (void)argc;
    if (need_mat(&a[0]) || a[0].u.m->vec) return E_BADTYPE;
    if (a[1].t != T_MAT || !a[1].u.m->vec) return E_BADTYPE;
    m = a[0].u.m; x = a[1].u.m;
    if (x->cols != m->rows) return E_DIM;
    if ((st = row_arg(&a[2], 1, m->cols + 1, &at))) return st;
    if ((st = v_mat(out, m->rows, m->cols + 1, 0))) return st;
    for (i = 0; i < m->rows; i++)
        for (j = 0, c = 0; j <= m->cols; j++) {
            *EL(out->u.m, i, j) = (j == at - 1) ? x->v[i] : *EL(m, i, c);
            if (j != at - 1) c++;
        }
    return 0;
}

BI(b_delrow)
{
    pmat *m;
    int32_t at;
    int i, j, st, r;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    m = a[0].u.m;
    if (m->vec) {
        if ((st = row_arg(&a[1], 1, m->cols, &at))) return st;
        if ((st = v_mat(out, 1, m->cols - 1, 1))) return st;
        for (j = 0, r = 0; j < m->cols; j++) if (j != at - 1) out->u.m->v[r++] = m->v[j];
        return 0;
    }
    if ((st = row_arg(&a[1], 1, m->rows, &at))) return st;
    if (m->rows == 1) return E_DIM;
    if ((st = v_mat(out, m->rows - 1, m->cols, 0))) return st;
    for (i = 0, r = 0; i < m->rows; i++) {
        if (i == at - 1) continue;
        for (j = 0; j < m->cols; j++) *EL(out->u.m, r, j) = *EL(m, i, j);
        r++;
    }
    return 0;
}

BI(b_delcol)
{
    pmat *m;
    int32_t at;
    int i, j, st, c;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    m = a[0].u.m;
    if (m->vec) return b_delrow(V, argc, a, out);
    if ((st = row_arg(&a[1], 1, m->cols, &at))) return st;
    if (m->cols == 1) return E_DIM;
    if ((st = v_mat(out, m->rows, m->cols - 1, 0))) return st;
    for (i = 0; i < m->rows; i++)
        for (j = 0, c = 0; j < m->cols; j++) if (j != at - 1) *EL(out->u.m, i, c++) = *EL(m, i, j);
    return 0;
}

BI(b_redim)
{
    pmat *m;
    int32_t r, c;
    int i, j, st, vec;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    m = a[0].u.m;
    if (a[1].t == T_LIST && a[1].u.l->n == 2) {
        if (bi_int(&a[1].u.l->v[0], &r) || bi_int(&a[1].u.l->v[1], &c)) return E_BADVAL;
        vec = 0;
    } else if (a[1].t == T_LIST && a[1].u.l->n == 1) {
        if (bi_int(&a[1].u.l->v[0], &c)) return E_BADVAL;
        r = 1; vec = 1;
    } else if (bi_int(&a[1], &c) == 0) { r = 1; vec = 1; }
    else return E_BADTYPE;
    if (r < 1 || c < 1 || r > 2000 || c > 2000) return E_BADVAL;
    if ((st = v_mat(out, r, c, vec))) return st;
    for (i = 0; i < r && i < nrows(m); i++)
        for (j = 0; j < c && j < m->cols; j++) *EL(out->u.m, i, j) = *EL(m, i, j);
    return 0;
}

BI(b_scale)
{
    pmat *m;
    int32_t r;
    int j, st;
    (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    if ((st = row_arg(&a[2], 1, nrows(a[0].u.m), &r))) return st;
    v_copy(out, &a[0]);
    m = mat_own(out);
    if (!m) return E_MEM;
    for (j = 0; j < m->cols; j++) {
        value t;
        if ((st = ar_binary(V, OP_MUL, EL(m, r - 1, j), &a[1], &t))) { v_decref(out); return st; }
        *EL(m, r - 1, j) = t;
    }
    return 0;
}

BI(b_scaleadd)
{
    /* row2 := row2 + value * row1 */
    pmat *m;
    int32_t r1, r2;
    int j, st;
    (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    if ((st = row_arg(&a[2], 1, nrows(a[0].u.m), &r1)) || (st = row_arg(&a[3], 1, nrows(a[0].u.m), &r2))) return st;
    v_copy(out, &a[0]);
    m = mat_own(out);
    if (!m) return E_MEM;
    for (j = 0; j < m->cols; j++) {
        value t, u;
        if ((st = ar_binary(V, OP_MUL, EL(m, r1 - 1, j), &a[1], &t))) { v_decref(out); return st; }
        if ((st = ar_binary(V, OP_ADD, EL(m, r2 - 1, j), &t, &u))) { v_decref(out); return st; }
        *EL(m, r2 - 1, j) = u;
    }
    return 0;
}

static int swap(value *a, value *out, int cols)
{
    pmat *m;
    int32_t p, q;
    int k, st, n;
    if (need_mat(&a[0])) return E_BADTYPE;
    m = a[0].u.m;
    if (m->vec) cols = 1;
    n = cols ? m->cols : nrows(m);
    if ((st = row_arg(&a[1], 1, n, &p)) || (st = row_arg(&a[2], 1, n, &q))) return st;
    v_copy(out, &a[0]);
    m = mat_own(out);
    if (!m) return E_MEM;
    if (cols) for (k = 0; k < nrows(m); k++) { value t = *EL(m, k, p - 1); *EL(m, k, p - 1) = *EL(m, k, q - 1); *EL(m, k, q - 1) = t; }
    else for (k = 0; k < m->cols; k++) { value t = *EL(m, p - 1, k); *EL(m, p - 1, k) = *EL(m, q - 1, k); *EL(m, q - 1, k) = t; }
    return 0;
}
BI(b_swaprow) { (void)V; (void)argc; return swap(a, out, 0); }
BI(b_swapcol) { (void)V; (void)argc; return swap(a, out, 1); }

BI(b_sub)
{
    int st;
    (void)V; (void)argc;
    if (a[0].t == T_MAT && !a[0].u.m->vec) {
        int32_t r1, c1, r2, c2;
        int i, j;
        pmat *m = a[0].u.m;
        if (a[1].t != T_LIST || a[2].t != T_LIST || a[1].u.l->n != 2 || a[2].u.l->n != 2) return E_BADTYPE;
        if (bi_int(&a[1].u.l->v[0], &r1) || bi_int(&a[1].u.l->v[1], &c1) ||
            bi_int(&a[2].u.l->v[0], &r2) || bi_int(&a[2].u.l->v[1], &c2)) return E_BADVAL;
        if (r1 < 1 || c1 < 1 || r2 > m->rows || c2 > m->cols || r2 < r1 || c2 < c1) return E_INDEX;
        if ((st = v_mat(out, r2 - r1 + 1, c2 - c1 + 1, 0))) return st;
        for (i = r1; i <= r2; i++) for (j = c1; j <= c2; j++) *EL(out->u.m, i - r1, j - c1) = *EL(m, i - 1, j - 1);
        return 0;
    }
    {
        int32_t s, e, i;
        const value *el;
        int n;
        if (elems(&a[0], &el, &n)) return E_BADTYPE;
        if (bi_int(&a[1], &s) || bi_int(&a[2], &e)) return E_BADVAL;
        if (s < 1 || e > n || e < s) return E_INDEX;
        if (a[0].t == T_LIST) {
            if ((st = v_list(out, 0))) return st;
            for (i = s; i <= e; i++) list_push(out->u.l, &el[i - 1]);
        } else {
            if ((st = v_mat(out, 1, e - s + 1, 1))) return st;
            for (i = s; i <= e; i++) out->u.m->v[i - s] = el[i - 1];
        }
    }
    return 0;
}

BI(b_pivot)
{
    pmat *m;
    int32_t r, c;
    int i, j, st;
    value piv;
    (void)argc;
    if (need_mat(&a[0]) || a[0].u.m->vec) return E_BADTYPE;
    if ((st = row_arg(&a[1], 1, a[0].u.m->rows, &r)) || (st = row_arg(&a[2], 1, a[0].u.m->cols, &c))) return st;
    v_copy(out, &a[0]);
    m = mat_own(out);
    if (!m) return E_MEM;
    piv = *EL(m, r - 1, c - 1);
    if (piv.t == T_REAL && dec_is_zero(&piv.u.r)) { v_decref(out); return E_BADVAL; }
    for (i = 0; i < m->rows; i++) {
        value f;
        if (i == r - 1) continue;
        if ((st = ar_binary(V, OP_DIV, EL(m, i, c - 1), &piv, &f))) { v_decref(out); return st; }
        for (j = 0; j < m->cols; j++) {
            value t, u;
            if (j == c - 1) { v_int32(EL(m, i, j), 0); continue; }
            ar_binary(V, OP_MUL, &f, EL(m, r - 1, j), &t);
            ar_binary(V, OP_SUB, EL(m, i, j), &t, &u);
            *EL(m, i, j) = u;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ products */

BI(b_dot)
{
    const value *x, *y;
    int n, k, i, st;
    value acc;
    (void)argc;
    if (elems(&a[0], &x, &n) || elems(&a[1], &y, &k)) return E_BADTYPE;
    if (n != k) return E_DIM;
    v_int32(&acc, 0);
    for (i = 0; i < n; i++) {
        value t, u;
        if ((st = ar_binary(V, OP_MUL, &x[i], &y[i], &t))) return st;
        if ((st = ar_binary(V, OP_ADD, &acc, &t, &u))) return st;
        acc = u;
    }
    *out = acc;
    return 0;
}

BI(b_cross)
{
    value x[3], y[3];
    const value *p, *q;
    int n, k, i, st;
    static const int I1[3] = { 1, 2, 0 }, I2[3] = { 2, 0, 1 };
    (void)argc;
    if (elems(&a[0], &p, &n) || elems(&a[1], &q, &k)) return E_BADTYPE;
    if (n < 2 || n > 3 || k < 2 || k > 3) return E_DIM;
    for (i = 0; i < 3; i++) {
        if (i < n) x[i] = p[i]; else v_int32(&x[i], 0);
        if (i < k) y[i] = q[i]; else v_int32(&y[i], 0);
    }
    if ((st = v_mat(out, 1, 3, 1))) return st;
    for (i = 0; i < 3; i++) {
        value t1, t2, r;
        ar_binary(V, OP_MUL, &x[I1[i]], &y[I2[i]], &t1);
        ar_binary(V, OP_MUL, &x[I2[i]], &y[I1[i]], &t2);
        if ((st = ar_binary(V, OP_SUB, &t1, &t2, &r))) { v_decref(out); return st; }
        out->u.m->v[i] = r;
    }
    return 0;
}

BI(b_trace)
{
    int i, st;
    value acc;
    (void)argc;
    if ((st = need_square(&a[0]))) return st;
    v_int32(&acc, 0);
    for (i = 0; i < a[0].u.m->rows; i++) {
        value t;
        if ((st = ar_binary(V, OP_ADD, &acc, EL(a[0].u.m, i, i), &t))) return st;
        acc = t;
    }
    *out = acc;
    return 0;
}

static void absx(const value *v, xn *r)
{
    real t;
    if (v->t == T_CPLX) c_abs(&t, &v->u.c);
    else { to_real(v, &t); t.s = 0; }
    xn_from_real(r, &t);
}

/* maximum over rows (bycol=0) or columns of the sum of |a_ij| */
static int abs_norm(const value *a, value *out, int bycol)
{
    pmat *m;
    int i, j, R, C;
    xn best, acc, t;
    real r;
    if (need_mat(a)) return E_BADTYPE;
    m = a->u.m;
    R = nrows(m); C = m->cols;
    xn_zero(&best);
    for (i = 0; i < (bycol ? C : R); i++) {
        xn_zero(&acc);
        for (j = 0; j < (bycol ? R : C); j++) {
            absx(bycol ? EL(m, j, i) : EL(m, i, j), &t);
            xn_add(&acc, &acc, &t, MP);
        }
        if (xn_cmp(&acc, &best) > 0) best = acc;
    }
    xn_to_real(&r, &best);
    v_real(out, &r);
    return 0;
}
BI(b_rownorm) { (void)V; (void)argc; return abs_norm(&a[0], out, 0); }
BI(b_colnorm) { (void)V; (void)argc; return abs_norm(&a[0], out, 1); }

static int vnorm(const value *a, value *out, int kind)   /* 1, 2, max */
{
    const value *e;
    int n, i;
    xn acc, t;
    real r;
    if (elems(a, &e, &n)) return E_BADTYPE;
    xn_zero(&acc);
    for (i = 0; i < n; i++) {
        absx(&e[i], &t);
        if (kind == 2) { xn_mul(&t, &t, &t, MP); xn_add(&acc, &acc, &t, MP); }
        else if (kind == 1) xn_add(&acc, &acc, &t, MP);
        else if (xn_cmp(&t, &acc) > 0) acc = t;
    }
    if (kind == 2) xn_sqrt(&acc, &acc, DEC_DIGITS);
    xn_to_real(&r, &acc);
    v_real(out, &r);
    return 0;
}
BI(b_l1norm) { (void)V; (void)argc; return vnorm(&a[0], out, 1); }
BI(b_l2norm) { (void)V; (void)argc; return vnorm(&a[0], out, 2); }
BI(b_maxnorm) { (void)V; (void)argc; return vnorm(&a[0], out, 0); }

/* ------------------------------------------------------------ elimination */

/* determinant of a complex matrix by elimination in complex xn */
static int cx_det(const value *a, value *out)
{
    const pmat *p = a->u.m;
    int n = p->rows, i, j, k;
    cx *m = (cx *)malloc((size_t)(n * n) * sizeof(cx)), d, t, f;
    if (!m) return E_MEM;
    for (i = 0; i < n * n; i++) if (cx_from(&p->v[i], &m[i])) { free(m); return E_BADTYPE; }
    xn_from_int(&d.re, 1); xn_zero(&d.im);
    for (k = 0; k < n; k++) {
        int q = -1;
        xn best, mg;
        xn_zero(&best);
        for (i = k; i < n; i++) { cx_abs2(&mg, &m[i * n + k]); if (xn_cmp(&mg, &best) > 0) { best = mg; q = i; } }
        if (q < 0) { xn_zero(&d.re); xn_zero(&d.im); break; }
        if (q != k) {
            for (j = 0; j < n; j++) { t = m[q * n + j]; m[q * n + j] = m[k * n + j]; m[k * n + j] = t; }
            if (d.re.n) d.re.s ^= 1;
            if (d.im.n) d.im.s ^= 1;
        }
        cx_mul(&d, &d, &m[k * n + k]);
        for (i = k + 1; i < n; i++) {
            cx_div(&f, &m[i * n + k], &m[k * n + k]);
            for (j = k; j < n; j++) { cx_mul(&t, &f, &m[k * n + j]); cx_sub(&m[i * n + j], &m[i * n + j], &t); }
        }
    }
    free(m);
    cx_to(&d, out);
    return 0;
}

static int det1(ppl_vm *V, int argc, value *a, value *out)
{
    xm m;
    xn d;
    real r;
    int st;
    (void)V; (void)argc;
    if ((st = need_square(&a[0]))) return st;
    st = xm_from(&a[0], &m);
    if (st == E_BADTYPE) return cx_det(&a[0], out);
    if (st) return st;
    xm_eliminate(&m, 0, &d);
    xm_free(&m);
    xn_to_real(&r, &d);
    v_real(out, &r);
    return 0;
}
BI(b_det) { return listmap(V, argc, a, out, det1); }

static int rref1(ppl_vm *V, int argc, value *a, value *out)
{
    xm m;
    int st;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    if ((st = xm_from(&a[0], &m))) return st;
    xm_eliminate(&m, 1, NULL);
    st = xm_to(&m, out, a[0].u.m->vec);
    xm_free(&m);
    return st;
}
BI(b_rref) { return listmap(V, argc, a, out, rref1); }

BI(b_rank)
{
    xm m;
    int st, r;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    if ((st = xm_from(&a[0], &m))) return st;
    r = xm_eliminate(&m, 0, NULL);
    xm_free(&m);
    v_int32(out, r);
    return 0;
}

/* INVERSE is refused on the Prime (the inverse is written M^-1) */
BI(b_inverse) { (void)V; (void)argc; (void)a; (void)out; return E_BADTYPE; }

/* X with A X = B (A square), by Gauss-Jordan on [A|B] */
static int xm_solve(xm *A, xm *B, xm *X)
{
    xm W;
    int n = A->r, k = B->c, i, j, st;
    if (A->c != n || B->r != n) return E_DIM;
    if ((st = xm_new(&W, n, n + k))) return st;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) *XE(&W, i, j) = *XE(A, i, j);
        for (j = 0; j < k; j++) *XE(&W, i, n + j) = *XE(B, i, j);
    }
    xm_eliminate(&W, 1, NULL);
    for (i = 0; i < n; i++) if (!XE(&W, i, i)->n) { xm_free(&W); return E_BADVAL; }
    if ((st = xm_new(X, n, k))) { xm_free(&W); return st; }
    for (i = 0; i < n; i++) for (j = 0; j < k; j++) *XE(X, i, j) = *XE(&W, i, n + j);
    xm_free(&W);
    return 0;
}

static void xm_colnorm(xm *m, xn *best)
{
    int i, j;
    xn acc, t;
    xn_zero(best);
    for (j = 0; j < m->c; j++) {
        xn_zero(&acc);
        for (i = 0; i < m->r; i++) { xn_absv(&t, XE(m, i, j)); xn_add(&acc, &acc, &t, MP); }
        if (xn_cmp(&acc, best) > 0) *best = acc;
    }
}

BI(b_cond)
{
    xm A, I, X;
    xn n1, n2;
    real r;
    int st;
    (void)argc;
    if ((st = need_square(&a[0]))) return st;
    st = xm_from(&a[0], &A);
    if (st == E_BADTYPE) {
        value inv, c1, c2;
        if ((st = mat_inv(V, &a[0], &inv, NULL))) return st;
        abs_norm(&a[0], &c1, 1); abs_norm(&inv, &c2, 1);
        v_decref(&inv);
        return ar_binary(V, OP_MUL, &c1, &c2, out);
    }
    if (st) return st;
    xm_new(&I, A.r, A.r);
    xm_ident(&I);
    st = xm_solve(&A, &I, &X);
    xm_free(&I);
    if (st) { xm_free(&A); return st; }
    xm_colnorm(&A, &n1); xm_colnorm(&X, &n2);
    xn_mul(&n1, &n1, &n2, MP);
    xm_free(&A); xm_free(&X);
    xn_to_real(&r, &n1);
    v_real(out, &r);
    return 0;
}

/* minimum-norm least squares: A X = B */
BI(b_lsq)
{
    xm A, B, At, G, H, X, Y;
    int st, m, n, vecb;
    (void)V; (void)argc;
    if (need_mat(&a[0]) || need_mat(&a[1])) return E_BADTYPE;
    if ((st = xm_from(&a[0], &A))) return st;
    if ((st = xm_from(&a[1], &B))) { xm_free(&A); return st; }
    vecb = a[1].u.m->vec;
    if (vecb) { xm t; xm_trn(&t, &B); xm_free(&B); B = t; }   /* a vector is a column */
    m = A.r; n = A.c;
    if (B.r != m) { xm_free(&A); xm_free(&B); return E_DIM; }
    xm_trn(&At, &A);
    if (m == n) st = xm_solve(&A, &B, &X);
    else if (m > n) {
        xm_mul(&G, &At, &A); xm_mul(&H, &At, &B);
        st = xm_solve(&G, &H, &X);
        xm_free(&G); xm_free(&H);
    } else {
        xm_mul(&G, &A, &At);
        st = xm_solve(&G, &B, &Y);
        if (!st) { xm_mul(&X, &At, &Y); xm_free(&Y); }
        xm_free(&G);
    }
    xm_free(&A); xm_free(&B); xm_free(&At);
    if (st) return st;
    if (vecb) { xm t; xm_trn(&t, &X); xm_free(&X); X = t; }
    st = xm_to(&X, out, vecb);
    xm_free(&X);
    return st;
}

static int list_of(value *out, value *items, int n)
{
    int i, st;
    if ((st = v_list(out, (uint32_t)n))) { for (i = 0; i < n; i++) v_decref(&items[i]); return st; }
    for (i = 0; i < n; i++) out->u.l->v[i] = items[i];
    return 0;
}

BI(b_lu)
{
    xm A, L, P;
    int n, i, j, k, st;
    value parts[3];
    (void)V; (void)argc;
    if ((st = need_square(&a[0]))) return st;
    if ((st = xm_from(&a[0], &A))) return st;
    n = A.r;
    xm_new(&L, n, n); xm_new(&P, n, n);
    xm_ident(&P);
    for (k = 0; k < n; k++) {
        int p = k;
        for (i = k + 1; i < n; i++) if (xn_cmpmag(XE(&A, i, k), XE(&A, p, k)) > 0) p = i;
        if (p != k)
            for (j = 0; j < n; j++) {
                xn t = *XE(&A, p, j); *XE(&A, p, j) = *XE(&A, k, j); *XE(&A, k, j) = t;
                t = *XE(&P, p, j); *XE(&P, p, j) = *XE(&P, k, j); *XE(&P, k, j) = t;
                if (j < k) { t = *XE(&L, p, j); *XE(&L, p, j) = *XE(&L, k, j); *XE(&L, k, j) = t; }
            }
        xn_from_int(XE(&L, k, k), 1);
        if (!XE(&A, k, k)->n) continue;
        for (i = k + 1; i < n; i++) {
            xn f, t;
            xn_div(&f, XE(&A, i, k), XE(&A, k, k), MP);
            *XE(&L, i, k) = f;
            for (j = k; j < n; j++) { xn_mul(&t, &f, XE(&A, k, j), MP); xn_sub(XE(&A, i, j), XE(&A, i, j), &t, MP); }
            xn_zero(XE(&A, i, k));
        }
    }
    xm_to(&L, &parts[0], 0); xm_to(&A, &parts[1], 0); xm_to(&P, &parts[2], 0);
    xm_free(&A); xm_free(&L); xm_free(&P);
    return list_of(out, parts, 3);
}

static int eigvecs(const value *a, value *P, value *D);

/* lu(M), the CAS form: [p, L, U] with P*M = L*U, p the permutation as a
 * vector of row numbers (row j of P has its 1 in column p(j)).  The User Guide
 * shows lu([[1,2],[3,4]]) = [[1 2] [[1 0],[3 1]] [[1 2],[0 -2]]] for an exact
 * matrix; here the pivot is the largest in its column, as LU (assumed for
 * approximate matrices; gaussjordan.ppl only builds P from p).  A list here. */
BI(b_lucas)
{
    value r, p;
    pmat *P;
    int i, j, st;
    if ((st = b_lu(V, argc, a, &r))) return st;
    P = r.u.l->v[2].u.m;
    if ((st = v_mat(&p, 1, P->rows, 1))) { v_decref(&r); return st; }
    for (i = 0; i < P->rows; i++)
        for (j = 0; j < P->cols; j++) {
            real x;
            if (!to_real(EL(P, i, j), &x) && !dec_is_zero(&x)) v_int32(&p.u.m->v[i], j + 1);
        }
    /* {L, U, P} -> {p, L, U} */
    v_decref(&r.u.l->v[2]);
    r.u.l->v[2] = r.u.l->v[1];
    r.u.l->v[1] = r.u.l->v[0];
    r.u.l->v[0] = p;
    *out = r;
    return 0;
}

/* rowDim/colDim: rows and columns of a matrix; a vector counts as one row
 * (assumed) */
BI(b_rowdim)
{
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    v_int32(out, nrows(a[0].u.m));
    return 0;
}

BI(b_coldim)
{
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    v_int32(out, a[0].u.m->cols);
    return 0;
}

/* eigVc / eigVl (giac's egv / egvl): the eigenvector matrix (columns, as
 * EIGENVV's first part) and the diagonal eigenvalue matrix */
static int eig_part(const value *a, value *out, int which)
{
    value P, D;
    int st;
    if ((st = eigvecs(a, &P, &D))) return st;
    if (which) { *out = D; v_decref(&P); } else { *out = P; v_decref(&D); }
    return 0;
}
BI(b_eigvc) { (void)V; (void)argc; return eig_part(&a[0], out, 0); }
BI(b_eigvl) { (void)V; (void)argc; return eig_part(&a[0], out, 1); }

/* Householder QR: A = Q R, Q orthogonal (m x m), R upper (m x n) */
static int house_qr(xm *A, xm *Q)
{
    int m = A->r, n = A->c, k, i, j, st;
    if ((st = xm_new(Q, m, m))) return st;
    xm_ident(Q);
    for (k = 0; k < n && k < m; k++) {
        xn nrm, alpha, vv, t, dot;
        xn *v = (xn *)malloc((size_t)m * sizeof(xn));
        if (!v) return E_MEM;
        xn_zero(&nrm);
        for (i = k; i < m; i++) { xn_mul(&t, XE(A, i, k), XE(A, i, k), MP + 2); xn_add(&nrm, &nrm, &t, MP + 2); }
        xn_sqrt(&nrm, &nrm, MP);
        if (!nrm.n) { free(v); continue; }
        alpha = nrm;
        if (!XE(A, k, k)->s) alpha.s = 1;           /* alpha = -sign(a_kk) |x| */
        for (i = 0; i < m; i++) xn_zero(&v[i]);
        for (i = k; i < m; i++) v[i] = *XE(A, i, k);
        xn_sub(&v[k], &v[k], &alpha, MP);
        xn_zero(&vv);
        for (i = k; i < m; i++) { xn_mul(&t, &v[i], &v[i], MP + 2); xn_add(&vv, &vv, &t, MP + 2); }
        if (!vv.n) { free(v); continue; }
        /* A <- H A,  Q <- Q H  with H = I - 2 v v' / v'v */
        for (j = 0; j < n; j++) {
            xn_zero(&dot);
            for (i = k; i < m; i++) { xn_mul(&t, &v[i], XE(A, i, j), MP + 2); xn_add(&dot, &dot, &t, MP + 2); }
            xn_mul_int(&dot, &dot, 2, MP + 2); xn_div(&dot, &dot, &vv, MP + 2);
            for (i = k; i < m; i++) { xn_mul(&t, &dot, &v[i], MP + 2); xn_sub(XE(A, i, j), XE(A, i, j), &t, MP); }
        }
        for (i = 0; i < m; i++) {
            xn_zero(&dot);
            for (j = k; j < m; j++) { xn_mul(&t, XE(Q, i, j), &v[j], MP + 2); xn_add(&dot, &dot, &t, MP + 2); }
            xn_mul_int(&dot, &dot, 2, MP + 2); xn_div(&dot, &dot, &vv, MP + 2);
            for (j = k; j < m; j++) { xn_mul(&t, &dot, &v[j], MP + 2); xn_sub(XE(Q, i, j), XE(Q, i, j), &t, MP); }
        }
        for (i = k + 1; i < m; i++) xn_zero(XE(A, i, k));
        free(v);
    }
    return 0;
}

static int ident_value(value *out, int n)
{
    value one;
    int st;
    v_int32(&one, n);
    st = idenmat1(NULL, 1, &one, out);
    return st;
}

BI(b_qr)
{
    xm A, Q;
    value parts[3];
    int st;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    if ((st = xm_from(&a[0], &A))) return st;
    if ((st = house_qr(&A, &Q))) { xm_free(&A); return st; }
    xm_clean(&Q); xm_clean(&A);
    xm_to(&Q, &parts[0], 0); xm_to(&A, &parts[1], 0);
    ident_value(&parts[2], A.c);
    xm_free(&A); xm_free(&Q);
    return list_of(out, parts, 3);
}

BI(b_lq)
{
    xm A, At, Q, L, Qt;
    value parts[3];
    int st;
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    if ((st = xm_from(&a[0], &A))) return st;
    xm_trn(&At, &A);
    xm_free(&A);
    if ((st = house_qr(&At, &Q))) { xm_free(&At); return st; }
    xm_clean(&At); xm_clean(&Q);
    xm_trn(&L, &At); xm_trn(&Qt, &Q);
    xm_to(&L, &parts[0], 0); xm_to(&Qt, &parts[1], 0);
    ident_value(&parts[2], L.r);
    xm_free(&At); xm_free(&Q); xm_free(&L); xm_free(&Qt);
    return list_of(out, parts, 3);
}

BI(b_cholesky)
{
    xm A, L;
    int n, i, j, k, st;
    (void)V; (void)argc;
    if ((st = need_square(&a[0]))) return st;
    if ((st = xm_from(&a[0], &A))) return st;
    n = A.r;
    xm_new(&L, n, n);
    for (j = 0; j < n; j++) {
        xn s, t;
        s = *XE(&A, j, j);
        for (k = 0; k < j; k++) { xn_mul(&t, XE(&L, j, k), XE(&L, j, k), MP); xn_sub(&s, &s, &t, MP); }
        if (s.s || !s.n) { xm_free(&A); xm_free(&L); return E_BADVAL; }
        xn_sqrt(XE(&L, j, j), &s, MP);
        for (i = j + 1; i < n; i++) {
            s = *XE(&A, i, j);
            for (k = 0; k < j; k++) { xn_mul(&t, XE(&L, i, k), XE(&L, j, k), MP); xn_sub(&s, &s, &t, MP); }
            xn_div(XE(&L, i, j), &s, XE(&L, j, j), MP);
        }
    }
    st = xm_to(&L, out, 0);
    xm_free(&A); xm_free(&L);
    return st;
}

BI(b_hessenberg)
{
    /* Householder reduction: H = P A P' with P orthogonal; answers {P, H} */
    xm A, P;
    int n, k, i, j, st;
    value parts[2];
    (void)V; (void)argc;
    if ((st = need_square(&a[0]))) return st;
    if ((st = xm_from(&a[0], &A))) return st;
    n = A.r;
    xm_new(&P, n, n);
    xm_ident(&P);
    for (k = 0; k + 2 < n; k++) {
        xn nrm, alpha, vv, t, dot, *v = (xn *)malloc((size_t)n * sizeof(xn));
        if (!v) { xm_free(&A); xm_free(&P); return E_MEM; }
        for (i = 0; i < n; i++) xn_zero(&v[i]);
        xn_zero(&nrm);
        for (i = k + 1; i < n; i++) { xn_mul(&t, XE(&A, i, k), XE(&A, i, k), MP); xn_add(&nrm, &nrm, &t, MP); }
        xn_sqrt(&nrm, &nrm, MP);
        if (!nrm.n) { free(v); continue; }
        alpha = nrm;
        if (!XE(&A, k + 1, k)->s) alpha.s = 1;
        for (i = k + 1; i < n; i++) v[i] = *XE(&A, i, k);
        xn_sub(&v[k + 1], &v[k + 1], &alpha, MP);
        xn_zero(&vv);
        for (i = k + 1; i < n; i++) { xn_mul(&t, &v[i], &v[i], MP); xn_add(&vv, &vv, &t, MP); }
        if (!vv.n) { free(v); continue; }
        /* A <- H A H, P <- H P */
        for (j = 0; j < n; j++) {   /* H A */
            xn_zero(&dot);
            for (i = k + 1; i < n; i++) { xn_mul(&t, &v[i], XE(&A, i, j), MP); xn_add(&dot, &dot, &t, MP); }
            xn_mul_int(&dot, &dot, 2, MP); xn_div(&dot, &dot, &vv, MP);
            for (i = k + 1; i < n; i++) { xn_mul(&t, &dot, &v[i], MP); xn_sub(XE(&A, i, j), XE(&A, i, j), &t, MP); }
            xn_zero(&dot);
            for (i = k + 1; i < n; i++) { xn_mul(&t, &v[i], XE(&P, i, j), MP); xn_add(&dot, &dot, &t, MP); }
            xn_mul_int(&dot, &dot, 2, MP); xn_div(&dot, &dot, &vv, MP);
            for (i = k + 1; i < n; i++) { xn_mul(&t, &dot, &v[i], MP); xn_sub(XE(&P, i, j), XE(&P, i, j), &t, MP); }
        }
        for (i = 0; i < n; i++) {   /* (H A) H */
            xn_zero(&dot);
            for (j = k + 1; j < n; j++) { xn_mul(&t, XE(&A, i, j), &v[j], MP); xn_add(&dot, &dot, &t, MP); }
            xn_mul_int(&dot, &dot, 2, MP); xn_div(&dot, &dot, &vv, MP);
            for (j = k + 1; j < n; j++) { xn_mul(&t, &dot, &v[j], MP); xn_sub(XE(&A, i, j), XE(&A, i, j), &t, MP); }
        }
        for (i = k + 2; i < n; i++) xn_zero(XE(&A, i, k));
        free(v);
    }
    xm_clean(&P); xm_clean(&A);
    xm_to(&P, &parts[0], 0); xm_to(&A, &parts[1], 0);
    xm_free(&A); xm_free(&P);
    return list_of(out, parts, 2);
}

/* ------------------------------------------------------------ eigen */

BI(b_eigenval)
{
    cx *z;
    int n, i, st;
    (void)V; (void)argc;
    if ((st = eigenvalues(&a[0], &z, &n))) return st;
    if ((st = v_mat(out, 1, n, 1))) { free(z); return st; }
    for (i = 0; i < n; i++) cx_to(&z[i], &out->u.m->v[i]);
    free(z);
    return 0;
}

BI(b_specrad)
{
    cx *z;
    int n, i, st;
    xn best, m;
    real r;
    (void)V; (void)argc;
    if ((st = eigenvalues(&a[0], &z, &n))) return st;
    xn_zero(&best);
    for (i = 0; i < n; i++) { cx_abs2(&m, &z[i]); if (xn_cmp(&m, &best) > 0) best = m; }
    free(z);
    xn_sqrt(&best, &best, DEC_DIGITS);
    xn_to_real(&r, &best);
    v_real(out, &r);
    return 0;
}

/* eigenvalues (decreasing real part) and unit eigenvectors: vec[j*n + i] is
 * component i of the vector of eigenvalue j */
static int eig_vecs_x(xm *A, cx **z, cx **vec)
{
    int n = A->r, j, st;
    if ((st = eig_x(A, z))) return st;
    *vec = (cx *)malloc((size_t)(n * n) * sizeof(cx));
    if (!*vec) { free(*z); return E_MEM; }
    for (j = 0; j < n; j++) eigenvector(A, &(*z)[j], &(*vec)[j * n]);
    return 0;
}

static int eigvecs(const value *a, value *P, value *D)
{
    xm A;
    cx *z, *v;
    int n, i, j, st;
    if ((st = need_square(a))) return st;
    if ((st = xm_from(a, &A))) return st;
    n = A.r;
    st = eig_vecs_x(&A, &z, &v);
    xm_free(&A);
    if (st) return st;
    if ((st = v_mat(P, n, n, 0)) || (st = v_mat(D, n, n, 0))) { free(z); free(v); return st; }
    for (j = 0; j < n; j++) {
        for (i = 0; i < n; i++) cx_to(&v[j * n + i], EL(P->u.m, i, j));
        cx_to(&z[j], EL(D->u.m, j, j));
    }
    free(z); free(v);
    return 0;
}

BI(b_eigenvv)
{
    value parts[2];
    int st;
    (void)V; (void)argc;
    if ((st = eigvecs(&a[0], &parts[0], &parts[1]))) return st;
    return list_of(out, parts, 2);
}

/* singular values (increasing) and right singular vectors of a real matrix */
static int svd_x(const value *a, xm *A, xn **s, xm *Vm, int *n)
{
    xm At, G;
    cx *z, *v;
    int i, j, st, k;
    if (need_mat(a)) return E_BADTYPE;
    if ((st = xm_from(a, A))) return st;
    xm_trn(&At, A);
    if (A->c <= A->r) xm_mul(&G, &At, A); else xm_mul(&G, A, &At);
    xm_free(&At);
    k = G.r;
    st = eig_vecs_x(&G, &z, &v);
    xm_free(&G);
    if (st) { xm_free(A); return st; }
    *s = (xn *)malloc((size_t)k * sizeof(xn));
    xm_new(Vm, k, k);
    for (j = 0; j < k; j++) {          /* reverse: increasing */
        int src = k - 1 - j;
        xn e = z[src].re;
        if (e.s) xn_zero(&e);
        xn_sqrt(&(*s)[j], &e, MP);
        for (i = 0; i < k; i++) *XE(Vm, i, j) = v[src * k + i].re;
    }
    free(z); free(v);
    *n = k;
    return 0;
}

BI(b_svl)
{
    xm A, Vm;
    xn *s;
    int n, i, st;
    (void)V; (void)argc;
    if ((st = svd_x(&a[0], &A, &s, &Vm, &n))) return st;
    st = v_mat(out, 1, n, 1);
    for (i = 0; !st && i < n; i++) { real r; xn_to_real(&r, &s[i]); v_real(&out->u.m->v[i], &r); }
    free(s); xm_free(&A); xm_free(&Vm);
    return st;
}

BI(b_specnorm)
{
    xm A, Vm;
    xn *s;
    int n, st;
    real r;
    (void)V; (void)argc;
    if ((st = svd_x(&a[0], &A, &s, &Vm, &n))) return st;
    xn_to_real(&r, &s[n - 1]);
    v_real(out, &r);
    free(s); xm_free(&A); xm_free(&Vm);
    return 0;
}

BI(b_svd)
{
    /* {U, S, V} with A = U diag(S) V', singular values increasing */
    xm A, Vm, U;
    xn *s;
    int n, i, j, st;
    value parts[3];
    (void)V; (void)argc;
    if (need_mat(&a[0])) return E_BADTYPE;
    if (a[0].u.m->cols > nrows(a[0].u.m)) return E_DIM;
    if ((st = svd_x(&a[0], &A, &s, &Vm, &n))) return st;
    xm_mul(&U, &A, &Vm);
    for (j = 0; j < n; j++)
        for (i = 0; i < U.r; i++) if (s[j].n) xn_div(XE(&U, i, j), XE(&U, i, j), &s[j], MP);
    xm_clean(&U); xm_clean(&Vm);
    xm_to(&U, &parts[0], 0);
    v_mat(&parts[1], 1, n, 1);
    for (i = 0; i < n; i++) { real r; xn_to_real(&r, &s[i]); v_real(&parts[1].u.m->v[i], &r); }
    xm_to(&Vm, &parts[2], 0);
    free(s); xm_free(&A); xm_free(&Vm); xm_free(&U);
    return list_of(out, parts, 3);
}

BI(b_schur)
{
    /* real eigenvalues: U = the eigenvectors made orthonormal, T = U'AU */
    xm A, X, Xt, T1, T;
    cx *z, *v;
    int st, n, i, j, k;
    value parts[2];
    (void)V; (void)argc;
    if ((st = need_square(&a[0]))) return st;
    if ((st = xm_from(&a[0], &A))) return st;
    n = A.r;
    if ((st = eig_vecs_x(&A, &z, &v))) { xm_free(&A); return st; }
    for (j = 0; j < n; j++) if (!negligible(&z[j].im, &z[j].re)) { free(z); free(v); xm_free(&A); return E_NOTIMPL; }
    xm_new(&X, n, n);
    for (j = 0; j < n; j++) for (i = 0; i < n; i++) *XE(&X, i, j) = v[j * n + i].re;
    free(z); free(v);
    for (j = 0; j < n; j++) {      /* Gram-Schmidt on the columns */
        xn d, t, nrm;
        for (k = 0; k < j; k++) {
            xn_zero(&d);
            for (i = 0; i < n; i++) { xn_mul(&t, XE(&X, i, j), XE(&X, i, k), MP); xn_add(&d, &d, &t, MP); }
            for (i = 0; i < n; i++) { xn_mul(&t, &d, XE(&X, i, k), MP); xn_sub(XE(&X, i, j), XE(&X, i, j), &t, MP); }
        }
        xn_zero(&nrm);
        for (i = 0; i < n; i++) { xn_mul(&t, XE(&X, i, j), XE(&X, i, j), MP); xn_add(&nrm, &nrm, &t, MP); }
        xn_sqrt(&nrm, &nrm, MP);
        if (j > 0) {               /* first non-zero component positive */
            for (i = 0; i < n && negligible(XE(&X, i, j), &nrm); i++) {}
            if (i < n && XE(&X, i, j)->s) nrm.s = 1;
        }
        if (nrm.n) for (i = 0; i < n; i++) xn_div(XE(&X, i, j), XE(&X, i, j), &nrm, MP);
    }
    xm_trn(&Xt, &X);
    xm_mul(&T1, &Xt, &A);
    xm_mul(&T, &T1, &X);
    xm_clean(&X); xm_clean(&T);
    xm_to(&X, &parts[0], 0);
    xm_to(&T, &parts[1], 0);
    xm_free(&A); xm_free(&X); xm_free(&Xt); xm_free(&T1); xm_free(&T);
    return list_of(out, parts, 2);
}

/* ------------------------------------------------------------ polynomials */

BI(b_polyeval)
{
    const value *c;
    int n, i, st;
    value acc;
    (void)argc;
    if (elems(&a[0], &c, &n)) return E_BADTYPE;
    v_int32(&acc, 0);
    for (i = 0; i < n; i++) {
        value t, u;
        if ((st = ar_binary(V, OP_MUL, &acc, &a[1], &t))) { v_decref(&acc); return st; }
        v_decref(&acc);
        st = ar_binary(V, OP_ADD, &t, &c[i], &u);
        v_decref(&t);
        if (st) return st;
        acc = u;
    }
    *out = acc;
    return 0;
}

BI(b_polycoef)
{
    const value *r;
    value *c;
    int n, i, k, st = 0;
    (void)argc;
    if (elems(&a[0], &r, &n)) return E_BADTYPE;
    c = (value *)malloc((size_t)(n + 1) * sizeof(value));
    if (!c) return E_MEM;
    v_int32(&c[0], 1);
    for (k = 0; k < n && !st; k++) {
        /* c(x) * (x - r_k) */
        v_int32(&c[k + 1], 0);
        for (i = k + 1; i >= 1 && !st; i--) {
            value t, u;
            if ((st = ar_binary(V, OP_MUL, &c[i - 1], &r[k], &t))) break;
            st = ar_binary(V, OP_SUB, &c[i], &t, &u);
            v_decref(&t);
            if (st) break;
            v_decref(&c[i]);
            c[i] = u;
        }
    }
    if (!st) {
        if (a[0].t == T_LIST) {
            if (!(st = v_list(out, (uint32_t)(n + 1)))) for (i = 0; i <= n; i++) { out->u.l->v[i] = c[i]; v_none(&c[i]); }
        } else {
            if (!(st = v_mat(out, 1, n + 1, 1))) for (i = 0; i <= n; i++) out->u.m->v[i] = c[i];
        }
    }
    for (i = 0; i <= n; i++) v_decref(&c[i]);
    free(c);
    return st;
}

BI(b_polyroot)
{
    const value *e;
    int n, i, st, s = 0;
    cx *p, *z;
    (void)V; (void)argc;
    if (elems(&a[0], &e, &n)) return E_BADTYPE;
    while (s < n && ((e[s].t == T_REAL && dec_is_zero(&e[s].u.r)))) s++;   /* leading zeros */
    n -= s;
    if (n <= 1) return v_list(out, 0);
    p = (cx *)malloc((size_t)n * sizeof(cx));
    z = (cx *)malloc((size_t)n * sizeof(cx));
    if (!p || !z) { free(p); free(z); return E_MEM; }
    for (i = 0; i < n; i++) if (cx_from(&e[s + i], &p[i])) { free(p); free(z); return E_BADTYPE; }
    st = dk_roots(p, n - 1, z);
    if (!st) {
        cx_sort(z, n - 1, 0);
        if (!(st = v_list(out, (uint32_t)(n - 1))))
            for (i = 0; i < n - 1; i++) cx_to(&z[i], &out->u.l->v[i]);
    }
    free(p); free(z);
    return st;
}
