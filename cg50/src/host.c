/* The VM's hooks and the add-in's built-ins (host.h). */
#include "host.h"
#include "bi.h"
#include "clock.h"
#include "dialogs.h"
#include "gfx.h"
#include "home.h"
#include "io.h"
#include "screen.h"
#include "stats.h"
#include "term.h"

#include <gint/cpu.h>
#include <gint/rtc.h>
#include <string.h>

static bool g0_used;          /* the running program drew on G0 */
static uint32_t last_panel;
static ppl_vm *VM;

/* the screen while a program runs: what changed, at most every few tens of ms */
static void refresh(int every)
{
	gfx_flush(VM);             /* the back end presents rows it holds back */
	g0_flush(every);
	term_flush(every ? 60 : 0);
	uint32_t now = clock_real_ms();
	if (!every || now - last_panel >= 250 || ptr.moved) {
		panel_update();
		last_panel = now;
	}
}

/* ---- hooks ------------------------------------------------------------------------------- */

static void h_print(ppl_vm *V, const char *s)
{
	(void)V;
	term_print(s);
	if (s) {
		if (disp != DISP_TERM)
			term_show(0);
		term_flush(60);
	}
	else if (disp == DISP_TERM) {
		/* PRINT(): the terminal is cleared and hidden */
		if (g0_used)
			g0_show();
		else
			term_show(0);
	}
}

static void h_present(ppl_vm *V, int y0, int y1)
{
	(void)V;
	g0_used = true;
	home_freeze = false;       /* drawn after FREEZE: nothing frozen any more */
	if (disp != DISP_G0) {
		panel_state = "Running";
		g0_show();
		return;
	}
	g0_mark(y0, y1);
	g0_flush(0);               /* the back end paces its presents (GFX_PRESENT_MS) */
}

static int h_poll(ppl_vm *V)
{
	(void)V;
	ST->polls++;
	if (io_pump()) {
		io_clear_break();
		return 1;
	}
	refresh(40);
	return 0;
}

static uint32_t h_ticks(void) { return clock_real_ms(); }

static void h_now(int *t)
{
	rtc_time_t r;
	rtc_get_time(&r);
	t[0] = r.year, t[1] = r.month + 1, t[2] = r.month_day;
	t[3] = r.hours, t[4] = r.minutes, t[5] = r.seconds;
}

static int h_getkey(void)
{
	io_pump();
	return io_getkey();
}

void host_hooks(ppl_vm *V)
{
	VM = V;
	V->print = h_print;
	V->present = h_present;
	V->poll = h_poll;
	V->pollcount = POLL_LINES;
	V->ticks = h_ticks;
	V->now = h_now;
	V->getkey = h_getkey;
	g0_used = false;
}

/* a run starts (home.c) */
void host_run_start(void) { g0_used = false; }

/* the end of a run, a dialog: everything drawn is on the screen */
void host_flush(void) { refresh(0); }

/* ---- built-ins --------------------------------------------------------------------------- */

static BI(b_getkey)
{
	(void)V; (void)argc; (void)a;
	if (io_pump()) {
		io_clear_break();
		return E_BREAK;
	}
	refresh(30);
	v_int32(out, io_getkey());
	return 0;
}

static BI(b_iskeydown)
{
	int32_t k;
	(void)V; (void)argc;
	if (bi_int(&a[0], &k)) return E_BADTYPE;
	if (io_pump()) {
		io_clear_break();
		return E_BREAK;
	}
	refresh(30);
	v_int32(out, io_iskeydown(k));
	return 0;
}

static int touch_list(value *out, const touch_ev_t *t)
{
	int st = v_list(out, 3);
	if (st) return st;
	v_int32(&out->u.l->v[0], t->type);
	v_int32(&out->u.l->v[1], t->x);
	v_int32(&out->u.l->v[2], t->y);
	return 0;
}

/* WAIT(n): n > 0 pauses n seconds; WAIT or WAIT(0) waits for a key (at most a minute) and
   answers its code; WAIT(-1) waits for a key or a touch (at most a minute) and answers the
   key code or {type, x, y}; -1 after the minute */
static BI(b_wait)
{
	int32_t ms = 0;
	(void)V;
	if (argc) {
		real r, k;
		if (bi_real(&a[0], &r)) return E_BADTYPE;
		dec_from_int(&k, 1000);
		dec_mul(&r, &r, &k);
		if (dec_to_int32(&r, &ms)) ms = r.s ? -1000 : 0x7FFFFFFF;
	}
	const char *saved = panel_state;
	uint32_t t0 = clock_real_ms();
	int mode = ms > 0 ? 0 : ms == 0 ? 1 : 2;
	uint32_t limit = ms > 0 ? (uint32_t)ms : 60000;
	ST->phase = PH_WAIT;
	panel_state = ms > 0 ? "WAIT" : "Waiting";
	int rc = 0;
	v_int32(out, mode ? -1 : 0);
	for (;;) {
		if (io_pump()) {
			io_clear_break();
			rc = E_BREAK;
			break;
		}
		if (mode == 1) {
			int k = io_getkey();
			if (k >= 0) {
				v_int32(out, k);
				break;
			}
		}
		else if (mode == 2) {
			int k;
			touch_ev_t t;
			if (io_next_event(&k, &t)) {
				if (k >= 0)
					v_int32(out, k);
				else
					rc = touch_list(out, &t);
				break;
			}
		}
		refresh(30);
		if (clock_real_ms() - t0 >= limit)
			break;
		sleep();
	}
	panel_state = saved;
	ST->phase = PH_RUN;
	return rc;
}

/* MOUSE: {{x, y, x0, y0, type}, {}} while touched (a tap stays until read once), {{}, {}}
   otherwise; MOUSE(i): element i (0-4) of the first pointer, -1 if none */
static BI(b_mouse)
{
	int v[5];
	(void)V;
	if (io_pump()) {
		io_clear_break();
		return E_BREAK;
	}
	refresh(30);
	bool on = io_mouse(v);
	if (argc) {
		int32_t i;
		if (bi_int(&a[0], &i)) return E_BADTYPE;
		v_int32(out, on && i >= 0 && i < 5 ? v[i] : -1);
		return 0;
	}
	int st = v_list(out, 2);
	if (st) return st;
	value l;
	if ((st = v_list(&l, on ? 5 : 0))) return st;
	for (int i = 0; on && i < 5; i++)
		v_int32(&l.u.l->v[i], v[i]);
	out->u.l->v[0] = l;
	if ((st = v_list(&l, 0))) return st;
	out->u.l->v[1] = l;
	return 0;
}

static BI(b_freeze)
{
	(void)V; (void)argc; (void)a;
	refresh(0);                /* first: a present clears the freeze */
	home_freeze = true;
	v_int32(out, 1);
	return 0;
}

#ifdef GFX_STUB
/* the stub graphics' commands (src/gfx.c), until the gfx branch is merged */
static BI(b_rect) { return gfx_stub_rect(V, argc, a, out); }
static BI(b_line) { return gfx_stub_line(V, argc, a, out); }
static BI(b_pixon) { return gfx_stub_pixon(V, argc, a, out); }
static BI(b_drawmenu) { return gfx_stub_drawmenu(V, argc, a, out); }
#endif

const builtin host_table[] = {
	{ "GETKEY", b_getkey, 0, 0, BF_NOPAREN }, { "ISKEYDOWN", b_iskeydown, 1, 1, 0 },
	{ "WAIT", b_wait, 0, 1, BF_NOPAREN }, { "MOUSE", b_mouse, 0, 1, BF_NOPAREN },
	{ "INPUT", dlg_input, 1, 6, BF_REF1 }, { "CHOOSE", dlg_choose, 2, -1, BF_REF1 },
	{ "MSGBOX", dlg_msgbox, 1, 3, 0 }, { "FREEZE", b_freeze, 0, 0, BF_NOPAREN },
#ifdef GFX_STUB
	{ "RECT", b_rect, 0, -1, BF_NOPAREN }, { "RECT_P", b_rect, 0, -1, BF_NOPAREN },
	{ "LINE_P", b_line, 4, -1, 0 }, { "PIXON_P", b_pixon, 2, -1, 0 },
	{ "DRAWMENU", b_drawmenu, 0, -1, BF_NOPAREN },
#endif
	{ NULL, NULL, 0, 0, 0 }
};
