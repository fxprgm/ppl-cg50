/* Shared screens and widgets: colours, the soft-key bar, boxes, messages, key waits, and the
 * line editor (Home's command line and INPUT's fields). */
#ifndef PPL_UI_H
#define PPL_UI_H

#include <gint/display.h>
#include <stdbool.h>
#include <stdint.h>

#define C_PANEL  C_RGB(3, 6, 10)
#define C_TITLE  C_RGB(31, 24, 4)
#define C_LABEL  C_RGB(20, 22, 25)
#define C_SEL    C_RGB(6, 14, 28)
#define C_FOOT   C_RGB(12, 12, 12)
#define C_BAR    C_RGB(25, 27, 30)
#define C_ERR    C_RGB(24, 0, 0)
#define C_OUT    C_RGB(0, 0, 14)
#define C_SOFT   C_RGB(29, 29, 29)

#define LINE_H 14            /* text rows (6x13 font) */
#define COLS 66              /* characters across the display */

/* the soft-key bar (y 206-223): six labels, NULL = empty */
void ui_softbar(const char *const labels[6]);
/* a framed box with a title bar; returns the y of the first content row */
int ui_box(int x0, int y0, int x1, int y1, const char *title);
/* a full-screen message: lines separated by \n, wrapped; footer at the bottom */
void ui_message(const char *title, const char *text, const char *footer, int color);
/* wait for a key press (gint key code); AC/ON counts. Sleeps between events. */
int ui_key(void);
/* draws text wrapped into [x, x + cols*6) from y, at most rows rows; returns rows used */
int ui_wrap(int x, int y, int cols, int rows, const char *s, int fg);

/* ---- the line editor ---- */
#define ED_MAX 600
typedef struct {
	char buf[ED_MAX];        /* UTF-8 */
	int cur;                 /* cursor, in code points */
	bool shift, alpha, lock, lower;
	int scroll;              /* first code point shown */
} editor_t;

enum { ED_NONE, ED_CHANGED, ED_EXE, ED_EXIT, ED_UNUSED };
void ed_clear(editor_t *e);
void ed_set(editor_t *e, const char *s);
void ed_insert(editor_t *e, const char *s);
/* a gint key press: edits, or ED_UNUSED (arrows up/down, F keys, MENU, OPTN, VARS...) */
int ed_key(editor_t *e, int key);
/* draws the editor in rows of cols characters from (x, y), at most rows rows, the cursor
   kept visible */
void ed_draw(editor_t *e, int x, int y, int cols, int rows, int fg, int bg);
/* the modifier state as a short string: "", "SHIFT", "A", "a", "A-LOCK", "a-lock" */
const char *ed_mode(const editor_t *e);

#endif
