/* INPUT, CHOOSE and MSGBOX on the CG50 (dialogs.h). */
#include "dialogs.h"
#include "home.h"
#include "io.h"
#include "screen.h"
#include "stats.h"
#include "term.h"
#include "text.h"
#include "ui.h"

#include <gint/keyboard.h>
#include <stdlib.h>
#include <string.h>

void value_text(ppl_vm *V, const value *v, char *out, int n, int quote)
{
	sbuf b;
	sb_init(&b);
	v_to_text(V, &b, v, quote);
	int k = b.p ? (int)b.n : 0;
	if (k > n - 1) {
		k = n - 1;
		while (k > 0 && ((unsigned char)b.p[k] & 0xC0) == 0x80)
			k--;   /* don't cut a UTF-8 sequence */
	}
	if (k)
		memcpy(out, b.p, k);
	out[k] = 0;
	sb_free(&b);
}

/* a string value (or the i-th element of a list of them) as UTF-8 */
static void str_arg(ppl_vm *V, const value *v, int i, char *out, int n)
{
	out[0] = 0;
	if (!v)
		return;
	if (v->t == T_LIST) {
		if (i < 0 || (uint32_t)i >= v->u.l->n)
			return;
		v = &v->u.l->v[i];
	}
	else if (i > 0 && v->t != T_STR)
		return;
	if (v->t == T_STR) {
		int k = u16_to_utf8(NULL, v->u.s->c, v->u.s->len);
		char *tmp = malloc(k + 1);
		if (!tmp)
			return;
		u16_to_utf8(tmp, v->u.s->c, v->u.s->len);
		tmp[k] = 0;
		strncpy(out, tmp, n - 1);
		out[n - 1] = 0;
		free(tmp);
	}
	else
		value_text(V, v, out, n, 0);
}

/* what the program drew or printed so far is on the screen before a dialog covers it */
static void flush_screen(void)
{
	g0_flush(0);
	term_flush(0);
}

/* ---- INPUT ------------------------------------------------------------------------------- */

#define FMAX 24
enum { F_EDIT, F_CHECK, F_CHOOSE };
typedef struct {
	value ref, val;
	int kind, group;          /* F_CHECK: radio group size (1 = a plain check box) */
	int gstart, gsize;        /* the radio group this check box is in (gsize 1: none) */
	const value *choices;     /* F_CHOOSE: the list of items */
	char label[40], help[100];
} field_t;

static int fields_from(ppl_vm *V, const value *spec, field_t *f, int *n)
{
	(void)V;
	if (spec->t == T_VREF) {
		f[(*n)++].ref = *spec;
		return 0;
	}
	if (spec->t != T_LIST)
		return E_BADTYPE;
	for (uint32_t i = 0; i < spec->u.l->n && *n < FMAX; i++) {
		const value *e = &spec->u.l->v[i];
		field_t *x = &f[*n];
		if (e->t == T_VREF) {
			x->ref = *e;
			(*n)++;
		}
		else if (e->t == T_LIST && e->u.l->n >= 1 && e->u.l->v[0].t == T_VREF) {
			x->ref = e->u.l->v[0];
			if (e->u.l->n >= 2) {
				const value *k = &e->u.l->v[1];
				if (k->t == T_REAL || k->t == T_INT) {
					int32_t g = 1;
					to_index(k, &g);
					x->kind = F_CHECK;
					x->group = g < 1 ? 1 : g;
				}
				else if (k->t == T_LIST) {
					x->kind = F_CHOOSE;
					x->choices = k;
				}
			}
			(*n)++;
		}
		else
			return E_BADTYPE;
	}
	return 0;
}

static int truth(const value *v)
{
	int t = 0;
	v_truth(v, &t);
	return t;
}

static void field_text(ppl_vm *V, field_t *f, char *out, int n)
{
	if (f->kind == F_CHECK) {
		strcpy(out, truth(&f->val) ? "[x]" : "[ ]");
		return;
	}
	if (f->kind == F_CHOOSE) {
		int32_t k = 0;
		to_index(&f->val, &k);
		if (k >= 1 && (uint32_t)k <= f->choices->u.l->n)
			str_arg(V, f->choices, k - 1, out, n);
		else
			strcpy(out, "");
		return;
	}
	value_text(V, &f->val, out, n, 1);
}

#define ROWS_PER_PAGE 7

static void input_draw(ppl_vm *V, const char *title, field_t *f, int n, int sel, editor_t *ed,
                       bool editing, const char *err)
{
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 17, C_PANEL);
	text_draw(FONT, 6, 2, title, C_TITLE, -1, COLS - 2);
	int lw = 0;
	for (int i = 0; i < n; i++) {
		int w = text_width(FONT, f[i].label);
		if (w > lw) lw = w;
	}
	if (lw > 150) lw = 150;
	int page = sel / ROWS_PER_PAGE;
	for (int r = 0; r < ROWS_PER_PAGE; r++) {
		int i = page * ROWS_PER_PAGE + r;
		if (i >= n)
			break;
		int y = 26 + r * 22;
		int vx = 8 + lw + 6;
		text_draw(FONT, 8 + lw - text_width(FONT, f[i].label), y + 3, f[i].label, C_BLACK, -1, 25);
		bool s = i == sel;
		drect_border(vx, y, DWIDTH - 8, y + 18, s ? C_RGB(29, 30, 31) : C_WHITE, 1, s ? C_SEL : C_LABEL);
		int cols = (DWIDTH - 8 - vx - 6) / 6;
		if (s && editing)
			ed_draw(ed, vx + 3, y + 3, cols, 1, C_BLACK, C_RGB(29, 30, 31));
		else {
			char t[200];
			field_text(V, &f[i], t, sizeof t);
			text_draw(FONT, vx + 3, y + 3, t, C_BLACK, -1, cols);
		}
	}
	if (n > ROWS_PER_PAGE) {
		char pg[16];
		int np = (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
		pg[0] = '1' + page, pg[1] = '/', pg[2] = '0' + np, pg[3] = 0;
		text_right(FONT, DWIDTH - 6, 2, pg, C_LABEL, -1);
	}
	drect(0, 186, DWIDTH - 1, 204, C_BAR);
	if (err)
		text_draw(FONT, 6, 189, err, C_ERR, -1, COLS - 2);
	else
		text_draw(FONT, 6, 189, f[sel].help, C_BLACK, -1, COLS - 2);
	static const char *const sk[6] = { "Cancel", "Reset", NULL, NULL, "Edit", "OK" };
	static const char *const ske[6] = { "Cancel", NULL, NULL, NULL, NULL, "OK" };
	ui_softbar(editing ? ske : sk);
	const char *m = ed_mode(ed);
	if (*m)
		text_right(FONT, DWIDTH - 40, 2, m, C_WHITE, -1);
	scr_push_all();
}

int dlg_input(ppl_vm *V, int argc, value *a, value *out)
{
	static field_t f[FMAX];
	static editor_t ed;
	int n = 0;
	memset(f, 0, sizeof f);
	int st = fields_from(V, &a[0], f, &n);
	if (st)
		return st;
	if (!n)
		return E_BADVAL;
	for (int i = 0; i < n; i++) {
		f[i].gstart = i, f[i].gsize = 1;
	}
	for (int i = 0; i < n; i++)
		if (f[i].kind == F_CHECK && f[i].group > 1 && f[i].gsize == 1) {
			int g = f[i].group;
			for (int j = i; j < n && j < i + g; j++)
				if (f[j].kind == F_CHECK)
					f[j].gstart = i, f[j].gsize = (i + g <= n ? g : n - i);
			i += g - 1;
		}
	char title[80];
	str_arg(V, argc > 1 ? &a[1] : NULL, 0, title, sizeof title);
	if (!title[0])
		strcpy(title, "Input");
	for (int i = 0; i < n; i++) {
		if (argc > 2)
			str_arg(V, &a[2], a[2].t == T_LIST ? i : (i ? -1 : 0), f[i].label, sizeof f[i].label);
		if (argc > 3)
			str_arg(V, &a[3], a[3].t == T_LIST ? i : (i ? -1 : 0), f[i].help, sizeof f[i].help);
		if (argc > 5) {
			const value *iv = a[5].t == T_LIST && n > 1 ? (uint32_t)i < a[5].u.l->n ? &a[5].u.l->v[i] : NULL : &a[5];
			if (iv) v_copy(&f[i].val, iv);
			else vm_load_ref(V, &f[i].ref, &f[i].val);
		}
		else
			vm_load_ref(V, &f[i].ref, &f[i].val);
	}
	flush_screen();
	ST->phase = PH_INPUT;
	int sel = 0, rc = -1;
	bool editing = false;
	const char *err = NULL;
	ed_clear(&ed);
	ed.shift = ed.alpha = ed.lock = false;
	while (rc < 0) {
		input_draw(V, title, f, n, sel, &ed, editing, err);
		int k = ui_key();
		err = NULL;
		if (k == KEY_ACON && !editing) {
			rc = -2;
			break;
		}
		if (k == KEY_F1 || (k == KEY_EXIT && !editing)) {
			rc = 0;
			break;
		}
		if (editing) {
			int r = ed_key(&ed, k);
			if (r == ED_EXIT) {
				editing = false;
				continue;
			}
			if (r == ED_EXE || k == KEY_F6 || ((k == KEY_UP || k == KEY_DOWN) && r == ED_UNUSED)) {
				value v;
				if (!ed.buf[0]) {
					editing = false;
					continue;
				}
				int e = vm_eval_string(V, ed.buf, strlen(ed.buf), &v);
				if (e == E_BREAK) { rc = -2; break; }
				if (e) {
					err = ppl_errmsg(e);
					continue;
				}
				v_set(&f[sel].val, &v);
				v_decref(&v);
				editing = false;
				if (k == KEY_F6) { rc = 1; break; }
				if (k == KEY_UP) sel = sel > 0 ? sel - 1 : 0;
				else if (sel < n - 1) sel++;
			}
			continue;
		}
		if (k == KEY_F6) { rc = 1; break; }
		if (k == KEY_UP) { if (sel > 0) sel--; continue; }
		if (k == KEY_DOWN) { if (sel < n - 1) sel++; continue; }
		if (k == KEY_F2 && argc > 4) { /* Reset */
			for (int i = 0; i < n; i++) {
				const value *rv = a[4].t == T_LIST && n > 1 ? (uint32_t)i < a[4].u.l->n ? &a[4].u.l->v[i] : NULL : &a[4];
				if (rv) v_set(&f[i].val, rv);
			}
			continue;
		}
		field_t *x = &f[sel];
		if (x->kind == F_CHECK) {
			if (k == KEY_EXE || k == KEY_LEFT || k == KEY_RIGHT) {
				value t;
				v_int32(&t, !truth(&x->val));
				if (x->gsize > 1) { /* a radio group: this one on, the others off */
					v_int32(&t, 1);
					for (int i = x->gstart; i < x->gstart + x->gsize; i++) {
						value z;
						v_int32(&z, 0);
						v_set(&f[i].val, &z);
					}
				}
				v_set(&x->val, &t);
			}
			continue;
		}
		if (x->kind == F_CHOOSE) {
			int32_t c = 0, m = x->choices->u.l->n;
			to_index(&x->val, &c);
			if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_EXE) {
				c = k == KEY_LEFT ? c - 1 : c + 1;
				if (c < 1) c = m;
				if (c > m) c = 1;
				value t;
				v_int32(&t, c);
				v_set(&x->val, &t);
			}
			continue;
		}
		if (k == KEY_EXE) {
			if (sel == n - 1) { rc = 1; break; }
			sel++;
			continue;
		}
		if (k == KEY_F5 || k == KEY_DEL || k == KEY_RIGHT || k == KEY_LEFT) {
			/* edit the current value */
			char t[ED_MAX];
			value_text(V, &x->val, t, sizeof t, 1);
			ed_set(&ed, k == KEY_DEL ? "" : t);
			editing = true;
			continue;
		}
		/* any other key starts a new value */
		ed_clear(&ed);
		int r = ed_key(&ed, k);
		if (r == ED_CHANGED && (ed.buf[0] || ed.shift || ed.alpha))
			editing = true;
	}
	if (rc == 1)
		for (int i = 0; i < n; i++) {
			int e = vm_store_ref(V, &f[i].ref, &f[i].val);
			if (e) { rc = e; break; }
		}
	for (int i = 0; i < n; i++)
		v_decref(&f[i].val);
	home_restore();
	ST->phase = PH_RUN;
	if (rc == -2) {
		io_clear_break();
		return E_BREAK;
	}
	if (rc > 1)
		return rc;
	v_int32(out, rc);
	return 0;
}

/* ---- CHOOSE ------------------------------------------------------------------------------ */

int dlg_choose(ppl_vm *V, int argc, value *a, value *out)
{
	if (a[0].t != T_VREF)
		return E_BADTYPE;
	char title[80];
	str_arg(V, &a[1], 0, title, sizeof title);
	/* the items: the remaining arguments, or one list */
	const value *items = NULL;
	int n = argc - 2;
	if (argc == 3 && a[2].t == T_LIST) {
		items = &a[2];
		n = a[2].u.l->n;
	}
	if (n <= 0)
		return E_BADVAL;
	value cur;
	vm_load_ref(V, &a[0], &cur);
	int32_t sel = 1;
	to_index(&cur, &sel);
	v_decref(&cur);
	if (sel < 1 || sel > n)
		sel = 1;
	sel--;
	flush_screen();
	ST->phase = PH_CHOOSE;
	int rows = n < 10 ? n : 10, top = 0, rc = -1;
	int w = text_width(FONT, title) + 24;
	for (int i = 0; i < n; i++) {
		char t[80];
		str_arg(V, items ? items : &a[2 + i], items ? i : 0, t, sizeof t);
		int tw = text_width(FONT, t) + 40;
		if (tw > w) w = tw;
	}
	if (w > DWIDTH - 20) w = DWIDTH - 20;
	if (w < 160) w = 160;
	int x0 = (DWIDTH - w) / 2, h = 26 + rows * 16, y0 = (206 - h) / 2;
	while (rc < 0) {
		if (sel < top) top = sel;
		if (sel >= top + rows) top = sel - rows + 1;
		int y = ui_box(x0, y0, x0 + w, y0 + h, title);
		for (int r = 0; r < rows; r++) {
			int i = top + r;
			char t[80];
			str_arg(V, items ? items : &a[2 + i], items ? i : 0, t, sizeof t);
			bool s = i == sel;
			drect(x0 + 4, y + r * 16 - 1, x0 + w - 4, y + r * 16 + 14, s ? C_SEL : C_WHITE);
			char num[4] = { 0 };
			if (i < 9) num[0] = '1' + i;
			text_draw(FONT, x0 + 8, y + r * 16, num, s ? C_TITLE : C_LABEL, -1, -1);
			text_draw(FONT, x0 + 24, y + r * 16, t, s ? C_WHITE : C_BLACK, -1, (w - 30) / 6);
		}
		static const char *const sk[6] = { "Cancel", NULL, NULL, NULL, NULL, "OK" };
		ui_softbar(sk);
		scr_push_all();
		int k = ui_key();
		if (k == KEY_UP) sel = (sel + n - 1) % n;
		else if (k == KEY_DOWN) sel = (sel + 1) % n;
		else if (k == KEY_EXE || k == KEY_F6) rc = sel + 1;
		else if (k == KEY_EXIT || k == KEY_F1) rc = 0;
		else if (k == KEY_ACON) rc = -2;
		else {
			static const int digits[9] = { KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
			for (int i = 0; i < 9 && i < n; i++)
				if (k == digits[i]) rc = i + 1;
		}
	}
	home_restore();
	ST->phase = PH_RUN;
	if (rc == -2) {
		io_clear_break();
		return E_BREAK;
	}
	value v;
	v_int32(&v, rc);
	int e = vm_store_ref(V, &a[0], &v);
	if (e)
		return e;
	v_int32(out, rc != 0);
	return 0;
}

/* ---- MSGBOX ------------------------------------------------------------------------------ */

int dlg_msgbox(ppl_vm *V, int argc, value *a, value *out)
{
	char text[600];
	if (a[0].t == T_STR)
		str_arg(V, &a[0], 0, text, sizeof text);
	else
		value_text(V, &a[0], text, sizeof text, 0);
	bool okc = argc > 1 && truth(&a[1]);
	flush_screen();
	ST->phase = PH_MSGBOX;
	/* the box: as wide as the text, up to the screen, as many rows as it wraps to */
	int len = utf8_len(text), cols = len < 20 ? 20 : len > 60 ? 60 : len;
	int w = cols * 6 + 20, rows = 1;
	{
		/* count the wrapped rows by drawing off-screen? simpler: estimate */
		rows = (len + cols - 1) / cols;
		for (const char *p = text; *p; p++)
			if (*p == '\n') rows++;
		if (rows > 10) rows = 10;
		if (rows < 1) rows = 1;
	}
	int h = 30 + rows * LINE_H, x0 = (DWIDTH - w) / 2, y0 = (206 - h) / 2;
	int y = ui_box(x0, y0, x0 + w, y0 + h, "");
	ui_wrap(x0 + 10, y + 2, cols, rows, text, C_BLACK);
	static const char *const sk1[6] = { NULL, NULL, NULL, NULL, NULL, "OK" };
	static const char *const sk2[6] = { "Cancel", NULL, NULL, NULL, NULL, "OK" };
	ui_softbar(okc ? sk2 : sk1);
	scr_push_all();
	int rc = -1;
	while (rc < 0) {
		int k = ui_key();
		if (k == KEY_EXE || k == KEY_F6) rc = 1;
		else if (k == KEY_EXIT || k == KEY_F1) rc = okc ? 0 : 1;
		else if (k == KEY_ACON) rc = 2;
	}
	home_restore();
	ST->phase = PH_RUN;
	if (rc == 2) {
		io_clear_break();
		return E_BREAK;
	}
	v_int32(out, rc);
	return 0;
}
