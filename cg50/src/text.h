/* Fixed-width text (X.org misc-fixed 6x13 and 6x10) straight into gint's VRAM, from UTF-8.
 * The Prime's own glyphs that the fonts lack are drawn as their nearest ASCII. */
#ifndef PPL_TEXT_H
#define PPL_TEXT_H

#include <stdint.h>

typedef struct {
	uint8_t w, h;
	uint16_t count;
	const uint16_t *cps;   /* sorted code points */
	const uint8_t *bits;   /* count * h rows, bit 7 = leftmost pixel */
} pfont_t;

extern const pfont_t font_6x13, font_6x10;
#define FONT (&font_6x13)
#define SMALL (&font_6x10)

/* next code point of a UTF-8 string (advances *s; 0 at the end) */
uint32_t utf8_next(const char **s);
/* append code point c to buf as UTF-8; returns the bytes written (1-4) */
int utf8_put(char *buf, uint32_t c);
/* number of code points */
int utf8_len(const char *s);
/* the byte offset of code point i */
int utf8_offset(const char *s, int i);

/* bg < 0: transparent */
void text_char(const pfont_t *f, int x, int y, uint32_t cp, int fg, int bg);
/* draws at most max code points (max < 0: all); returns the x after the text */
int text_draw(const pfont_t *f, int x, int y, const char *s, int fg, int bg, int max);
/* text right-aligned so that it ends at x_end */
void text_right(const pfont_t *f, int x_end, int y, const char *s, int fg, int bg);
int text_width(const pfont_t *f, const char *s);

#endif
