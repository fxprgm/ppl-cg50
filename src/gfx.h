/* gfx.h -- STUB (branch addin). The graphics back end (G0-G9, drawing commands, fonts) is
 * the gfx branch's; this file and gfx.c only stand in for it until the merge, which
 * replaces both. The contract the CG50 add-in codes against:
 *   gfx_init / gfx_free     create / release the grobs (G0 is 320x240)
 *   gfx_row565              row y of G0 as 320 RGB565 pixels (the LCD's format)
 *   gfx_flush               present what changed now (before waits, from polls)
 *   V->present(V, y0, y1)   called by the drawing code with the rows of G0 that changed
 */
#ifndef PPL_GFX_H
#define PPL_GFX_H

#include <stdint.h>
#include "vm.h"

void gfx_init(ppl_vm *V);
void gfx_free(ppl_vm *V);
void gfx_row565(ppl_vm *V, int y, uint16_t *dst /* 320 px RGB565 */);
/* present the changed rows of G0 now (the stub presents at once: a no-op) */
void gfx_flush(ppl_vm *V);

/* ---- STUB only: a few drawing commands for the add-in's tests (cg50/src/bi_host.c
 * registers them while GFX_STUB is defined) ---- */
#define GFX_STUB 1
int gfx_stub_rect(ppl_vm *V, int argc, value *a, value *out);      /* RECT, RECT_P */
int gfx_stub_line(ppl_vm *V, int argc, value *a, value *out);      /* LINE_P */
int gfx_stub_pixon(ppl_vm *V, int argc, value *a, value *out);     /* PIXON_P */
int gfx_stub_drawmenu(ppl_vm *V, int argc, value *a, value *out);  /* DRAWMENU (boxes only) */

#endif
