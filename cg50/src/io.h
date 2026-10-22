/* What a running program sees of the keyboard: the Prime's key codes (GETKEY, ISKEYDOWN,
 * WAIT), touches emulated for MOUSE and WAIT(-1), and the ON key (AC/ON) that breaks it.
 *
 * Touches: F1-F6 tap the six soft-menu buttons that DRAWMENU draws (x 26, 79, 132, 185, 238,
 * 291; y 230) and give no key code (a tap on the Prime gives none). SHIFT+F1-F6 (SHIFT held or
 * pressed just before) are the Prime keys 0, 5, 10, 1, 6, 11 (Apps, Home, CAS, Symb, Plot,
 * Num) for GETKEY, ISKEYDOWN and WAIT, with no touch. OPTN toggles the pointer: the
 * arrows then move it (faster when held) and EXE touches the screen under it (held EXE +
 * arrows = a drag). */
#ifndef PPL_IO_H
#define PPL_IO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	int cg_key;
	int8_t prime;              /* Prime key code 0-50, -1 = none */
	const char *cg_name, *prime_name;
} keymap_t;

extern const keymap_t keymap[];
extern const int keymap_count;

/* a touch event as WAIT(-1) answers it: {type, x, y} (0 down, 1 move, 2 up, 3 click) */
typedef struct { int8_t type; int16_t x, y; } touch_ev_t;

/* the pointer (OPTN) */
typedef struct {
	bool mode;                 /* pointer mode: arrows move it, EXE touches */
	int x, y;                  /* Prime coordinates, 0-319 x 0-239 */
	bool moved;                /* the screen must redraw it */
} pointer_t;
extern pointer_t ptr;

void io_reset(void);           /* a program starts: empty queues, nothing touched */
/* reads gint's key events; true if AC/ON was pressed (break) */
bool io_pump(void);
bool io_break_pending(void);
void io_clear_break(void);
int io_getkey(void);           /* next Prime key code, -1 if none */
bool io_iskeydown(int code);
/* WAIT(-1): the oldest key (*key >= 0) or touch event (*key = -1, *t); false if none */
bool io_next_event(int *key, touch_ev_t *t);
/* MOUSE: the current touch, if any (a tap stays visible until MOUSE has seen it once) */
bool io_mouse(int v[5]);
/* drop the waiting touch events and MOUSE's latched touch (a dialog closes) */
void io_flush_touches(void);

#endif
