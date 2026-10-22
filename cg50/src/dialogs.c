/* INPUT, CHOOSE and MSGBOX on the CG50 (dialogs.h). */
#include "dialogs.h"
#include "parse.h"
#include "home.h"
#include "host.h"
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
	sb_drop_nul(&b);
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
	host_flush();
}

/* ---- INPUT ------------------------------------------------------------------------------- */

#define FMAX 40
#define ROWS_PER_PAGE 7          /* the Prime: at most seven lines of controls per page */
enum { F_EDIT, F_CHECK, F_CHOOSE };
typedef struct {
	value ref, val;
	int kind, group;          /* F_CHECK: radio group size (1 = a plain check box) */
	int gstart, gsize;        /* the radio group this check box is in (gsize 1: none) */
	const value *choices;     /* F_CHOOSE: the list of items */
	bool str_only;            /* F_EDIT with allowed types [2]: text without quotes */
	bool sym_only;            /* F_EDIT with allowed types [8]: kept as an expression */
	bool has_pos;             /* {x%, width%, line} given */
	int pos[3];
	int line, x, w, lx0;      /* layout: line (0..), field box x and width, label's left limit */
	char label[48], help[100];
} field_t;

/* allowed types [t1, t2, ...]: only type t (2 strings, 8 expressions)? */
static bool only_type(const value *m, int32_t want)
{
	if (m->t != T_MAT || !m->u.m)
		return false;
	int k = m->u.m->vec ? m->u.m->cols : m->u.m->rows * m->u.m->cols;
	if (k < 1)
		return false;
	for (int i = 0; i < k; i++) {
		int32_t t = -1;
		if (to_index(&m->u.m->v[i], &t) || t != want)
			return false;
	}
	return true;
}

/* a symbolic value 'text' after checking that text parses */
static int sym_from_text(const char *t, value *out)
{
	parser P;
	value tv;
	psym *y;
	parser_init(&P, t, strlen(t));
	parse_home(&P);
	int bad = P.err;
	parser_free(&P);
	if (bad)
		return E_SYNTAX;
	if (v_str_utf8(&tv, t, strlen(t)))
		return E_MEM;
	if (!(y = malloc(sizeof *y))) {
		v_decref(&tv);
		return E_MEM;
	}
	y->h.rc = 1;
	y->text = tv.u.s;
	v_none(out);
	out->t = T_SYM;
	out->u.y = y;
	return 0;
}

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
				else if (k->t == T_MAT) {
					x->str_only = only_type(k, T_STR);
					x->sym_only = only_type(k, T_SYM);
				}
			}
			/* {x%, width%, line} */
			if (e->u.l->n >= 3 && e->u.l->v[2].t == T_LIST && e->u.l->v[2].u.l->n >= 3) {
				const value *p = e->u.l->v[2].u.l->v;
				x->has_pos = true;
				for (int j = 0; j < 3; j++) {
					int32_t q = 0;
					to_index(&p[j], &q);
					x->pos[j] = q < 0 ? 0 : q > 1000 ? 1000 : q;
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
	if (f->str_only && f->val.t == T_STR)
		str_arg(V, &f->val, 0, out, n);
	else
		value_text(V, &f->val, out, n, !f->str_only);
}

/* where each field goes: with every pos given, {x%, width%, line} of the screen width (the
   Prime's 320 pixels become our 396); else one field per line, labels in a column */
static void input_layout(field_t *f, int n)
{
	bool pos = true;
	for (int i = 0; i < n; i++)
		if (!f[i].has_pos)
			pos = false;   /* the Prime wants pos for all fields or none */
	if (!pos) {
		int lw = 0;
		for (int i = 0; i < n; i++) {
			int w = text_width(FONT, f[i].label);
			if (w > lw) lw = w;
		}
		if (lw > 150) lw = 150;
		for (int i = 0; i < n; i++) {
			f[i].line = i;
			f[i].x = 8 + lw + 6;
			f[i].w = DWIDTH - 8 - f[i].x;
			f[i].lx0 = 8;
		}
		return;
	}
	for (int i = 0; i < n; i++) {
		int x = f[i].pos[0] * DWIDTH / 100, w = f[i].pos[1] * DWIDTH / 100;
		if (x < 2) x = 2;
		if (x > DWIDTH - 6) x = DWIDTH - 6;
		if (x + w > DWIDTH - 4) w = DWIDTH - 4 - x;
		f[i].line = f[i].pos[2], f[i].x = x, f[i].w = w;
	}
	/* a label may use the room between the field before it on its line and its field */
	for (int i = 0; i < n; i++) {
		int l = 4;
		for (int j = 0; j < n; j++)
			if (j != i && f[j].line == f[i].line && f[j].x < f[i].x && f[j].x + f[j].w + 4 > l)
				l = f[j].x + f[j].w + 4;
		f[i].lx0 = l;
	}
}

static void input_draw(ppl_vm *V, const char *title, field_t *f, int n, int sel, editor_t *ed,
                       bool editing, const char *err, int page, int npages)
{
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 17, C_PANEL);
	text_draw(FONT, 6, 2, title, C_TITLE, -1, COLS - 10);
	for (int i = 0; i < n; i++) {
		if (f[i].line / ROWS_PER_PAGE != page)
			continue;
		int y = 26 + (f[i].line % ROWS_PER_PAGE) * 22;
		int vx = f[i].x, w = f[i].w;
		/* the label ends just before its field; too long, it shows its start, clipped */
		if (f[i].label[0]) {
			int room = vx - 6 - f[i].lx0, lw = text_width(FONT, f[i].label);
			if (lw <= room)
				text_draw(FONT, vx - 6 - lw, y + 3, f[i].label, C_BLACK, -1, -1);
			else if (room >= 6)
				text_draw(FONT, f[i].lx0, y + 3, f[i].label, C_BLACK, -1, room / 6);
		}
		bool s = i == sel;
		if (w < 4) {               /* a zero-width field: only a mark when selected */
			if (s)
				drect(vx, y, vx + 3, y + 18, C_SEL);
			continue;
		}
		drect_border(vx, y, vx + w, y + 18, s ? C_RGB(29, 30, 31) : C_WHITE, 1, s ? C_SEL : C_LABEL);
		int cols = (w - 6) / 6;
		if (cols < 1) cols = 1;
		if (s && editing)
			ed_draw(ed, vx + 3, y + 3, cols, 1, C_BLACK, C_RGB(29, 30, 31));
		else {
			char t[200];
			field_text(V, &f[i], t, sizeof t);
			text_draw(FONT, vx + 3, y + 3, t, C_BLACK, -1, cols);
		}
	}
	if (npages > 1) {
		char pg[16];
		pg[0] = '1' + page, pg[1] = '/', pg[2] = '0' + npages, pg[3] = 0;
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

static int input_dialog(ppl_vm *V, int argc, value *a, value *out)
{
	static field_t f[FMAX];
	static editor_t ed;
	static int ord[FMAX];        /* the fields in reading order (line, then x) */
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
	input_layout(f, n);
	int npages = 1;
	for (int i = 0; i < n; i++) {
		ord[i] = i;
		if (f[i].line / ROWS_PER_PAGE + 1 > npages)
			npages = f[i].line / ROWS_PER_PAGE + 1;
	}
	if (npages > 9) npages = 9;
	for (int i = 1; i < n; i++)   /* insertion sort: stable */
		for (int j = i; j > 0; j--) {
			field_t *p = &f[ord[j - 1]], *q = &f[ord[j]];
			if (p->line < q->line || (p->line == q->line && p->x <= q->x))
				break;
			int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t;
		}
	flush_screen();
	ST->phase = PH_INPUT;
	int pi = 0, rc = -1;       /* pi: position of the selected field in ord */
	bool editing = false;
	const char *err = NULL;
	ed_clear(&ed);
	ed.shift = ed.alpha = ed.lock = false;
	while (rc < 0) {
		int sel = ord[pi];
		int page = f[sel].line / ROWS_PER_PAGE;
		if (page >= npages) page = npages - 1;
		/* the title: one string, or a list of titles, one per page */
		char title[80];
		str_arg(V, argc > 1 ? &a[1] : NULL, argc > 1 && a[1].t == T_LIST ? page : 0, title, sizeof title);
		if (!title[0] && argc > 1 && a[1].t == T_LIST)
			str_arg(V, &a[1], 0, title, sizeof title);
		if (!title[0])
			strcpy(title, "Input");
		input_draw(V, title, f, n, sel, &ed, editing, err, page, npages);
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
				if (f[sel].str_only) {
					/* a string-only field: the text as typed, without quotes */
					if (v_str_utf8(&v, ed.buf, strlen(ed.buf))) {
						err = ppl_errmsg(E_MEM);
						continue;
					}
				}
				else if (f[sel].sym_only) {
					/* an expression field (simprule's {f,[8]}): what is typed, not its value */
					if (!ed.buf[0]) {
						editing = false;
						continue;
					}
					int e = sym_from_text(ed.buf, &v);
					if (e) {
						err = ppl_errmsg(e);
						continue;
					}
				}
				else {
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
				}
				v_set(&f[sel].val, &v);
				v_decref(&v);
				editing = false;
				if (k == KEY_F6) { rc = 1; break; }
				if (k == KEY_UP) pi = pi > 0 ? pi - 1 : 0;
				else if (pi < n - 1) pi++;
			}
			continue;
		}
		if (k == KEY_F6) { rc = 1; break; }
		if (k == KEY_UP) { if (pi > 0) pi--; continue; }
		if (k == KEY_DOWN) { if (pi < n - 1) pi++; continue; }
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
			if (pi == n - 1) { rc = 1; break; }
			pi++;
			continue;
		}
		if (k == KEY_F5 || k == KEY_DEL || k == KEY_RIGHT || k == KEY_LEFT) {
			/* edit the current value */
			char t[ED_MAX];
			field_text(V, x, t, sizeof t);
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

static int choose_dialog(ppl_vm *V, int argc, value *a, value *out)
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

static int msgbox_dialog(ppl_vm *V, int argc, value *a, value *out)
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

/* a dialog takes the touches that were waiting and those made while it was up (the Prime:
 * dfx's menu tap, read by MOUSE, must not end the WAIT(-1) after its INPUT) */
int dlg_input(ppl_vm *V, int argc, value *a, value *out)
{
	int e = input_dialog(V, argc, a, out);
	io_flush_touches();
	return e;
}

int dlg_choose(ppl_vm *V, int argc, value *a, value *out)
{
	int e = choose_dialog(V, argc, a, out);
	io_flush_touches();
	return e;
}

int dlg_msgbox(ppl_vm *V, int argc, value *a, value *out)
{
	int e = msgbox_dialog(V, argc, a, out);
	io_flush_touches();
	return e;
}

/* ---- EDITMAT / EDITLIST ------------------------------------------------------------------ */

#define EM_CW 10                  /* a cell's width in characters */
#define EM_Y0 36                  /* the first row */
#define EM_ROWS 10                /* rows on the screen */

typedef struct {
	int rows, cols;               /* cols 1 for a list */
	bool list;
	value *c;                     /* rows x cols cells */
} grid_t;

static value *cell(grid_t *g, int r, int c) { return &g->c[r * g->cols + c]; }

/* r x c cells, the old ones kept, new ones 0 */
static int grid_resize(grid_t *g, int rows, int cols)
{
	value *n = malloc(sizeof(value) * (rows * cols > 0 ? rows * cols : 1));
	if (!n)
		return E_MEM;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++) {
			value *d = &n[r * cols + c];
			if (r < g->rows && c < g->cols)
				*d = *cell(g, r, c);
			else
				v_int32(d, 0);
		}
	for (int r = 0; r < g->rows; r++)
		for (int c = 0; c < g->cols; c++)
			if (r >= rows || c >= cols)
				v_decref(cell(g, r, c));
	free(g->c);
	g->c = n, g->rows = rows, g->cols = cols;
	return 0;
}

static void grid_free(grid_t *g)
{
	for (int i = 0; i < g->rows * g->cols; i++)
		v_decref(&g->c[i]);
	free(g->c);
	g->c = NULL;
}

/* a row label, a column label: the program's, or the index */
static void grid_label(ppl_vm *V, const value *labels, int i, char *out, int n)
{
	out[0] = 0;
	if (labels && labels->t == T_LIST && (uint32_t)i < labels->u.l->n)
		str_arg(V, labels, i, out, n);
	if (!out[0] && !(labels && labels->t == T_LIST && labels->u.l->n)) {
		int k = i + 1, p = 0;
		char t[8];
		do t[p++] = '0' + k % 10; while ((k /= 10) && p < 7);
		for (int j = 0; j < p; j++)
			out[j] = t[p - 1 - j];
		out[p] = 0;
	}
}

static void grid_draw(ppl_vm *V, grid_t *g, const char *title, const value *rl, const value *cl,
                      int cr, int cc, int r0, int c0, int lw, int ncol, editor_t *ed, bool editing,
                      const char *err, bool ro)
{
	char t[200];
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 17, C_PANEL);
	text_draw(FONT, 6, 2, title, C_TITLE, -1, COLS - 10);
	/* the column labels; the phantom row and column (one past the end) take a new value */
	int cw = EM_CW * 6 + 4;
	drect(0, 19, DWIDTH - 1, EM_Y0 - 3, C_BAR);
	for (int j = 0; j < ncol && c0 + j <= g->cols - (g->list ? 1 : 0); j++) {
		int x = 4 + lw + j * cw;
		if (c0 + j < g->cols) {
			grid_label(V, cl, c0 + j, t, sizeof t);
			text_draw(FONT, x + 2, 20, t, C_BLACK, -1, EM_CW);
		}
	}
	for (int i = 0; i < EM_ROWS && r0 + i <= g->rows; i++) {
		int y = EM_Y0 + i * LINE_H, r = r0 + i;
		if (r < g->rows) {
			grid_label(V, rl, r, t, sizeof t);
			text_draw(FONT, 2, y, t, C_LABEL, -1, lw / 6);
		}
		for (int j = 0; j < ncol; j++) {
			int c = c0 + j, x = 4 + lw + j * cw;
			if (c > g->cols - (g->list ? 1 : 0) || (r == g->rows && c == g->cols))
				break;
			bool s = r == cr && c == cc;
			if (s)
				drect(x, y - 1, x + cw - 3, y + LINE_H - 2, editing ? C_RGB(29, 30, 31) : C_SEL);
			if (r < g->rows && c < g->cols) {
				value_text(V, cell(g, r, c), t, sizeof t, 1);
				text_right(FONT, x + cw - 5, y, t, s && !editing ? C_WHITE : C_BLACK, -1);
			}
		}
	}
	drect(0, 186, DWIDTH - 1, 204, C_BAR);
	if (editing)
		ed_draw(ed, 6, 189, COLS - 2, 1, C_BLACK, C_BAR);
	else if (err)
		text_draw(FONT, 6, 189, err, C_ERR, -1, COLS - 2);
	else if (cr < g->rows && cc < g->cols) {
		value_text(V, cell(g, cr, cc), t, sizeof t, 1);
		text_draw(FONT, 6, 189, t, C_BLACK, -1, COLS - 2);
	}
	static const char *const sk[6] = { "Edit", "Ins", "Del", NULL, NULL, "OK" };
	static const char *const skr[6] = { NULL, NULL, NULL, NULL, NULL, "OK" };
	static const char *const ske[6] = { "Cancel", NULL, NULL, NULL, NULL, "OK" };
	ui_softbar(editing ? ske : ro ? skr : sk);
	const char *m = ed_mode(ed);
	if (editing && *m)
		text_right(FONT, DWIDTH - 40, 2, m, C_WHITE, -1);
	scr_push_all();
}

static int edit_grid(ppl_vm *V, int argc, value *a, value *out, bool list)
{
	static editor_t ed;
	grid_t g = { 0, 0, list, NULL };
	const value *src = &a[0], *rl = NULL, *cl = NULL;
	char title[80];
	bool ro = argc > 2 && truth(&a[2]);
	int st = 0;
	/* the cells */
	if (list) {
		if (src->t == T_LIST) {
			if ((st = grid_resize(&g, src->u.l->n, 1)))
				return st;
			for (uint32_t i = 0; i < src->u.l->n; i++)
				v_set(cell(&g, i, 0), &src->u.l->v[i]);
		}
		else if (src->t == T_MAT && src->u.m->vec) {
			if ((st = grid_resize(&g, src->u.m->cols, 1)))
				return st;
			for (int i = 0; i < src->u.m->cols; i++)
				*cell(&g, i, 0) = src->u.m->v[i];
		}
		else
			return E_BADTYPE;
	}
	else {
		if (src->t != T_MAT)
			return E_BADTYPE;
		const pmat *m = src->u.m;
		int rows = m->vec ? 1 : m->rows;
		if ((st = grid_resize(&g, rows, m->cols)))
			return st;
		for (int i = 0; i < rows * m->cols; i++)
			g.c[i] = m->v[i];
	}
	/* the title, and the labels of {title, row labels, column labels} */
	str_arg(V, argc > 1 ? &a[1] : NULL, 0, title, sizeof title);
	if (argc > 1 && a[1].t == T_LIST) {
		if (a[1].u.l->n > 1) rl = &a[1].u.l->v[1];
		if (a[1].u.l->n > 2) cl = &a[1].u.l->v[2];
	}
	if (!title[0])
		strcpy(title, list ? "List" : "Matrix");
	/* the row label column: as wide as the widest label (3 to 8 characters) */
	int lw = 3;
	for (int i = 0; i < g.rows + 1; i++) {
		char t[40];
		grid_label(V, rl, i, t, sizeof t);
		int w = utf8_len(t);
		if (w > lw) lw = w;
	}
	if (lw > 8) lw = 8;
	lw *= 6;
	int ncol = (DWIDTH - 4 - lw) / (EM_CW * 6 + 4);
	if (ncol < 1) ncol = 1;
	flush_screen();
	ST->phase = PH_EDIT;
	int cr = 0, cc = 0, r0 = 0, c0 = 0, rc = -1;
	bool editing = false;
	const char *err = NULL;
	ed_clear(&ed);
	ed.shift = ed.alpha = ed.lock = false;
	while (rc < 0) {
		/* the cursor may stand one past the last row or column: a value there adds it */
		int maxr = g.rows, maxc = list ? 0 : g.cols;
		if (cr > maxr) cr = maxr;
		if (cc > maxc) cc = maxc;
		if (cr == g.rows && cc == g.cols && !list && g.rows) cc = g.cols - 1;
		if (cr < r0) r0 = cr;
		if (cr >= r0 + EM_ROWS) r0 = cr - EM_ROWS + 1;
		if (cc < c0) c0 = cc;
		if (cc >= c0 + ncol) c0 = cc - ncol + 1;
		grid_draw(V, &g, title, rl, cl, cr, cc, r0, c0, lw, ncol, &ed, editing, err, ro);
		int k = ui_key();
		err = NULL;
		if (k == KEY_ACON && !editing) {
			rc = -2;
			break;
		}
		if (editing) {
			int r = ed_key(&ed, k);
			if (r == ED_EXIT || k == KEY_F1) {
				editing = false;
				continue;
			}
			if (r == ED_EXE || k == KEY_F6) {
				value v;
				if (!ed.buf[0]) {
					editing = false;
					continue;
				}
				int e = vm_eval_string(V, ed.buf, strlen(ed.buf), &v);
				if (e == E_BREAK) { rc = -2; break; }
				if (!e && !list) {
					/* a matrix holds numbers */
					if (v.t == T_INT) { real x; to_real(&v, &x); v_real(&v, &x); }
					else if (v.t != T_REAL && v.t != T_CPLX) { v_decref(&v); e = E_BADTYPE; }
				}
				if (e) {
					err = ppl_errmsg(e);
					continue;
				}
				if (cr >= g.rows || cc >= g.cols) {
					e = grid_resize(&g, cr >= g.rows ? cr + 1 : g.rows, cc >= g.cols ? cc + 1 : g.cols);
					if (e) { v_decref(&v); err = ppl_errmsg(e); continue; }
				}
				v_set(cell(&g, cr, cc), &v);
				v_decref(&v);
				editing = false;
				if (k == KEY_F6) { rc = 1; break; }
				/* a list goes down; a matrix right, then to the next row */
				if (list) cr++;
				else if (cc + 1 < g.cols) cc++;
				else cc = 0, cr++;
			}
			continue;
		}
		if (k == KEY_F6 || k == KEY_EXIT) { rc = 1; break; }
		if (k == KEY_UP) { if (cr > 0) cr--; continue; }
		if (k == KEY_DOWN) { cr++; continue; }
		if (k == KEY_LEFT) { if (cc > 0) cc--; continue; }
		if (k == KEY_RIGHT) { cc++; continue; }
		if (ro)
			continue;
		if (k == KEY_F2 && cr < g.rows) {
			/* Ins: a row of zeros before the cursor's */
			if ((st = grid_resize(&g, g.rows + 1, g.cols ? g.cols : 1)))
				break;
			for (int r = g.rows - 1; r > cr; r--)
				for (int c = 0; c < g.cols; c++) {
					value t = *cell(&g, r, c);
					*cell(&g, r, c) = *cell(&g, r - 1, c);
					*cell(&g, r - 1, c) = t;
				}
			continue;
		}
		if ((k == KEY_F3 || k == KEY_DEL) && cr < g.rows && (list || g.rows > 1)) {
			/* Del: the cursor's row */
			for (int r = cr; r + 1 < g.rows; r++)
				for (int c = 0; c < g.cols; c++) {
					value t = *cell(&g, r, c);
					*cell(&g, r, c) = *cell(&g, r + 1, c);
					*cell(&g, r + 1, c) = t;
				}
			if ((st = grid_resize(&g, g.rows - 1, g.cols)))
				break;
			continue;
		}
		ed_clear(&ed);
		if (k == KEY_F1 || k == KEY_EXE) {
			/* Edit: the current value */
			char t[ED_MAX];
			if (cr < g.rows && cc < g.cols) {
				value_text(V, cell(&g, cr, cc), t, sizeof t, 1);
				ed_set(&ed, t);
			}
			editing = true;
			continue;
		}
		/* any other key starts a new value with that key */
		int r = ed_key(&ed, k);
		if (r != ED_UNUSED && r != ED_EXIT)
			editing = true;
	}
	home_restore();
	ST->phase = PH_RUN;
	if (rc == -2) {
		grid_free(&g);
		io_clear_break();
		return E_BREAK;
	}
	if (!st) {
		/* the result: a list, or a matrix (a vector stays one while it has one row) */
		if (list) {
			if (!(st = v_list(out, g.rows)))
				for (int i = 0; i < g.rows; i++)
					v_set(&out->u.l->v[i], cell(&g, i, 0));
		}
		else {
			bool vec = src->u.m->vec && g.rows == 1;
			if (!(st = v_mat(out, g.rows, g.cols, vec)))
				for (int i = 0; i < g.rows * g.cols; i++)
					out->u.m->v[i] = g.c[i];
		}
	}
	grid_free(&g);
	return st;
}

int dlg_editmat(ppl_vm *V, int argc, value *a, value *out)
{
	int e = edit_grid(V, argc, a, out, false);
	io_flush_touches();
	return e;
}

int dlg_editlist(ppl_vm *V, int argc, value *a, value *out)
{
	int e = edit_grid(V, argc, a, out, true);
	io_flush_touches();
	return e;
}
