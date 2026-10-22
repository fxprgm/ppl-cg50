/* gfx.c -- grobs and drawing primitives (see gfx.h).
 *
 * What is known about the Prime's drawing, and from where, is in
 * notes/ppl.md ("Graphics").  In short (HP's Virtual Calculator, 2026-10-18):
 *   - pixels are 0xAARRGGBB; a colour whose alpha byte a is 0 is drawn as
 *     is, 255 draws nothing, and 1..254 blends each channel as
 *     (src*(255-a) + dst*a) >> 8 (PIXON_P of RGB(255,0,0,200) on black gives
 *     #360000h, RGB(0,0,0,254) on white #FDFDFDh);
 *   - lines include both end points; rectangles both corners;
 *   - circles are the midpoint-style circle with y = ceil(sqrt(r^2-x^2)) in
 *     the first octant; arcs are the circle's pixels between the two angles.
 */
#include "gfx.h"
#include "gfx_font.h"
#include <stdlib.h>
#include <string.h>

#ifndef GFX_MALLOC
#define GFX_MALLOC(n) malloc(n)
#define GFX_FREE(p) free(p)
#endif

/* ------------------------------------------------------------ state */

gfx_state *gfx_get(ppl_vm *V)
{
    if (!V->gfx) gfx_init(V);
    return V->gfx;
}

void gfx_init(ppl_vm *V)
{
    gfx_state *G;
    if (V->gfx) return;
    G = (gfx_state *)calloc(1, sizeof *G);
    if (!G) return;
    V->gfx = G;
    G->dirty0 = GFX_H; G->dirty1 = -1;
    if (gfx_dim(V, 0, GFX_W, GFX_H, GFX_WHITE)) { free(G); V->gfx = NULL; }
}

/* a program starts on a blank screen: G0 white at its full size (on the Prime the screen
 * holds Home then, not the last program's drawing: angryb draws only rows 0-219 and showed
 * spiro's DRAWMENU below them) */
void gfx_clear_screen(ppl_vm *V)
{
    gfx_state *G = gfx_get(V);
    long i, n;
    if (!G) return;
    if (G->g[0].w != GFX_W || G->g[0].h != GFX_H || !G->g[0].px) {
        gfx_dim(V, 0, GFX_W, GFX_H, GFX_WHITE);
        return;
    }
    n = (long)GFX_W * GFX_H;
    for (i = 0; i < n; i++) G->g[0].px[i] = GFX_WHITE;
}

void gfx_free(ppl_vm *V)
{
    int i;
    gfx_state *G = V->gfx;
    if (!G) return;
    for (i = 0; i < GFX_NGROB; i++) if (G->g[i].px) GFX_FREE(G->g[i].px);
    free(G);
    V->gfx = NULL;
}

grob *gfx_grob(ppl_vm *V, int n)
{
    gfx_state *G = gfx_get(V);
    if (!G || n < 0 || n >= GFX_NGROB) return NULL;
    return &G->g[n];
}

int gfx_dim(ppl_vm *V, int n, int w, int h, uint32_t fill)
{
    gfx_state *G = V->gfx;
    grob *g;
    uint32_t *px;
    long i, np;
    if (!G || n < 0 || n >= GFX_NGROB) return E_BADVAL;
    if (w < 0 || h < 0) return E_BADVAL;
    np = (long)w * h;
    if (w > 65535 || h > 65535 || np > GFX_MAX_PIXELS) return E_MEM;
    g = &G->g[n];
    if (np == (long)g->w * g->h && g->px) px = g->px;
    else {
        px = np ? (uint32_t *)GFX_MALLOC((size_t)np * sizeof(uint32_t)) : NULL;
        if (np && !px) return E_MEM;
        if (g->px) GFX_FREE(g->px);
    }
    for (i = 0; i < np; i++) px[i] = fill;
    g->px = px; g->w = w; g->h = h;
    if (n == 0) gfx_touch(V, g, 0, GFX_H - 1);
    return 0;
}

/* ------------------------------------------------------------ presenting */

void gfx_touch(ppl_vm *V, const grob *g, int y0, int y1)
{
    gfx_state *G = V->gfx;
    if (!G || g != &G->g[0]) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= GFX_H) y1 = GFX_H - 1;
    if (y0 > y1) return;
    if (y0 < G->dirty0) G->dirty0 = y0;
    if (y1 > G->dirty1) G->dirty1 = y1;
}

void gfx_flush(ppl_vm *V)
{
    gfx_state *G = V->gfx;
    if (!G || G->dirty0 > G->dirty1) return;
    if (V->present) V->present(V, G->dirty0, G->dirty1);
    G->dirty0 = GFX_H; G->dirty1 = -1;
    G->ops = 0;
    if (V->ticks) G->last_present = V->ticks();
}

void gfx_drawn(ppl_vm *V)
{
    gfx_state *G = V->gfx;
    if (!G || !V->present || G->dirty0 > G->dirty1) return;
    if (V->ticks) {
        if ((uint32_t)(V->ticks() - G->last_present) >= GFX_PRESENT_MS) gfx_flush(V);
    } else if (++G->ops >= GFX_PRESENT_OPS) gfx_flush(V);
}

void gfx_row565(ppl_vm *V, int y, uint16_t *dst)
{
    gfx_state *G = gfx_get(V);
    const grob *g = G ? &G->g[0] : NULL;
    int x, w;
    const uint32_t *p;
    if (!g || y < 0 || y >= g->h) { for (x = 0; x < GFX_W; x++) dst[x] = 0xFFFF; return; }
    w = g->w < GFX_W ? g->w : GFX_W;
    p = g->px + (long)y * g->w;
    for (x = 0; x < w; x++) {
        uint32_t c = p[x];
        dst[x] = (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
    }
    for (; x < GFX_W; x++) dst[x] = 0xFFFF;
}

/* ------------------------------------------------------------ pixels */

static uint32_t blend(uint32_t d, uint32_t c, unsigned a)
{
    unsigned ia = 255 - a;
    uint32_t r = ((((c >> 16) & 255u) * ia + ((d >> 16) & 255u) * a) >> 8);
    uint32_t gg = ((((c >> 8) & 255u) * ia + ((d >> 8) & 255u) * a) >> 8);
    uint32_t b = (((c & 255u) * ia + (d & 255u) * a) >> 8);
    return (d & 0xFF000000u) | (r << 16) | (gg << 8) | b;
}

/* fills (RECT, FILLPOLY, TRIANGLE) blend a little differently from single
 * pixels: (src*(256-a) + dst*a) >> 8 (RGB(255,0,0,128) on white is #FF7F7Fh
 * from RECT_P, where PIXON_P's rule would give #FE7F7Fh) */
static uint32_t blend_fill(uint32_t d, uint32_t c, unsigned a)
{
    unsigned ia = 256 - a;
    uint32_t r = ((((c >> 16) & 255u) * ia + ((d >> 16) & 255u) * a) >> 8);
    uint32_t gg = ((((c >> 8) & 255u) * ia + ((d >> 8) & 255u) * a) >> 8);
    uint32_t b = (((c & 255u) * ia + (d & 255u) * a) >> 8);
    return (d & 0xFF000000u) | (r << 16) | (gg << 8) | b;
}

static void put(grob *g, int x, int y, uint32_t c)
{
    unsigned a = c >> 24;
    uint32_t *p;
    if ((unsigned)x >= (unsigned)g->w || (unsigned)y >= (unsigned)g->h) return;
    p = &g->px[(long)y * g->w + x];
    if (a == 0) *p = c;
    else if (a != 255) *p = blend(*p, c, a);
}

void gfx_pixel(grob *g, int x, int y, uint32_t c) { put(g, x, y, c); }

uint32_t gfx_getpix(const grob *g, int x, int y)
{
    if ((unsigned)x >= (unsigned)g->w || (unsigned)y >= (unsigned)g->h) return GFX_OUTSIDE;
    return g->px[(long)y * g->w + x];
}

static void hspan(grob *g, int x0, int x1, int y, uint32_t c)
{
    unsigned a = c >> 24;
    uint32_t *p;
    if ((unsigned)y >= (unsigned)g->h || a == 255) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= g->w) x1 = g->w - 1;
    if (x0 > x1) return;
    p = &g->px[(long)y * g->w];
    if (a == 0) for (; x0 <= x1; x0++) p[x0] = c;
    else for (; x0 <= x1; x0++) p[x0] = blend_fill(p[x0], c, a);
}

void gfx_fill(grob *g, int x0, int y0, int x1, int y1, uint32_t c)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= g->h) y1 = g->h - 1;
    for (; y0 <= y1; y0++) hspan(g, x0, x1, y0, c);
}

void gfx_rect(grob *g, int x0, int y0, int x1, int y1, uint32_t edge, uint32_t fill)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (edge == fill) { gfx_fill(g, x0, y0, x1, y1, fill); return; }
    hspan(g, x0, x1, y0, edge);
    if (y1 > y0) hspan(g, x0, x1, y1, edge);
    for (t = y0 + 1; t < y1; t++) {
        hspan(g, x0, x0, t, edge);
        if (x1 > x0) hspan(g, x1, x1, t, edge);
        if (x1 - x0 >= 2) hspan(g, x0 + 1, x1 - 1, t, fill);
    }
}

void gfx_invert(grob *g, int x0, int y0, int x1, int y1)
{
    int t, x;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= g->w) x1 = g->w - 1;
    if (y1 >= g->h) y1 = g->h - 1;
    for (; y0 <= y1; y0++)
        for (x = x0; x <= x1; x++) g->px[(long)y0 * g->w + x] ^= 0x00FFFFFFu;
}

/* ------------------------------------------------------------ lines */

/* Bresenham as the Prime does it (matched pixel for pixel on the Virtual
 * Calculator): drawn from the left end; along the major axis the minor
 * coordinate steps when n*dminor - floor(dmajor/2) reaches a multiple of
 * dmajor, so an odd dmajor rounds up a little below one half (a line from
 * (137,33) to (160,120) has x = 142 at y = 50, where it is 141.494). */
void gfx_line(grob *g, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx, dy, sx, sy, err, n, steep, t;
    if ((c >> 24) == 255) return;
    /* whole line on one side of the grob: nothing to draw */
    if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) ||
        (x0 >= g->w && x1 >= g->w) || (y0 >= g->h && y1 >= g->h)) return;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    dx = x1 - x0;
    dy = y1 > y0 ? y1 - y0 : y0 - y1;
    sx = 1;
    sy = y0 < y1 ? 1 : -1;
    steep = dy > dx;
    if (!steep) {
        err = -(dx / 2);
        for (n = 0; n <= dx; n++) {
            put(g, x0, y0, c);
            x0 += sx;
            err += dy;
            if (dy && err >= 0) { y0 += sy; err -= dx; }
        }
    } else {
        err = -(dy / 2);
        for (n = 0; n <= dy; n++) {
            put(g, x0, y0, c);
            y0 += sy;
            err += dx;
            if (dx && err >= 0) { x0 += sx; err -= dy; }
        }
    }
}

/* ------------------------------------------------------------ circles */

typedef struct arcf { int full; int32_t sx, sy, ex, ey; int big; } arcf;

static int in_arc(const arcf *f, int32_t px, int32_t py)
{
    /* (px, py): x right, y up */
    int32_t c1, c2;
    if (f->full) return 1;
    if (!f->big) {
        c1 = f->sx * py - f->sy * px;     /* cross(S, P) */
        c2 = px * f->ey - py * f->ex;     /* cross(P, E) */
        return c1 >= 0 && c2 >= 0;
    }
    c1 = f->ex * py - f->ey * px;         /* cross(E, P) */
    c2 = px * f->sy - py * f->sx;         /* cross(P, S) */
    return !(c1 > 0 && c2 > 0);
}

static void arc_pt(grob *g, int cx, int cy, int dx, int dy, const arcf *f, uint32_t c)
{
    if (in_arc(f, dx, -dy)) put(g, cx + dx, cy + dy, c);
}

void gfx_arc(grob *g, int cx, int cy, int r, const int32_t *s, const int32_t *e, uint32_t c)
{
    arcf f;
    int x, y;
    int32_t rr;
    if (r < 0) r = -r;
    if (r > 16384) r = 16384;
    f.full = s == NULL;
    if (!f.full) {
        f.sx = s[0]; f.sy = s[1]; f.ex = e[0]; f.ey = e[1];
        f.big = s[2];
    }
    if (r == 0) { arc_pt(g, cx, cy, 0, 0, &f, c); return; }
    /* skip circles that cannot touch the grob */
    if (cx + r < 0 || cy + r < 0 || cx - r >= g->w || cy - r >= g->h) return;
    rr = (int32_t)r * r;
    x = 0; y = r;
    for (;;) {
        while (y > 0 && (int32_t)(y - 1) * (y - 1) + (int32_t)x * x >= rr) y--;
        if (x > y) break;
        if (x == 0) {
            arc_pt(g, cx, cy, 0, -y, &f, c); arc_pt(g, cx, cy, 0, y, &f, c);
            arc_pt(g, cx, cy, -y, 0, &f, c); arc_pt(g, cx, cy, y, 0, &f, c);
        } else if (x == y) {
            arc_pt(g, cx, cy, x, -y, &f, c); arc_pt(g, cx, cy, -x, -y, &f, c);
            arc_pt(g, cx, cy, x, y, &f, c); arc_pt(g, cx, cy, -x, y, &f, c);
        } else {
            arc_pt(g, cx, cy, x, -y, &f, c); arc_pt(g, cx, cy, -x, -y, &f, c);
            arc_pt(g, cx, cy, x, y, &f, c); arc_pt(g, cx, cy, -x, y, &f, c);
            arc_pt(g, cx, cy, y, -x, &f, c); arc_pt(g, cx, cy, -y, -x, &f, c);
            arc_pt(g, cx, cy, y, x, &f, c); arc_pt(g, cx, cy, -y, x, &f, c);
        }
        x++;
    }
}

/* An ellipse (ARC_P with a radius list {rx, ry}).  The Prime draws these as
 * polygons; this approximates them row by row: half-width
 * round(rx * sqrt(1 - ((|dy| + 0.6) / ry)^2)), the outline covering on each
 * row the widths up to the next row out.  s/e (y up, 2^14) limit it to a
 * sector, drawn with its two radii; fill: the inside too. */
static int ell_w(int rx, int ry, int dy)
{
    /* largest k with (2k-1)^2 * 100 ry^2 <= 4 rx^2 (100 ry^2 - (10|dy|+6)^2) */
    int64_t D, N, t = 10 * (int64_t)(dy < 0 ? -dy : dy) + 6;
    int k;
    if ((dy < 0 ? -dy : dy) >= ry) return 0;
    D = 100 * (int64_t)ry * ry;
    N = 4 * (int64_t)rx * rx * (D - t * t);
    if (N <= 0) return 0;
    k = rx;
    while (k > 0 && (int64_t)(2 * k - 1) * (2 * k - 1) * D > N) k--;
    return k;
}

void gfx_ellipse(grob *g, int cx, int cy, int rx, int ry, const int32_t *s, const int32_t *e,
                 const int *p1, const int *p2, uint32_t edge, int filled, uint32_t fill)
{
    arcf f;
    int dy, x;
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (rx > 16384) rx = 16384;
    if (ry > 16384) ry = 16384;
    f.full = s == NULL;
    if (s) { f.sx = s[0]; f.sy = s[1]; f.ex = e[0]; f.ey = e[1]; f.big = s[2]; }
    for (dy = -ry; dy <= ry; dy++) {
        int w = ell_w(rx, ry, dy), wo, ady = dy < 0 ? -dy : dy;
        if (cy + dy < 0 || cy + dy >= g->h) continue;
        wo = ady < ry ? ell_w(rx, ry, ady + 1) + 1 : 0;
        if (wo > w) wo = w;
        if (filled)
            for (x = -w + 1; x < w; x++) if (in_arc(&f, x, -dy)) put(g, cx + x, cy + dy, fill);
        for (x = wo; x <= w; x++) {
            if (in_arc(&f, x, -dy)) put(g, cx + x, cy + dy, edge);
            if (x && in_arc(&f, -x, -dy)) put(g, cx - x, cy + dy, edge);
        }
    }
    if (s) {
        gfx_line(g, cx, cy, p1[0], p1[1], edge);
        gfx_line(g, cx, cy, p2[0], p2[1], edge);
    }
}

/* the inside of the circle of radius r (rows between its pixels) */
void gfx_disc(grob *g, int cx, int cy, int r, uint32_t c)
{
    int x, y;
    if (r < 0) r = -r;
    if (r > 16384) r = 16384;
    for (y = -r + 1; y < r; y++) {
        int32_t ay = y < 0 ? -y : y;
        /* the circle's x on this row, then the pixels strictly inside it */
        x = 0;
        while ((int32_t)(x + 1) * (x + 1) + ay * ay < (int32_t)r * r) x++;
        if (x > 0) hspan(g, cx - x + 1, cx + x - 1, cy + y, c);
        else if (ay < r) put(g, cx, cy + y, c);
    }
}

/* a filled sector (ARC_P with {edge, fill} colours).  Unlike the outline,
 * the Prime measures these angles clockwise on the screen (y down): from a1
 * to a2 is the math sector from -a2 to -a1.  s, e: the directions of -a2 and
 * -a1 (x right, y up, 2^14), s[2]: sweep > pi; p1, p2: the end points.  An
 * approximation of the Prime's drawing: the pixels inside the circle and
 * the sector, then the arc and the two radii in the edge colour. */
void gfx_pie(grob *g, int cx, int cy, int r, const int32_t *s, const int32_t *e,
             const int *p1, const int *p2, uint32_t edge, uint32_t fill)
{
    arcf f;
    int x, y;
    if (r < 0) r = -r;
    if (r > 16384) r = 16384;
    f.full = 0; f.sx = s[0]; f.sy = s[1]; f.ex = e[0]; f.ey = e[1]; f.big = s[2];
    for (y = -r; y <= r; y++) {
        if (cy + y < 0 || cy + y >= g->h) continue;
        for (x = -r; x <= r; x++)
            if ((int32_t)x * x + (int32_t)y * y <= (int32_t)r * r && in_arc(&f, x, -y)) put(g, cx + x, cy + y, fill);
    }
    gfx_arc(g, cx, cy, r, s, e, edge);
    gfx_line(g, cx, cy, p1[0], p1[1], edge);
    gfx_line(g, cx, cy, p2[0], p2[1], edge);
}

/* ------------------------------------------------------------ polygons */

/* FILLPOLY, as the Prime fills (matched on the Virtual Calculator): every
 * row from the top to the bottom vertex, both included; each edge gives one
 * x per row, walked from its nearer end with a 16.16 slope truncated toward
 * that end; the x's of a row are sorted and filled in pairs, both ends
 * included (even-odd).  A vertex where the outline passes straight through
 * counts once, a top or bottom vertex twice. */
/* (each edge P->Q: rows nearer P are walked from P, the rest from Q) */
static int32_t edge_x(int32_t xp, int32_t yp, int32_t xq, int32_t yq, int32_t y)
{
    /* the edge P->Q in the polygon's order: rows nearer P from P, the others
     * (the middle row too) from Q */
    int64_t dy = yq > yp ? (int64_t)yq - yp : (int64_t)yp - yq;
    int64_t adx = xq >= xp ? (int64_t)xq - xp : (int64_t)xp - xq;
    int64_t slope = (adx << 16) / dy;
    int64_t k = y > yp ? (int64_t)y - yp : (int64_t)yp - y;
    int32_t sgn = xq >= xp ? 1 : -1;
    if (2 * k < dy) return xp + sgn * (int32_t)((k * slope) >> 16);
    k = dy - k;
    return xq - sgn * (int32_t)((k * slope) >> 16);
}

/* the y direction of the outline arriving at vertex i (skipping flat edges):
 * +1 down, -1 up, 0 all flat */
static int dir_into(const int32_t *xy, int n, int i)
{
    int k, j = i;
    for (k = 0; k < n; k++) {
        int p = (j + n - 1) % n;
        if (xy[2 * p + 1] != xy[2 * j + 1]) return xy[2 * j + 1] > xy[2 * p + 1] ? 1 : -1;
        j = p;
    }
    return 0;
}
static int dir_out(const int32_t *xy, int n, int i)
{
    int k, j = i;
    for (k = 0; k < n; k++) {
        int q = (j + 1) % n;
        if (xy[2 * q + 1] != xy[2 * j + 1]) return xy[2 * q + 1] > xy[2 * j + 1] ? 1 : -1;
        j = q;
    }
    return 0;
}

void gfx_poly(grob *g, const int32_t *xy, int n, uint32_t c)
{
    int y, ymin, ymax, i, k, nx;
    int32_t *xs;
    uint8_t *skip_end;      /* edge i: do not count its lower end (pass-through vertex) */
    if (n < 2 || (c >> 24) == 255) return;
    ymin = ymax = xy[1];
    for (i = 1; i < n; i++) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    if (ymin < 0) ymin = 0;
    if (ymax >= g->h) ymax = g->h - 1;
    if (ymin > ymax) return;
    xs = (int32_t *)malloc((size_t)n * sizeof *xs);
    skip_end = (uint8_t *)calloc((size_t)n, 1);
    if (!xs || !skip_end) { free(xs); free(skip_end); return; }
    if (ymin == ymax || n == 2) {
        /* flat: the run(s) between the extreme x's */
        int32_t lo = xy[0], hi = xy[0];
        for (i = 1; i < n; i++) { if (xy[2 * i] < lo) lo = xy[2 * i]; if (xy[2 * i] > hi) hi = xy[2 * i]; }
        if (ymin == ymax) hspan(g, lo, hi, ymin, c);
        else gfx_line(g, xy[0], xy[1], xy[2], xy[3], c);
        free(xs); free(skip_end);
        return;
    }
    for (i = 0; i < n; i++) {
        /* vertex i between edge i-1 (into) and edge i (out of) */
        int din = dir_into(xy, n, i), dout = dir_out(xy, n, i);
        if (din && din == dout) {
            /* passes through: drop it from the edge that ends there lower...
             * i.e. from whichever adjacent non-flat edge has it as its lower end */
            int e = din > 0 ? (i + n - 1) % n : i;   /* the edge with i at its bottom */
            while (xy[2 * e + 1] == xy[2 * ((e + 1) % n) + 1]) e = din > 0 ? (e + n - 1) % n : (e + 1) % n;
            skip_end[e] = 1;
        }
    }
    for (y = ymin; y <= ymax; y++) {
        nx = 0;
        for (i = 0; i < n; i++) {
            int32_t x0 = xy[2 * i], y0 = xy[2 * i + 1];
            int32_t x1 = xy[2 * ((i + 1) % n)], y1 = xy[2 * ((i + 1) % n) + 1];
            if (y0 == y1) continue;
            if (y < (y0 < y1 ? y0 : y1) || y > (y0 > y1 ? y0 : y1) || (y == (y0 > y1 ? y0 : y1) && skip_end[i])) continue;
            xs[nx++] = edge_x(x0, y0, x1, y1, y);
        }
        for (i = 1; i < nx; i++)
            for (k = i; k > 0 && xs[k - 1] > xs[k]; k--) { int32_t t = xs[k]; xs[k] = xs[k - 1]; xs[k - 1] = t; }
        for (i = 0; i + 1 < nx; i += 2) hspan(g, xs[i], xs[i + 1], y, c);
    }
    free(xs); free(skip_end);
}

/* TRIANGLE_P's rows (matched on the Virtual Calculator): from x to x' where
 * each edge gives the largest integer below its exact crossing, or the
 * vertex itself on a vertex's row (and a vertical edge its x) */
static int32_t tri_x(int32_t xt, int32_t yt, int32_t xb, int32_t yb, int32_t y)
{
    int64_t dy = (int64_t)yb - yt, num;
    if (y == yt || xb == xt) return xt;
    if (y == yb) return xb;
    num = (int64_t)xt * dy + ((int64_t)y - yt) * ((int64_t)xb - xt);   /* x = num / dy */
    /* ceil(num / dy) - 1 */
    return (int32_t)((num >= 0 ? (num + dy - 1) / dy : -((-num) / dy)) - 1);
}

static int32_t imin3(int32_t a, int32_t b, int32_t c) { int32_t m = a < b ? a : b; return m < c ? m : c; }
static int32_t imax3(int32_t a, int32_t b, int32_t c) { int32_t m = a > b ? a : b; return m > c ? m : c; }

void gfx_triangle(grob *g, const int32_t *xy, const uint32_t *c3, int ncol, int alpha)
{
    int32_t miny, maxy, y, x;
    int64_t area;
    unsigned a = alpha < 0 ? 0 : (unsigned)(alpha > 255 ? 255 : alpha);
    uint32_t solid = c3[0];
    if (ncol < 3) {
        if (alpha >= 0) solid = (solid & 0xFFFFFFu) | ((uint32_t)a << 24);
        if ((solid >> 24) == 255) return;
    } else if (a == 255) return;
    area = (int64_t)(xy[2] - xy[0]) * (xy[5] - xy[1]) - (int64_t)(xy[4] - xy[0]) * (xy[3] - xy[1]);
    miny = imin3(xy[1], xy[3], xy[5]); maxy = imax3(xy[1], xy[3], xy[5]);
    if (miny < 0) miny = 0;
    if (maxy >= g->h) maxy = g->h - 1;
    for (y = miny; y <= maxy; y++) {
        int32_t lo = 0, hi = 0;
        int i, have = 0;
        for (i = 0; i < 3; i++) {
            int32_t x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * ((i + 1) % 3)], y1 = xy[2 * ((i + 1) % 3) + 1], xv;
            if (y0 == y1) {
                if (y != y0) continue;
                if (!have || x0 < lo) lo = x0;
                if (!have || x0 > hi) hi = x0;
                have = 1;
                if (x1 < lo) lo = x1;
                if (x1 > hi) hi = x1;
                continue;
            }
            if (y0 > y1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
            if (y < y0 || y > y1) continue;
            xv = tri_x(x0, y0, x1, y1, y);
            if (!have || xv < lo) lo = xv;
            if (!have || xv > hi) hi = xv;
            have = 1;
        }
        if (!have) continue;
        if (ncol < 3 || area == 0) { hspan(g, lo, hi, y, solid); continue; }
        for (x = lo < 0 ? 0 : lo; x <= hi && x < g->w; x++) {
            /* barycentric weights at the pixel, colour per channel */
            int64_t px = x, py = y, A = area;
            int64_t w0 = ((int64_t)xy[4] - xy[2]) * (py - xy[3]) - ((int64_t)xy[5] - xy[3]) * (px - xy[2]);
            int64_t w1 = ((int64_t)xy[0] - xy[4]) * (py - xy[5]) - ((int64_t)xy[1] - xy[5]) * (px - xy[4]);
            int64_t w2;
            uint32_t col = 0;
            int ch;
            if (A < 0) { A = -A; w0 = -w0; w1 = -w1; }
            if (w0 < 0) w0 = 0;
            if (w1 < 0) w1 = 0;
            w2 = A - w0 - w1;
            if (w2 < 0) w2 = 0;
            for (ch = 0; ch < 24; ch += 8) {
                int64_t v = (int64_t)((c3[0] >> ch) & 255) * w0 + (int64_t)((c3[1] >> ch) & 255) * w1 + (int64_t)((c3[2] >> ch) & 255) * w2;
                int64_t s = w0 + w1 + w2;
                col |= (uint32_t)(s ? (v + s / 2) / s : 0) << ch;
            }
            put(g, x, y, col | ((uint32_t)a << 24));
        }
    }
}

/* ------------------------------------------------------------ blit */

void gfx_blit(grob *dst, int dx0, int dy0, int dx1, int dy1,
              const grob *src, int sx0, int sy0, int sx1, int sy1,
              int use_key, uint32_t key, int alpha)
{
    int dw = dx1 - dx0, dh = dy1 - dy0, sw = sx1 - sx0, sh = sy1 - sy0, x, y;
    unsigned a = alpha >= 255 ? 0 : 255u - (unsigned)(alpha < 0 ? 0 : alpha);
    uint32_t *tmp = NULL;
    const uint32_t *sp = src->px;
    long sstride = src->w;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || a == 255) return;
    if (src == dst) {
        /* overlapping copy: read from a snapshot */
        long np = (long)src->w * src->h;
        tmp = (uint32_t *)GFX_MALLOC((size_t)np * sizeof(uint32_t));
        if (!tmp) return;
        memcpy(tmp, src->px, (size_t)np * sizeof(uint32_t));
        sp = tmp;
    }
    for (y = dy0 < 0 ? 0 : dy0; y < dy1 && y < dst->h; y++) {
        int sy = sy0 + (int)(((int64_t)(y - dy0) * sh) / dh);
        if (sy < 0 || sy >= src->h) continue;
        if (sw == dw) {
            /* the same width: a straight run of pixels, clipped once (BLIT_P of a whole grob
             * every frame: gravity) */
            const uint32_t *s;
            uint32_t *d;
            int xa = dx0 < 0 ? 0 : dx0, xb = dx1 < dst->w ? dx1 : dst->w;
            if (sx0 + (xa - dx0) < 0) xa = dx0 - sx0;
            if (sx0 + (xb - dx0) > src->w) xb = dx0 + src->w - sx0;
            if (xa >= xb) continue;
            s = sp + (long)sy * sstride + sx0 + (xa - dx0);
            d = dst->px + (long)y * dst->w + xa;
            for (x = xa; x < xb; x++, s++, d++) {
                uint32_t p = *s;
                if ((p >> 24) == 255 || (use_key && (p & 0xFFFFFFu) == (key & 0xFFFFFFu))) continue;
                if (a) put(dst, x, y, (p & 0xFFFFFFu) | ((uint32_t)a << 24));
                else *d = p;
            }
            continue;
        }
        /* sx = sx0 + floor((x - dx0) sw / dw), stepped: the quotient q and the remainder
         * rr of sw / dw per pixel (a 64-bit division per pixel was most of a game's time
         * on the SH-4) */
        int xs = dx0 < 0 ? 0 : dx0, q = sw / dw, rr = sw % dw;
        int64_t n0 = (int64_t)(xs - dx0) * sw;
        int sx = sx0 + (int)(n0 / dw), acc = (int)(n0 % dw);
        for (x = xs; x < dx1 && x < dst->w; x++, sx += q, acc += rr) {
            uint32_t p;
            if (acc >= dw) { acc -= dw; sx++; }
            if (sx < 0 || sx >= src->w) continue;
            p = sp[(long)sy * sstride + sx];
            if (use_key && (p & 0xFFFFFFu) == (key & 0xFFFFFFu)) continue;
            if ((p >> 24) == 255) continue;
            if (a) put(dst, x, y, (p & 0xFFFFFFu) | ((uint32_t)a << 24));
            else dst->px[(long)y * dst->w + x] = p;
        }
    }
    if (tmp) GFX_FREE(tmp);
}

/* ------------------------------------------------------------ text */

static const gfx_glyph *glyph(const gfx_font *f, unsigned cp)
{
    int lo = 1, hi = gfx_font_ncp - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (gfx_font_cp[mid] == cp) return &f->g[mid];
        if (gfx_font_cp[mid] < cp) lo = mid + 1; else hi = mid - 1;
    }
    return &f->g[0];
}

static const gfx_font *font_of(int font)
{
    if (font < 1 || font > 7) font = 3;
    return &gfx_fonts[font - 1];
}

int gfx_font_height(int font) { return font_of(font)->cell; }

static int char_adv(const gfx_font *f, unsigned cp)
{
    if (cp < 32) return 0;
    return glyph(f, cp)->adv;
}

int gfx_text_width(int font, const uint16_t *s, uint32_t n)
{
    const gfx_font *f = font_of(font);
    uint32_t i;
    int w = 0;
    for (i = 0; i < n; i++) w += char_adv(f, s[i]);
    return w;
}

#define DIV15(v) ((((v) + 1u) * 4369u) >> 16)

static void glyph_draw(grob *g, const gfx_font *f, const gfx_glyph *gl, int px, int base, int xlim,
                       uint32_t fg, unsigned op)
{
    int r, c, k = 0;
    const uint8_t *d = f->data + gl->off;
    int x0 = px + gl->x, y0 = base + gl->y;
    for (r = 0; r < gl->h; r++) {
        int y = y0 + r;
        for (c = 0; c < gl->w; c++, k++) {
            unsigned cov = (k & 1) ? (d[k >> 1] & 15u) : (d[k >> 1] >> 4);
            int x = x0 + c;
            uint32_t *p, o;
            unsigned w, iw;
            if (!cov || x >= xlim || (unsigned)x >= (unsigned)g->w || (unsigned)y >= (unsigned)g->h) continue;
            w = cov * 17u;                    /* 0..255 */
            if (op != 255) w = (w * op + 127u) / 255u;
            iw = 255u - w;
            p = &g->px[(long)y * g->w + x];
            o = *p;
            *p = (o & 0xFF000000u)
               | ((((fg >> 16) & 255u) * w + ((o >> 16) & 255u) * iw + 127u) / 255u) << 16
               | ((((fg >> 8) & 255u) * w + ((o >> 8) & 255u) * iw + 127u) / 255u) << 8
               | (((fg & 255u) * w + (o & 255u) * iw + 127u) / 255u);
        }
    }
}

int gfx_text(grob *g, int font, const uint16_t *s, uint32_t n, int x, int y,
             uint32_t fg, int maxw, int bgon, uint32_t bg)
{
    const gfx_font *f = font_of(font);
    int w = 0, xlim, px;
    uint32_t i;
    unsigned op = 255u;               /* the text colour's alpha is ignored (measured) */
    for (i = 0; i < n; i++) w += char_adv(f, s[i]);
    if (x - 1 >= g->w || y >= g->h || y + f->cell <= 0 || x + w < 0) {
        /* nothing of it on the grob: the Prime answers with font 3's width */
        f = font_of(3);
        for (w = 0, i = 0; i < n; i++) w += char_adv(f, s[i]);
        if (maxw >= 0 && w > maxw) w = maxw;
        return x + w;
    }
    if (maxw >= 0 && w > maxw) w = maxw;
    xlim = x + w;
    if (bgon) gfx_fill(g, x - 1, y - 1, x + w, y + f->cell - 1, bg);
    if (op) {
        px = x;
        for (i = 0; i < n && px < xlim; i++) {
            const gfx_glyph *gl;
            if (s[i] < 32) continue;
            gl = glyph(f, s[i]);
            glyph_draw(g, f, gl, px, y + f->base, xlim, fg, op);
            px += gl->adv;
        }
    }
    return x + w;
}
