/* Shared screens and the line editor (ui.h). */
#include "ui.h"
#include "text.h"
#include "screen.h"

#include <gint/keyboard.h>
#include <string.h>

void ui_softbar(const char *const labels[6])
{
	drect(0, 206, DWIDTH - 1, DHEIGHT - 1, C_PANEL);
	for (int i = 0; i < 6; i++) {
		int x0 = i * 66 + 1, x1 = i * 66 + 64;
		if (!labels[i] || !labels[i][0])
			continue;
		drect(x0, 207, x1, DHEIGHT - 2, C_SOFT);
		int w = text_width(FONT, labels[i]);
		text_draw(FONT, x0 + (x1 - x0 + 1 - w) / 2, 209, labels[i], C_BLACK, -1, 10);
	}
}

int ui_box(int x0, int y0, int x1, int y1, const char *title)
{
	drect_border(x0, y0, x1, y1, C_WHITE, 2, C_PANEL);
	drect(x0 + 2, y0 + 2, x1 - 2, y0 + 17, C_PANEL);
	text_draw(FONT, x0 + 6, y0 + 3, title, C_TITLE, -1, (x1 - x0 - 12) / 6);
	return y0 + 22;
}

int ui_wrap(int x, int y, int cols, int rows, const char *s, int fg)
{
	int used = 0;
	while (*s && used < rows) {
		/* one row: up to cols code points, cut at a space if the line goes on */
		const char *p = s, *cut = NULL;
		int n = 0;
		while (*p && *p != '\n' && n < cols) {
			if (*p == ' ')
				cut = p;
			utf8_next(&p);
			n++;
		}
		const char *end = p;
		if (*p && *p != '\n' && cut && cut > s)
			end = cut;
		int k = 0;
		for (const char *q = s; q < end; utf8_next(&q))
			k++;
		text_draw(FONT, x, y, s, fg, -1, k);
		y += LINE_H;
		used++;
		s = end;
		if (*s == '\n' || *s == ' ')
			s++;
	}
	return used;
}

void ui_message(const char *title, const char *text, const char *footer, int color)
{
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 17, C_PANEL);
	text_draw(FONT, 6, 2, title, C_TITLE, -1, COLS - 2);
	ui_wrap(6, 26, COLS - 2, 12, text, color);
	if (footer)
		text_draw(FONT, 6, DHEIGHT - 16, footer, C_FOOT, -1, -1);
	scr_push_all();
}

int ui_key(void)
{
	/* presses, and repeats of held arrows; no MENU or modifier handling: that is ours */
	for (;;) {
		key_event_t ev = getkey_opt(GETKEY_REP_ARROWS, NULL);
		if (ev.type == KEYEV_DOWN || ev.type == KEYEV_HOLD)
			return ev.key;
	}
}

/* ---- the line editor ---------------------------------------------------------------------- */

/* what each key types: normal, SHIFT, ALPHA (letters as printed on the CG50's keys) */
static const struct { int key; const char *n, *s, *a; } chars[] = {
	{ KEY_0, "0", "\xE2\x85\x88" /* imaginary i (U+2148 typed as i) */, "Z" },
	{ KEY_1, "1", NULL, "U" }, { KEY_2, "2", NULL, "V" }, { KEY_3, "3", NULL, "W" },
	{ KEY_4, "4", NULL, "P" }, { KEY_5, "5", NULL, "Q" }, { KEY_6, "6", NULL, "R" },
	{ KEY_7, "7", NULL, "M" }, { KEY_8, "8", NULL, "N" }, { KEY_9, "9", NULL, "O" },
	{ KEY_DOT, ".", "=", " " },
	{ KEY_EXP, "\xE1\xB4\x87" /* ᴇ */, "\xCF\x80" /* π */, "\"" },
	{ KEY_NEG, "-", "Ans", "'" },
	{ KEY_ADD, "+", "[", "X" }, { KEY_SUB, "-", "]", "Y" },
	{ KEY_MUL, "*", "{", "S" }, { KEY_DIV, "/", "}", "T" },
	{ KEY_LEFTP, "(", "<", "I" }, { KEY_RIGHTP, ")", ">", "J" },
	{ KEY_COMMA, ",", "\xE2\x89\xA0" /* ≠ */, "K" },
	{ KEY_ARROW, "\xE2\x96\xB6" /* ▶ */, ":=", "L" },
	{ KEY_XOT, "X", "\xCE\xB8" /* θ */, "A" },
	{ KEY_LOG, "LOG(", "ALOG(", "B" }, { KEY_LN, "LN(", "EXP(", "C" },
	{ KEY_SIN, "SIN(", "ASIN(", "D" }, { KEY_COS, "COS(", "ACOS(", "E" },
	{ KEY_TAN, "TAN(", "ATAN(", "F" },
	{ KEY_FRAC, "/", "\xE2\x89\xA4" /* ≤ */, "G" }, { KEY_FD, ";", "\xE2\x89\xA5" /* ≥ */, "H" },
	{ KEY_SQUARE, "\xC2\xB2" /* ² */, "\xE2\x88\x9A" /* √ */, "r" },
	{ KEY_POWER, "^", "NTHROOT", "\xCE\xB8" },
};

void ed_clear(editor_t *e)
{
	e->buf[0] = 0;
	e->cur = 0;
	e->scroll = 0;
}

void ed_set(editor_t *e, const char *s)
{
	strncpy(e->buf, s, ED_MAX - 1);
	e->buf[ED_MAX - 1] = 0;
	e->cur = utf8_len(e->buf);
}

void ed_insert(editor_t *e, const char *s)
{
	int at = utf8_offset(e->buf, e->cur), n = strlen(s), len = strlen(e->buf);
	if (len + n >= ED_MAX)
		return;
	memmove(e->buf + at + n, e->buf + at, len - at + 1);
	memcpy(e->buf + at, s, n);
	e->cur += utf8_len(s);
}

static void ed_delete(editor_t *e)
{
	if (e->cur <= 0)
		return;
	int a = utf8_offset(e->buf, e->cur - 1), b = utf8_offset(e->buf, e->cur);
	memmove(e->buf + a, e->buf + b, strlen(e->buf) - b + 1);
	e->cur--;
}

const char *ed_mode(const editor_t *e)
{
	if (e->shift)
		return "SHIFT";
	if (e->alpha)
		return e->lock ? (e->lower ? "a-lock" : "A-LOCK") : (e->lower ? "a" : "A");
	return "";
}

int ed_key(editor_t *e, int key)
{
	if (key == KEY_SHIFT) {
		e->shift = !e->shift;
		return ED_CHANGED;
	}
	if (key == KEY_ALPHA) {
		if (e->shift) { /* SHIFT+ALPHA: alpha lock */
			e->shift = false;
			e->alpha = e->lock = true;
		}
		else if (e->alpha && !e->lock)
			e->lock = true;
		else if (e->alpha)
			e->alpha = e->lock = false;
		else
			e->alpha = true;
		return ED_CHANGED;
	}
	switch (key) {
	case KEY_LEFT:
		if (e->cur > 0) e->cur--;
		e->shift = false;
		return ED_CHANGED;
	case KEY_RIGHT:
		if (e->cur < utf8_len(e->buf)) e->cur++;
		e->shift = false;
		return ED_CHANGED;
	case KEY_DEL:
		if (e->shift) { /* SHIFT+DEL: delete at the cursor */
			if (e->cur < utf8_len(e->buf)) { e->cur++; ed_delete(e); }
			e->shift = false;
		}
		else
			ed_delete(e);
		return ED_CHANGED;
	case KEY_ACON:
		ed_clear(e);
		e->shift = e->alpha = e->lock = false;
		return ED_CHANGED;
	case KEY_EXE:
		e->shift = false;
		if (!e->lock) e->alpha = false;
		return ED_EXE;
	case KEY_EXIT:
		e->shift = false;
		if (!e->lock) e->alpha = false;
		return ED_EXIT;
	}
	for (unsigned i = 0; i < sizeof chars / sizeof chars[0]; i++) {
		if (chars[i].key != key)
			continue;
		const char *s = e->alpha ? chars[i].a : e->shift ? chars[i].s : chars[i].n;
		char low[4];
		if (e->alpha && e->lower && s && s[0] >= 'A' && s[0] <= 'Z' && !s[1]) {
			low[0] = s[0] + 32, low[1] = 0;
			s = low;
		}
		e->shift = false;
		if (e->alpha && !e->lock)
			e->alpha = false;
		if (s)
			ed_insert(e, s);
		return ED_CHANGED;
	}
	return ED_UNUSED;
}

void ed_draw(editor_t *e, int x, int y, int cols, int rows, int fg, int bg)
{
	ed_draw_font(e, FONT, x, y, cols, rows, fg, bg);
}

void ed_draw_font(editor_t *e, const pfont_t *f, int x, int y, int cols, int rows, int fg, int bg)
{
	int len = utf8_len(e->buf), cap = cols * rows, lh = f->h + 1;
	/* keep the cursor visible: scroll by whole rows */
	if (e->cur < e->scroll)
		e->scroll = e->cur / cols * cols;
	while (e->cur >= e->scroll + cap)
		e->scroll += cols;
	if (len < cap)
		e->scroll = 0;
	drect(x, y, x + cols * f->w - 1, y + rows * lh - 1, bg);
	const char *p = e->buf + utf8_offset(e->buf, e->scroll);
	for (int r = 0; r < rows; r++) {
		text_draw(f, x, y + r * lh, p, fg, -1, cols);
		p += utf8_offset(p, cols);
	}
	int c = e->cur - e->scroll;
	int cx = x + (c % cols) * f->w, cy = y + (c / cols) * lh;
	if (c / cols < rows)
		drect(cx, cy, cx + 1, cy + f->h - 1, e->shift ? C_RGB(31, 16, 0) : e->alpha ? C_RGB(0, 20, 0) : fg);
}
