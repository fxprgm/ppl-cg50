/* gfx.c -- STUB (branch addin), replaced by the gfx branch at the merge: G0 as a 320x240
 * RGB565 buffer holding a test pattern, and a few drawing commands for the add-in's tests. */
#include "gfx.h"
#include "bi.h"
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240
static uint16_t *g0;

static uint16_t rgb565(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3); }

void gfx_init(ppl_vm *V)
{
    int x, y;
    (void)V;
    if (!g0) g0 = (uint16_t *)malloc(W * H * 2);
    if (!g0) return;
    /* test pattern: a colour ramp, a 20 px grid, a frame and diagonals */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint16_t c = rgb565(x * 255 / W, y * 255 / H, 160);
            if (x % 20 == 0 || y % 20 == 0) c = 0xFFFF;
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1 || x * 3 == y * 4 || x * 3 == (H - 1 - y) * 4) c = 0;
            g0[y * W + x] = c;
        }
}

void gfx_free(ppl_vm *V) { (void)V; free(g0); g0 = NULL; }

void gfx_flush(ppl_vm *V) { (void)V; }

void gfx_row565(ppl_vm *V, int y, uint16_t *dst)
{
    (void)V;
    if (!g0 || y < 0 || y >= H) { memset(dst, 0xFF, W * 2); return; }
    memcpy(dst, g0 + y * W, W * 2);
}

/* ---- stub drawing ---- */

static int ival(const value *v, int32_t *n) { return to_index(v, n); }

static uint16_t color_of(const value *v)
{
    uint32_t c;
    if (v->t == T_INT) c = v->u.i.lo;
    else { int32_t n = 0; to_index(v, &n); c = (uint32_t)n; }
    return rgb565((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

static void pset(int x, int y, uint16_t c) { if (g0 && x >= 0 && x < W && y >= 0 && y < H) g0[y * W + x] = c; }

static void fill(int x0, int y0, int x1, int y1, uint16_t c)
{
    int x, y, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) pset(x, y, c);
}

static void changed(ppl_vm *V, int y0, int y1)
{
    int t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 > H - 1) y1 = H - 1;
    if (V->present && y0 <= y1) V->present(V, y0, y1);
}

/* RECT; RECT(color) RECT_P(x1,y1,x2,y2[,color]): white by default */
int gfx_stub_rect(ppl_vm *V, int argc, value *a, value *out)
{
    int32_t x0 = 0, y0 = 0, x1 = W - 1, y1 = H - 1;
    uint16_t c = 0xFFFF;
    if (argc >= 4) {
        if (ival(&a[0], &x0) || ival(&a[1], &y0) || ival(&a[2], &x1) || ival(&a[3], &y1)) return E_BADTYPE;
        if (argc >= 5) c = color_of(&a[4]);
    } else if (argc >= 1) c = color_of(&a[0]);
    fill(x0, y0, x1, y1, c);
    changed(V, y0, y1);
    v_int32(out, 1);
    return 0;
}

int gfx_stub_line(ppl_vm *V, int argc, value *a, value *out)
{
    int32_t x0, y0, x1, y1;
    uint16_t c = 0;
    int dx, dy, sx, sy, err;
    if (argc < 4 || ival(&a[0], &x0) || ival(&a[1], &y0) || ival(&a[2], &x1) || ival(&a[3], &y1)) return E_BADTYPE;
    if (argc >= 5) c = color_of(&a[4]);
    dx = x1 > x0 ? x1 - x0 : x0 - x1; sx = x0 < x1 ? 1 : -1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0; sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    {
        int ya = y0, yb = y1;
        for (;;) {
            int e2;
            pset(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
        changed(V, ya, yb);
    }
    v_int32(out, 1);
    return 0;
}

int gfx_stub_pixon(ppl_vm *V, int argc, value *a, value *out)
{
    int32_t x, y;
    if (argc < 2 || ival(&a[0], &x) || ival(&a[1], &y)) return E_BADTYPE;
    pset(x, y, argc >= 3 ? color_of(&a[2]) : 0);
    changed(V, y, y);
    v_int32(out, 1);
    return 0;
}

/* six boxes along the bottom (y 220-239, 53 px each), no labels in the stub */
int gfx_stub_drawmenu(ppl_vm *V, int argc, value *a, value *out)
{
    int i;
    (void)argc; (void)a;
    fill(0, 220, W - 1, H - 1, rgb565(230, 230, 230));
    for (i = 0; i < 6; i++) {
        int x0 = i * 53, x1 = i == 5 ? W - 1 : i * 53 + 52;
        fill(x0, 220, x0, H - 1, 0);
        fill(x0, 220, x1, 220, 0);
        if (i < argc) fill(x0 + 10, 228, x1 - 10, 231, rgb565(40, 40, 160));
    }
    changed(V, 220, H - 1);
    v_int32(out, 1);
    return 0;
}
