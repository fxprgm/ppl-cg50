/* The source viewer (viewer.h): a program's text with line numbers and colours, in the font
 * size the Setup of the viewer keeps (config.h). Read only. */
#include "viewer.h"
#include "config.h"
#include "screen.h"
#include "stats.h"
#include "text.h"
#include "ui.h"

#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOP 19                    /* the first text row, below the title bar */
#define BOTTOM 205                /* the soft keys start here */
#define TABW 4
#define LMAX 4096                 /* cells of one line that are looked at */

enum { K_TEXT, K_KEYWORD, K_COMMENT, K_STRING, K_NUMBER };
static const int kcolor[] = {
	C_RGB(0, 0, 0), C_RGB(0, 6, 22), C_RGB(2, 15, 2), C_RGB(20, 6, 0), C_RGB(0, 13, 15),
};

/* the PPL keywords (case-insensitive, as the lexer reads them) */
static const char *const keywords[] = {
	"BEGIN", "END", "LOCAL", "EXPORT", "IF", "THEN", "ELSE", "CASE", "DEFAULT", "FOR", "FROM",
	"TO", "DOWNTO", "STEP", "DO", "WHILE", "REPEAT", "UNTIL", "BREAK", "CONTINUE", "RETURN",
	"IFERR", "AND", "OR", "NOT", "XOR", "MOD", "KILL", "CONST",
};

static struct {
	const char *src;
	int *start;                   /* the byte offset of each line */
	int n;
} doc;

static uint32_t cell[LMAX];
static uint8_t kind[LMAX];

static bool is_word(uint32_t c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

static bool keyword(const uint32_t *c, int n)
{
	for (unsigned k = 0; k < sizeof keywords / sizeof keywords[0]; k++) {
		const char *w = keywords[k];
		int i = 0;
		for (; i < n && w[i]; i++) {
			uint32_t a = c[i] >= 'a' && c[i] <= 'z' ? c[i] - 32 : c[i];
			if (a != (uint8_t)w[i])
				break;
		}
		if (i == n && !w[i])
			return true;
	}
	return false;
}

/* line l as cells (tabs expanded) with their kinds; returns the number of cells */
static int line_cells(int l)
{
	const char *p = doc.src + doc.start[l];
	int n = 0;
	uint32_t c;
	while (n < LMAX && (c = utf8_next(&p)) && c != '\n') {
		if (c == '\r')
			continue;
		if (c == '\t') {
			do cell[n++] = ' '; while (n % TABW && n < LMAX);
			continue;
		}
		cell[n++] = c;
	}
	/* the kinds: // comments, "strings", numbers (and #hex), keywords */
	for (int i = 0; i < n;) {
		if (cell[i] == '/' && i + 1 < n && cell[i + 1] == '/') {
			while (i < n) kind[i++] = K_COMMENT;
		}
		else if (cell[i] == '"') {
			kind[i++] = K_STRING;
			while (i < n && cell[i] != '"') kind[i++] = K_STRING;
			if (i < n) kind[i++] = K_STRING;
		}
		else if ((cell[i] >= '0' && cell[i] <= '9') || (cell[i] == '#' && i + 1 < n && is_word(cell[i + 1]))) {
			kind[i++] = K_NUMBER;
			while (i < n && (is_word(cell[i]) || cell[i] == '.')) kind[i++] = K_NUMBER;
		}
		else if (is_word(cell[i])) {
			int j = i;
			while (j < n && is_word(cell[j])) j++;
			int k = keyword(cell + i, j - i) ? K_KEYWORD : K_TEXT;
			while (i < j) kind[i++] = k;
		}
		else
			kind[i++] = K_TEXT;
	}
	return n;
}

static int digits(int n)
{
	int d = 1;
	while (n >= 10) n /= 10, d++;
	return d;
}

/* rows that line l takes (wrapping at cols) */
static int rows_of(int l, int cols)
{
	if (!cfg.view_wrap)
		return 1;
	int n = line_cells(l);
	return n ? (n + cols - 1) / cols : 1;
}

void viewer_show(const char *title, const char *src, int line)
{
	/* the lines */
	doc.src = src;
	doc.n = 1;
	for (const char *p = src; *p; p++)
		if (*p == '\n' && p[1])
			doc.n++;
	doc.start = malloc(sizeof(int) * doc.n);
	if (!doc.start)
		return;
	doc.n = 0;
	doc.start[doc.n++] = 0;
	for (const char *p = src; *p; p++)
		if (*p == '\n' && p[1])
			doc.start[doc.n++] = p + 1 - src;
	int cur = line > 0 ? line - 1 : 0, top = 0, hx = 0;
	if (cur >= doc.n)
		cur = doc.n - 1;
	bool shift = false, centre = true;
	int old_phase = ST->phase;
	ST->phase = PH_VIEW;
	for (;;) {
		pfont_t f;
		font_pick(&f, cfg.view_font, cfg.view_bold);
		int lh = f.h + 1, nrows = (BOTTOM - TOP) / lh;
		int gutter = cfg.view_numbers ? (digits(doc.n) + 1) * f.w : 0;
		int cols = (DWIDTH - 2 - gutter) / f.w;
		/* the current line in view: rows from top to cur (wrapping), at most nrows */
		if (cur < top)
			top = cur;
		if (centre) {
			/* the line asked for: a third of the way down */
			top = cur;
			for (int r = 0; top > 0 && r < nrows / 3; r += rows_of(top - 1, cols))
				top--;
			centre = false;
		}
		{
			/* the highest top that still shows cur: counted back from cur */
			int t = cur, r = rows_of(cur, cols);
			while (t > 0 && r + rows_of(t - 1, cols) <= nrows)
				r += rows_of(--t, cols);
			if (top < t)
				top = t;
			/* no empty rows below the end while there are lines above: the rows from top to
			 * the end, counted only as far as a screen */
			int below = 0;
			for (int l = top; l < doc.n && below <= nrows; l++)
				below += rows_of(l, cols);
			while (top > 0 && below + rows_of(top - 1, cols) <= nrows)
				below += rows_of(--top, cols);
		}
		/* the screen */
		dclear(C_WHITE);
		drect(0, 0, DWIDTH - 1, 17, C_PANEL);
		text_draw(FONT, 6, 2, title, C_TITLE, -1, 40);
		char t[48];
		snprintf(t, sizeof t, "line %d/%d%s", cur + 1, doc.n, shift ? "  SHIFT" : "");
		text_right(FONT, DWIDTH - 6, 2, t, C_LABEL, -1);
		int y = TOP;
		for (int l = top; l < doc.n && y + lh <= BOTTOM; l++) {
			int n = line_cells(l), first = cfg.view_wrap ? 0 : hx, rows = rows_of(l, cols);
			if (l == cur)
				drect(0, y - 1, DWIDTH - 1, y + rows * lh - 2, C_RGB(27, 29, 31));
			if (gutter) {
				snprintf(t, sizeof t, "%d", l + 1);
				pfont_t g = f;
				g.bold = 0;
				text_right(&g, gutter - f.w / 2, y, t, l == cur ? C_SEL : C_LABEL, -1);
			}
			for (int r = 0; r < rows && y + lh <= BOTTOM; r++, y += lh)
				for (int c = 0; c < cols; c++) {
					int i = first + r * cols + c;
					if (i >= n)
						break;
					if (cell[i] != ' ')
						text_char(&f, gutter + c * f.w, y, cell[i], kcolor[kind[i]], -1);
				}
			if (!cfg.view_wrap && hx && n > hx)
				drect(gutter, y - lh, gutter, y - 2, C_LABEL);   /* scrolled: a mark at the left */
		}
		if (gutter)
			dline(gutter - 3, TOP - 1, gutter - 3, BOTTOM - 1, C_RGB(26, 26, 26));
		const char *const sk[6] = { "Size-", "Size+", cfg.view_wrap ? "NoWrap" : "Wrap",
		                            cfg.view_numbers ? "No #" : "Line #", cfg.view_bold ? "Thin" : "Bold",
		                            cur ? "Top" : "End" };
		ui_softbar(sk);
		scr_push_all();
		int k = ui_key();
		int page = nrows > 2 ? nrows - 1 : 1;
		if (k == KEY_SHIFT) { shift = !shift; continue; }
		if (k == KEY_EXIT || k == KEY_ACON || k == KEY_MENU)
			break;
		else if (k == KEY_UP) cur -= shift ? page : 1;
		else if (k == KEY_DOWN) cur += shift ? page : 1;
		else if (k == KEY_LEFT) {
			if (cfg.view_wrap) cur -= page;
			else hx = hx > 8 ? hx - 8 : 0;
		}
		else if (k == KEY_RIGHT) {
			if (cfg.view_wrap) cur += page;
			else hx += 8;
		}
		else if (k == KEY_F1 || k == KEY_SUB) { if (cfg.view_font > 0) cfg.view_font--; config_save(); }
		else if (k == KEY_F2 || k == KEY_ADD) { if (cfg.view_font < FONT_SIZES - 1) cfg.view_font++; config_save(); }
		else if (k == KEY_F3) { cfg.view_wrap = !cfg.view_wrap; hx = 0; config_save(); }
		else if (k == KEY_F4) { cfg.view_numbers = !cfg.view_numbers; config_save(); }
		else if (k == KEY_F5) { cfg.view_bold = !cfg.view_bold; config_save(); }
		else if (k == KEY_F6) { cur = cur ? 0 : doc.n - 1; top = 0; }
		shift = false;
		if (cur < 0) cur = 0;
		if (cur >= doc.n) cur = doc.n - 1;
	}
	free(doc.start);
	doc.start = NULL;
	ST->phase = old_phase;
}
