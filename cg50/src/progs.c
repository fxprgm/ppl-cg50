/* Program files (progs.h). */
#include "progs.h"
#include "files.h"
#include "screen.h"
#include "stats.h"
#include "text.h"
#include "ui.h"

#include <gint/keyboard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ppl_vm *V;

#define PMAX 160
static struct { char name[48]; char *src; } loaded[PMAX];
static int nloaded;

void progs_init(ppl_vm *vm) { V = vm; }

static void keep_source(const char *name, char *src)
{
	for (int i = 0; i < nloaded; i++)
		if (!strcmp(loaded[i].name, name)) {
			free(loaded[i].src);
			loaded[i].src = src;
			return;
		}
	if (nloaded == PMAX) {
		free(src);
		return;
	}
	strncpy(loaded[nloaded].name, name, sizeof loaded[0].name - 1);
	loaded[nloaded++].src = src;
	ST->progs = nloaded;
}

void progs_line(const char *name, int line, char *out, int n)
{
	out[0] = 0;
	for (int i = 0; i < nloaded; i++) {
		if (strcmp(loaded[i].name, name))
			continue;
		const char *p = loaded[i].src;
		for (int l = 1; l < line && p; l++) {
			p = strchr(p, '\n');
			if (p) p++;
		}
		if (!p)
			return;
		while (*p == ' ' || *p == '\t')
			p++;
		int k = 0;
		while (p[k] && p[k] != '\n' && p[k] != '\r' && k < n - 1)
			k++;
		memcpy(out, p, k);
		out[k] = 0;
		return;
	}
}

/* the file's text as UTF-8: HP's binary .hpprgm, UTF-16LE with a byte-order mark, or UTF-8 */
static char *source_of(char *buf, int n, int *outn)
{
	if (hpprgm_is((const uint8_t *)buf, n)) {
		char *src;
		size_t sn;
		if (hpprgm_source((const uint8_t *)buf, n, &src, &sn))
			return NULL;
		*outn = sn;
		return src;
	}
	if (n >= 2 && (uint8_t)buf[0] == 0xFF && (uint8_t)buf[1] == 0xFE) {
		uint32_t units = (n - 2) / 2;
		uint16_t *u = malloc(units * 2 + 2);
		if (!u)
			return NULL;
		for (uint32_t i = 0; i < units; i++)
			u[i] = (uint8_t)buf[2 + 2 * i] | (uint8_t)buf[3 + 2 * i] << 8;
		size_t k = u16_to_utf8(NULL, u, units);
		char *o = malloc(k + 1);
		if (o) {
			u16_to_utf8(o, u, units);
			o[k] = 0;
			*outn = k;
		}
		free(u);
		return o;
	}
	int skip = n >= 3 && (uint8_t)buf[0] == 0xEF && (uint8_t)buf[1] == 0xBB && (uint8_t)buf[2] == 0xBF ? 3 : 0;
	char *o = malloc(n - skip + 1);
	if (o) {
		memcpy(o, buf + skip, n - skip);
		o[n - skip] = 0;
		*outn = n - skip;
	}
	return o;
}

static void base_name(const char *path, char *out, int n)
{
	const char *s = strrchr(path, '\\');
	s = s ? s + 1 : path;
	const char *d = strrchr(s, '.');
	int k = d ? d - s : (int)strlen(s);
	if (k >= n)
		k = n - 1;
	memcpy(out, s, k);
	out[k] = 0;
}

static void load_error(const char *path, const char *name, const char *msg, int line)
{
	char text[400], src[200];
	progs_line(name, line, src, sizeof src);
	snprintf(text, sizeof text, "%s\n\n%s\n\n%s", path, msg, src);
	ui_message("Program not loaded", text, "Press any key", C_ERR);
	ui_key();
}

int progs_load(const char *path, bool quiet)
{
	char *buf;
	char name[48], msg[160];
	base_name(path, name, sizeof name);
	int n = files_read_all(path, &buf, 1 << 20);
	if (n < 0) {
		snprintf(msg, sizeof msg, "The file could not be read (%d).", n);
		ST->load_err = E_MEM;
		if (!quiet) load_error(path, name, msg, 0);
		return E_MEM;
	}
	int sn = 0;
	char *src = source_of(buf, n, &sn);
	free(buf);
	if (!src) {
		ST->load_err = E_SYNTAX;
		if (!quiet) load_error(path, name, "Not a program file.", 0);
		return E_SYNTAX;
	}
	ui_message("Loading", path, NULL, C_BLACK);
	int e = ppl_load(V, name, src, sn, msg, sizeof msg);
	ST->load_err = e;
	ST->load_line = e ? V->errline : 0;
	if (e) {
		/* keep the text long enough to show the line */
		keep_source(name, src);
		st_str(ST->msg, msg);
		if (!quiet) load_error(path, name, msg, V->errline);
		return e;
	}
	keep_source(name, src);
	/* what it exports */
	char text[600];
	int k = snprintf(text, sizeof text, "%s is loaded as program %s.\n\nExported:", path, name);
	for (pprog *p = V->progs; p; p = p->next) {
		if (strcmp(p->name, name))
			continue;
		for (int i = 0; i < p->nf && k < (int)sizeof text - 40; i++)
			if (p->f[i]->exported)
				k += snprintf(text + k, sizeof text - k, " %s(%s)", p->f[i]->name,
				              p->f[i]->nparams ? "..." : "");
		for (int i = 0; i < p->ng && k < (int)sizeof text - 40; i++)
			if (p->g[i].exported)
				k += snprintf(text + k, sizeof text - k, " %s", p->g[i].name);
	}
	st_str(ST->msg, text);
	if (!quiet) {
		ui_message("Program loaded", text, "Press any key", C_BLACK);
		ui_key();
	}
	return 0;
}

/* ---- the picker -------------------------------------------------------------------------- */

#define LMAX 256
static files_entry_t list[LMAX];
static int nlist;

static bool is_program(const char *name)
{
	const char *d = strrchr(name, '.');
	return d && (!strcasecmp(d, ".hpprgm") || !strcasecmp(d, ".ppl") || !strcasecmp(d, ".txt") ||
	             !strcasecmp(d, ".hppl") || !strcasecmp(d, ".prgm"));
}

static void scan(void)
{
	static files_entry_t all[LMAX];
	static const char *const dirs[] = { "PPL\\", "" };
	nlist = 0;
	for (int d = 0; d < 2; d++) {
		int n = files_list(dirs[d], all, LMAX);
		int first = nlist;
		for (int i = 0; i < n && nlist < LMAX; i++) {
			if (!is_program(all[i].name))
				continue;
			int j = nlist++;
			while (j > first && strcasecmp(list[j - 1].name, all[i].name) > 0) {
				list[j] = list[j - 1];
				j--;
			}
			list[j] = all[i];
		}
	}
	ST->files = nlist;
}

void progs_picker(void)
{
	ST->phase = PH_PICK;
	ui_message("Programs", "Looking for program files...", NULL, C_BLACK);
	scan();
	if (!nlist) {
		ui_message("Programs",
		           "No program files found.\n\n"
		           "Copy HP Prime programs (.hpprgm, or the text as .ppl or .txt, UTF-8 or "
		           "UTF-16) to a folder named PPL or to the root of the storage memory, over "
		           "USB (\"USB Flash\" mode), then try again.",
		           "Press any key", C_BLACK);
		ui_key();
		return;
	}
	const int rows = 12;
	int sel = 0, top = 0;
	for (;;) {
		ST->phase = PH_PICK;
		if (sel < top) top = sel;
		if (sel >= top + rows) top = sel - rows + 1;
		dclear(C_WHITE);
		drect(0, 0, DWIDTH - 1, 17, C_PANEL);
		text_draw(FONT, 6, 2, "Load a program", C_TITLE, -1, -1);
		char t[32];
		snprintf(t, sizeof t, "%d files", nlist);
		text_right(FONT, DWIDTH - 6, 2, t, C_LABEL, -1);
		for (int r = 0; r < rows && top + r < nlist; r++) {
			int i = top + r, y = 22 + r * 15;
			bool s = i == sel;
			if (s)
				drect(2, y - 1, DWIDTH - 3, y + 13, C_SEL);
			text_draw(FONT, 8, y, list[i].name, s ? C_WHITE : C_BLACK, -1, 48);
			snprintf(t, sizeof t, "%u B", (unsigned)list[i].size);
			text_right(FONT, DWIDTH - 8, y, t, s ? C_WHITE : C_LABEL, -1);
		}
		st_str(ST->msg, list[sel].name);   /* the probe test steers by it */
		static const char *const sk[6] = { "Load", NULL, NULL, NULL, NULL, "Back" };
		ui_softbar(sk);
		scr_push_all();
		int k = ui_key();
		if (k == KEY_UP) sel = (sel + nlist - 1) % nlist;
		else if (k == KEY_DOWN) sel = (sel + 1) % nlist;
		else if (k == KEY_EXE || k == KEY_F1) {
			progs_load(list[sel].name, false);
			return;
		}
		else if (k == KEY_EXIT || k == KEY_F6 || k == KEY_MENU)
			return;
	}
}

void progs_autoload(char *out, int n, bool *any_error)
{
	static files_entry_t all[LMAX];
	int k = 0, m = files_list("PPL\\", all, LMAX), ok = 0, bad = 0;
	out[0] = 0;
	*any_error = false;
	for (int i = 0; i < m; i++) {
		if (!is_program(all[i].name) || all[i].size > 256 * 1024)
			continue;
		char name[48];
		base_name(all[i].name, name, sizeof name);
		int e = progs_load(all[i].name, true);
		if (e) {
			bad++;
			*any_error = true;
		}
		else
			ok++;
		if (k < n - 60)
			k += snprintf(out + k, n - k, "%s%s%s", k ? " " : "", name, e ? "(error)" : "");
	}
	ST->phase = PH_HOME;
	(void)ok, (void)bad;
}

/* ---- exported functions ------------------------------------------------------------------ */

#define FLMAX 1024

static int name_cmp(const char *a, const char *b)   /* ASCII, case-insensitive */
{
	for (;; a++, b++) {
		int x = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a, y = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
		if (x != y || !x) return x - y;
	}
}

/* the letter printed in red on a CG50 key (its ALPHA letter), 0 for none */
static char key_letter(int k)
{
	static const struct { int key; char c; } t[] = {
		{ KEY_XOT, 'A' }, { KEY_LOG, 'B' }, { KEY_LN, 'C' }, { KEY_SIN, 'D' }, { KEY_COS, 'E' },
		{ KEY_TAN, 'F' }, { KEY_FRAC, 'G' }, { KEY_FD, 'H' }, { KEY_LEFTP, 'I' }, { KEY_RIGHTP, 'J' },
		{ KEY_COMMA, 'K' }, { KEY_ARROW, 'L' }, { KEY_7, 'M' }, { KEY_8, 'N' }, { KEY_9, 'O' },
		{ KEY_4, 'P' }, { KEY_5, 'Q' }, { KEY_6, 'R' }, { KEY_MUL, 'S' }, { KEY_DIV, 'T' },
		{ KEY_1, 'U' }, { KEY_2, 'V' }, { KEY_3, 'W' }, { KEY_ADD, 'X' }, { KEY_SUB, 'Y' },
		{ KEY_0, 'Z' },
	};
	for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
		if (t[i].key == k) return t[i].c;
	return 0;
}

int progs_functions(char *call, int n, int *run)
{
	/* every exported function, by program name (A-Z), each program's in source order */
	static struct { pfunc *f; const char *prog; } fl[FLMAX];
	int nf = 0;
	for (pprog *p = V->progs; p; p = p->next)
		for (int i = 0; i < p->nf && nf < FLMAX; i++)
			if (p->f[i]->exported)
				fl[nf].f = p->f[i], fl[nf].prog = p->name, nf++;
	for (int i = 1; i < nf; i++) {          /* insertion sort: stable, a few hundred entries */
		__typeof__(fl[0]) t = fl[i];
		int j = i;
		while (j > 0 && name_cmp(fl[j - 1].prog, t.prog) > 0)
			fl[j] = fl[j - 1], j--;
		fl[j] = t;
	}
	ST->phase = PH_FUNCS;
	if (!nf) {
		ui_message("Run", "No program is loaded, or none exports a function.\n\n"
		           "F1 (Load) on the Home screen loads a program file.", "Press any key", C_BLACK);
		ui_key();
		return 0;
	}
	const int rows = 12;
	int sel = 0, top = 0;
	for (;;) {
		if (sel < top) top = sel;
		if (sel >= top + rows) top = sel - rows + 1;
		dclear(C_WHITE);
		drect(0, 0, DWIDTH - 1, 17, C_PANEL);
		text_draw(FONT, 6, 2, "Functions   \xE2\x86\x90\xE2\x86\x92 program   letter: jump", C_TITLE, -1, -1);
		for (int r = 0; r < rows && top + r < nf; r++) {
			int i = top + r, y = 22 + r * 15;
			bool s = i == sel;
			if (s)
				drect(2, y - 1, DWIDTH - 3, y + 13, C_SEL);
			char t[80];
			snprintf(t, sizeof t, "%s(%s)", fl[i].f->name, fl[i].f->nparams ? "..." : "");
			text_draw(FONT, 8, y, t, s ? C_WHITE : C_BLACK, -1, 40);
			text_right(FONT, DWIDTH - 8, y, fl[i].prog, s ? C_WHITE : C_LABEL, -1);
		}
		st_str(ST->msg, fl[sel].f->name);
		static const char *const sk[6] = { "Run", "Paste", NULL, NULL, NULL, "Back" };
		ui_softbar(sk);
		scr_push_all();
		int k = ui_key();
		if (k == KEY_UP) sel = (sel + nf - 1) % nf;
		else if (k == KEY_DOWN) sel = (sel + 1) % nf;
		else if (k == KEY_RIGHT) {              /* the next program's first function */
			int i = sel;
			while (i < nf && !strcmp(fl[i].prog, fl[sel].prog)) i++;
			sel = i < nf ? i : 0;
		}
		else if (k == KEY_LEFT) {               /* this program's first, or the previous one's */
			int i = sel;
			if (i > 0 && strcmp(fl[i - 1].prog, fl[i].prog)) i--;
			while (i > 0 && !strcmp(fl[i - 1].prog, fl[i].prog)) i--;
			if (i == sel) { i = nf - 1; while (i > 0 && !strcmp(fl[i - 1].prog, fl[i].prog)) i--; }
			sel = i;
		}
		else if (key_letter(k)) {               /* the next program starting with that letter */
			char c = key_letter(k);
			for (int d = 1; d <= nf; d++) {
				int i = (sel + d) % nf;
				if ((fl[i].prog[0] & ~32) == c && (i == 0 || strcmp(fl[i - 1].prog, fl[i].prog))) {
					sel = i;
					break;
				}
			}
		}
		else if (k == KEY_EXE || k == KEY_F1 || k == KEY_F2) {
			*run = !fl[sel].f->nparams && k != KEY_F2;
			snprintf(call, n, "%s(%s", fl[sel].f->name, fl[sel].f->nparams ? "" : ")");
			return 1;
		}
		else if (k == KEY_EXIT || k == KEY_F6 || k == KEY_MENU)
			return 0;
	}
}
