/* units.c -- measurements (TYPE 9): 2_m, 1_(kgf/m), CONVERT, MKSA, UFACTOR...
 *
 * A unit is a product of catalogue units with SI prefixes and integer powers
 * (units.h).  Conversions go through SI: each catalogue unit has its SI factor
 * (as text, computed at 30 digits, so a conversion is rounded once to 12
 * digits) and its dimension over m, kg, s, A, K, mol, cd.  Temperatures (°C,
 * °F) also have an offset, applied only when a lone temperature is converted
 * (CONVERT(20_°C,1_°F) = 68_°F); sums and products use the size of a degree.
 *
 * What the Prime shows was taken from HP's User Guide (ch. 25) and the
 * conformance table; the rest is marked "unverified" below.
 */
#include "units.h"
#include "bi.h"
#include "decx.h"
#include <stdlib.h>
#include <string.h>

#define UP 30                      /* working digits of conversions */

/* dimension exponents: m, kg, s, A, K, mol, cd */
#define D(m, kg, s, A, K, mol, cd) { m, kg, s, A, K, mol, cd }
#define LEN  D(1, 0, 0, 0, 0, 0, 0)
#define AREA D(2, 0, 0, 0, 0, 0, 0)
#define VOL  D(3, 0, 0, 0, 0, 0, 0)
#define TIME D(0, 0, 1, 0, 0, 0, 0)
#define FREQ D(0, 0, -1, 0, 0, 0, 0)
#define SPD  D(1, 0, -1, 0, 0, 0, 0)
#define ACC  D(1, 0, -2, 0, 0, 0, 0)
#define MASS D(0, 1, 0, 0, 0, 0, 0)
#define FRC  D(1, 1, -2, 0, 0, 0, 0)
#define ENR  D(2, 1, -2, 0, 0, 0, 0)
#define PWR  D(2, 1, -3, 0, 0, 0, 0)
#define PRS  D(-1, 1, -2, 0, 0, 0, 0)
#define TMP  D(0, 0, 0, 0, 1, 0, 0)
#define ONE  D(0, 0, 0, 0, 0, 0, 0)
#define CHG  D(0, 0, 1, 1, 0, 0, 0)
#define LUM  D(0, 0, 0, 0, 0, 0, 1)
#define ILL  D(-2, 0, 0, 0, 0, 0, 1)
#define DOSE D(2, 0, -2, 0, 0, 0, 0)

/* The catalogue: the Prime's Units menu (giac's names), SI factor as "a",
 * "a/b" or "a/b;offset".  Exact names are tried first, then prefix + name. */
static const struct ucat { const char *n, *k; int8_t d[7]; } CAT[] = {
    /* length */
    {"m", "1", LEN}, {"yd", "0.9144", LEN}, {"ft", "0.3048", LEN}, {"inch", "0.0254", LEN},
    {"in", "0.0254", LEN}, {"mile", "1609.344", LEN}, {"mi", "1609.344", LEN},
    {"nmi", "1852", LEN}, {"ftUS", "1200/3937", LEN}, {"miUS", "6336000/3937", LEN},
    {"mil", "0.0000254", LEN}, {"\xC3\x85", "1E-10", LEN}, {"fermi", "1E-15", LEN},
    {"au", "149597870691", LEN}, {"lyr", "9.46052840488E15", LEN},
    {"pc", "3.08567818585E16", LEN}, {"chain", "20.1168", LEN}, {"rod", "5.0292", LEN},
    {"fath", "1.8288", LEN},
    /* area */
    {"a", "100", AREA}, {"ha", "10000", AREA}, {"acre", "4046.87260987", AREA}, {"b", "1E-28", AREA},
    /* volume */
    {"L", "0.001", VOL}, {"l", "0.001", VOL}, {"gal", "0.003785411784", VOL},
    {"galUK", "0.004546092", VOL}, {"galC", "0.00454609", VOL}, {"qt", "0.000946352946", VOL},
    {"pt", "0.000473176473", VOL}, {"cu", "0.0002365882365", VOL},
    {"ozfl", "0.0000295735295625", VOL}, {"ozUK", "0.000028413075", VOL},
    {"tbsp", "0.0000147867647813", VOL}, {"tsp", "0.00000492892159375", VOL},
    {"bbl", "0.158987294928", VOL}, {"bu", "0.03523907016688", VOL},
    {"pk", "0.00880976754172", VOL}, {"fbm", "0.002359737216", VOL}, {"st", "1", VOL},
    /* time, frequency */
    {"s", "1", TIME}, {"min", "60", TIME}, {"h", "3600", TIME}, {"d", "86400", TIME},
    {"yr", "31556925.9747", TIME}, {"Hz", "1", FREQ},
    /* speed, acceleration */
    {"mph", "0.44704", SPD}, {"knot", "1852/3600", SPD}, {"kph", "1/3.6", SPD},
    {"c", "299792458", SPD}, {"ga", "9.80665", ACC},
    /* mass, amount */
    {"g", "0.001", MASS}, {"lb", "0.45359237", MASS},
    {"oz", "0.028349523125", MASS}, {"slug", "14.5939029372", MASS},
    {"lbt", "0.3732417216", MASS}, {"ton", "907.18474", MASS}, {"tonUK", "1016.0469088", MASS},
    {"t", "1000", MASS}, {"ozt", "0.0311034768", MASS}, {"ct", "0.0002", MASS},
    {"grain", "0.00006479891", MASS}, {"u", "1.6605402E-27", MASS},
    {"mol", "1", D(0, 0, 0, 0, 0, 1, 0)},
    /* force */
    {"N", "1", FRC}, {"dyn", "1E-5", FRC}, {"gf", "0.00980665", FRC},
    {"kip", "4448.2216152605", FRC}, {"lbf", "4.4482216152605", FRC},
    {"pdl", "0.138254954376", FRC},
    /* energy, power */
    {"J", "1", ENR}, {"erg", "1E-7", ENR}, {"cal", "4.1868", ENR}, {"Btu", "1055.05585262", ENR},
    {"ftlbf", "1.3558179483314", ENR}, {"therm", "105506000", ENR},
    {"eV", "1.60217733E-19", ENR}, {"Wh", "3600", ENR},
    {"W", "1", PWR}, {"hp", "745.699871582", PWR},
    /* pressure */
    {"Pa", "1", PRS}, {"atm", "101325", PRS}, {"bar", "100000", PRS},
    {"psi", "6894.75729317", PRS}, {"torr", "101325/760", PRS},
    {"mmHg", "133.322387415", PRS}, {"inHg", "3386.38815789", PRS}, {"inH2O", "248.84", PRS},
    /* temperature (°C, °F, °R) */
    {"K", "1", TMP}, {"\xC2\xB0" "C", "1;273.15", TMP}, {"\xC2\xB0" "F", "5/9;459.67", TMP},
    {"\xC2\xB0" "R", "5/9", TMP},
    /* electricity, magnetism */
    {"A", "1", D(0, 0, 0, 1, 0, 0, 0)}, {"C", "1", CHG}, {"Fdy", "96487", CHG},
    {"V", "1", D(2, 1, -3, -1, 0, 0, 0)},
    {"\xCE\xA9", "1", D(2, 1, -3, -2, 0, 0, 0)}, {"\xE2\x84\xA6", "1", D(2, 1, -3, -2, 0, 0, 0)},
    {"F", "1", D(-2, -1, 4, 2, 0, 0, 0)}, {"S", "1", D(-2, -1, 3, 2, 0, 0, 0)},
    {"mho", "1", D(-2, -1, 3, 2, 0, 0, 0)},
    {"Wb", "1", D(2, 1, -2, -1, 0, 0, 0)}, {"H", "1", D(2, 1, -2, -2, 0, 0, 0)},
    {"T", "1", D(0, 1, -2, -1, 0, 0, 0)}, {"G", "0.0001", D(0, 1, -2, -1, 0, 0, 0)},
    /* light */
    {"cd", "1", LUM}, {"lm", "1", LUM}, {"lx", "1", ILL}, {"fc", "10.7639104167", ILL},
    {"flam", "3.42625909964", ILL}, {"ph", "10000", ILL}, {"sb", "10000", ILL},
    {"lam", "3183.09886184", ILL},
    /* angle (dimensionless) */
    {"rad", "1", ONE}, {"sr", "1", ONE}, {"\xC2\xB0", "0.0174532925199432957692369077", ONE},
    {"deg", "0.0174532925199432957692369077", ONE}, {"grad", "0.0157079632679489661923132169", ONE},
    {"arcmin", "0.000290888208665721596153948461", ONE},
    {"arcs", "0.00000484813681109535993589914102", ONE},
    /* viscosity */
    {"P", "0.1", D(-1, 1, -1, 0, 0, 0, 0)}, {"St", "0.0001", D(2, 0, -1, 0, 0, 0, 0)},
    /* radiation */
    {"Gy", "1", DOSE}, {"rem", "0.01", DOSE}, {"Sv", "1", DOSE}, {"Bq", "1", FREQ},
    {"Ci", "3.7E10", FREQ}, {"R", "0.000258", D(0, -1, 1, 1, 0, 0, 0)},
};
#define NCAT ((int)(sizeof CAT / sizeof CAT[0]))

static const struct { char s[3]; int8_t e; } PRE[] = {
    {"", 0}, {"Y", 24}, {"Z", 21}, {"E", 18}, {"P", 15}, {"T", 12}, {"G", 9}, {"M", 6},
    {"k", 3}, {"h", 2}, {"D", 1}, {"d", -1}, {"c", -2}, {"m", -3},
    {"\xC2\xB5", -6}, {"\xCE\xBC", -6}, {"n", -9}, {"p", -12}, {"f", -15}, {"a", -18},
    {"z", -21}, {"y", -24},
};
#define NPRE ((int)(sizeof PRE / sizeof PRE[0]))
#define PRE_K 8

static int cat_find(const char *s, size_t n)
{
    int i;
    for (i = 0; i < NCAT; i++) if (strlen(CAT[i].n) == n && !memcmp(CAT[i].n, s, n)) return i;
    return -1;
}

/* a unit name, exactly or prefix + name: id and prefix, or -1 */
static int name_find(const char *s, size_t n, int *pre)
{
    int i, id = cat_find(s, n);
    *pre = 0;
    if (id >= 0) return id;
    for (i = 1; i < NPRE; i++) {
        size_t l = strlen(PRE[i].s);
        if (l < n && !memcmp(PRE[i].s, s, l) && (id = cat_find(s + l, n - l)) >= 0) { *pre = i; return id; }
    }
    return -1;
}

/* ------------------------------------------------------------ factors */

static int fac_add(ufac *f, int *n, int id, int pre, int pow)
{
    int i;
    for (i = 0; i < *n; i++)
        if (f[i].id == id && f[i].pre == pre) {
            pow += f[i].pow;
            if (pow > 99 || pow < -99) return E_BADVAL;
            f[i].pow = (int8_t)pow;
            return 0;
        }
    if (*n >= UN_MAXF || pow > 99 || pow < -99) return E_BADVAL;
    f[*n].id = (uint8_t)id; f[*n].pre = (uint8_t)pre; f[*n].pow = (int8_t)pow;
    (*n)++;
    return 0;
}

static void fac_clean(ufac *f, int *n)
{
    int i, j = 0;
    for (i = 0; i < *n; i++) if (f[i].pow) f[j++] = f[i];
    *n = j;
}

/* SI factor (and temperature offset) of a catalogue unit */
static void cat_k(int id, xn *k, xn *off)
{
    const char *s = CAT[id].k, *q = strchr(s, '/'), *r = strchr(s, ';');
    xn_from_str(k, s);
    if (q && (!r || q < r)) { xn d; xn_from_str(&d, q + 1); xn_div(k, k, &d, UP); }
    if (off) { if (r) xn_from_str(off, r + 1); else xn_zero(off); }
}

/* SI factor and dimension of a unit */
static void un_k(const pun *p, xn *k, int8_t *d)
{
    int i, j;
    memset(d, 0, 7);
    xn_from_int(k, 1);
    for (i = 0; i < p->n; i++) {
        const ufac *f = &p->f[i];
        int e = f->pow < 0 ? -f->pow : f->pow;
        xn c;
        cat_k(f->id, &c, NULL);
        xn_scale10(&c, PRE[f->pre].e);
        for (j = 0; j < e; j++) {
            if (f->pow > 0) xn_mul(k, k, &c, UP); else xn_div(k, k, &c, UP);
        }
        for (j = 0; j < 7; j++) d[j] = (int8_t)(d[j] + CAT[f->id].d[j] * f->pow);
    }
}

/* a lone temperature (°C, °F...) converts with its offset */
static int lone(const pun *p) { return p->n == 1 && p->f[0].pow == 1 && !p->f[0].pre; }

/* magnitude m in unit a -> unit b (same dimension, else E_UNITS) */
static int conv(const real *m, const pun *a, const pun *b, real *out, int temp)
{
    xn x, ka, kb, oa, ob;
    int8_t da[7], db[7];
    un_k(a, &ka, da);
    un_k(b, &kb, db);
    if (memcmp(da, db, 7)) return E_UNITS;
    xn_from_real(&x, m);
    if (temp && lone(a) && lone(b)) {
        cat_k(a->f[0].id, &ka, &oa);
        cat_k(b->f[0].id, &kb, &ob);
        xn_add(&x, &x, &oa, UP);
        xn_mul(&x, &x, &ka, UP);
        xn_div(&x, &x, &kb, UP);
        xn_sub(&x, &x, &ob, UP);
    } else {
        xn_mul(&x, &x, &ka, UP);
        xn_div(&x, &x, &kb, UP);
    }
    return dec_err(xn_to_real(out, &x));
}

/* ------------------------------------------------------------ values */

static pun *pun_new(const ufac *f, int n, const char *text, size_t tl)
{
    pun *p = (pun *)malloc(sizeof(pun) + tl);
    if (!p) return NULL;
    p->h.rc = 1;
    p->n = (uint8_t)n;
    memcpy(p->f, f, (size_t)n * sizeof *f);
    p->tlen = (uint16_t)tl;
    if (tl) memcpy(p->text, text, tl);
    p->text[tl] = 0;
    return p;
}

static void v_unit(value *v, const real *r, pun *p)
{
    v_none(v);
    v->t = T_UNIT;
    v->u.un.r = *r;
    v->u.un.p = p;
}

/* r with the same unit as a measurement u */
static void v_same(value *v, const real *r, const value *u)
{
    v_unit(v, r, u->u.un.p);
    u->u.un.p->h.rc++;
}

/* r times the unit f[0..n): a real when nothing is left */
static int mk(value *out, const real *r, ufac *f, int n)
{
    pun *p;
    fac_clean(f, &n);
    if (!n) { v_real(out, r); return 0; }
    if (!(p = pun_new(f, n, NULL, 0))) return E_MEM;
    v_unit(out, r, p);
    return 0;
}

/* the SI base units of dimension d, appended to f */
static int base_units(ufac *f, int *n, const int8_t *d)
{
    static const char *const nm[7] = { "m", "g", "s", "A", "K", "mol", "cd" };
    int j, st;
    for (j = 0; j < 7; j++)
        if (d[j] && (st = fac_add(f, n, cat_find(nm[j], strlen(nm[j])), j == 1 ? PRE_K : 0, d[j]))) return st;
    return 0;
}

/* ------------------------------------------------------------ parsing */

typedef struct { const char *p, *e; } usrc;

/* the operator at p ('*', '/', '^', '(', ')', '-', '2' for ², '3' for ³) or 0 */
static int u_op(const char *p, const char *e, int *len)
{
    const unsigned char *u = (const unsigned char *)p;
    *len = 1;
    if (p >= e) return 0;
    if (strchr("*/^()-", *u)) return *u;
    if (u[0] == 0xC2 && p + 1 < e) {
        *len = 2;
        if (u[1] == 0xB7) return '*';                           /* · */
        if (u[1] == 0xB2) return '2';                           /* ² */
        if (u[1] == 0xB3) return '3';                           /* ³ */
    }
    if (u[0] == 0xE2 && p + 2 < e) {
        *len = 3;
        if (u[1] == 0x8B && u[2] == 0x85) return '*';           /* ⋅ */
        if (u[1] == 0x88 && u[2] == 0x92) return '-';           /* − */
    }
    *len = 1;
    return 0;
}

static void u_sp(usrc *s) { while (s->p < s->e && (*s->p == ' ' || *s->p == '\t')) s->p++; }

static int u_expr(usrc *s, ufac *f, int *n);

/* name or (expr), with an optional integer power; added to f with sign sg */
static int u_term(usrc *s, ufac *f, int *n, int sg)
{
    ufac g[UN_MAXF];
    int m = 0, st, i, len, op, pw = 1;
    u_sp(s);
    if (s->p < s->e && *s->p == '(') {
        s->p++;
        if ((st = u_expr(s, g, &m))) return st;
        u_sp(s);
        if (s->p >= s->e || *s->p != ')') return E_SYNTAX;
        s->p++;
    } else {
        const char *a = s->p;
        int pre, id;
        while (s->p < s->e && *s->p != ' ' && !u_op(s->p, s->e, &len)) {
            s->p++;
            while (s->p < s->e && (*s->p & 0xC0) == 0x80) s->p++;   /* rest of a UTF-8 character */
        }
        if (s->p == a || (id = name_find(a, (size_t)(s->p - a), &pre)) < 0) return E_SYNTAX;
        g[0].id = (uint8_t)id; g[0].pre = (uint8_t)pre; g[0].pow = 1; m = 1;
    }
    u_sp(s);
    op = u_op(s->p, s->e, &len);
    if (op == '^') {
        int neg = 0, par = 0, v = 0, nd = 0;
        s->p += len; u_sp(s);
        if (s->p < s->e && *s->p == '(') { par = 1; s->p++; }
        if (u_op(s->p, s->e, &len) == '-') { neg = 1; s->p += len; }
        while (s->p < s->e && *s->p >= '0' && *s->p <= '9' && v < 100) { v = v * 10 + (*s->p++ - '0'); nd++; }
        if (!nd) return E_SYNTAX;
        if (par) { if (s->p >= s->e || *s->p != ')') return E_SYNTAX; s->p++; }
        pw = neg ? -v : v;
    } else if (op == '2' || op == '3') { pw = op - '0'; s->p += len; }
    for (i = 0; i < m; i++)
        if ((st = fac_add(f, n, g[i].id, g[i].pre, g[i].pow * pw * sg))) return st;
    return 0;
}

/* term (('*' | '/') term)*, left to right: a/b*c is (a/b)*c */
static int u_expr(usrc *s, ufac *f, int *n)
{
    int st, sg = 1, len, op;
    for (;;) {
        if ((st = u_term(s, f, n, sg))) return st;
        u_sp(s);
        op = u_op(s->p, s->e, &len);
        if (op == '*') sg = 1;
        else if (op == '/') sg = -1;
        else return 0;
        s->p += len;
    }
}

/* ------------------------------------------------------------ text */

static void fac_text(sbuf *b, const ufac *f)
{
    sb_puts(b, PRE[f->pre].s);
    sb_puts(b, CAT[f->id].n);
    if (f->pow != 1) {
        char t[8];
        int k = 0, v = f->pow < 0 ? -f->pow : f->pow;
        sb_puts(b, "^");
        if (f->pow < 0) sb_puts(b, "\xE2\x88\x92");   /* U+2212, as the Prime's minus (unverified here) */
        if (v >= 10) t[k++] = (char)('0' + v / 10);
        t[k++] = (char)('0' + v % 10);
        sb_put(b, t, (size_t)k);
    }
}

/* 2_m, 10_ft^2, 1_(kgf/m) as written, 8.175_(cm*s^-1) when computed (the
 * User Guide's display of 32.7_cm/4_s); a compound unit is in parentheses */
void un_text(ppl_vm *V, sbuf *b, const value *v)
{
    const pun *p = v->u.un.p;
    value m;
    int i;
    v_real(&m, &v->u.un.r);
    v_to_text(V, b, &m, 0);
    sb_puts(b, "_");
    if (p->n == 1) { fac_text(b, &p->f[0]); return; }
    sb_puts(b, "(");
    if (p->tlen) sb_puts(b, p->text);
    else for (i = 0; i < p->n; i++) { if (i) sb_puts(b, "*"); fac_text(b, &p->f[i]); }
    sb_puts(b, ")");
}

int un_equal(const value *a, const value *b)
{
    const pun *p = a->u.un.p, *q = b->u.un.p;
    return p->n == q->n && !memcmp(p->f, q->f, p->n * sizeof(ufac)) && !dec_cmp(&a->u.un.r, &b->u.un.r);
}

/* ------------------------------------------------------------ operators */

int un_neg(const value *a, value *out)
{
    real r;
    dec_neg(&r, &a->u.un.r);
    v_same(out, &r, a);
    return 0;
}

int un_binary(ppl_vm *V, int op, const value *a, const value *b, value *out)
{
    const pun *pa = a->t == T_UNIT ? a->u.un.p : NULL, *pb = b->t == T_UNIT ? b->u.un.p : NULL;
    real x, y, r;
    int st, i, n = 0;
    ufac f[UN_MAXF];
    if (pa) x = a->u.un.r; else if (to_real(a, &x)) return E_BADTYPE;
    if (pb) y = b->u.un.r; else if (to_real(b, &y)) return E_BADTYPE;
    switch (op) {
    case OP_ADD: case OP_SUB:
        /* the result is in the left operand's unit (User Guide: 20_cm+5_inch = 32.7_cm) */
        if (!pa || !pb) return E_UNITS;
        if ((st = conv(&y, pb, pa, &y, 0))) return st;
        st = op == OP_ADD ? dec_add(&r, &x, &y) : dec_sub(&r, &x, &y);
        if (st && !DEC_SOFT(st)) return dec_err(st);
        v_same(out, &r, a);
        return 0;
    case OP_MUL: case OP_DIV:
        st = op == OP_MUL ? dec_mul(&r, &x, &y) : dec_div(&r, &x, &y);
        if (st && !DEC_SOFT(st)) return dec_err(st);
        if (!pb) { v_same(out, &r, a); return 0; }
        if (!pa && op == OP_MUL) { v_same(out, &r, b); return 0; }
        if (pa) { memcpy(f, pa->f, pa->n * sizeof *f); n = pa->n; }
        for (i = 0; i < pb->n; i++)
            if ((st = fac_add(f, &n, pb->f[i].id, pb->f[i].pre, op == OP_MUL ? pb->f[i].pow : -pb->f[i].pow))) return st;
        return mk(out, &r, f, n);
    case OP_POW: {
        int32_t k;
        if (pb || !pa) return E_BADTYPE;
        if (dec_to_int32(&y, &k) || k > 99 || k < -99) return E_BADVAL;
        if ((st = dec_pow(&r, &x, &y)) && !DEC_SOFT(st)) return dec_err(st);
        for (i = 0; i < pa->n; i++) {
            int32_t p = pa->f[i].pow * k;
            if (p > 99 || p < -99) return E_BADVAL;
            f[i] = pa->f[i]; f[i].pow = (int8_t)p;
        }
        return mk(out, &r, f, pa->n);
    }
    case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        value p, q;
        st = (!pa || !pb) ? E_UNITS : conv(&y, pb, pa, &y, 0);
        if (st == E_UNITS && (op == OP_EQ || op == OP_NE)) { v_int32(out, op == OP_NE); return 0; }
        if (st) return st;
        v_real(&p, &x); v_real(&q, &y);
        return ar_binary(V, op, &p, &q, out);
    }
    default: return E_BADTYPE;
    }
}

/* ------------------------------------------------------------ built-ins */

/* the literal number_unit: _UNIT(number, "unit text") */
static BI(b_unit)
{
    char buf[128];
    size_t n, i, j = 0;
    ufac f[UN_MAXF];
    int nf = 0, st;
    usrc s;
    pun *p;
    real x;
    (void)V; (void)argc;
    if (a[1].t != T_STR) return E_BADTYPE;
    if (a[0].t == T_UNIT) return E_BADTYPE;
    if (bi_real(&a[0], &x)) return E_BADTYPE;
    n = u16_to_utf8(NULL, a[1].u.s->c, a[1].u.s->len);
    if (n >= sizeof buf) return E_SYNTAX;
    u16_to_utf8(buf, a[1].u.s->c, a[1].u.s->len);
    for (i = 0; i < n; i++) if (buf[i] != ' ' && buf[i] != '\t') buf[j++] = buf[i];
    n = j;
    buf[n] = 0;
    s.p = buf; s.e = buf + n;
    if ((st = u_expr(&s, f, &nf))) return st;
    if (s.p != s.e) return E_SYNTAX;
    fac_clean(f, &nf);
    if (!nf) { v_real(out, &x); return 0; }
    /* keep the text as written, without enclosing parentheses: 1_(kgf/m) shows kgf/m */
    i = 0;
    if (n > 1 && buf[0] == '(') {
        int depth = 0;
        for (j = 0; j < n; j++) {
            if (buf[j] == '(') depth++;
            else if (buf[j] == ')' && --depth == 0) break;
        }
        if (j == n - 1) { i = 1; n -= 1; }
    }
    if (!(p = pun_new(f, nf, buf + i, nf > 1 ? n - i : 0))) return E_MEM;
    v_unit(out, &x, p);
    return 0;
}

static int need_unit(const value *v) { return v->t == T_UNIT ? 0 : E_BADTYPE; }

/* CONVERT(2_m, 1_ft) = 6.56167979003_ft */
static BI(b_convert)
{
    real r;
    int st;
    (void)V; (void)argc;
    if ((st = need_unit(&a[0])) || (st = need_unit(&a[1]))) return st;
    if ((st = conv(&a[0].u.un.r, a[0].u.un.p, a[1].u.un.p, &r, 1))) return st;
    v_same(out, &r, &a[1]);
    return 0;
}

/* MKSA(8.175_cm/s) = .08175_m/s: the magnitude in SI base units */
static BI(b_mksa)
{
    const pun *p;
    xn x, k, off;
    int8_t d[7];
    ufac f[UN_MAXF];
    int n = 0, st;
    real r;
    (void)V; (void)argc;
    if (a[0].t != T_UNIT) { v_copy(out, &a[0]); return 0; }
    p = a[0].u.un.p;
    un_k(p, &k, d);
    xn_from_real(&x, &a[0].u.un.r);
    if (lone(p)) { cat_k(p->f[0].id, &k, &off); xn_add(&x, &x, &off, UP); }
    xn_mul(&x, &x, &k, UP);
    if ((st = dec_err(xn_to_real(&r, &x)))) return st;
    if ((st = base_units(f, &n, d))) return st;
    return mk(out, &r, f, n);
}

/* UFACTOR(100_C, 1_A) = 100_(A*s): x expressed with the given unit and SI base units */
static BI(b_ufactor)
{
    const pun *p, *q;
    xn x, ka, kb;
    int8_t da[7], db[7];
    ufac f[UN_MAXF];
    int n, st, j;
    real r;
    (void)V; (void)argc;
    if ((st = need_unit(&a[0])) || (st = need_unit(&a[1]))) return st;
    p = a[0].u.un.p; q = a[1].u.un.p;
    un_k(p, &ka, da);
    un_k(q, &kb, db);
    if (!memcmp(da, db, 7)) return b_convert(V, argc, a, out);
    xn_from_real(&x, &a[0].u.un.r);
    xn_mul(&x, &x, &ka, UP);
    xn_div(&x, &x, &kb, UP);
    if ((st = dec_err(xn_to_real(&r, &x)))) return st;
    memcpy(f, q->f, q->n * sizeof *f); n = q->n;
    for (j = 0; j < 7; j++) da[j] = (int8_t)(da[j] - db[j]);
    if ((st = base_units(f, &n, da))) return st;
    return mk(out, &r, f, n);
}

/* USIMPLIFY(5_(kg*m^2/s^2)) = 5_J: a compound unit with the dimension of a
 * named SI unit becomes that unit; anything else is unchanged */
static BI(b_usimplify)
{
    static const char *const nm[] = { "N", "J", "W", "Pa", "C", "V", "\xCE\xA9", "F", "H", "T", "Wb", "S" };
    const pun *p;
    xn k;
    int8_t d[7];
    unsigned i;
    (void)V; (void)argc;
    if (a[0].t != T_UNIT || (p = a[0].u.un.p)->n == 1) { v_copy(out, &a[0]); return 0; }
    un_k(p, &k, d);
    for (i = 0; i < sizeof nm / sizeof nm[0]; i++) {
        int id = cat_find(nm[i], strlen(nm[i]));
        if (!memcmp(CAT[id].d, d, 7)) {
            xn x;
            real r;
            ufac f;
            int st;
            xn_from_real(&x, &a[0].u.un.r);
            xn_mul(&x, &x, &k, UP);
            if ((st = dec_err(xn_to_real(&r, &x)))) return st;
            f.id = (uint8_t)id; f.pre = 0; f.pow = 1;
            return mk(out, &r, &f, 1);
        }
    }
    v_copy(out, &a[0]);
    return 0;
}

/* UPART(2_m) = 1_m, UVAL(2_m) = 2 (a plain number: 1 and itself, unverified) */
static BI(b_upart)
{
    real one;
    (void)V; (void)argc;
    dec_from_int(&one, 1);
    if (a[0].t != T_UNIT) { if (to_real(&a[0], &one)) return E_BADTYPE; dec_from_int(&one, 1); v_real(out, &one); return 0; }
    v_same(out, &one, &a[0]);
    return 0;
}

static BI(b_uval)
{
    (void)V; (void)argc;
    if (a[0].t != T_UNIT) { v_copy(out, &a[0]); return 0; }
    v_real(out, &a[0].u.un.r);
    return 0;
}

/* ABS, IP, ROUND... of a measurement act on its magnitude and keep the unit
 * (unverified on the Prime); anything else goes to the core built-in */
static int mine(const builtin *c)
{
    const builtin *t;
    for (t = bi_unit_table; t->name; t++) if (t == c) return 1;
    return 0;
}

static int magfn(ppl_vm *V, int argc, value *a, value *out, const char *name, const builtin **b)
{
    value m, r;
    int i, st;
    if (!*b) {
        int n = bi_count();
        for (i = 0; i < n && !*b; i++) {
            const builtin *c = bi_get(i);
            if (!strcmp(c->name, name) && !mine(c)) *b = c;
        }
        if (!*b) return E_UNDEF;
    }
    if (a[0].t != T_UNIT) return (*b)->fn(V, argc, a, out);
    m = a[0];
    v_real(&a[0], &m.u.un.r);
    st = (*b)->fn(V, argc, a, &r);
    a[0] = m;
    if (st) return st;
    if (r.t != T_REAL) { v_decref(&r); return E_BADTYPE; }
    v_same(out, &r.u.r, &m);
    return 0;
}

#define MAGFN(f, nm) static BI(f) { static const builtin *b; return magfn(V, argc, a, out, nm, &b); }
MAGFN(u_abs, "ABS") MAGFN(u_ip, "IP") MAGFN(u_fp, "FP") MAGFN(u_floor, "FLOOR")
MAGFN(u_ceiling, "CEILING") MAGFN(u_round, "ROUND") MAGFN(u_truncate, "TRUNCATE")

/* TEVAL(expr): the time its evaluation takes, in seconds (User Guide: a measurement, as
 * 0.012_s; bezier, repeating); the clock is the host's millisecond TICKS */
static BI(b_teval)
{
    uint32_t t0, t1;
    value r;
    real sec, k;
    ufac f;
    int st;
    (void)argc;
    t0 = V->ticks ? V->ticks() : 0;
    if ((st = vm_run_thunk(V, &a[0], &r))) return st;
    t1 = V->ticks ? V->ticks() : 0;
    v_decref(&r);
    dec_from_int(&sec, (int32_t)(t1 - t0));
    dec_from_int(&k, 1000);
    dec_div(&sec, &sec, &k);
    f.id = (uint8_t)cat_find("s", 1); f.pre = 0; f.pow = 1;
    return mk(out, &sec, &f, 1);
}

const builtin bi_unit_table[] = {
    { "TEVAL", b_teval, 1, 1, BF_QUOTE1 },
    { "_UNIT", b_unit, 2, 2, 0 }, { "CONVERT", b_convert, 2, 2, 0 }, { "MKSA", b_mksa, 1, 1, 0 },
    { "UFACTOR", b_ufactor, 2, 2, 0 }, { "USIMPLIFY", b_usimplify, 1, 1, 0 },
    { "UPART", b_upart, 1, 1, 0 }, { "UVAL", b_uval, 1, 1, 0 },
    { "ABS", u_abs, 1, 1, 0 }, { "IP", u_ip, 1, 1, 0 }, { "FP", u_fp, 1, 1, 0 },
    { "FLOOR", u_floor, 1, 1, 0 }, { "CEILING", u_ceiling, 1, 1, 0 },
    { "ROUND", u_round, 1, 2, 0 }, { "TRUNCATE", u_truncate, 1, 2, 0 },
    { NULL, NULL, 0, 0, 0 }
};
