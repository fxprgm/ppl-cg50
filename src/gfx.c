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
    else for (; x0 <= x1; x0++) p[x0] = blend(p[x0], c, a);
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
        put(g, x0, t, edge);
        if (x1 > x0) put(g, x1, t, edge);
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

void gfx_line(grob *g, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx, dy, sx, sy, err, e2;
    if ((c >> 24) == 255) return;
    /* whole line on one side of the grob: nothing to draw */
    if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) ||
        (x0 >= g->w && x1 >= g->w) || (y0 >= g->h && y1 >= g->h)) return;
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        put(g, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
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

/* ------------------------------------------------------------ polygons */

/* even-odd scanline fill sampled at pixel centres: pixel (x, y) is inside
 * when (x + 1/2, y + 1/2) is; edges are half-open in y */
typedef struct xing { int64_t n, d; } xing;     /* crossing at x = n/d - 1/2, d > 0 */

static int64_t ceil_div(int64_t n, int64_t d) { return n >= 0 ? (n + d - 1) / d : -((-n) / d); }

void gfx_poly(grob *g, const int32_t *xy, int n, uint32_t c)
{
    int y, ymin, ymax, i, k, nx;
    xing *xs;
    if (n < 3 || (c >> 24) == 255) return;
    ymin = ymax = xy[1];
    for (i = 1; i < n; i++) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    if (ymin < 0) ymin = 0;
    if (ymax > g->h) ymax = g->h;
    if (ymin >= ymax) return;
    xs = (xing *)malloc((size_t)n * sizeof *xs);
    if (!xs) return;
    for (y = ymin; y < ymax; y++) {
        int64_t sy2 = 2 * (int64_t)y + 1;            /* 2 * sample y */
        nx = 0;
        for (i = 0; i < n; i++) {
            int32_t x0 = xy[2 * i], y0 = xy[2 * i + 1];
            int32_t x1 = xy[2 * ((i + 1) % n)], y1 = xy[2 * ((i + 1) % n) + 1];
            int64_t den;
            if (y0 == y1) continue;
            if (y0 > y1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
            if (sy2 < 2 * (int64_t)y0 || sy2 >= 2 * (int64_t)y1) continue;
            /* the edge crosses the sample line at X = x0 + (sy2/2 - y0)(x1-x0)/(y1-y0);
             * pixel x is right of it when x + 1/2 >= X, i.e. x >= X - 1/2 */
            den = 2 * (int64_t)(y1 - y0);
            xs[nx].n = (sy2 - 2 * (int64_t)y0) * (x1 - x0) + (2 * (int64_t)x0 - 1) * (y1 - y0);
            xs[nx].d = den;
            nx++;
        }
        for (i = 1; i < nx; i++)
            for (k = i; k > 0 && xs[k - 1].n * xs[k].d > xs[k].n * xs[k - 1].d; k--) {
                xing t = xs[k]; xs[k] = xs[k - 1]; xs[k - 1] = t;
            }
        for (i = 0; i + 1 < nx; i += 2) {
            int64_t xa = ceil_div(xs[i].n, xs[i].d), xb = ceil_div(xs[i + 1].n, xs[i + 1].d) - 1;
            if (xa < 0) xa = 0;
            if (xb >= g->w) xb = g->w - 1;
            if (xa <= xb) hspan(g, (int)xa, (int)xb, y, c);
        }
    }
    free(xs);
}

static int32_t imin3(int32_t a, int32_t b, int32_t c) { int32_t m = a < b ? a : b; return m < c ? m : c; }
static int32_t imax3(int32_t a, int32_t b, int32_t c) { int32_t m = a > b ? a : b; return m > c ? m : c; }

void gfx_triangle(grob *g, const int32_t *xy, const uint32_t *c3, int ncol, int alpha)
{
    int32_t x0 = xy[0], y0 = xy[1], x1 = xy[2], y1 = xy[3], x2 = xy[4], y2 = xy[5];
    int32_t minx, maxx, miny, maxy, x, y;
    int64_t area;
    unsigned a = alpha < 0 ? 0 : 255u - (unsigned)(alpha > 255 ? 255 : alpha);
    if (ncol < 3) {
        uint32_t c = c3[0];
        if (alpha >= 0 && (c >> 24) == 0) c = (c & 0xFFFFFFu) | ((uint32_t)a << 24);
        if ((c >> 24) == 255) return;
        gfx_poly(g, xy, 3, c);
        return;
    }
    area = (int64_t)(x1 - x0) * (y2 - y0) - (int64_t)(x2 - x0) * (y1 - y0);
    if (area == 0) return;
    minx = imin3(x0, x1, x2); maxx = imax3(x0, x1, x2);
    miny = imin3(y0, y1, y2); maxy = imax3(y0, y1, y2);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= g->w) maxx = g->w - 1;
    if (maxy >= g->h) maxy = g->h - 1;
    for (y = miny; y <= maxy; y++)
        for (x = minx; x <= maxx; x++) {
            /* barycentric weights at the pixel centre, times 2 */
            int64_t px = 2 * (int64_t)x + 1, py = 2 * (int64_t)y + 1;
            int64_t w0 = (2 * (int64_t)x2 - 2 * x1) * (py - 2 * (int64_t)y1) - (2 * (int64_t)y2 - 2 * y1) * (px - 2 * (int64_t)x1);
            int64_t w1 = (2 * (int64_t)x0 - 2 * x2) * (py - 2 * (int64_t)y2) - (2 * (int64_t)y0 - 2 * y2) * (px - 2 * (int64_t)x2);
            int64_t w2 = 4 * area - w0 - w1;
            int64_t A = 4 * area;
            uint32_t col = 0;
            int ch;
            if (A < 0) { w0 = -w0; w1 = -w1; w2 = -w2; A = -A; }
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            for (ch = 0; ch < 24; ch += 8) {
                int64_t v = (int64_t)((c3[0] >> ch) & 255) * w0 + (int64_t)((c3[1] >> ch) & 255) * w1 + (int64_t)((c3[2] >> ch) & 255) * w2;
                col |= (uint32_t)((v + A / 2) / A) << ch;
            }
            put(g, x, y, col | ((uint32_t)a << 24));
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
        for (x = dx0 < 0 ? 0 : dx0; x < dx1 && x < dst->w; x++) {
            int sx = sx0 + (int)(((int64_t)(x - dx0) * sw) / dw);
            uint32_t p;
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
    unsigned op = 255u - (fg >> 24);
    for (i = 0; i < n; i++) w += char_adv(f, s[i]);
    if (maxw >= 0 && w > maxw) w = maxw;
    xlim = x + w;
    if (bgon) gfx_fill(g, x - 1, y, x + w, y + f->cell - 1, bg);
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
