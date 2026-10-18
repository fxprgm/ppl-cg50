/* Home (home.h). */
#include "home.h"
#include "host.h"
#include "clock.h"
#include "dialogs.h"
#include "io.h"
#include "mem.h"
#include "progs.h"
#include "screen.h"
#include "stats.h"
#include "term.h"
#include "text.h"
#include "ui.h"

#include <gint/display.h>
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ppl_vm *V;
static editor_t ed;
bool home_freeze;
static bool running;

/* ---- history ----------------------------------------------------------------------------- */

#define HMAX 60
static struct { char *in, *out; bool err; } hist[HMAX];
static int nhist;
static int recall = -1;          /* UP/DOWN: the history entry in the line */

static char *dup(const char *s)
{
	char *d = malloc(strlen(s) + 1);
	if (d) strcpy(d, s);
	return d;
}

static void hist_add(const char *in, const char *out, bool err)
{
	if (nhist == HMAX) {
		free(hist[0].in);
		free(hist[0].out);
		memmove(hist, hist + 1, sizeof hist[0] * (HMAX - 1));
		nhist--;
	}
	hist[nhist].in = dup(in);
	hist[nhist].out = dup(out);
	hist[nhist].err = err;
	nhist++;
}

static void hist_clear(void)
{
	for (int i = 0; i < nhist; i++) {
		free(hist[i].in);
		free(hist[i].out);
	}
	nhist = 0;
}

void home_note(const char *in, const char *out, bool err) { hist_add(in, out, err); }

/* ---- drawing ----------------------------------------------------------------------------- */

#define HIST_Y 19
#define HIST_ROWS 11
#define ED_Y 177

static const char *angle_name(void)
{
	return V->hset.angle == 1 ? "DEG" : V->hset.angle == 2 ? "GRD" : "RAD";
}

/* text cut to cols code points with an ellipsis */
static void cut(const char *s, char *out, int cols)
{
	int n = utf8_len(s);
	if (n <= cols) {
		strcpy(out, s);
		return;
	}
	int k = utf8_offset(s, cols - 1);
	memcpy(out, s, k);
	strcpy(out + k, "\xE2\x80\xA6");
}

static void draw_history(void)
{
	/* from the bottom: each entry takes 1 row (input and result fit) or 2 */
	int y = HIST_Y + HIST_ROWS * LINE_H;
	char a[COLS * 4 + 8], b[COLS * 4 + 8];
	for (int i = nhist - 1; i >= 0 && y > HIST_Y; i--) {
		int li = utf8_len(hist[i].in), lo = utf8_len(hist[i].out);
		int col = hist[i].err ? C_ERR : C_OUT;
		if (li + lo + 2 <= COLS) {
			y -= LINE_H;
			text_draw(FONT, 2, y, hist[i].in, C_BLACK, -1, -1);
			text_right(FONT, DWIDTH - 2, y, hist[i].out, col, -1);
		}
		else {
			y -= LINE_H;
			cut(hist[i].out, b, COLS);
			text_right(FONT, DWIDTH - 2, y, b, col, -1);
			if (y - LINE_H < HIST_Y)
				break;
			y -= LINE_H;
			cut(hist[i].in, a, COLS);
			text_draw(FONT, 2, y, a, C_BLACK, -1, -1);
		}
		dline(0, y - 1, DWIDTH - 1, y - 1, C_RGB(28, 28, 28));
	}
}

static void draw_home(void)
{
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 17, C_PANEL);
	text_draw(FONT, 6, 2, "HP PPL", C_TITLE, -1, -1);
	if (running)
		text_draw(FONT, 60, 2, "running... (AC/ON: break)", C_WHITE, -1, -1);
	char t[40];
	const char *m = ed_mode(&ed);
	snprintf(t, sizeof t, "%s%s%s", m, *m ? "  " : "", angle_name());
	text_right(FONT, DWIDTH - 6, 2, t, *m ? C_TITLE : C_LABEL, -1);
	draw_history();
	dline(0, ED_Y - 3, DWIDTH - 1, ED_Y - 3, C_LABEL);
	ed_draw(&ed, 2, ED_Y, COLS, 2, C_BLACK, C_WHITE);
	static const char *const sk[6] = { "Load", "Run", "Term", "Chars", "Setup", "A<>a" };
	static const char *const skl[6] = { "Load", "Run", "Term", "Chars", "Setup", "a<>A" };
	ui_softbar(ed.lower ? skl : sk);
}

void home_restore(void)
{
	if (disp == DISP_G0)
		g0_show();
	else if (disp == DISP_TERM)
		term_show(0);
	else {
		draw_home();
		scr_push_all();
	}
}

/* ---- running ----------------------------------------------------------------------------- */

static void error_screen(int e, const char *in)
{
	char text[600], src[200] = "";
	int k = snprintf(text, sizeof text, "%s", ppl_errmsg(e));
	if (V->errname[0] && (e == E_UNDEF || e == E_UNSUPPORTED))
		k += snprintf(text + k, sizeof text - k, ": %s", V->errname);
	if (V->errfn && V->errfn->prog) {
		k += snprintf(text + k, sizeof text - k, "\n\nin %s, function %s, line %d",
		              V->errfn->prog->name, V->errfn->name, V->errline);
		progs_line(V->errfn->prog->name, V->errline, src, sizeof src);
		if (src[0])
			k += snprintf(text + k, sizeof text - k, ":\n  %s", src);
	}
	else
		k += snprintf(text + k, sizeof text - k, "\n\nin: %s", in);
	ST->phase = PH_ERROR;
	ui_message("Error", text, "Press any key", C_ERR);
	ui_key();
}

static void run(const char *line)
{
	char out[200] = "";
	bool err = false;
	st_str(ST->last_in, line);
	ST->phase = PH_RUN;
	running = true;
	home_freeze = false;
	V->errname[0] = 0;
	io_reset();
	host_run_start();
	term_clear();
	panel_state = "Running";
	panel_what = line;
	disp = DISP_HOME;
	draw_home();
	scr_push_all();
	uint32_t t0 = clock_real_ms();
	value r;
	v_none(&r);
	int e = ppl_eval(V, line, &r);
	ST->run_ms = clock_real_ms() - t0;
	running = false;
	panel_state = e == E_BREAK ? "Break" : e && e != E_KILL ? "Error" : "Done";
	host_flush();
	ST->last_err = e;
	ST->last_errline = e ? V->errline : 0;
	if (!e) {
		value_text(V, &r, out, sizeof out, 1);
		v_decref(&r);
	}
	else if (e == E_KILL)
		out[0] = 0;
	else {
		snprintf(out, sizeof out, "%s", ppl_errmsg(e));
		err = e != E_BREAK;
	}
	st_str(ST->last_out, out);
	uint32_t used, peak, size;
	mem_stats(&used, &peak, &size);
	ST->arena_used = used, ST->arena_peak = peak, ST->arena_size = size;
	if (err)
		error_screen(e, line);
	else if (disp == DISP_TERM) {
		ST->phase = PH_TERM;
		term_view("Program ended. UP/DOWN scroll, any other key: Home");
	}
	else if (disp == DISP_G0 && home_freeze) {
		ST->phase = PH_FREEZE;
		clearevents();
		ui_key();
	}
	hist_add(line, out, err || e == E_BREAK);
	ST->runs++;
	disp = DISP_HOME;
	ST->phase = PH_HOME;
}

/* ---- the character catalog --------------------------------------------------------------- */

static const char *const catalog[] = {
	":=", "==", "\xE2\x89\xA0", "\xE2\x89\xA4", "\xE2\x89\xA5", "<", ">", "\xE2\x96\xB6",
	"{", "}", "[", "]", "(", ")", "\"", "'",
	":", ";", "_", "#", "!", "%", "\xC2\xB0", "&",
	"\xE2\x88\x9A", "\xCF\x80", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xBB\xC2\xB9", "\xE2\x85\x88", "e", "\xCE\xA3",
	"AND ", "OR ", "XOR ", "NOT ", "MOD ", "BEGIN ", "END;", "LOCAL ",
	"IF ", "THEN ", "ELSE ", "CASE ", "FOR ", "FROM ", "TO ", "STEP ",
	"DO ", "WHILE ", "REPEAT ", "UNTIL ", "RETURN ", "BREAK", "EXPORT ", "KILL",
	"PRINT(", "MSGBOX(", "INPUT(", "CHOOSE(", "GETKEY", "WAIT(", "MOUSE", "TICKS",
	"STRING(", "SIZE(", "MAKELIST(", "CONCAT(", "SORT(", "RANDOM", "RANDINT(", "ROUND(",
	"IP(", "FP(", "ABS(", "MAX(", "MIN(", "FLOOR(", "CEILING(", "\xCE\xA3LIST(",
	"RECT_P(", "LINE_P(", "PIXON_P(", "TEXTOUT_P(", "RGB(", "DRAWMENU(", "FREEZE", "ISKEYDOWN(",
	"HAngle", "HFormat", "HDigits", "Ans", "a", "b", "c", "x",
};
#define NCAT (int)(sizeof catalog / sizeof catalog[0])

static void chars(void)
{
	ST->phase = PH_CHARS;
	static int sel;
	const int cols = 8, rows = (NCAT + cols - 1) / cols;
	for (;;) {
		dclear(C_WHITE);
		drect(0, 0, DWIDTH - 1, 17, C_PANEL);
		text_draw(FONT, 6, 2, "Characters and keywords", C_TITLE, -1, -1);
		int top = sel / cols - 5;
		if (top < 0) top = 0;
		if (top > rows - 12) top = rows - 12 > 0 ? rows - 12 : 0;
		for (int r = 0; r < 12 && top + r < rows; r++)
			for (int c = 0; c < cols; c++) {
				int i = (top + r) * cols + c;
				if (i >= NCAT) break;
				int x = c * 49 + 2, y = 21 + r * 15;
				bool s = i == sel;
				if (s)
					drect(x, y - 1, x + 47, y + 13, C_SEL);
				text_draw(FONT, x + 2, y, catalog[i], s ? C_WHITE : C_BLACK, -1, 7);
			}
		static const char *const sk[6] = { "Insert", NULL, NULL, NULL, NULL, "Back" };
		ui_softbar(sk);
		scr_push_all();
		int k = ui_key();
		if (k == KEY_LEFT) sel = (sel + NCAT - 1) % NCAT;
		else if (k == KEY_RIGHT) sel = (sel + 1) % NCAT;
		else if (k == KEY_UP) sel = sel >= cols ? sel - cols : sel;
		else if (k == KEY_DOWN) sel = sel + cols < NCAT ? sel + cols : sel;
		else if (k == KEY_EXE || k == KEY_F1) {
			ed_insert(&ed, catalog[sel]);
			return;
		}
		else if (k == KEY_EXIT || k == KEY_F6 || k == KEY_OPTN)
			return;
	}
}

/* ---- the key map ------------------------------------------------------------------------- */

static void help(void)
{
	ST->phase = PH_HELP;
	for (int page = 0; page < 2;) {
		dclear(C_WHITE);
		drect(0, 0, DWIDTH - 1, 17, C_PANEL);
		if (page == 0) {
			text_draw(FONT, 6, 2, "Keys in programs: CG50 key, GETKEY code, Prime key", C_TITLE, -1, -1);
			for (int i = 0; i < keymap_count && i < 51; i++) {
				int x = 2 + (i / 17) * 132, y = 21 + 11 * (i % 17);
				char t[40];
				if (keymap[i].prime >= 0)
					snprintf(t, sizeof t, "%-6s%3d %.11s", keymap[i].cg_name, keymap[i].prime, keymap[i].prime_name);
				else
					snprintf(t, sizeof t, "%-6s    %.11s", keymap[i].cg_name, keymap[i].prime_name);
				text_draw(SMALL, x, y, t, C_BLACK, -1, 21);
			}
			text_draw(FONT, 6, DHEIGHT - 16, "EXE: next page   EXIT: back", C_FOOT, -1, -1);
		}
		else {
			text_draw(FONT, 6, 2, "Touch, pointer, views", C_TITLE, -1, -1);
			ui_wrap(6, 22, COLS - 2, 12,
			        "F1-F6 tap the six soft-menu buttons that DRAWMENU draws (MOUSE and WAIT(-1) see "
			        "a touch at y 230); GETKEY sees them as Prime keys 0 5 10 1 6 11.\n"
			        "OPTN: the pointer. Arrows move it (faster when held), EXE touches under it, EXE "
			        "held + arrows drags. OPTN again: arrows and EXE are keys again.\n"
			        "AC/ON stops the program (Break). Setup (F5 at Home) picks the view: Fit (240 rows "
			        "in 224) or 1:1 top/bottom.\n"
			        "Home: SHIFT/ALPHA as printed on the keys; SHIFT+ALPHA locks alpha; F6 lower case; "
			        "SHIFT+. =  SHIFT+-> :=  SHIFT+( <  SHIFT+) >  SHIFT+a b/c <=  SHIFT+S<>D >=",
			        C_BLACK);
			text_draw(FONT, 6, DHEIGHT - 16, "EXE/EXIT: back", C_FOOT, -1, -1);
		}
		scr_push_all();
		int k = ui_key();
		if (k == KEY_EXIT)
			return;
		page++;
	}
}

/* ---- setup ------------------------------------------------------------------------------- */

static void setup(void)
{
	ST->phase = PH_SETUP;
	int sel = 0;
	enum { S_VIEW, S_ANGLE, S_KEYS, S_CLEAR, S_N };
	for (;;) {
		dclear(C_WHITE);
		drect(0, 0, DWIDTH - 1, 17, C_PANEL);
		text_draw(FONT, 6, 2, "Setup", C_TITLE, -1, -1);
		for (int i = 0; i < S_N; i++) {
			char t[80];
			switch (i) {
			case S_VIEW: snprintf(t, sizeof t, "Graphics view:  %s   (LEFT/RIGHT)", view_names[view]); break;
			case S_ANGLE: snprintf(t, sizeof t, "Angle (HAngle): %s   (LEFT/RIGHT)",
			                       V->hset.angle == 1 ? "Degrees" : V->hset.angle == 2 ? "Gradians" : "Radians"); break;
			case S_KEYS: strcpy(t, "Key map, touch and pointer"); break;
			case S_CLEAR: strcpy(t, "Clear the history"); break;
			}
			int y = 26 + i * 18;
			if (i == sel)
				drect(2, y - 2, DWIDTH - 3, y + 13, C_SEL);
			text_draw(FONT, 8, y, t, i == sel ? C_WHITE : C_BLACK, -1, -1);
		}
		uint32_t used, peak, size;
		mem_stats(&used, &peak, &size);
		char m[120];
		snprintf(m, sizeof m, "Memory: %u KB used of %u KB (peak %u KB); VM %u KB",
		         (unsigned)(used >> 10), (unsigned)(size >> 10), (unsigned)(peak >> 10),
		         (unsigned)(sizeof(ppl_vm) >> 10));
		text_draw(FONT, 8, 120, m, C_LABEL, -1, -1);
		text_draw(FONT, 8, 140, "HP PPL for the fx-CG50 - GPL-3.0-or-later", C_LABEL, -1, -1);
		static const char *const sk[6] = { NULL, NULL, NULL, NULL, NULL, "Back" };
		ui_softbar(sk);
		scr_push_all();
		int k = ui_key();
		if (k == KEY_UP) sel = (sel + S_N - 1) % S_N;
		else if (k == KEY_DOWN) sel = (sel + 1) % S_N;
		else if (k == KEY_EXIT || k == KEY_F6 || k == KEY_F5)
			return;
		else if (sel == S_VIEW && (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_EXE)) {
			view = (view + (k == KEY_LEFT ? VIEW_COUNT - 1 : 1)) % VIEW_COUNT;
			ST->view = view;
		}
		else if (sel == S_ANGLE && (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_EXE)) {
			value v;
			v_int32(&v, (V->hset.angle + (k == KEY_LEFT ? 2 : 1)) % 3);
			vm_setting_set(V, "HAngle", &v);
		}
		else if (sel == S_KEYS && k == KEY_EXE)
			help();
		else if (sel == S_CLEAR && k == KEY_EXE) {
			hist_clear();
			return;
		}
	}
}

/* ---- the loop ---------------------------------------------------------------------------- */

void home_init(ppl_vm *vm)
{
	V = vm;
	ed_clear(&ed);
}

void home_loop(void)
{
	for (;;) {
		ST->phase = PH_HOME;
		disp = DISP_HOME;
		draw_home();
		scr_push_all();
		int k = ui_key();
		int r = ed_key(&ed, k);
		if (r == ED_EXE) {
			if (!ed.buf[0])
				continue;
			char line[ED_MAX];
			strcpy(line, ed.buf);
			ed_clear(&ed);
			recall = -1;
			run(line);
			continue;
		}
		if (r == ED_EXIT) {
			ed_clear(&ed);
			recall = -1;
			continue;
		}
		if (r != ED_UNUSED)
			continue;
		switch (k) {
		case KEY_UP:
			if (nhist && recall != 0) {
				recall = recall < 0 ? nhist - 1 : recall - 1;
				ed_set(&ed, hist[recall].in);
			}
			break;
		case KEY_DOWN:
			if (recall >= 0 && recall < nhist - 1)
				ed_set(&ed, hist[++recall].in);
			else {
				recall = -1;
				ed_clear(&ed);
			}
			break;
		case KEY_F1:
			progs_picker();
			break;
		case KEY_F2:
		case KEY_VARS: {
			char call[80];
			int go = 0;
			if (progs_functions(call, sizeof call, &go)) {
				if (go)
					run(call);
				else
					ed_insert(&ed, call);
			}
			break;
		}
		case KEY_F3:
			ST->phase = PH_TERM;
			term_view("The PRINT terminal. UP/DOWN scroll, any other key: Home");
			break;
		case KEY_F4:
		case KEY_OPTN:
			chars();
			break;
		case KEY_F5:
			setup();
			break;
		case KEY_F6:
			ed.lower = !ed.lower;
			break;
		case KEY_MENU:
			gint_osmenu();
			break;
		}
	}
}
