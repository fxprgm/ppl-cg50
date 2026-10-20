/* bi_list.c -- list, string and expression built-ins. */
#include "bi.h"
#include <stdlib.h>
#include <string.h>

int bi_str_new(value *out, const uint16_t *s, uint32_t n) { return v_str_u16(out, s, n); }
int bi_list_from(value *out, uint32_t n) { return v_list(out, n); }

void bi_text(ppl_vm *V, value *out, const value *v, int quote)
{
    sbuf s;
    sb_init(&s);
    v_to_text(V, &s, v, quote);
    v_str_utf8(out, s.p ? s.p : "", s.n);
    sb_free(&s);
}

/* ------------------------------------------------------------ sizes */

BI(b_size)
{
    (void)V; (void)argc;
    switch (a[0].t) {
    case T_LIST: v_int32(out, (int32_t)a[0].u.l->n); return 0;
    case T_STR: v_int32(out, (int32_t)a[0].u.s->len); return 0;
    case T_MAT: {
        int st;
        if (a[0].u.m->vec) {
            if ((st = v_list(out, 1))) return st;
            v_int32(&out->u.l->v[0], a[0].u.m->cols);
        } else {
            if ((st = v_list(out, 2))) return st;
            v_int32(&out->u.l->v[0], a[0].u.m->rows);
            v_int32(&out->u.l->v[1], a[0].u.m->cols);
        }
        return 0;
    }
    default: v_int32(out, 1); return 0;
    }
}

BI(b_dim) { return b_size(V, argc, a, out); }

/* ------------------------------------------------------------ lists */

BI(b_concat)
{
    int i, st;
    value r;
    (void)V;
    if (argc == 2 && a[0].t == T_STR && a[1].t == T_STR) return ar_binary(V, OP_ADD, &a[0], &a[1], out);
    if ((st = v_list(&r, 0))) return st;
    for (i = 0; i < argc; i++) {
        if (a[i].t == T_LIST) {
            uint32_t k;
            for (k = 0; k < a[i].u.l->n; k++) if ((st = list_push(r.u.l, &a[i].u.l->v[k]))) { v_decref(&r); return st; }
        } else if ((st = list_push(r.u.l, &a[i]))) { v_decref(&r); return st; }
    }
    *out = r;
    return 0;
}

static ppl_vm *sort_vm;
static int sort_key;
static int sort_err;

static int cmp_values(const value *x, const value *y)
{
    real a, b;
    if (x->t == T_STR && y->t == T_STR) {
        uint32_t i, k = sort_key > 0 ? (uint32_t)sort_key - 1 : 0;
        const pstr *s = x->u.s, *t = y->u.s;
        for (i = k; i < s->len && i < t->len; i++) if (s->c[i] != t->c[i]) return s->c[i] < t->c[i] ? -1 : 1;
        return s->len < t->len ? -1 : s->len > t->len ? 1 : 0;
    }
    if (x->t == T_LIST && y->t == T_LIST && sort_key > 0) {
        uint32_t k = (uint32_t)sort_key - 1;
        if (k < x->u.l->n && k < y->u.l->n) return cmp_values(&x->u.l->v[k], &y->u.l->v[k]);
        return 0;
    }
    if (to_real(x, &a) || to_real(y, &b)) { sort_err = E_BADTYPE; return 0; }
    return dec_cmp(&a, &b);
}

static int qcmp(const void *p, const void *q) { return cmp_values((const value *)p, (const value *)q); }

/* a vector as a list (new reference) and back: SORT and REVERSE take vectors too
 * (factoranalysis.ppl sorts EIGENVAL's vector) */
static int vec_to_list(const value *v, value *out)
{
    const pmat *m = v->u.m;
    int i, st;
    if ((st = v_list(out, m->cols))) return st;
    for (i = 0; i < m->cols; i++) v_copy(&out->u.l->v[i], &m->v[i]);
    return 0;
}

static int list_to_vec(value *l, value *out)
{
    uint32_t i, n = l->u.l->n;
    int st;
    if ((st = v_mat(out, 1, (int)n, 1))) { v_decref(l); return st; }
    for (i = 0; i < n; i++) v_copy(&out->u.m->v[i], &l->u.l->v[i]);
    v_decref(l);
    return 0;
}

#define IS_VEC(v) ((v)->t == T_MAT && (v)->u.m->vec)

BI(b_sort)
{
    plist *l;
    int32_t key = 0;
    if (IS_VEC(&a[0])) {
        value b[2], r;
        int st;
        if ((st = vec_to_list(&a[0], &b[0]))) return st;
        if (argc > 1) b[1] = a[1];
        st = b_sort(V, argc, b, &r);
        v_decref(&b[0]);
        if (st) return st;
        return list_to_vec(&r, out);
    }
    if (a[0].t != T_LIST) return E_BADTYPE;
    if (argc > 1 && bi_int(&a[1], &key)) return E_BADVAL;
    v_copy(out, &a[0]);
    l = list_own(out);
    if (!l) return E_MEM;
    sort_vm = V; sort_key = key; sort_err = 0;
    /* insertion sort: stable, and lists are short */
    {
        uint32_t i, j;
        for (i = 1; i < l->n; i++) {
            value t = l->v[i];
            for (j = i; j > 0 && cmp_values(&l->v[j - 1], &t) > 0; j--) l->v[j] = l->v[j - 1];
            l->v[j] = t;
        }
    }
    (void)qcmp;
    if (sort_err) { v_decref(out); return sort_err; }
    return 0;
}

BI(b_reverse)
{
    (void)V; (void)argc;
    if (IS_VEC(&a[0])) {
        int i, n = a[0].u.m->cols, st;
        if ((st = v_mat(out, 1, n, 1))) return st;
        for (i = 0; i < n; i++) v_copy(&out->u.m->v[i], &a[0].u.m->v[n - 1 - i]);
        return 0;
    }
    if (a[0].t == T_LIST) {
        uint32_t i, n = a[0].u.l->n;
        int st;
        if ((st = v_list(out, 0))) return st;
        for (i = 0; i < n; i++) list_push(out->u.l, &a[0].u.l->v[n - 1 - i]);
        return 0;
    }
    if (a[0].t == T_STR) {
        uint32_t i, n = a[0].u.s->len;
        int st;
        if ((st = v_str_u16(out, a[0].u.s->c, n))) return st;
        for (i = 0; i < n; i++) out->u.s->c[i] = a[0].u.s->c[n - 1 - i];
        return 0;
    }
    return E_BADTYPE;
}

BI(b_pos)
{
    uint32_t i;
    (void)V; (void)argc;
    if (a[0].t == T_STR && a[1].t == T_STR) {
        /* position of a substring */
        const pstr *s = a[0].u.s, *t = a[1].u.s;
        for (i = 0; i + t->len <= s->len; i++)
            if (!memcmp(s->c + i, t->c, t->len * sizeof(uint16_t))) { v_int32(out, (int32_t)i + 1); return 0; }
        v_int32(out, 0);
        return 0;
    }
    if (a[0].t != T_LIST) return E_BADTYPE;
    for (i = 0; i < a[0].u.l->n; i++) if (v_equal(&a[0].u.l->v[i], &a[1])) { v_int32(out, (int32_t)i + 1); return 0; }
    v_int32(out, 0);
    return 0;
}

BI(b_insert)
{
    int32_t p;
    plist *l;
    uint32_t i;
    (void)V; (void)argc;
    if (a[0].t != T_LIST) return E_BADTYPE;
    if (bi_int(&a[1], &p)) return E_BADVAL;
    if (p < 1 || (uint32_t)p > a[0].u.l->n + 1) return E_INDEX;
    v_copy(out, &a[0]);
    l = list_own(out);
    if (!l || list_push(l, &a[2])) return E_MEM;
    {
        value t = l->v[l->n - 1];
        for (i = l->n - 1; i > (uint32_t)p - 1; i--) l->v[i] = l->v[i - 1];
        l->v[p - 1] = t;
    }
    return 0;
}

BI(b_suppress)
{
    int32_t p;
    uint32_t i;
    int st;
    (void)V;
    if (a[0].t == T_STR && argc > 1 && a[1].t == T_STR) {
        /* remove every occurrence of a character */
        uint32_t k = 0;
        const pstr *s = a[0].u.s;
        if ((st = v_str_u16(out, s->c, s->len))) return st;
        for (i = 0; i < s->len; i++) {
            uint32_t j; int hit = 0;
            for (j = 0; j < a[1].u.s->len; j++) if (s->c[i] == a[1].u.s->c[j]) hit = 1;
            if (!hit) out->u.s->c[k++] = s->c[i];
        }
        out->u.s->len = k;
        return 0;
    }
    if (a[0].t != T_LIST) return E_BADTYPE;
    if (argc == 1) return v_list(out, 0);
    if (bi_int(&a[1], &p)) return E_BADVAL;
    if (p < 1 || (uint32_t)p > a[0].u.l->n) return E_INDEX;
    if ((st = v_list(out, 0))) return st;
    for (i = 0; i < a[0].u.l->n; i++) if (i != (uint32_t)p - 1) list_push(out->u.l, &a[0].u.l->v[i]);
    return 0;
}

static int fold(ppl_vm *V, const value *l, int op, value *out)
{
    uint32_t i;
    value acc;
    const value *el;
    uint32_t n;
    /* a vector too: ΣLIST(variance(matrix)) in factoranalysis.ppl (assumed) */
    if (l->t == T_LIST) { el = l->u.l->v; n = l->u.l->n; }
    else if (IS_VEC(l)) { el = l->u.m->v; n = l->u.m->cols; }
    else return E_BADTYPE;
    if (!n) { v_int32(out, op == OP_MUL ? 1 : 0); return 0; }
    v_copy(&acc, &el[0]);
    for (i = 1; i < n; i++) {
        value t;
        int st = ar_binary(V, op, &acc, &el[i], &t);
        v_decref(&acc);
        if (st) return st;
        acc = t;
    }
    *out = acc;
    return 0;
}

BI(b_sigmalist) { (void)argc; return fold(V, &a[0], OP_ADD, out); }
BI(b_pilist) { (void)argc; return fold(V, &a[0], OP_MUL, out); }

BI(b_deltalist)
{
    uint32_t i, n;
    int st;
    (void)argc;
    if (a[0].t != T_LIST) return E_BADTYPE;
    n = a[0].u.l->n;
    if ((st = v_list(out, n ? n - 1 : 0))) return st;
    for (i = 1; i < n; i++) {
        value t;
        if ((st = ar_binary(V, OP_SUB, &a[0].u.l->v[i], &a[0].u.l->v[i - 1], &t))) { v_decref(out); return st; }
        out->u.l->v[i - 1] = t;
    }
    return 0;
}

static int in_list(const value *l, const value *v)
{
    uint32_t i;
    for (i = 0; i < l->u.l->n; i++) if (v_equal(&l->u.l->v[i], v)) return 1;
    return 0;
}

static int setop(const value *x, const value *y, value *out, int kind)
{
    uint32_t i;
    int st;
    if (x->t != T_LIST || y->t != T_LIST) return E_BADTYPE;
    if ((st = v_list(out, 0))) return st;
    if (kind == 0) {          /* union */
        for (i = 0; i < x->u.l->n; i++) if (!in_list(out, &x->u.l->v[i])) list_push(out->u.l, &x->u.l->v[i]);
        for (i = 0; i < y->u.l->n; i++) if (!in_list(out, &y->u.l->v[i])) list_push(out->u.l, &y->u.l->v[i]);
    } else if (kind == 1) {   /* intersection */
        for (i = 0; i < x->u.l->n; i++)
            if (in_list(y, &x->u.l->v[i]) && !in_list(out, &x->u.l->v[i])) list_push(out->u.l, &x->u.l->v[i]);
    } else {                  /* symmetric difference */
        for (i = 0; i < x->u.l->n; i++)
            if (!in_list(y, &x->u.l->v[i]) && !in_list(out, &x->u.l->v[i])) list_push(out->u.l, &x->u.l->v[i]);
        for (i = 0; i < y->u.l->n; i++)
            if (!in_list(x, &y->u.l->v[i]) && !in_list(out, &y->u.l->v[i])) list_push(out->u.l, &y->u.l->v[i]);
    }
    return 0;
}

/* UNION(x, ...): every argument's elements (a list's or a vector's; any other
 * value is one element), duplicates removed, in order.  One argument dedupes a
 * list; the User Guide: UNION({1,2,3},{2,4,8},10) = {1,2,3,4,8,10}. */
BI(b_union)
{
    int i, st;
    (void)V;
    if ((st = v_list(out, 0))) return st;
    for (i = 0; i < argc; i++) {
        const value *el = &a[i];
        uint32_t k, n = 1;
        if (a[i].t == T_LIST) { el = a[i].u.l->v; n = a[i].u.l->n; }
        else if (IS_VEC(&a[i])) { el = a[i].u.m->v; n = (uint32_t)a[i].u.m->cols; }
        for (k = 0; k < n; k++)
            if (!in_list(out, &el[k]) && (st = list_push(out->u.l, &el[k]))) { v_decref(out); return st; }
    }
    return 0;
}

/* INTERSECT(list, list or value): the elements of the first that are in the
 * second, duplicates of the first kept (as giac's intersect: listapi.ppl's
 * ListMODE counts occurrences with SIZE(INTERSECT(L, x)); assumed) */
BI(b_intersect)
{
    uint32_t i;
    int st;
    value y;
    (void)V; (void)argc;
    if (a[0].t != T_LIST) return E_BADTYPE;
    if (a[1].t == T_LIST) v_copy(&y, &a[1]);
    else {
        if ((st = v_list(&y, 0))) return st;
        if ((st = list_push(y.u.l, &a[1]))) { v_decref(&y); return st; }
    }
    if ((st = v_list(out, 0))) { v_decref(&y); return st; }
    for (i = 0; i < a[0].u.l->n; i++)
        if (in_list(&y, &a[0].u.l->v[i]) && (st = list_push(out->u.l, &a[0].u.l->v[i]))) break;
    v_decref(&y);
    if (st) v_decref(out);
    return st;
}
BI(b_difference)
{
    int i, st;
    value acc;
    (void)V;
    if ((st = setop(&a[0], &a[1], &acc, 2))) return st;
    for (i = 2; i < argc; i++) {   /* more lists: fold */
        value t;
        st = setop(&acc, &a[i], &t, 2);
        v_decref(&acc);
        if (st) return st;
        acc = t;
    }
    *out = acc;
    return 0;
}

BI(b_eq) { (void)V; (void)argc; v_int32(out, v_equal(&a[0], &a[1])); return 0; }

/* MAKELIST(expr, var, start, end [, step]) */
BI(b_makelist)
{
    real x, end, step, z;
    value saved, xv, r;
    int st, up;
    uint32_t guard = 0;
    if (argc < 4) {
        /* MAKELIST(value, n): n copies */
        int32_t n, i;
        value v;
        if (argc != 2 || bi_int(&a[1], &n) || n < 0) return E_BADVAL;
        if ((st = vm_run_thunk(V, &a[0], &v))) return st;
        if ((st = v_list(&r, 0))) { v_decref(&v); return st; }
        for (i = 0; i < n; i++) list_push(r.u.l, &v);
        v_decref(&v);
        *out = r;
        return 0;
    }
    if (bi_real(&a[2], &x) || bi_real(&a[3], &end)) return E_BADTYPE;
    if (argc > 4) { if (bi_real(&a[4], &step)) return E_BADTYPE; } else dec_from_int(&step, 1);
    dec_zero(&z);
    if (dec_is_zero(&step)) return E_BADVAL;
    up = dec_cmp(&step, &z) > 0;
    vm_load_ref(V, &a[1], &saved);
    if ((st = v_list(&r, 0))) { v_decref(&saved); return st; }
    while (up ? dec_cmp(&x, &end) <= 0 : dec_cmp(&x, &end) >= 0) {
        value e;
        v_real(&xv, &x);
        if ((st = vm_store_ref(V, &a[1], &xv))) break;
        if ((st = vm_run_thunk(V, &a[0], &e))) break;
        st = list_push(r.u.l, &e);
        v_decref(&e);
        if (st) break;
        dec_add(&x, &x, &step);
        if (++guard > 1000000u) { st = E_MEM; break; }
    }
    vm_store_ref(V, &a[1], &saved);
    v_decref(&saved);
    if (st) { v_decref(&r); return st; }
    *out = r;
    return 0;
}

/* Σ(expr, var, start, end) */
BI(b_sigma)
{
    real x, end, one;
    value saved, xv, acc;
    int st = 0;
    (void)argc;
    if (bi_real(&a[2], &x) || bi_real(&a[3], &end)) return E_BADTYPE;
    dec_from_int(&one, 1);
    vm_load_ref(V, &a[1], &saved);
    v_int32(&acc, 0);
    while (dec_cmp(&x, &end) <= 0) {
        value e, t;
        v_real(&xv, &x);
        if ((st = vm_store_ref(V, &a[1], &xv))) break;
        if ((st = vm_run_thunk(V, &a[0], &e))) break;
        st = ar_binary(V, OP_ADD, &acc, &e, &t);
        v_decref(&e);
        if (st) break;
        v_decref(&acc);
        acc = t;
        dec_add(&x, &x, &one);
    }
    vm_store_ref(V, &a[1], &saved);
    v_decref(&saved);
    if (st) { v_decref(&acc); return st; }
    *out = acc;
    return 0;
}

/* ITERATE(expr, var, start, count) */
BI(b_iterate)
{
    int32_t n, i;
    value saved, cur;
    int st = 0;
    (void)argc;
    if (bi_int(&a[3], &n) || n < 0) return E_BADVAL;
    vm_load_ref(V, &a[1], &saved);
    v_copy(&cur, &a[2]);
    for (i = 0; i < n; i++) {
        value e;
        if ((st = vm_store_ref(V, &a[1], &cur))) break;
        if ((st = vm_run_thunk(V, &a[0], &e))) break;
        v_decref(&cur);
        cur = e;
    }
    vm_store_ref(V, &a[1], &saved);
    v_decref(&saved);
    if (st) { v_decref(&cur); return st; }
    *out = cur;
    return 0;
}

BI(b_evallist) { (void)V; (void)argc; v_copy(out, &a[0]); return 0; }

/* EXECON("&1+&2", list1, list2...): the expression with &k replaced */
BI(b_execon)
{
    uint32_t n, i;
    int st, k;
    char *src;
    size_t sl;
    if (a[0].t != T_STR) return E_BADTYPE;
    for (k = 1; k < argc; k++) if (a[k].t != T_LIST) return E_BADTYPE;
    n = a[1].u.l->n;
    for (k = 2; k < argc; k++) if (a[k].u.l->n < n) n = a[k].u.l->n;
    sl = u16_to_utf8(NULL, a[0].u.s->c, a[0].u.s->len);
    src = (char *)malloc(sl + 1);
    if (!src) return E_MEM;
    u16_to_utf8(src, a[0].u.s->c, a[0].u.s->len);
    src[sl] = 0;
    if ((st = v_list(out, 0))) { free(src); return st; }
    for (i = 0; i < n; i++) {
        sbuf e;
        const char *p;
        value r;
        sb_init(&e);
        for (p = src; *p; p++) {
            if (*p == '&' && p[1] >= '1' && p[1] <= '9') {
                int j = p[1] - '0';
                sb_puts(&e, "(");
                if (j < argc) v_to_text(V, &e, &a[j].u.l->v[i], 1);
                else sb_puts(&e, "0");
                sb_puts(&e, ")");
                p++;
            } else sb_put(&e, p, 1);
        }
        st = vm_eval_string(V, e.p ? e.p : "", e.n, &r);
        sb_free(&e);
        if (st) { v_decref(out); free(src); return st; }
        list_push(out->u.l, &r);
        v_decref(&r);
    }
    free(src);
    return 0;
}

/* ------------------------------------------------------------ strings */

static int need_str(const value *v) { return v->t == T_STR ? 0 : E_BADTYPE; }

BI(b_asc)
{
    uint32_t i;
    int st;
    (void)V; (void)argc;
    if (need_str(&a[0])) return E_BADTYPE;
    if ((st = v_list(out, a[0].u.s->len))) return st;
    for (i = 0; i < a[0].u.s->len; i++) v_int32(&out->u.l->v[i], a[0].u.s->c[i]);
    return 0;
}

BI(b_char)
{
    uint16_t buf[512];
    uint32_t n = 0, i;
    int32_t c;
    (void)V; (void)argc;
    if (a[0].t == T_LIST) {
        for (i = 0; i < a[0].u.l->n && n < 512; i++) {
            if (bi_int(&a[0].u.l->v[i], &c) || c < 0 || c > 0xFFFF) return E_BADVAL;
            buf[n++] = (uint16_t)c;
        }
    } else {
        if (bi_int(&a[0], &c) || c < 0 || c > 0xFFFF) return E_BADVAL;
        buf[n++] = (uint16_t)c;
    }
    return v_str_u16(out, buf, n);
}

BI(b_left)
{
    int32_t n;
    uint32_t len;
    (void)V; (void)argc;
    if (need_str(&a[0])) return E_BADTYPE;
    if (bi_int(&a[1], &n)) return E_BADVAL;
    if (n < 0) return E_BADVAL;
    len = a[0].u.s->len;
    if (n == 0 || (uint32_t)n > len) n = (int32_t)len;
    return v_str_u16(out, a[0].u.s->c, (uint32_t)n);
}

BI(b_right)
{
    int32_t n;
    uint32_t len;
    (void)V; (void)argc;
    if (need_str(&a[0])) return E_BADTYPE;
    if (bi_int(&a[1], &n)) return E_BADVAL;
    if (n < 0) return E_BADVAL;
    len = a[0].u.s->len;
    if (n == 0 || (uint32_t)n > len) n = (int32_t)len;
    return v_str_u16(out, a[0].u.s->c + len - (uint32_t)n, (uint32_t)n);
}

BI(b_mid)
{
    int32_t s, n;
    uint32_t len;
    (void)V;
    if (need_str(&a[0])) return E_BADTYPE;
    if (bi_int(&a[1], &s)) return E_BADVAL;
    len = a[0].u.s->len;
    if (s < 1) return E_BADVAL;
    if (argc > 2) { if (bi_int(&a[2], &n)) return E_BADVAL; if (n < 0) n = (int32_t)len; }
    else n = (int32_t)len;
    if ((uint32_t)s > len) return v_str_u16(out, NULL, 0);
    if ((uint32_t)(s - 1 + n) > len) n = (int32_t)len - (s - 1);
    return v_str_u16(out, a[0].u.s->c + s - 1, (uint32_t)n);
}

BI(b_instring)
{
    const pstr *s, *t;
    uint32_t i;
    (void)V; (void)argc;
    if (need_str(&a[0]) || need_str(&a[1])) return E_BADTYPE;
    s = a[0].u.s; t = a[1].u.s;
    if (!t->len) { v_int32(out, 1); return 0; }
    for (i = 0; i + t->len <= s->len; i++)
        if (!memcmp(s->c + i, t->c, t->len * sizeof(uint16_t))) { v_int32(out, (int32_t)i + 1); return 0; }
    v_int32(out, 0);
    return 0;
}

static uint16_t lower1(uint16_t c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (uint16_t)(c + 32);
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return (uint16_t)(c + 32);
    return c;
}
static uint16_t upper1(uint16_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return (uint16_t)(c - 32);
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return (uint16_t)(c - 32);
    return c;
}

static int casemap(const value *s, value *out, uint16_t (*f)(uint16_t))
{
    uint32_t i;
    int st;
    if (need_str(s)) return E_BADTYPE;
    if ((st = v_str_u16(out, s->u.s->c, s->u.s->len))) return st;
    for (i = 0; i < s->u.s->len; i++) out->u.s->c[i] = f(out->u.s->c[i]);
    return 0;
}
BI(b_lower) { (void)V; (void)argc; return casemap(&a[0], out, lower1); }
BI(b_upper) { (void)V; (void)argc; return casemap(&a[0], out, upper1); }

BI(b_rotate)
{
    int32_t n;
    uint32_t len, i;
    int st;
    (void)V;
    if (need_str(&a[0])) return E_BADTYPE;
    if (argc > 1) { if (bi_int(&a[1], &n)) return E_BADVAL; } else n = -1;
    len = a[0].u.s->len;
    if ((st = v_str_u16(out, a[0].u.s->c, len))) return st;
    if (!len) return 0;
    n %= (int32_t)len;
    if (n < 0) n += (int32_t)len;
    for (i = 0; i < len; i++) out->u.s->c[i] = a[0].u.s->c[(i + (uint32_t)n) % len];
    return 0;
}

BI(b_replace)
{
    (void)V;
    if (a[0].t == T_STR && argc == 3 && a[1].t == T_STR && a[2].t == T_STR) {
        /* replace every occurrence */
        const pstr *s = a[0].u.s, *f = a[1].u.s, *r = a[2].u.s;
        uint16_t *buf;
        uint32_t i = 0, k = 0, cap;
        int st;
        if (!f->len) { v_copy(out, &a[0]); return 0; }
        cap = s->len * (r->len + 1) + 1;
        buf = (uint16_t *)malloc(cap * sizeof(uint16_t));
        if (!buf) return E_MEM;
        while (i < s->len) {
            if (i + f->len <= s->len && !memcmp(s->c + i, f->c, f->len * sizeof(uint16_t))) {
                memcpy(buf + k, r->c, r->len * sizeof(uint16_t));
                k += r->len; i += f->len;
            } else buf[k++] = s->c[i++];
        }
        st = v_str_u16(out, buf, k);
        free(buf);
        return st;
    }
    if (a[0].t == T_STR && argc == 3 && a[2].t == T_STR) {
        /* REPLACE(str, start, new): overwrite from position start */
        int32_t p;
        uint32_t len = a[0].u.s->len, nl, i;
        int st;
        if (bi_int(&a[1], &p) || p < 1) return E_BADVAL;
        nl = (uint32_t)p - 1 + a[2].u.s->len;
        if (nl < len) nl = len;
        if ((st = v_str_u16(out, NULL, 0))) return st;
        v_decref(out);
        {
            uint16_t *buf = (uint16_t *)calloc(nl + 1, sizeof(uint16_t));
            if (!buf) return E_MEM;
            memcpy(buf, a[0].u.s->c, len * sizeof(uint16_t));
            for (i = len; i < (uint32_t)p - 1; i++) buf[i] = ' ';
            memcpy(buf + p - 1, a[2].u.s->c, a[2].u.s->len * sizeof(uint16_t));
            st = v_str_u16(out, buf, nl);
            free(buf);
        }
        return st;
    }
    if (a[0].t == T_MAT && argc == 3) {
        /* REPLACE(M, {row, col}, submatrix) */
        int32_t r0, c0;
        pmat *m;
        const pmat *s;
        int i, j, st;
        if (a[1].t != T_LIST || a[1].u.l->n < 2 || a[2].t != T_MAT) {
            if (a[1].t == T_REAL && a[2].t == T_MAT && a[0].u.m->vec) {
                if (bi_int(&a[1], &c0)) return E_BADVAL;
                r0 = 1;
            } else return E_BADTYPE;
        } else if (bi_int(&a[1].u.l->v[0], &r0) || bi_int(&a[1].u.l->v[1], &c0)) return E_BADVAL;
        v_copy(out, &a[0]);
        m = mat_own(out);
        s = a[2].u.m;
        if (!m) return E_MEM;
        for (i = 0; i < s->rows; i++)
            for (j = 0; j < s->cols; j++) {
                int r = r0 - 1 + i, c = c0 - 1 + j;
                if (r < 0 || c < 0 || r >= m->rows || c >= m->cols) continue;
                m->v[r * m->cols + c] = s->v[i * s->cols + j];
            }
        st = 0;
        return st;
    }
    return E_BADTYPE;
}

BI(b_string)
{
    if (argc >= 2 && (a[0].t == T_REAL || a[0].t == T_CPLX || a[0].t == T_LIST || a[0].t == T_MAT)) {
        /* STRING(value, format [, digits]) */
        int32_t f, d = V->set.digits;
        int sf = V->set.format, sd = V->set.digits;
        if (bi_int(&a[1], &f)) return E_BADVAL;
        if (argc > 2 && bi_int(&a[2], &d)) return E_BADVAL;
        if (f >= 1 && f <= 4) { V->set.format = f - 1; V->set.digits = d; }
        bi_text(V, out, &a[0], 1);
        V->set.format = sf; V->set.digits = sd;
        return 0;
    }
    bi_text(V, out, &a[0], 1);
    return 0;
}

BI(b_expr)
{
    char *s;
    size_t n;
    int st;
    (void)argc;
    if (a[0].t != T_STR) { v_copy(out, &a[0]); return 0; }
    n = u16_to_utf8(NULL, a[0].u.s->c, a[0].u.s->len);
    s = (char *)malloc(n + 1);
    if (!s) return E_MEM;
    u16_to_utf8(s, a[0].u.s->c, a[0].u.s->len);
    s[n] = 0;
    st = vm_eval_string(V, s, n, out);
    free(s);
    return st;
}

BI(b_strfromid)
{
    int32_t n;
    const char *m;
    (void)V; (void)argc;
    if (bi_int(&a[0], &n)) return E_BADVAL;
    m = ppl_errmsg(n);
    return v_str_utf8(out, m, strlen(m));
}

/* ------------------------------------------------------------ conversions */

BI(b_listtomat)
{
    const plist *l;
    uint32_t i, j, rows, cols;
    int st;
    (void)V; (void)argc;
    if (a[0].t != T_LIST) return E_BADTYPE;
    l = a[0].u.l;
    if (l->n && l->v[0].t == T_LIST) {
        rows = l->n; cols = l->v[0].u.l->n;
        if ((st = v_mat(out, (int)rows, (int)cols, 0))) return st;
        for (i = 0; i < rows; i++) {
            if (l->v[i].t != T_LIST || l->v[i].u.l->n != cols) { v_decref(out); return E_DIM; }
            for (j = 0; j < cols; j++) {
                value e = l->v[i].u.l->v[j];
                if (e.t != T_REAL && e.t != T_CPLX) { v_decref(out); return E_BADTYPE; }
                out->u.m->v[i * cols + j] = e;
            }
        }
        return 0;
    }
    if ((st = v_mat(out, 1, (int)l->n, 1))) return st;
    for (i = 0; i < l->n; i++) {
        if (l->v[i].t != T_REAL && l->v[i].t != T_CPLX) { v_decref(out); return E_BADTYPE; }
        out->u.m->v[i] = l->v[i];
    }
    return 0;
}

BI(b_mattolist)
{
    const pmat *m;
    int i, j, st;
    (void)V; (void)argc;
    if (a[0].t != T_MAT) return E_BADTYPE;
    m = a[0].u.m;
    if (m->vec) {
        if ((st = v_list(out, m->cols))) return st;
        for (i = 0; i < m->cols; i++) out->u.l->v[i] = m->v[i];
        return 0;
    }
    if ((st = v_list(out, m->rows))) return st;
    for (i = 0; i < m->rows; i++) {
        value row;
        v_list(&row, m->cols);
        for (j = 0; j < m->cols; j++) row.u.l->v[j] = m->v[i * m->cols + j];
        out->u.l->v[i] = row;
    }
    return 0;
}

/* list2mat(list [, n]): with n, the list cut into rows of n, the last row
 * padded with zeros (User Guide); without, as ListToMat */
BI(b_list2mat)
{
    const plist *l;
    int32_t n, rows, i;
    int st;
    if (argc < 2) return b_listtomat(V, argc, a, out);
    if (a[0].t != T_LIST) return E_BADTYPE;
    if (bi_int(&a[1], &n) || n < 1) return E_BADVAL;
    l = a[0].u.l;
    if (!l->n) return E_DIM;
    rows = (int32_t)((l->n + (uint32_t)n - 1) / (uint32_t)n);
    if (rows > 65535 || n > 65535) return E_DIM;
    if ((st = v_mat(out, rows, n, 0))) return st;
    for (i = 0; i < (int32_t)l->n; i++) {
        value e = l->v[i];
        if (e.t == T_INT) { real x; to_real(&e, &x); v_real(&e, &x); }
        if (e.t != T_REAL && e.t != T_CPLX) { v_decref(out); return E_BADTYPE; }
        out->u.m->v[i] = e;
    }
    return 0;
}

static int flat_push(value *out, const value *v, int depth)
{
    int st;
    if (v->t == T_MAT) {
        int k, n = (v->u.m->vec ? 1 : v->u.m->rows) * v->u.m->cols;
        for (k = 0; k < n; k++) if ((st = list_push(out->u.l, &v->u.m->v[k]))) return st;
        return 0;
    }
    if (v->t == T_LIST && depth == 0) {
        uint32_t k;
        for (k = 0; k < v->u.l->n; k++) if ((st = flat_push(out, &v->u.l->v[k], 1))) return st;
        return 0;
    }
    return list_push(out->u.l, v);
}

/* mat2list(matrix, vector or list): the elements in row order, as a list.  The
 * User Guide shows mat2list([[1 8],[4 9]]) = [1 8 4 9]; the type is a list
 * because repeating.ppl checks TYPE(mat2list(idivis(n))) == TYPE({}) ("empirical
 * test" in its source).  A list's sub-lists and vectors are spread one level. */
BI(b_mat2list)
{
    int st;
    (void)V; (void)argc;
    if (a[0].t != T_MAT && a[0].t != T_LIST) return E_BADTYPE;
    if ((st = v_list(out, 0))) return st;
    if ((st = flat_push(out, &a[0], 0))) { v_decref(out); return st; }
    return 0;
}

/* remove(value, list or vector): without the elements equal to value (giac;
 * the test form remove(x→test, l) needs a CAS function and is not here) */
BI(b_remove)
{
    const value *el;
    uint32_t i, n;
    int st;
    value r;
    (void)V; (void)argc;
    if (a[1].t == T_LIST) { el = a[1].u.l->v; n = a[1].u.l->n; }
    else if (IS_VEC(&a[1])) { el = a[1].u.m->v; n = (uint32_t)a[1].u.m->cols; }
    else return E_BADTYPE;
    if ((st = v_list(&r, 0))) return st;
    for (i = 0; i < n; i++)
        if (!v_equal(&el[i], &a[0]) && (st = list_push(r.u.l, &el[i]))) { v_decref(&r); return st; }
    if (a[1].t == T_LIST) { *out = r; return 0; }
    return list_to_vec(&r, out);
}

/* placeholder for list functions not written yet */
BI(b_listfn) { (void)V; (void)argc; (void)a; (void)out; return E_NOTIMPL; }
