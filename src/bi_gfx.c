/* bi_gfx.c -- the drawing built-ins: G0..G9, the _P (pixel) commands and
 * their Cartesian twins, TEXTOUT, DRAWMENU, FREEZE, RGB, C->PX / PX->C.
 *
 * Arguments follow the Prime: an optional grob first (G0 when absent), then
 * the coordinates, then optional colours; optional arguments fill from the
 * left.  Pixel coordinates are truncated toward zero (5.7 -> 5, -0.5 -> 0);
 * Cartesian ones go through the Function app's window (Xmin..Ymax) the way
 * the Prime maps them (see cart_x/cart_y).  Every drawing command answers 1.
 * What was checked on HP's Virtual Calculator is in notes/ppl.md
 * ("Graphics") and tests/lang/gfx.ppl.
 */
#include "bi.h"
#include "gfx.h"
#include <string.h>
#include <stdlib.h>

#define COORD_MAX 16384

static void set_int(value *out, uint32_t x)
{
    v_none(out);
    out->t = T_INT; out->ib = B_DEFAULT; out->iw = 0;
    out->u.i.hi = 0; out->u.i.lo = x;
}

static int one(value *out) { v_int32(out, 1); return 0; }

/* the grob of a T_GROB value */
static grob *grob_of(ppl_vm *V, const value *v) { return gfx_grob(V, v->ib); }

/* an optional grob first: *g is it (or G0), a/argc skip it */
static int opt_grob(ppl_vm *V, value **a, int *argc, grob **g)
{
    if (!gfx_get(V)) return E_MEM;
    if (*argc && (*a)[0].t == T_GROB) { *g = grob_of(V, &(*a)[0]); (*a)++; (*argc)--; }
    else *g = gfx_grob(V, 0);
    return *g ? 0 : E_MEM;
}

static int clampi(int32_t v) { return v < -COORD_MAX ? -COORD_MAX : v > COORD_MAX ? COORD_MAX : (int)v; }

/* a number: real (or # integer) -> int, truncated toward zero, clamped */
static int num_i(const value *v, int *out)
{
    real r;
    int32_t n;
    if (v->t == T_INT) { *out = clampi((int32_t)v->u.i.lo); return 0; }
    if (v->t != T_REAL) return E_BADTYPE;
    r = v->u.r;
    if (dec_xpon(&r) >= 9) { *out = r.s ? -COORD_MAX : COORD_MAX; return 0; }
    if (dec_to_int32_trunc(&r, &n)) return E_BADVAL;
    *out = clampi(n);
    return 0;
}

/* a colour: the low 32 bits of an integer value (#FF0000h, 16711680, RGB()) */
static int colour(const value *v, uint32_t *c)
{
    if (v->t == T_INT) { *c = v->u.i.lo; return 0; }
    if (v->t == T_REAL) {
        real t;
        uint32_t hi, lo;
        dec_abs(&t, &v->u.r);
        if (dec_to_u64(&t, &hi, &lo)) { *c = 0xFFFFFFFFu; return 0; }
        *c = v->u.r.s ? (uint32_t)0 - lo : lo;
        return 0;
    }
    return E_BADTYPE;
}

/* ---- Cartesian coordinates ----
 * Measured with PIXON on the Virtual Calculator, default window
 * (-15.9..15.9, -10.9..10.9) and 0..31.8, 0..21.8:
 *   column = floor(u + 1/2) + 1 with u = (x - Xmin) * 318 / (Xmax - Xmin)
 *            (x = 0.04 -> 160, 0.05 -> 161, -1.45 -> 146, -1.4 -> 146);
 *   row    = ceil(v) in the upper half (v < 109), floor(v) + 1 from the
 *            middle down, with v = (Ymax - y) * 218 / (Ymax - Ymin)
 *            (y = 0.1 -> 108, 0.05 -> 109, 0 -> 110, -0.1 -> 111; in the
 *            second window y = 1 -> 209 and y = 15 -> 68).
 * C->PX answers {floor(u) + 1, floor(v)} (C->PX(0,0) = {160,109}). */
static int win_x(ppl_vm *V, const real *x, real *v)
{
    real t, k, d;
    int st;
    dec_sub(&t, x, &V->plot[0]);
    dec_from_int(&k, 318);
    dec_mul(&t, &t, &k);
    dec_sub(&d, &V->plot[1], &V->plot[0]);
    if (dec_is_zero(&d)) return E_DIV0;
    st = dec_div(v, &t, &d);
    return st && !DEC_SOFT(st) ? E_BADVAL : 0;
}

static int win_y(ppl_vm *V, const real *y, real *v)
{
    real t, k, d;
    int st;
    dec_sub(&t, &V->plot[3], y);
    dec_from_int(&k, 218);
    dec_mul(&t, &t, &k);
    dec_sub(&d, &V->plot[3], &V->plot[2]);
    if (dec_is_zero(&d)) return E_DIV0;
    st = dec_div(v, &t, &d);
    return st && !DEC_SOFT(st) ? E_BADVAL : 0;
}

static int real_to_i(const real *r, int *out)
{
    int32_t n;
    if (dec_xpon(r) >= 9) { *out = r->s ? -COORD_MAX : COORD_MAX; return 0; }
    if (dec_to_int32_trunc(r, &n)) return E_BADVAL;
    *out = clampi(n);
    return 0;
}

static int cart_x(ppl_vm *V, const value *a, int *out)
{
    real x, v, h;
    int st;
    if (to_real(a, &x)) return E_BADTYPE;
    if ((st = win_x(V, &x, &v))) return st;
    dec_from_str(&h, "0.5", NULL);
    dec_add(&v, &v, &h);
    dec_floor(&v, &v);
    if ((st = real_to_i(&v, out))) return st;
    *out = clampi(*out + 1);
    return 0;
}

static int cart_y(ppl_vm *V, const value *a, int *out)
{
    real y, v, f, mid;
    int st;
    if (to_real(a, &y)) return E_BADTYPE;
    if ((st = win_y(V, &y, &v))) return st;
    dec_floor(&f, &v);
    if ((st = real_to_i(&f, out))) return st;
    dec_from_int(&mid, 109);
    if (!(dec_cmp(&f, &v) == 0 && dec_cmp(&v, &mid) < 0)) *out = clampi(*out + 1);
    return 0;
}

/* a pair of coordinates, pixel or Cartesian */
static int xy(ppl_vm *V, int cart, const value *a, int *x, int *y)
{
    int st;
    if (cart) {
        if ((st = cart_x(V, &a[0], x)) || (st = cart_y(V, &a[1], y))) return st;
        return 0;
    }
    if ((st = num_i(&a[0], x)) || (st = num_i(&a[1], y))) return st;
    return 0;
}

/* Cartesian lengths in pixels (DIMGROB) and back (GROBW, GROBH) */
static int cart_len(ppl_vm *V, const value *a, int vertical, int *out)
{
    real l, k, d;
    if (to_real(a, &l)) return E_BADTYPE;
    dec_from_int(&k, vertical ? 218 : 318);
    dec_mul(&l, &l, &k);
    dec_sub(&d, &V->plot[vertical ? 3 : 1], &V->plot[vertical ? 2 : 0]);
    if (dec_is_zero(&d)) return E_DIV0;
    dec_div(&l, &l, &d);
    dec_from_str(&k, "0.5", NULL);
    dec_add(&l, &l, &k);
    dec_floor(&l, &l);
    return real_to_i(&l, out);
}

static void len_cart(ppl_vm *V, int px, int vertical, value *out)
{
    real l, k, d;
    dec_from_int(&l, px);
    dec_sub(&d, &V->plot[vertical ? 3 : 1], &V->plot[vertical ? 2 : 0]);
    dec_mul(&l, &l, &d);
    dec_from_int(&k, vertical ? 218 : 318);
    dec_div(&l, &l, &k);
    v_real(out, &l);
}

static int done(ppl_vm *V, grob *g, int y0, int y1, value *out)
{
    gfx_touch(V, g, y0 < y1 ? y0 : y1, y0 < y1 ? y1 : y0);
    gfx_drawn(V);
    return one(out);
}

/* ------------------------------------------------------------ pixels */

static int pixon(ppl_vm *V, int argc, value *a, value *out, int cart, int off)
{
    grob *g;
    int st, x, y;
    uint32_t c = off ? GFX_WHITE : GFX_BLACK;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (argc < 2) return E_ARGC;
    if ((st = xy(V, cart, a, &x, &y))) return st;
    if (!off && argc > 2 && (st = colour(&a[2], &c))) return st;
    gfx_pixel(g, x, y, c);
    return done(V, g, y, y, out);
}
static BI(b_pixon_p) { return pixon(V, argc, a, out, 0, 0); }
static BI(b_pixon) { return pixon(V, argc, a, out, 1, 0); }
static BI(b_pixoff_p) { return pixon(V, argc, a, out, 0, 1); }
static BI(b_pixoff) { return pixon(V, argc, a, out, 1, 1); }

static int getpix(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    int st, x, y;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (argc < 2) return E_ARGC;
    if ((st = xy(V, cart, a, &x, &y))) return st;
    set_int(out, gfx_getpix(g, x, y));
    return 0;
}
static BI(b_getpix_p) { return getpix(V, argc, a, out, 0); }
static BI(b_getpix) { return getpix(V, argc, a, out, 1); }

/* ------------------------------------------------------------ RECT */

/* RECT_P([G], [x1, y1, [x2, y2]], [edge, [fill]]): optional arguments fill
 * from the left (one argument alone is the colour); the corners default to
 * the grob's, the colour to white (RECT() clears the screen); both corners
 * are drawn */
static int rect(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    int st, x0 = 0, y0 = 0, x1, y1, nc = 0;
    uint32_t edge = GFX_WHITE, fill;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    x1 = g->w - 1; y1 = g->h - 1;
    if (argc == 1) nc = 1;
    else if (argc == 2 || argc == 3) {
        if ((st = xy(V, cart, a, &x0, &y0))) return st;
        a += 2; nc = argc - 2;
    } else if (argc >= 4) {
        if ((st = xy(V, cart, a, &x0, &y0)) || (st = xy(V, cart, a + 2, &x1, &y1))) return st;
        a += 4; nc = argc - 4; if (nc > 2) nc = 2;
    }
    if (nc >= 1 && (st = colour(&a[0], &edge))) return st;
    fill = edge;
    if (nc >= 2 && (st = colour(&a[1], &fill))) return st;
    gfx_rect(g, x0, y0, x1, y1, edge, fill);
    return done(V, g, y0, y1, out);
}
static BI(b_rect_p) { return rect(V, argc, a, out, 0); }
static BI(b_rect) { return rect(V, argc, a, out, 1); }

/* ------------------------------------------------------------ LINE */

static int line(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    int st, x0, y0, x1, y1;
    uint32_t c = GFX_BLACK;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (argc && (a[0].t == T_LIST || a[0].t == T_MAT || a[0].t == T_STR)) return E_UNSUPPORTED;  /* 3D form */
    if (argc < 4) return E_ARGC;
    if ((st = xy(V, cart, a, &x0, &y0)) || (st = xy(V, cart, a + 2, &x1, &y1))) return st;
    if (argc > 4 && (st = colour(&a[4], &c))) return st;
    gfx_line(g, x0, y0, x1, y1, c);
    return done(V, g, y0, y1, out);
}
static BI(b_line_p) { return line(V, argc, a, out, 0); }
static BI(b_line) { return line(V, argc, a, out, 1); }

/* ------------------------------------------------------------ ARC */

/* a unit direction (x right, y up) scaled by 2^14 for angle t (Home angle mode) */
static int direction(ppl_vm *V, const real *t, int32_t *d)
{
    real c, s, k;
    int32_t x, y;
    dec_cos(&c, t, V->set.angle);
    dec_sin(&s, t, V->set.angle);
    dec_from_int(&k, 16384);
    dec_mul(&c, &c, &k); dec_mul(&s, &s, &k);
    dec_from_str(&k, "0.5", NULL);
    dec_add(&c, &c, &k); dec_add(&s, &s, &k);
    dec_floor(&c, &c); dec_floor(&s, &s);
    if (dec_to_int32(&c, &x) || dec_to_int32(&s, &y)) return E_BADVAL;
    d[0] = x; d[1] = y;
    return 0;
}

/* ARC_P([G], x, y, r, [a1, a2], [c]): a1 to a2 counterclockwise in the
 * Home angle mode (a1 = a2: just the point at that angle; a sweep of a full
 * turn or more: the circle); r in pixels in both forms.  c = {edge, fill}
 * draws a filled sector, measured clockwise (see gfx_pie). */
static int arc(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    int st, x, y, r, partial = 0, pie = 0;
    uint32_t c = GFX_BLACK, fill = GFX_BLACK;
    int32_t s[3], e[2];
    real a1, a2;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (argc < 3) return E_ARGC;
    if ((st = xy(V, cart, a, &x, &y)) || (st = num_i(&a[2], &r))) return st;
    if (argc == 4 || argc >= 6) {
        const value *cv = &a[argc == 4 ? 3 : 5];
        if (cv->t == T_LIST && cv->u.l->n >= 2) {
            if ((st = colour(&cv->u.l->v[1], &fill))) return st;
            pie = 1;
            cv = &cv->u.l->v[0];
        } else if (cv->t == T_LIST && cv->u.l->n == 1) cv = &cv->u.l->v[0];
        if ((st = colour(cv, &c))) return st;
    }
    if (argc >= 5) {
        real d, full, sweep, pi, two;
        if (to_real(&a[3], &a1) || to_real(&a[4], &a2)) return E_BADTYPE;
        /* the sweep from a1 to a2, counterclockwise, in radians */
        dec_sub(&d, &a2, &a1);
        if (dec_angle_to_rad(&sweep, &d, V->set.angle)) return E_BADVAL;
        dec_pi(&pi);
        dec_from_int(&two, 2);
        dec_mul(&full, &pi, &two);
        if (dec_cmp(&sweep, &full) < 0) {
            if (sweep.s) dec_mod(&sweep, &sweep, &full);
            partial = 1;
            s[2] = dec_cmp(&sweep, &pi) > 0;
        }
    }
    if (pie && partial) {
        real n1, n2;
        int32_t d1[2], d2[2];
        int p1[2], p2[2];
        dec_neg(&n1, &a1); dec_neg(&n2, &a2);
        if ((st = direction(V, &n2, s)) || (st = direction(V, &n1, e))) return st;
        if ((st = direction(V, &a1, d1)) || (st = direction(V, &a2, d2))) return st;
        /* the ends on the screen (y down): centre + r (cos a, sin a) */
        p1[0] = x + (int)(((int64_t)r * d1[0] + (d1[0] >= 0 ? 8192 : -8192)) / 16384);
        p1[1] = y + (int)(((int64_t)r * d1[1] + (d1[1] >= 0 ? 8192 : -8192)) / 16384);
        p2[0] = x + (int)(((int64_t)r * d2[0] + (d2[0] >= 0 ? 8192 : -8192)) / 16384);
        p2[1] = y + (int)(((int64_t)r * d2[1] + (d2[1] >= 0 ? 8192 : -8192)) / 16384);
        gfx_pie(g, x, y, r, s, e, p1, p2, c, fill);
        return done(V, g, y - r, y + r, out);
    }
    if (partial && ((st = direction(V, &a1, s)) || (st = direction(V, &a2, e)))) return st;
    gfx_arc(g, x, y, r, partial ? s : NULL, e, c);
    return done(V, g, y - r, y + r, out);
}
static BI(b_arc_p) { return arc(V, argc, a, out, 0); }
static BI(b_arc) { return arc(V, argc, a, out, 1); }

/* ------------------------------------------------------------ polygons */

/* points: {{x,y},...}, {(x,y),...} or [(x,y),...], also flat {x,y,x,y...} */
static int points(ppl_vm *V, const value *v, int cart, int32_t **pts, int *n)
{
    const value *e;
    uint32_t k, cnt;
    int32_t *p;
    int m = 0, st;
    value pend[2];
    int npend = 0;
    if (v->t == T_LIST) { e = v->u.l->v; cnt = v->u.l->n; }
    else if (v->t == T_MAT) { e = v->u.m->v; cnt = v->u.m->vec ? v->u.m->cols : (uint32_t)v->u.m->rows * v->u.m->cols; }
    else return E_BADTYPE;
    if (v->t == T_MAT && !v->u.m->vec && v->u.m->cols == 2) {
        /* a matrix of [x, y] rows */
        p = (int32_t *)malloc(sizeof(int32_t) * 2 * (v->u.m->rows + 1));
        if (!p) return E_MEM;
        for (k = 0; k < v->u.m->rows; k++) {
            int x, y;
            if ((st = xy(V, cart, &e[2 * k], &x, &y))) { free(p); return st; }
            p[2 * m] = x; p[2 * m + 1] = y; m++;
        }
        *pts = p; *n = m;
        return 0;
    }
    p = (int32_t *)malloc(sizeof(int32_t) * 2 * (cnt + 1));
    if (!p) return E_MEM;
    for (k = 0; k < cnt; k++) {
        int x, y;
        if (e[k].t == T_CPLX) {
            value pr[2];
            v_real(&pr[0], &e[k].u.c.re); v_real(&pr[1], &e[k].u.c.im);
            if ((st = xy(V, cart, pr, &x, &y))) { free(p); return st; }
        } else if (e[k].t == T_LIST && e[k].u.l->n >= 2) {
            if ((st = xy(V, cart, e[k].u.l->v, &x, &y))) { free(p); return st; }
        } else if (e[k].t == T_MAT && e[k].u.m->vec && e[k].u.m->cols >= 2) {
            if ((st = xy(V, cart, e[k].u.m->v, &x, &y))) { free(p); return st; }
        } else if (e[k].t == T_REAL || e[k].t == T_INT) {
            pend[npend++] = e[k];
            if (npend < 2) continue;
            npend = 0;
            if ((st = xy(V, cart, pend, &x, &y))) { free(p); return st; }
        } else { free(p); return E_BADTYPE; }
        p[2 * m] = x; p[2 * m + 1] = y; m++;
    }
    *pts = p; *n = m;
    return 0;
}

static int alpha_arg(const value *v, int *al)
{
    int st = num_i(v, al);
    if (st) return st;
    if (*al < 0) *al = 0;
    if (*al > 255) *al = 255;
    return 0;
}

/* FILLPOLY_P([G], points, colour, [alpha]) */
static int fillpoly(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    int st, n, i, y0 = GFX_H, y1 = -1, al = -1;
    int32_t *p;
    uint32_t c = GFX_BLACK;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (argc < 1) return E_ARGC;
    if (argc > 1 && (st = colour(&a[1], &c))) return st;
    if (argc > 2 && (st = alpha_arg(&a[2], &al))) return st;
    if (al >= 0) c = (c & 0xFFFFFFu) | ((uint32_t)al << 24);   /* alpha 128 on white: #7FFF7Fh for green */
    if ((st = points(V, &a[0], cart, &p, &n))) return st;
    gfx_poly(g, p, n, c);
    for (i = 0; i < n; i++) { if (p[2 * i + 1] < y0) y0 = p[2 * i + 1]; if (p[2 * i + 1] > y1) y1 = p[2 * i + 1]; }
    free(p);
    return done(V, g, y0, y1, out);
}
static BI(b_fillpoly_p) { return fillpoly(V, argc, a, out, 0); }
static BI(b_fillpoly) { return fillpoly(V, argc, a, out, 1); }

/* TRIANGLE_P([G], x1, y1, x2, y2, x3, y3, c1, [c2, c3], [alpha]) or with
 * {x, y, [c]} points */
static int triangle(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    int st, i, k = 0, al = -1, ncol = 0, x, y;
    int32_t p[6];
    uint32_t c[3] = { GFX_BLACK, GFX_BLACK, GFX_BLACK };
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (argc == 0) return E_UNSUPPORTED;            /* TRIANGLE_P(G): z-clipping string */
    if (a[0].t == T_LIST) {
        if (argc < 3) return E_UNSUPPORTED;
        for (i = 0; i < 3; i++) {
            const value *pv = &a[i];
            if (pv->t != T_LIST || pv->u.l->n < 2) return E_UNSUPPORTED;
            if ((st = xy(V, cart, pv->u.l->v, &x, &y))) return st;
            p[2 * i] = x; p[2 * i + 1] = y;
            if (pv->u.l->n > 2) { if ((st = colour(&pv->u.l->v[2], &c[i]))) return st; ncol++; }
        }
        if (ncol == 1) ncol = 0;
        k = 3;
        if (ncol == 0 && argc > k && (a[k].t == T_INT || a[k].t == T_REAL)) { if ((st = colour(&a[k], &c[0]))) return st; k++; }
    } else {
        if (argc < 6) return E_ARGC;
        for (i = 0; i < 3; i++) {
            if ((st = xy(V, cart, &a[2 * i], &x, &y))) return st;
            p[2 * i] = x; p[2 * i + 1] = y;
        }
        k = 6;
        if (argc >= 9) {
            for (i = 0; i < 3; i++) if ((st = colour(&a[k + i], &c[i]))) return st;
            ncol = 3; k += 3;
        } else if (argc > k) { if ((st = colour(&a[k], &c[0]))) return st; k++; }
    }
    if (argc > k && (a[k].t == T_INT || a[k].t == T_REAL) && (st = alpha_arg(&a[k], &al))) return st;
    if (ncol == 3) gfx_triangle(g, p, c, 3, al);
    else gfx_triangle(g, p, c, 1, al);
    {
        int y0 = p[1], y1 = p[1];
        for (i = 1; i < 3; i++) { if (p[2 * i + 1] < y0) y0 = p[2 * i + 1]; if (p[2 * i + 1] > y1) y1 = p[2 * i + 1]; }
        return done(V, g, y0, y1, out);
    }
}
static BI(b_triangle_p) { return triangle(V, argc, a, out, 0); }
static BI(b_triangle) { return triangle(V, argc, a, out, 1); }

/* ------------------------------------------------------------ INVERT */

static int invert(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    int st, x0 = 0, y0 = 0, x1, y1;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    x1 = g->w - 1; y1 = g->h - 1;
    if (argc >= 2 && (st = xy(V, cart, a, &x0, &y0))) return st;
    if (argc >= 4 && (st = xy(V, cart, a + 2, &x1, &y1))) return st;
    gfx_invert(g, x0, y0, x1, y1);
    return done(V, g, y0, y1, out);
}
static BI(b_invert_p) { return invert(V, argc, a, out, 0); }
static BI(b_invert) { return invert(V, argc, a, out, 1); }

/* ------------------------------------------------------------ grobs */

/* DIMGROB_P(G, w, h, [colour]) -- white when no colour */
static int dimgrob(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    int st, w, h, n;
    uint32_t c = GFX_WHITE;
    if (!gfx_get(V)) return E_MEM;
    if (argc < 2 || a[0].t != T_GROB) return argc < 2 ? E_ARGC : E_BADTYPE;
    n = a[0].ib;
    if (argc == 2) return E_UNSUPPORTED;          /* DIMGROB_P(G, list) */
    if (cart) {
        if ((st = cart_len(V, &a[1], 0, &w)) || (st = cart_len(V, &a[2], 1, &h))) return st;
    } else if ((st = num_i(&a[1], &w)) || (st = num_i(&a[2], &h))) return st;
    if (argc > 3) {
        if (a[3].t == T_LIST) return E_UNSUPPORTED;   /* pixel data */
        if ((st = colour(&a[3], &c))) return st;
    }
    if (w < 0 || h < 0) return E_BADVAL;
    if ((st = gfx_dim(V, n, w, h, c))) return st;
    gfx_drawn(V);
    return one(out);
}
static BI(b_dimgrob_p) { return dimgrob(V, argc, a, out, 0); }
static BI(b_dimgrob) { return dimgrob(V, argc, a, out, 1); }

static int grobwh(ppl_vm *V, int argc, value *a, value *out, int cart, int h)
{
    grob *g;
    int st;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (cart) len_cart(V, h ? g->h : g->w, h, out);
    else v_int32(out, h ? g->h : g->w);
    return 0;
}
static BI(b_grobw_p) { return grobwh(V, argc, a, out, 0, 0); }
static BI(b_grobh_p) { return grobwh(V, argc, a, out, 0, 1); }
static BI(b_grobw) { return grobwh(V, argc, a, out, 1, 0); }
static BI(b_grobh) { return grobwh(V, argc, a, out, 1, 1); }

/* SUBGROB_P([src], [x1, y1, x2, y2], trg): x2, y2 excluded (0,0,4,4 is 4 wide);
 * the region must lie inside src */
static int subgrob(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *s, *t;
    int st, x0 = 0, y0 = 0, x1, y1, n, y;
    if ((st = opt_grob(V, &a, &argc, &s))) return st;
    if (argc < 1 || a[argc - 1].t != T_GROB) return E_BADTYPE;
    n = a[argc - 1].ib;
    if (n == 0) return E_BADVAL;
    x1 = s->w; y1 = s->h;
    if (argc >= 3 && (st = xy(V, cart, a, &x0, &y0))) return st;
    if (argc >= 5 && (st = xy(V, cart, a + 2, &x1, &y1))) return st;
    if (x0 > x1) { int k = x0; x0 = x1; x1 = k; }
    if (y0 > y1) { int k = y0; y0 = y1; y1 = k; }
    if (x0 < 0 || y0 < 0 || x1 > s->w || y1 > s->h || x0 == x1 || y0 == y1) return E_BADVAL;
    t = gfx_grob(V, n);
    if (t == s) {
        /* in place: copy the region first */
        grob tmp;
        tmp.w = x1 - x0; tmp.h = y1 - y0;
        tmp.px = (uint32_t *)malloc((size_t)tmp.w * tmp.h * sizeof(uint32_t));
        if (!tmp.px) return E_MEM;
        for (y = 0; y < tmp.h; y++) memcpy(tmp.px + (long)y * tmp.w, s->px + (long)(y0 + y) * s->w + x0, (size_t)tmp.w * 4);
        if ((st = gfx_dim(V, n, tmp.w, tmp.h, 0))) { free(tmp.px); return st; }
        memcpy(t->px, tmp.px, (size_t)tmp.w * tmp.h * 4);
        free(tmp.px);
        return one(out);
    }
    if ((st = gfx_dim(V, n, x1 - x0, y1 - y0, 0))) return st;
    for (y = 0; y < t->h; y++) memcpy(t->px + (long)y * t->w, s->px + (long)(y0 + y) * s->w + x0, (size_t)t->w * 4);
    return one(out);
}
static BI(b_subgrob_p) { return subgrob(V, argc, a, out, 0); }
static BI(b_subgrob) { return subgrob(V, argc, a, out, 1); }

/* BLIT_P([trg, dx1, dy1, [dx2, dy2]], src, [sx1, sy1, [sx2, sy2]], [c], [alpha]) */
static int blit(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *d, *s;
    int st, i, k, nd, ns, dx0 = 0, dy0 = 0, dx1, dy1, sx0 = 0, sy0 = 0, sx1, sy1;
    int use_key = 0, al = 255;
    uint32_t key = 0;
    if (!gfx_get(V)) return E_MEM;
    /* find the source: the last grob argument */
    for (k = -1, i = 0; i < argc; i++) if (a[i].t == T_GROB) k = i;
    if (k < 0) return argc && a[argc - 1].t == T_STR ? E_UNSUPPORTED : E_BADTYPE;
    for (i = 0; i < argc; i++) if (a[i].t == T_STR) return E_UNSUPPORTED;
    s = grob_of(V, &a[k]);
    if (k > 0 && a[0].t == T_GROB) { d = grob_of(V, &a[0]); i = 1; }
    else { d = gfx_grob(V, 0); i = 0; }
    nd = k - i;
    ns = argc - k - 1;
    if (nd != 0 && nd != 2 && nd != 4) return E_ARGC;
    if (nd >= 2 && (st = xy(V, cart, &a[i], &dx0, &dy0))) return st;
    if (nd == 4 && (st = xy(V, cart, &a[i + 2], &dx1, &dy1))) return st;
    sx1 = s->w; sy1 = s->h;
    a += k + 1;
    switch (ns) {
    case 0: break;
    case 1: use_key = 1; if ((st = colour(&a[0], &key))) return st; break;
    case 2: if ((st = xy(V, cart, a, &sx0, &sy0))) return st; break;
    case 3: if ((st = xy(V, cart, a, &sx0, &sy0)) || (st = colour(&a[2], &key))) return st; use_key = 1; break;
    default:
        if ((st = xy(V, cart, a, &sx0, &sy0)) || (st = xy(V, cart, a + 2, &sx1, &sy1))) return st;
        if (ns >= 5) { if ((st = colour(&a[4], &key))) return st; use_key = 1; }
        if (ns >= 6 && (st = alpha_arg(&a[5], &al))) return st;
        break;
    }
    /* a destination given right to left or bottom to top draws nothing
     * (Virtual Calculator); the same is assumed of the source */
    if (nd < 4) { dx1 = dx0 + (sx1 - sx0); dy1 = dy0 + (sy1 - sy0); }
    gfx_blit(d, dx0, dy0, dx1, dy1, s, sx0, sy0, sx1, sy1, use_key, key, al);
    return done(V, d, dy0, dy1 - 1, out);
}
static BI(b_blit_p) { return blit(V, argc, a, out, 0); }
static BI(b_blit) { return blit(V, argc, a, out, 1); }

/* ------------------------------------------------------------ text */

/* the UTF-16 text of any value, as Home would show it */
static int text_of(ppl_vm *V, const value *v, value *s)
{
    sbuf b;
    int st;
    if (v->t == T_STR) { v_copy(s, v); return 0; }
    sb_init(&b);
    v_to_text(V, &b, v, 0);
    st = v_str_utf8(s, b.p ? b.p : "", b.n);
    sb_free(&b);
    return st;
}

/* TEXTOUT_P(text, [G], x, y, [font, c1, width, c2]) -> the x where it ended.
 * Font 0 = the Home font (3 here), 1..7 = 10..22 px; c2 fills the cell
 * [x-1, x+width] x [y, y+cell-1] first.  Text wholly outside the grob draws
 * nothing and answers x plus its width in font 3, whatever the font (the
 * Virtual Calculator answers 622 for "abc" in font 7 at x 600 on a 512-wide
 * grob). */
static int textout(ppl_vm *V, int argc, value *a, value *out, int cart)
{
    grob *g;
    value s;
    int st, x, y, font = 0, maxw = -1, bgon = 0, r;
    uint32_t fg = GFX_BLACK, bg = 0;
    value *t = &a[0];
    if (argc < 3) return E_ARGC;
    a++; argc--;
    if ((st = opt_grob(V, &a, &argc, &g))) return st;
    if (argc < 2) return E_ARGC;
    if ((st = xy(V, cart, a, &x, &y))) return st;
    if (argc > 2 && (st = num_i(&a[2], &font))) return st;
    if (font < 0 || font > 7) return E_BADVAL;
    if (argc > 3 && (st = colour(&a[3], &fg))) return st;
    if (argc > 4 && (st = num_i(&a[4], &maxw))) return st;
    if (argc > 5) { if ((st = colour(&a[5], &bg))) return st; bgon = 1; }
    if ((st = text_of(V, t, &s))) return st;
    r = gfx_text(g, font ? font : 3, s.u.s->c, s.u.s->len, x, y, fg, maxw, bgon, bg);
    v_decref(&s);
    v_int32(out, r);
    gfx_touch(V, g, y - 8, y + gfx_font_height(font ? font : 3));
    gfx_drawn(V);
    return 0;
}
static BI(b_textout_p) { return textout(V, argc, a, out, 0); }
static BI(b_textout) { return textout(V, argc, a, out, 1); }

/* ------------------------------------------------------------ DRAWMENU */

/* The soft-key bar as the Prime draws it (measured): row 219 white, six
 * buttons on rows 220-239 at x 0, 53, 106, 159, 212, 265 (52 wide, the last
 * 55), black rounded border, a grey gradient, the label in white, font 3,
 * centred, clipped to the button. */
static const uint8_t menu_grey[18] = { 32, 33, 35, 38, 40, 43, 46, 50, 52, 56, 60, 62, 66, 69, 71, 74, 76, 78 };

static void menu_button(grob *g, int x0, int x1, const pstr *label)
{
    int y, w;
    for (y = 220; y <= 239; y++) {
        int l = x0, r = x1;
        uint32_t edge = GFX_BLACK;
        if (y == 220 || y == 239) { gfx_fill(g, x0, y, x1, y, GFX_WHITE); gfx_fill(g, x0 + 3, y, x1 - 3, y, edge); continue; }
        if (y == 221 || y == 238) {
            gfx_pixel(g, x0, y, GFX_WHITE); gfx_pixel(g, x1, y, GFX_WHITE);
            gfx_fill(g, x0 + 1, y, x0 + 2, y, edge); gfx_fill(g, x1 - 2, y, x1 - 1, y, edge);
            l = x0 + 3; r = x1 - 3;
        } else {
            gfx_pixel(g, x0, y, edge); gfx_pixel(g, x1, y, edge);
            l = x0 + 1; r = x1 - 1;
        }
        {
            uint32_t v = menu_grey[y - 221];
            gfx_fill(g, l, y, r, y, (v << 16) | (v << 8) | v);
        }
    }
    if (!label || !label->len) return;
    w = gfx_text_width(3, label->c, label->len);
    {
        int tx = x0 + ((x1 - x0 + 1) - w) / 2;
        if ((x1 - x0 + 1) - w < 2) tx = x0 + 1;
        gfx_text(g, 3, label->c, label->len, tx, 222, GFX_WHITE, x1 + 1 - tx, 0, 0);
    }
}

static BI(b_drawmenu)
{
    static const int bx[7] = { 0, 53, 106, 159, 212, 265, 320 };
    grob *g;
    int i, st;
    value lab[6];
    const value *src = a;
    int n = argc;
    if (!gfx_get(V)) return E_MEM;
    g = gfx_grob(V, 0);
    if (argc == 1 && a[0].t == T_LIST) { src = a[0].u.l->v; n = (int)a[0].u.l->n; }
    if (n > 6) n = 6;
    for (i = 0; i < 6; i++) v_none(&lab[i]);
    for (i = 0; i < n; i++) if ((st = text_of(V, &src[i], &lab[i]))) { while (i--) v_decref(&lab[i]); return st; }
    gfx_fill(g, 0, 219, g->w - 1, 219, GFX_WHITE);
    for (i = 0; i < 6; i++) {
        menu_button(g, bx[i], bx[i + 1] - 2 + (i == 5), i < n ? lab[i].u.s : NULL);
        if (i < 5) gfx_fill(g, bx[i + 1] - 1, 220, bx[i + 1] - 1, 239, GFX_WHITE);
    }
    for (i = 0; i < n; i++) v_decref(&lab[i]);
    return done(V, g, 219, 239, out);
}

/* ------------------------------------------------------------ misc */

static BI(b_freeze)
{
    (void)argc; (void)a;
    if (!gfx_get(V)) return E_MEM;
    V->gfx->frozen = 1;
    gfx_flush(V);
    return one(out);
}

/* RGB(r, g, b, [a]) = #AARRGGBB: RGB(255,0,0,200) is #C8FF0000h */
static BI(b_rgb)
{
    int32_t r, g, b, al = 0;
    (void)V;
    if (bi_int(&a[0], &r) || bi_int(&a[1], &g) || bi_int(&a[2], &b)) return E_BADVAL;
    if (argc > 3 && bi_int(&a[3], &al)) return E_BADVAL;
    set_int(out, ((uint32_t)(al & 255) << 24) | ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255));
    return 0;
}

/* C->PX(x, y) = {floor(v_x), floor(v_y)} with the window's v of cart_x/cart_y
 * ({160,109} for (0,0) in the default window); PX->C the inverse */
static int plot_args(int argc, const value *a, real *x, real *y)
{
    if (argc == 1 && a[0].t == T_LIST && a[0].u.l->n == 2) a = a[0].u.l->v;
    else if (argc < 2) return 1;
    if (to_real(&a[0], x) || to_real(&a[1], y)) return -1;
    return 0;
}

static BI(b_ctopx)
{
    real x, y, v;
    int st = plot_args(argc, a, &x, &y);
    if (st < 0) return E_BADTYPE;
    if ((st = v_list(out, 2))) return st;
    if (plot_args(argc, a, &x, &y)) return 0;
    if ((st = win_x(V, &x, &v))) return st;
    { real one_; dec_from_int(&one_, 1); dec_add(&v, &v, &one_); }
    dec_floor(&v, &v);
    v_real(&out->u.l->v[0], &v);
    if ((st = win_y(V, &y, &v))) return st;
    dec_floor(&v, &v);
    v_real(&out->u.l->v[1], &v);
    return 0;
}

static BI(b_pxtoc)
{
    real x, y, t, k, d;
    int st = plot_args(argc, a, &x, &y);
    if (st < 0) return E_BADTYPE;
    if (st > 0) { dec_zero(&x); dec_zero(&y); }    /* PX->C(10) answers the corner */
    if ((st = v_list(out, 2))) return st;
    /* x = Xmin + (px - 1) * (Xmax - Xmin) / 318; y = Ymax - py * (Ymax - Ymin) / 218 */
    dec_from_int(&k, 1);
    dec_sub(&t, &x, &k);
    dec_sub(&d, &V->plot[1], &V->plot[0]);
    dec_mul(&t, &t, &d);
    dec_from_int(&k, 318);
    dec_div(&t, &t, &k);
    dec_add(&t, &t, &V->plot[0]);
    v_real(&out->u.l->v[0], &t);
    dec_sub(&d, &V->plot[3], &V->plot[2]);
    dec_mul(&t, &y, &d);
    dec_from_int(&k, 218);
    dec_div(&t, &t, &k);
    dec_sub(&t, &V->plot[3], &t);
    v_real(&out->u.l->v[1], &t);
    return 0;
}

const builtin bi_gfx_table[] = {
    { "PIXON_P", b_pixon_p, 2, 4, 0 }, { "PIXON", b_pixon, 2, 4, 0 },
    { "PIXOFF_P", b_pixoff_p, 2, 3, 0 }, { "PIXOFF", b_pixoff, 2, 3, 0 },
    { "GETPIX_P", b_getpix_p, 2, 3, 0 }, { "GETPIX", b_getpix, 2, 3, 0 },
    { "RECT_P", b_rect_p, 0, 7, 0 }, { "RECT", b_rect, 0, 7, 0 },
    { "LINE_P", b_line_p, 1, 7, 0 }, { "LINE", b_line, 1, 7, 0 },
    { "ARC_P", b_arc_p, 3, 7, 0 }, { "ARC", b_arc, 3, 7, 0 },
    { "FILLPOLY_P", b_fillpoly_p, 1, 4, 0 }, { "FILLPOLY", b_fillpoly, 1, 4, 0 },
    { "TRIANGLE_P", b_triangle_p, 0, -1, 0 }, { "TRIANGLE", b_triangle, 0, -1, 0 },
    { "INVERT_P", b_invert_p, 0, 5, 0 }, { "INVERT", b_invert, 0, 5, 0 },
    { "DIMGROB_P", b_dimgrob_p, 2, 4, 0 }, { "DIMGROB", b_dimgrob, 2, 4, 0 },
    { "GROBW_P", b_grobw_p, 0, 1, 0 }, { "GROBW", b_grobw, 0, 1, 0 },
    { "GROBH_P", b_grobh_p, 0, 1, 0 }, { "GROBH", b_grobh, 0, 1, 0 },
    { "SUBGROB_P", b_subgrob_p, 1, 6, 0 }, { "SUBGROB", b_subgrob, 1, 6, 0 },
    { "BLIT_P", b_blit_p, 1, 13, 0 }, { "BLIT", b_blit, 1, 13, 0 },
    { "TEXTOUT_P", b_textout_p, 3, 9, 0 }, { "TEXTOUT", b_textout, 3, 9, 0 },
    { "DRAWMENU", b_drawmenu, 0, -1, 0 },
    { "FREEZE", b_freeze, 0, 0, BF_NOPAREN },
    { "RGB", b_rgb, 3, 4, 0 },
    { "C\xE2\x86\x92PX", b_ctopx, 0, 2, 0 }, { "PX\xE2\x86\x92" "C", b_pxtoc, 0, 2, 0 },
    { NULL, NULL, 0, 0, 0 }
};
