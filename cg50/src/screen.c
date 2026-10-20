/* G0 on the LCD (screen.h). */
#include "screen.h"
#include "clock.h"
#include "gfx.h"
#include "io.h"
#include "stats.h"
#include "text.h"
#include "ui.h"

#include <gint/display.h>
#include <gint/drivers/r61524.h>
#include <stdio.h>
#include <string.h>

int view = VIEW_FIT;
const char *const view_names[VIEW_COUNT] = { "Fit", "1:1 top", "1:1 low" };
int disp = DISP_HOME;
const char *panel_state = "";
const char *panel_what = "";

static ppl_vm *VM;
static int dirty_lo = 999, dirty_hi = -1;    /* display rows */
static uint32_t last_flush;
static int ptr_drawn_x = -1, ptr_drawn_y = -1;  /* display position of the drawn pointer */

void g0_set_vm(ppl_vm *V) { VM = V; }

/* r61524_display() sends 99 * (height / 4) DMA blocks of 32 bytes from vram + start * 792,
   32-byte aligned only when start is a multiple of 4: a misaligned source makes the DMAC
   raise an address error and never finish (gint's wait hangs the calculator), and a height
   under 4 asks for 0 blocks, which the DMAC runs as 2^24. So: whole groups of 4 rows. */
void scr_push(int y0, int y1)
{
	if (y0 < 0) y0 = 0;
	if (y1 > DHEIGHT - 1) y1 = DHEIGHT - 1;
	if (y0 > y1)
		return;
	y0 &= ~3;
	y1 |= 3;
	if (y1 > DHEIGHT - 1) y1 = DHEIGHT - 1;
	r61524_display(gint_vram, y0, y1 - y0 + 1, R61524_DMA_WAIT);
}

void scr_push_all(void)
{
	r61524_display(gint_vram, 0, DHEIGHT, R61524_DMA_WAIT);
}

void g0_sources(int d, int *a, int *b)
{
	*b = -1;
	if (view == VIEW_TOP)
		*a = d;
	else if (view == VIEW_BOTTOM)
		*a = d + 16;
	else {
		int g = d / 14, k = d % 14;
		*a = g * 15 + k;
		if (k == 13)
			*b = *a + 1;
	}
}

int g0_display_row(int y)
{
	if (view == VIEW_TOP)
		return y < DHEIGHT ? y : -1;
	if (view == VIEW_BOTTOM)
		return y >= 16 ? y - 16 : -1;
	int g = y / 15, k = y % 15;
	return g * 14 + (k == 14 ? 13 : k);
}

static void mark_disp(int d0, int d1)
{
	if (d0 < 0) d0 = 0;
	if (d1 > DHEIGHT - 1) d1 = DHEIGHT - 1;
	if (d0 > d1)
		return;
	if (d0 < dirty_lo) dirty_lo = d0;
	if (d1 > dirty_hi) dirty_hi = d1;
}

void g0_mark(int y0, int y1)
{
	if (y0 < 0) y0 = 0;
	if (y1 > 239) y1 = 239;
	if (y0 > y1)
		return;
	int d0 = view == VIEW_BOTTOM && y0 < 16 ? 0 : g0_display_row(y0);
	int d1 = view == VIEW_TOP && y1 >= DHEIGHT ? DHEIGHT - 1 : g0_display_row(y1);
	if (d1 < 0)
		return;
	mark_disp(d0, d1);
}

static inline uint32_t avg565x2(uint32_t a, uint32_t b)
{
	return (a & b) + (((a ^ b) & 0xF7DEF7DE) >> 1);
}

static void convert_row(int d)
{
	int a, b;
	g0_sources(d, &a, &b);
	uint16_t *dst = gint_vram + d * DWIDTH;
	gfx_row565(VM, a, dst);
	if (b >= 0 && b < 240) {
		uint32_t tmp[160];
		gfx_row565(VM, b, (uint16_t *)tmp);
		uint32_t *d32 = (uint32_t *)dst;   /* gint_vram rows are 792 bytes: 4-aligned */
		for (int i = 0; i < 160; i++)
			d32[i] = avg565x2(d32[i], tmp[i]);
	}
}

/* the pointer: an arrow, 11 rows, its tip at the hot spot */
static const uint16_t arrow_shape[11] = {
	0x8000, 0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8E00, 0xA000, 0xD000, 0x8800, 0x0800,
};
static const uint16_t arrow_fill[11] = {
	0x0000, 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x0000, 0x4000, 0x0000, 0x0000, 0x0000,
};

static void ptr_draw(void)
{
	int x = ptr.x, y = g0_display_row(ptr.y);
	if (y < 0)
		y = ptr.y < 16 ? 0 : DHEIGHT - 1;
	for (int r = 0; r < 11; r++) {
		int yy = y + r;
		if (yy >= DHEIGHT)
			break;
		uint16_t *p = gint_vram + yy * DWIDTH;
		for (int c = 0; c < 8; c++) {
			int xx = x + c;
			if (xx >= 320)
				break;
			if (arrow_shape[r] & (0x8000 >> c))
				p[xx] = C_BLACK;
			else if (arrow_fill[r] & (0x8000 >> c))
				p[xx] = C_WHITE;
		}
	}
	ptr_drawn_x = x, ptr_drawn_y = y;
}

void g0_flush(int every)
{
	if (disp != DISP_G0 || !VM)
		return;
	uint32_t now = clock_real_ms();
	if (every && now - last_flush < (uint32_t)every)
		return;
	if (ptr.moved || (!ptr.mode && ptr_drawn_y >= 0)) {
		if (ptr_drawn_y >= 0)
			mark_disp(ptr_drawn_y, ptr_drawn_y + 10);
		ptr_drawn_y = -1;
		if (ptr.mode) {
			int y = g0_display_row(ptr.y);
			if (y < 0) y = ptr.y < 16 ? 0 : DHEIGHT - 1;
			mark_disp(y, y + 10);
		}
		ptr.moved = false;
	}
	if (dirty_hi < 0)
		return;
	last_flush = now;
	int lo = dirty_lo, hi = dirty_hi;
	dirty_lo = 999, dirty_hi = -1;
	for (int d = lo; d <= hi; d++)
		convert_row(d);
	if (ptr.mode) {
		int y = g0_display_row(ptr.y);
		if (y < 0) y = ptr.y < 16 ? 0 : DHEIGHT - 1;
		if (y + 10 >= lo && y <= hi)
			ptr_draw();
	}
	scr_push(lo, hi);
	ST->presents++;
}

/* ---- the status panel ------------------------------------------------------------------- */

static char panel_shown[160];

static void panel_text(char *out, int n)
{
	if (ptr.mode)
		snprintf(out, n, "%s|%s|%s|ptr %d,%d", panel_state, panel_what, view_names[view], ptr.x, ptr.y);
	else
		snprintf(out, n, "%s|%s|%s|", panel_state, panel_what, view_names[view]);
}

static void panel_draw(void)
{
	int x = 320;
	drect(x, 0, DWIDTH - 1, DHEIGHT - 1, C_PANEL);
	text_draw(SMALL, x + 4, 3, "HP PPL", C_TITLE, -1, -1);
	text_draw(SMALL, x + 4, 20, panel_state, C_WHITE, -1, 12);
	text_draw(SMALL, x + 4, 32, panel_what, C_LABEL, -1, 12);
	text_draw(SMALL, x + 4, 52, "view", C_LABEL, -1, -1);
	text_draw(SMALL, x + 4, 63, view_names[view], C_WHITE, -1, -1);
	if (ptr.mode) {
		char b[24];
		text_draw(SMALL, x + 4, 83, "pointer", C_TITLE, -1, -1);
		snprintf(b, sizeof b, "%d,%d", ptr.x, ptr.y);
		text_draw(SMALL, x + 4, 94, b, C_WHITE, -1, -1);
	}
	static const char *const help[] = { "F1-F6 tap", "the menu", "SHIFT+F:", "Apps..Num", "OPTN ptr", "AC/ON break" };
	for (int i = 0; i < 6; i++)
		text_draw(SMALL, x + 4, DHEIGHT - 70 + 11 * i, help[i], C_LABEL, -1, 12);
}

void panel_update(void)
{
	if (disp != DISP_G0)
		return;
	char t[160];
	panel_text(t, sizeof t);
	if (!strcmp(t, panel_shown))
		return;
	strcpy(panel_shown, t);
	panel_draw();
	scr_push_all();
}

void g0_show(void)
{
	disp = DISP_G0;
	dirty_lo = 0, dirty_hi = DHEIGHT - 1;
	ptr_drawn_y = -1;
	panel_draw();
	char t[160];
	panel_text(t, sizeof t);
	strcpy(panel_shown, t);
	for (int d = 0; d < DHEIGHT; d++)
		convert_row(d);
	dirty_lo = 999, dirty_hi = -1;
	if (ptr.mode)
		ptr_draw();
	ptr.moved = false;
	last_flush = clock_real_ms();
	scr_push_all();
	ST->view = view;
}
