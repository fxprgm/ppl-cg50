/* The keyboard as a running program sees it (io.h). */
#include "io.h"
#include "clock.h"
#include "stats.h"

#include <gint/keyboard.h>
#include <string.h>

/* Prime key codes: top block Apps 0, Symb 1, Up 2, Help 3, Esc 4, Home 5, Plot 6, Left 7,
   Right 8, View 9, CAS 10, Num 11, Down 12, Menu 13; keypad Vars 14, Mem 15, Units 16, xttn 17,
   a b/c 18, Del 19, x^y 20, SIN 21, COS 22, TAN 23, LN 24, LOG 25, x2 26, +/- 27, () 28, , 29,
   Enter 30, EEX 31, 7 32, 8 33, 9 34, / 35, ALPHA 36, 4 37, 5 38, 6 39, * 40, Shift 41, 1 42,
   2 43, 3 44, - 45, On 46, 0 47, . 48, space 49, + 50 (hp-prime-kit interface.key-codes) */
const keymap_t keymap[] = {
	/* F1-F6 alone tap the soft-menu buttons (no key code, as a tap on the Prime); SHIFT+F1-F6
	   (SHIFT held, or pressed just before) are the Prime keys Apps, Home, CAS, Symb, Plot, Num */
	{ KEY_F1, 0, "SH+F1", "Apps" },       { KEY_F2, 5, "SH+F2", "Home" },
	{ KEY_F3, 10, "SH+F3", "CAS" },       { KEY_F4, 1, "SH+F4", "Symb" },
	{ KEY_F5, 6, "SH+F5", "Plot" },       { KEY_F6, 11, "SH+F6", "Num" },
	{ KEY_UP, 2, "UP", "Up" },              { KEY_LEFT, 7, "LEFT", "Left" },
	{ KEY_RIGHT, 8, "RIGHT", "Right" },     { KEY_DOWN, 12, "DOWN", "Down" },
	{ KEY_EXIT, 4, "EXIT", "Esc" },         { KEY_MENU, 13, "MENU", "Menu" },
	{ KEY_FD, 9, "S<>D", "View" },          { KEY_VARS, 14, "VARS", "Vars" },
	{ KEY_SHIFT, 41, "SHIFT", "Shift" },    { KEY_ALPHA, 36, "ALPHA", "ALPHA" },
	{ KEY_XOT, 17, "X,th,T", "x t th n" },  { KEY_FRAC, 18, "a b/c", "a b/c" },
	{ KEY_DEL, 19, "DEL", "Del" },          { KEY_POWER, 20, "^", "x^y" },
	{ KEY_SIN, 21, "sin", "SIN" },          { KEY_COS, 22, "cos", "COS" },
	{ KEY_TAN, 23, "tan", "TAN" },          { KEY_LN, 24, "ln", "LN" },
	{ KEY_LOG, 25, "log", "LOG" },          { KEY_SQUARE, 26, "x^2", "x^2" },
	{ KEY_NEG, 27, "(-)", "+/-" },          { KEY_LEFTP, 28, "(", "( )" },
	{ KEY_RIGHTP, 28, ")", "( )" },         { KEY_COMMA, 29, ",", "," },
	{ KEY_EXE, 30, "EXE", "Enter" },        { KEY_EXP, 31, "x10^x", "EEX" },
	{ KEY_7, 32, "7", "7" },                { KEY_8, 33, "8", "8" },
	{ KEY_9, 34, "9", "9" },                { KEY_DIV, 35, "/", "/" },
	{ KEY_4, 37, "4", "4" },                { KEY_5, 38, "5", "5" },
	{ KEY_6, 39, "6", "6" },                { KEY_MUL, 40, "x", "*" },
	{ KEY_1, 42, "1", "1" },                { KEY_2, 43, "2", "2" },
	{ KEY_3, 44, "3", "3" },                { KEY_SUB, 45, "-", "-" },
	{ KEY_0, 47, "0", "0" },                { KEY_DOT, 48, ".", "." },
	{ KEY_ARROW, 49, "->", "space" },       { KEY_ADD, 50, "+", "+" },
	{ KEY_ACON, -1, "AC/ON", "break" },     { KEY_OPTN, -1, "OPTN", "pointer" },
};
const int keymap_count = sizeof keymap / sizeof keymap[0];

pointer_t ptr = { false, 160, 120, false };

#define QN 32
static struct { int8_t code; uint32_t seq; } kq[QN];
static int kq_n;
static struct { touch_ev_t ev; uint32_t seq; } tq[QN];
static int tq_n;
static uint32_t seq;
static bool brk;
static bool shift_pending;       /* SHIFT was the last key pressed: the next F key is a Prime key */
static uint8_t fshift;           /* F keys (bit n = F(n+1)) held since a shifted press */

/* the touch now: down, where, where it started, the Prime's type; seen: MOUSE has reported it */
static struct {
	bool down, latched;
	int x, y, x0, y0, type;
	bool moved;
	int fkey;                  /* the F key holding it, or 0 for the pointer's EXE */
} touch;

static uint32_t arrow_since, arrow_last;
static bool held[4];             /* LEFT RIGHT UP DOWN, from the key events */

void io_reset(void)
{
	kq_n = tq_n = 0;
	memset(&touch, 0, sizeof touch);
	brk = false;
	shift_pending = false, fshift = 0;
	arrow_since = 0;
	memset(held, 0, sizeof held);
	ptr.mode = false;               /* each run starts with the arrows as keys */
	ST->ptr_mode = 0;
	clearevents();
}

static void push_key(int code)
{
	if (kq_n == QN) {
		memmove(kq, kq + 1, sizeof kq[0] * (QN - 1));
		kq_n--;
	}
	kq[kq_n].code = code, kq[kq_n].seq = seq++;
	kq_n++;
}

static void push_touch(int type, int x, int y)
{
	if (tq_n == QN) {
		memmove(tq, tq + 1, sizeof tq[0] * (QN - 1));
		tq_n--;
	}
	tq[tq_n].ev.type = type, tq[tq_n].ev.x = x, tq[tq_n].ev.y = y, tq[tq_n].seq = seq++;
	tq_n++;
}

static void touch_down(int x, int y, int fkey)
{
	touch.down = touch.latched = true;
	touch.x = touch.x0 = x, touch.y = touch.y0 = y;
	touch.type = 0;
	touch.moved = false;
	touch.fkey = fkey;
	push_touch(0, x, y);
}

static void touch_up(void)
{
	if (!touch.down)
		return;
	touch.down = false;
	push_touch(2, touch.x, touch.y);
	if (!touch.moved)
		push_touch(3, touch.x, touch.y);
	touch.type = 1;
}

static void ptr_step(int dx, int dy)
{
	int x = ptr.x + dx, y = ptr.y + dy;
	if (x < 0) x = 0;
	if (x > 319) x = 319;
	if (y < 0) y = 0;
	if (y > 239) y = 239;
	if (x == ptr.x && y == ptr.y)
		return;
	ptr.x = x, ptr.y = y, ptr.moved = true;
	ST->ptr_x = x, ST->ptr_y = y;
	if (touch.down && !touch.fkey) {
		touch.x = x, touch.y = y, touch.type = 2, touch.moved = true;
		push_touch(1, x, y);
	}
}

static int prime_code(int key)
{
	for (int i = 0; i < keymap_count; i++)
		if (keymap[i].cg_key == key)
			return keymap[i].prime;
	return -1;
}

bool io_pump(void)
{
	key_event_t ev;
	while ((ev = pollevent()).type != KEYEV_NONE) {
		if (ev.type != KEYEV_DOWN && ev.type != KEYEV_UP)
			continue;
		ST->keyev++;
		bool down = ev.type == KEYEV_DOWN;
		int k = ev.key;
		if (k == KEY_ACON) {
			if (down)
				brk = true;
			continue;
		}
		if (k == KEY_OPTN) {
			if (down) {
				ptr.mode = !ptr.mode;
				ptr.moved = true;
				ST->ptr_mode = ptr.mode;
				if (!ptr.mode && touch.down && !touch.fkey)
					touch_up();
			}
			continue;
		}
		if (k >= KEY_F1 && k <= KEY_F6) {
			int n = k - KEY_F1;
			if (down) {
				if (shift_pending || keydown(KEY_SHIFT)) {
					/* SHIFT+F: the Prime key, no touch */
					shift_pending = false;
					fshift |= 1 << n;
					push_key(prime_code(k));
					continue;
				}
				if (touch.down)
					touch_up();
				touch_down(26 + 53 * n, 230, k);
			}
			else {
				fshift &= ~(1 << n);
				if (touch.down && touch.fkey == k)
					touch_up();
			}
			continue;
		}
		if (down)
			shift_pending = k == KEY_SHIFT;
		if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN)
			held[k == KEY_LEFT ? 0 : k == KEY_RIGHT ? 1 : k == KEY_UP ? 2 : 3] = down;
		if (ptr.mode && (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN)) {
			if (down) {
				ptr_step(k == KEY_RIGHT ? 1 : k == KEY_LEFT ? -1 : 0,
				         k == KEY_DOWN ? 1 : k == KEY_UP ? -1 : 0);
				arrow_since = arrow_last = clock_real_ms() | 1;
			}
			continue;
		}
		if (ptr.mode && k == KEY_EXE) {
			if (down) {
				if (touch.down)
					touch_up();
				touch_down(ptr.x, ptr.y, 0);
			}
			else if (touch.down && !touch.fkey)
				touch_up();
			continue;
		}
		if (down) {
			int c = prime_code(k);
			if (c >= 0)
				push_key(c);
		}
	}
	/* a release a dialog's own key loop took: the touch ends, the arrow is up */
	if (touch.down && !keydown(touch.fkey ? touch.fkey : KEY_EXE))
		touch_up();
	static const int arrows[4] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN };
	for (int i = 0; i < 4; i++)
		if (held[i] && !keydown(arrows[i]))
			held[i] = false;
	/* held arrows keep moving the pointer, faster the longer they are held */
	if (ptr.mode && arrow_since) {
		int dx = held[1] - held[0];
		int dy = held[3] - held[2];
		uint32_t now = clock_real_ms();
		if (!dx && !dy)
			arrow_since = 0;
		else if (now - arrow_last >= 30) {
			uint32_t held = now - arrow_since;
			int sp = held < 400 ? 0 : held < 1200 ? 1 : held < 2400 ? 3 : 6;
			if (sp)
				ptr_step(dx * sp, dy * sp);
			arrow_last = now;
		}
	}
	for (int n = 0; n < 6; n++)
		if ((fshift >> n & 1) && !keydown(KEY_F1 + n))
			fshift &= ~(1 << n);
	return brk;
}

bool io_break_pending(void) { return brk; }
void io_clear_break(void) { brk = false; }

int io_getkey(void)
{
	if (!kq_n)
		return -1;
	int c = kq[0].code;
	memmove(kq, kq + 1, sizeof kq[0] * --kq_n);
	return c;
}

bool io_iskeydown(int code)
{
	for (int i = 0; i < keymap_count; i++)
		if (keymap[i].prime == code && keymap[i].prime >= 0 && keydown(keymap[i].cg_key)) {
			int k = keymap[i].cg_key;
			if (k >= KEY_F1 && k <= KEY_F6 && !(fshift >> (k - KEY_F1) & 1))
				continue;       /* an F key alone is a tap, not the Prime key */
			if (ptr.mode && (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN ||
			                 k == KEY_EXE))
				continue;
			return true;
		}
	return false;
}

bool io_next_event(int *key, touch_ev_t *t)
{
	if (kq_n && (!tq_n || kq[0].seq < tq[0].seq)) {
		*key = io_getkey();
		return true;
	}
	if (tq_n) {
		*key = -1;
		*t = tq[0].ev;
		memmove(tq, tq + 1, sizeof tq[0] * --tq_n);
		return true;
	}
	return false;
}

bool io_mouse(int v[5])
{
	if (!touch.down && !touch.latched)
		return false;
	v[0] = touch.x, v[1] = touch.y, v[2] = touch.x0, v[3] = touch.y0;
	v[4] = touch.down ? touch.type : 1;
	touch.latched = false;
	return true;
}
