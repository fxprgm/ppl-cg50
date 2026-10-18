/* Fixed-width text into gint's VRAM. */
#include "text.h"

#include <gint/display.h>

uint32_t utf8_next(const char **sp)
{
	const unsigned char *s = (const unsigned char *)*sp;
	uint32_t c = *s;
	if (!c)
		return 0;
	int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
	if (n)
		c &= 0x3F >> n;
	s++;
	for (int i = 0; i < n && (*s & 0xC0) == 0x80; i++)
		c = c << 6 | (*s++ & 0x3F);
	*sp = (const char *)s;
	return c;
}

int utf8_put(char *b, uint32_t c)
{
	if (c < 0x80) { b[0] = c; return 1; }
	if (c < 0x800) { b[0] = 0xC0 | c >> 6; b[1] = 0x80 | (c & 63); return 2; }
	if (c < 0x10000) {
		b[0] = 0xE0 | c >> 12; b[1] = 0x80 | (c >> 6 & 63); b[2] = 0x80 | (c & 63);
		return 3;
	}
	b[0] = 0xF0 | c >> 18; b[1] = 0x80 | (c >> 12 & 63); b[2] = 0x80 | (c >> 6 & 63);
	b[3] = 0x80 | (c & 63);
	return 4;
}

int utf8_len(const char *s)
{
	int n = 0;
	while (utf8_next(&s))
		n++;
	return n;
}

int utf8_offset(const char *s, int i)
{
	const char *p = s;
	while (i-- > 0 && utf8_next(&p)) {}
	return p - s;
}

static int glyph(const pfont_t *f, uint32_t cp)
{
	int lo = 0, hi = f->count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (f->cps[mid] == cp)
			return mid;
		if (f->cps[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

/* the Prime's glyphs outside the fonts */
static uint32_t subst(uint32_t cp)
{
	switch (cp) {
	case 0xE003: return 'i';     /* imaginary unit */
	case 0x2148: return 'i';
	case 0x1D07: return 'E';     /* exponent mark, if the font lacks it */
	case 0x2212: return '-';
	case 0x25B6: return '>';
	case '\t': return ' ';
	}
	return '?';
}

void text_char(const pfont_t *f, int x, int y, uint32_t cp, int fg, int bg)
{
	int g = glyph(f, cp);
	if (g < 0)
		g = glyph(f, subst(cp));
	if (g < 0)
		g = glyph(f, '?');
	const uint8_t *rows = f->bits + g * f->h;
	int x0 = x < 0 ? -x : 0, x1 = x + f->w > DWIDTH ? DWIDTH - x : f->w;
	for (int r = 0; r < f->h; r++) {
		int yy = y + r;
		if (yy < 0 || yy >= DHEIGHT)
			continue;
		uint8_t bits = rows[r];
		uint16_t *p = gint_vram + yy * DWIDTH + x;
		for (int c = x0; c < x1; c++) {
			if (bits & (0x80 >> c))
				p[c] = fg;
			else if (bg >= 0)
				p[c] = bg;
		}
	}
}

int text_draw(const pfont_t *f, int x, int y, const char *s, int fg, int bg, int max)
{
	uint32_t c;
	while (max-- != 0 && (c = utf8_next(&s)) != 0) {
		text_char(f, x, y, c, fg, bg);
		x += f->w;
	}
	return x;
}

int text_width(const pfont_t *f, const char *s)
{
	return utf8_len(s) * f->w;
}

void text_right(const pfont_t *f, int x_end, int y, const char *s, int fg, int bg)
{
	text_draw(f, x_end - text_width(f, s), y, s, fg, bg, -1);
}
