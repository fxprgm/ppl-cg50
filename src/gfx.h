/* gfx.h -- the graphics back end: the grobs G0..G9 and the drawing primitives.
 *
 * A grob holds 32-bit pixels 0xAARRGGBB, as the Prime keeps them: GETPIX_P
 * answers exactly the colour that was drawn (#FF8040h stays #FF8040h), and
 * the alpha byte is stored (DIMGROB_P(G,w,h,#80123456h) reads back as
 * #80123456h).  G0 is the 320x240 screen; the host shows it with gfx_row565().
 *
 * Contract with the front end (the gint add-in):
 *   - gfx_init(V) after ppl_init(V) (optional: the first drawing call does
 *     it); ppl_free(V) calls gfx_free(V).
 *   - V->present(V, y0, y1) is called with the range of G0 rows changed since
 *     the last call (inclusive), when the screen should be shown:
 *       * FREEZE, and gfx_flush() (call it before waiting for a key, at the
 *         end of a program, and from any periodic poll you have);
 *       * during a drawing burst, from a drawing call when at least
 *         GFX_PRESENT_MS milliseconds (V->ticks) passed since the last
 *         present, or every GFX_PRESENT_OPS drawing calls when V->ticks is
 *         NULL.
 *     The front end then copies rows y0..y1 with gfx_row565().
 *   - Memory: G0 is 300 KB (320x240x4).  G1-G9 are allocated by DIMGROB_P and
 *     SUBGROB_P with GFX_MALLOC/GFX_FREE (malloc/free unless the build defines
 *     them), at most GFX_MAX_PIXELS pixels each.
 *
 * Portable C11: no floating point, no endianness or alignment assumptions.
 */
#ifndef PPL_GFX_H
#define PPL_GFX_H

#include "vm.h"

#define GFX_W 320
#define GFX_H 240
#define GFX_NGROB 10
#ifndef GFX_MAX_PIXELS
#define GFX_MAX_PIXELS (1024L * 1024L)  /* per grob; 4 MB */
#endif
#ifndef GFX_PRESENT_MS
#define GFX_PRESENT_MS 40
#endif
#ifndef GFX_PRESENT_OPS
#define GFX_PRESENT_OPS 64
#endif

#define GFX_WHITE 0x00FFFFFFu
#define GFX_BLACK 0x00000000u
#define GFX_OUTSIDE 0xFF000000u          /* GETPIX outside a grob */

typedef struct grob { int w, h; uint32_t *px; } grob;

typedef struct gfx_state {
    grob g[GFX_NGROB];
    int dirty0, dirty1;                  /* G0 rows changed (dirty0 > dirty1: none) */
    uint32_t last_present;               /* V->ticks() at the last present */
    int ops;                             /* drawing calls since the last present */
    int frozen;                          /* FREEZE was called */
} gfx_state;

void gfx_init(ppl_vm *V);
void gfx_free(ppl_vm *V);
/* G0's row y as RGB565 (320 pixels; rows outside G0 are white) */
void gfx_row565(ppl_vm *V, int y, uint16_t *dst);
/* present the changed rows of G0 now (no-op when nothing changed) */
void gfx_flush(ppl_vm *V);

/* ---- for the built-ins and the host ---- */
gfx_state *gfx_get(ppl_vm *V);           /* creates the state on first use */
grob *gfx_grob(ppl_vm *V, int n);        /* G0..G9 */
int  gfx_dim(ppl_vm *V, int n, int w, int h, uint32_t fill);   /* E_* */
void gfx_touch(ppl_vm *V, const grob *g, int y0, int y1);  /* mark rows changed */
void gfx_drawn(ppl_vm *V);               /* end of a drawing call: maybe present */

/* primitives: clip to the grob; colours with alpha 1..254 blend, 255 draws
 * nothing (see gfx.c) */
void gfx_pixel(grob *g, int x, int y, uint32_t c);
uint32_t gfx_getpix(const grob *g, int x, int y);
void gfx_fill(grob *g, int x0, int y0, int x1, int y1, uint32_t c);   /* inclusive */
void gfx_rect(grob *g, int x0, int y0, int x1, int y1, uint32_t edge, uint32_t fill);
void gfx_line(grob *g, int x0, int y0, int x1, int y1, uint32_t c);
/* circle or arc of radius r; full when s == NULL, else between the unit
 * directions s and e (x right, y up, scaled by 2^14), counterclockwise */
void gfx_arc(grob *g, int cx, int cy, int r, const int32_t *s, const int32_t *e, uint32_t c);
void gfx_pie(grob *g, int cx, int cy, int r, const int32_t *s, const int32_t *e,
             const int *p1, const int *p2, uint32_t edge, uint32_t fill);
void gfx_invert(grob *g, int x0, int y0, int x1, int y1);
/* polygon of n points (xy pairs), even-odd fill */
void gfx_poly(grob *g, const int32_t *xy, int n, uint32_t c);
/* triangle, one colour or three (gradient) */
void gfx_triangle(grob *g, const int32_t *xy, const uint32_t *c3, int ncol, int alpha);
/* copy src[sx0,sx1)x[sy0,sy1) scaled into dst[dx0,dx1)x[dy0,dy1); pixels equal
 * to key (when use_key) are skipped; alpha 0..255 opacity */
void gfx_blit(grob *dst, int dx0, int dy0, int dx1, int dy1,
              const grob *src, int sx0, int sy0, int sx1, int sy1,
              int use_key, uint32_t key, int alpha);

/* ---- text (gfx.c with the font data in font/gfx_font.c) ---- */
int gfx_text_width(int font, const uint16_t *s, uint32_t n);
/* draws s at x,y (top of the cell) and answers the x where it ended; maxw < 0:
 * no limit; bgon: fill the cell with bg first */
int gfx_text(grob *g, int font, const uint16_t *s, uint32_t n, int x, int y,
             uint32_t fg, int maxw, int bgon, uint32_t bg);
int gfx_font_height(int font);           /* the background cell height */

#endif
