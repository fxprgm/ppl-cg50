/* Fixed-width text (X.org misc-fixed 6x10, 6x13, 8x13, 9x15, 10x20) straight into gint's VRAM,
 * from UTF-8. The Prime's own glyphs that the fonts lack are drawn as their nearest ASCII. */
#ifndef PPL_TEXT_H
#define PPL_TEXT_H

#include <stdint.h>

typedef struct {
	uint8_t w, h;
	uint8_t bpr;           /* bytes per row: 1 (w <= 8) or 2 */
	uint16_t count;
	uint8_t bold;          /* drawn twice, one pixel apart (a copy of a font with this set) */
	const uint16_t *cps;   /* sorted code points */
	const uint8_t *bits;   /* count * h rows of bpr bytes, big-endian, top bit = leftmost pixel */
} pfont_t;

extern const pfont_t font_6x13, font_6x10, font_8x13, font_9x15, font_10x20;
#define FONT (&font_6x13)
#define SMALL (&font_6x10)

/* the sizes the Setup offers, small to large; FONT_SIZE_DEFAULT is 6x13 */
#define FONT_SIZES 5
#define FONT_SIZE_DEFAULT 1
extern const pfont_t *const font_sizes[FONT_SIZES];
extern const char *const font_size_names[FONT_SIZES];
/* the font of size i (clamped), bold or not, in *out */
void font_pick(pfont_t *out, int i, int bold);

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
