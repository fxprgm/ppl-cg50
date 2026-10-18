/* The PRINT terminal (term.h). Lines are kept as printed and wrapped when drawn. */
#include "term.h"
#include "clock.h"
#include "screen.h"
#include "text.h"
#include "ui.h"

#include <gint/keyboard.h>
#include <stdlib.h>
#include <string.h>

#define TMAX 400
static char *lines[TMAX];
static int nlines;
static bool pending;
static uint32_t last_draw;

void term_clear(void)
{
	for (int i = 0; i < nlines; i++)
		free(lines[i]);
	nlines = 0;
	pending = true;
}

int term_count(void) { return nlines; }

static void add(const char *s, int n)
{
	if (nlines == TMAX) {
		free(lines[0]);
		memmove(lines, lines + 1, sizeof lines[0] * (TMAX - 1));
		nlines--;
	}
	char *c = malloc(n + 1);
	if (!c)
		return;
	memcpy(c, s, n);
	c[n] = 0;
	lines[nlines++] = c;
}

void term_print(const char *s)
{
	if (!s) {
		term_clear();
		return;
	}
	for (;;) {
		const char *nl = strchr(s, '\n');
		add(s, nl ? nl - s : (int)strlen(s));
		if (!nl)
			break;
		s = nl + 1;
	}
	pending = true;
}

/* rows a line takes on screen */
static int rows_of(const char *s)
{
	int n = utf8_len(s);
	return n ? (n + COLS - 1) / COLS : 1;
}

#define TROWS 16

static int total_rows(void)
{
	int t = 0;
	for (int i = 0; i < nlines; i++)
		t += rows_of(lines[i]);
	return t;
}

static void draw(int scroll, int rows)
{
	/* the screen shows display rows [first, first + rows) of all the wrapped rows */
	int total = total_rows();
	int first = total - rows - scroll;
	if (first < 0)
		first = 0;
	drect(0, 0, DWIDTH - 1, rows * LINE_H - 1, C_WHITE);
	int r = 0, y = 0;
	for (int i = 0; i < nlines && y < rows; i++) {
		const char *p = lines[i];
		int k = rows_of(p);
		for (int j = 0; j < k; j++, r++) {
			if (r >= first && y < rows) {
				text_draw(FONT, 0, y * LINE_H, p, C_BLACK, -1, COLS);
				y++;
			}
			p += utf8_offset(p, COLS);
		}
	}
}

void term_show(int scroll)
{
	disp = DISP_TERM;
	dclear(C_WHITE);
	draw(scroll, TROWS);
	scr_push_all();
	pending = false;
	last_draw = clock_real_ms();
}

void term_flush(int every)
{
	if (disp != DISP_TERM || !pending)
		return;
	uint32_t now = clock_real_ms();
	if (every && now - last_draw < (uint32_t)every)
		return;
	term_show(0);
}

int term_view(const char *footer)
{
	int scroll = 0;
	int total = total_rows();
	for (;;) {
		disp = DISP_TERM;
		dclear(C_WHITE);
		draw(scroll, TROWS - 1);
		drect(0, DHEIGHT - 15, DWIDTH - 1, DHEIGHT - 1, C_PANEL);
		text_draw(FONT, 4, DHEIGHT - 14, footer, C_LABEL, -1, COLS - 1);
		scr_push_all();
		int k = ui_key();
		if (k == KEY_UP && scroll + TROWS - 1 < total)
			scroll++;
		else if (k == KEY_DOWN && scroll > 0)
			scroll--;
		else if (k != KEY_UP && k != KEY_DOWN)
			return k;
	}
}
