/* vm.c -- the PPL virtual machine: programs, frames, the interpreter loop,
 * indexing, run-time names, Home settings and EXPR. */
#include "vm.h"
#include "bc.h"
#include "compile.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

/* ------------------------------------------------------------ settings */

static const struct { const char *name; size_t off; int lo, hi; } SETS[] = {
    { "HAngle", offsetof(ppl_settings, angle), 0, 2 },
    { "HFormat", offsetof(ppl_settings, format), 0, 3 },
    { "HDigits", offsetof(ppl_settings, digits), 0, 11 },
    { "HComplex", offsetof(ppl_settings, complex_ok), 0, 1 },
    { "Base", offsetof(ppl_settings, base), 0, 3 },
    { "Bits", offsetof(ppl_settings, bits), 1, 64 },
    { "Signed", offsetof(ppl_settings, is_signed), 0, 1 },
    { "Entry", offsetof(ppl_settings, entry), 0, 2 },
    { "Language", offsetof(ppl_settings, language), 1, 10 },
    { "HSeparator", offsetof(ppl_settings, separator), 0, 10 },
    { NULL, 0, 0, 0 }
};

/* the current app's settings override Home's when non-zero: AAngle 1
 * radians, 2 degrees; AComplex 1 off, 2 on; AFormat n = Home format n-1
 * with ADigits (all measured on the Virtual Calculator, Function app) */
static const char *APPSETS[] = { "AAngle", "AComplex", "ADigits", "AFormat", NULL };

static void effective(ppl_vm *V)
{
    V->set = V->hset;
    if (V->appset[0] == 1) V->set.angle = ANG_RAD;
    else if (V->appset[0] == 2) V->set.angle = ANG_DEG;
    if (V->appset[1]) V->set.complex_ok = V->appset[1] == 2;
    if (V->appset[3]) { V->set.format = V->appset[3] - 1; V->set.digits = V->appset[2]; }
}

static const char *PLOTVARS[] = { "Xmin", "Xmax", "Ymin", "Ymax", NULL };

static int sysinfo_get(ppl_vm *V, const char *name, value *out)
{
    int i;
    for (i = 0; PLOTVARS[i]; i++) if (!strcmp(PLOTVARS[i], name)) { v_real(out, &V->plot[i]); return 0; }
    int t[6] = { 2026, 1, 1, 0, 0, 0 };
    if (!strcmp(name, "Date") || !strcmp(name, "Time")) {
        real r, h;
        if (V->now) V->now(t);
        if (name[0] == 'D') {
            dec_from_int(&r, t[0] * 10000 + t[1] * 100 + t[2]);
            dec_from_int(&h, 10000);
        } else {
            dec_from_int(&r, t[3] * 3600 + t[4] * 60 + t[5]);
            dec_from_int(&h, 3600);
        }
        dec_div(&r, &r, &h);
        v_real(out, &r);
        if (name[0] == 'T') out->x = 1;
        return 0;
    }
    if (!strcmp(name, "HVars") || !strcmp(name, "Programs")) {
        value s;
        v_list(out, 0);
        if (name[0] == 'H') {
            hvar *h;
            for (h = V->hvars; h; h = h->next) { v_str_utf8(&s, h->name, strlen(h->name)); list_push(out->u.l, &s); v_decref(&s); }
        } else {
            pprog *p;
            for (p = V->progs; p; p = p->next) { v_str_utf8(&s, p->name, strlen(p->name)); list_push(out->u.l, &s); v_decref(&s); }
        }
        return 0;
    }
    if (!strcmp(name, "Notes")) { v_list(out, 0); return 0; }
    return -1;
}

int vm_setting_get(ppl_vm *V, const char *name, value *out)
{
    int i;
    if (!sysinfo_get(V, name, out)) return 0;
    for (i = 0; SETS[i].name; i++)
        if (!strcmp(SETS[i].name, name)) { v_int32(out, *(int *)((char *)&V->hset + SETS[i].off)); return 0; }
    for (i = 0; APPSETS[i]; i++)
        if (!strcmp(APPSETS[i], name)) { v_int32(out, V->appset[i]); return 0; }
    return -1;
}

int vm_setting_set(ppl_vm *V, const char *name, const value *v)
{
    int i;
    int32_t n;
    for (i = 0; PLOTVARS[i]; i++)
        if (!strcmp(PLOTVARS[i], name)) { if (to_real(v, &V->plot[i])) return E_BADTYPE; return 0; }
    for (i = 0; SETS[i].name; i++)
        if (!strcmp(SETS[i].name, name)) {
            if (to_index(v, &n)) return E_BADTYPE;
            if (n < SETS[i].lo || n > SETS[i].hi) return E_BADVAL;
            *(int *)((char *)&V->hset + SETS[i].off) = n;
            effective(V);
            return 0;
        }
    for (i = 0; APPSETS[i]; i++)
        if (!strcmp(APPSETS[i], name)) {
            static const int hi[4] = { 2, 2, 11, 4 };
            if (to_index(v, &n)) return E_BADTYPE;
            if (n < 0 || n > hi[i]) return E_BADVAL;
            V->appset[i] = n;
            effective(V);
            return 0;
        }
    return -1;
}

/* ------------------------------------------------------------ init */

static pprog home_prog = { (char *)"Home", NULL, 0, 0, NULL, 0, 0, NULL };

void ppl_init(ppl_vm *V)
{
    int i;
    memset(V, 0, sizeof *V);
    V->set.angle = ANG_RAD;
    V->set.format = FMT_STD;
    V->set.digits = 8;
    V->set.complex_ok = 0;
    V->set.base = 3;
    V->set.bits = 32;
    V->set.is_signed = 0;
    V->set.language = 1;
    V->hset = V->set;
    V->appset[2] = 4;
    dec_from_str(&V->plot[0], "-15.9", NULL); dec_from_str(&V->plot[1], "15.9", NULL);
    dec_from_str(&V->plot[2], "-10.9", NULL); dec_from_str(&V->plot[3], "10.9", NULL);
    for (i = 0; i < SV_COUNT; i++) {
        if (i >= SV_L0 && i < SV_L0 + 10) v_list(&V->sysvars[i], 0);
        else if (i >= SV_M0 && i < SV_M0 + 10) v_mat(&V->sysvars[i], 1, 1, 0);
        else v_int32(&V->sysvars[i], 0);
    }
    V->rng[0] = 0x12345678u; V->rng[1] = 0x9ABCDEF1u; V->rng[2] = 0x0F1E2D3Cu; V->rng[3] = 0x4B5A6978u;
}

static void free_func(pfunc *f)
{
    int i;
    if (!f) return;
    for (i = 0; i < f->nk; i++) v_decref(&f->k[i]);
    for (i = 0; i < f->nnames; i++) free(f->names[i]);
    free(f->names);
    free(f->nc);
    free(f->k);
    free(f->code);
    free(f->name);
    free(f);
}

static void free_prog(pprog *p)
{
    int i;
    for (i = 0; i < p->nf; i++) free_func(p->f[i]);
    for (i = 0; i < p->ng; i++) { v_decref(&p->g[i].v); free(p->g[i].name); }
    free(p->f); free(p->g); free(p->name);
    free(p);
}

void ppl_free(ppl_vm *V)
{
    int i;
    hvar *h;
    while (V->sp) v_decref(&V->stack[--V->sp]);
    for (i = 0; i < SV_COUNT; i++) v_decref(&V->sysvars[i]);
    while (V->progs) { pprog *n = V->progs->next; free_prog(V->progs); V->progs = n; }
    for (h = V->hvars; h;) { hvar *n = h->next; v_decref(&h->v); free(h->name); free(h); h = n; }
    V->hvars = NULL;
}

static char *xstrdup(const char *s)
{
    char *d = (char *)malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

static pfunc *new_func(pprog *p, const char *name)
{
    pfunc *f = (pfunc *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->name = xstrdup(name);
    f->prog = p;
    return f;
}

/* ------------------------------------------------------------ loading */

int ppl_load(ppl_vm *V, const char *name, const char *src, size_t n, char *msg, size_t msgn)
{
    parser P;
    pprog *prog, **pp;
    int i, st = 0, errline = 0;
    char m[100];
    if (msg && msgn) msg[0] = 0;
    parser_init(&P, src, n);
    if (parse_program(&P)) {
        if (msg) { snprintf(msg, msgn, "line %d: %s", P.errline, P.msg); }
        V->errline = P.errline;
        parser_free(&P);
        return E_SYNTAX;
    }
    prog = (pprog *)calloc(1, sizeof *prog);
    prog->name = xstrdup(name);
    /* register variables and functions */
    for (i = 0; i < P.ndecls; i++) {
        topdecl *d = &P.decls[i];
        int j, found = -1;
        if (d->kind == 2) {
            for (j = 0; j < prog->ng; j++) if (!strcmp(prog->g[j].name, d->name)) found = j;
            if (found >= 0) { if (d->exported) prog->g[found].exported = 1; continue; }
            if (prog->ng == prog->capg) {
                prog->capg = prog->capg ? prog->capg * 2 : 8;
                prog->g = (pvar *)realloc(prog->g, (size_t)prog->capg * sizeof(pvar));
            }
            prog->g[prog->ng].name = xstrdup(d->name);
            prog->g[prog->ng].exported = (uint8_t)d->exported;
            v_int32(&prog->g[prog->ng].v, 0);
            prog->ng++;
        } else {
            for (j = 0; j < prog->nf; j++) if (!strcmp(prog->f[j]->name, d->name)) found = j;
            if (found >= 0) {
                if (d->exported) prog->f[found]->exported = 1;
                if (d->kind == 1 && prog->f[found]->line) { st = E_SYNTAX; errline = d->line; snprintf(m, sizeof m, "%s defined twice", d->name); break; }
                if (d->kind == 1) prog->f[found]->line = d->line;
                continue;
            }
            if (prog->nf == prog->capf) {
                prog->capf = prog->capf ? prog->capf * 2 : 8;
                prog->f = (pfunc **)realloc(prog->f, (size_t)prog->capf * sizeof(pfunc *));
            }
            prog->f[prog->nf] = new_func(prog, d->name);
            prog->f[prog->nf]->exported = (uint8_t)d->exported;
            prog->f[prog->nf]->nparams = (uint8_t)d->nparams;
            if (d->kind == 1) prog->f[prog->nf]->line = d->line;
            prog->nf++;
        }
    }
    /* compile the bodies */
    for (i = 0; i < P.ndecls && !st; i++) {
        topdecl *d = &P.decls[i];
        int j;
        if (d->kind != 1) continue;
        for (j = 0; j < prog->nf; j++) if (!strcmp(prog->f[j]->name, d->name)) break;
        st = compile_function(V, prog, prog->f[j], d->params, d->nparams, d->body, m, sizeof m, &errline);
    }
    if (!st) {
        int any = 0;
        for (i = 0; i < P.ndecls; i++) if (P.decls[i].kind == 2 && P.decls[i].body) any = 1;
        if (any) {
            pfunc *init = new_func(prog, "(init)");
            st = compile_inits(V, prog, init, P.decls, P.ndecls, m, sizeof m, &errline);
            if (!st) {
                /* run it */
                pprog *saved = V->progs;
                prog->next = V->progs;
                V->progs = prog;
                {
                    value r;
                    int e = 0;
                    extern int vm_run_func(ppl_vm *, pfunc *, int, const value *, value *);
                    /* an error in an initialiser leaves the variable at 0 (the
                     * program still loads; the error shows when it is used) */
                    e = vm_run_func(V, init, 0, NULL, &r);
                    if (!e) v_decref(&r);
                }
                V->progs = saved;
                prog->next = NULL;
            }
            free_func(init);
        }
    }
    parser_free(&P);
    if (st) {
        if (msg) snprintf(msg, msgn, "line %d: %s", errline, m);
        V->errline = errline;
        free_prog(prog);
        return st;
    }
    /* replace a program of the same name */
    for (pp = &V->progs; *pp; pp = &(*pp)->next)
        if (!strcmp((*pp)->name, name)) { pprog *old = *pp; *pp = old->next; free_prog(old); break; }
    prog->next = V->progs;
    V->progs = prog;
    V->gen++;
    return 0;
}

pfunc *vm_find_func(ppl_vm *V, const char *name, int exported_only)
{
    pprog *p;
    int i;
    for (p = V->progs; p; p = p->next)
        for (i = 0; i < p->nf; i++)
            if ((!exported_only || p->f[i]->exported) && !strcmp(p->f[i]->name, name)) return p->f[i];
    return NULL;
}

/* ------------------------------------------------------------ names */

enum { R_NONE, R_VAR, R_FUNC, R_SETTING };

static pprog *find_prog(ppl_vm *V, const char *name, size_t n)
{
    pprog *p;
    for (p = V->progs; p; p = p->next) if (strlen(p->name) == n && !strncmp(p->name, name, n)) return p;
    return NULL;
}

static int resolve(ppl_vm *V, const char *name, value **slot, pfunc **fn)
{
    pprog *p;
    hvar *h;
    int i;
    const char *dot = strchr(name, '.');
    if (dot) {
        p = find_prog(V, name, (size_t)(dot - name));
        if (p) {
            for (i = 0; i < p->ng; i++) if (!strcmp(p->g[i].name, dot + 1)) { *slot = &p->g[i].v; return R_VAR; }
            for (i = 0; i < p->nf; i++) if (!strcmp(p->f[i]->name, dot + 1)) { *fn = p->f[i]; return R_FUNC; }
        }
        return R_NONE;
    }
    for (p = V->progs; p; p = p->next)
        for (i = 0; i < p->ng; i++)
            if (p->g[i].exported && !strcmp(p->g[i].name, name)) { *slot = &p->g[i].v; return R_VAR; }
    for (h = V->hvars; h; h = h->next) if (!strcmp(h->name, name)) { *slot = &h->v; return R_VAR; }
    if ((*fn = vm_find_func(V, name, 1)) != NULL) return R_FUNC;
    {
        value t;
        if (!vm_setting_get(V, name, &t)) return R_SETTING;
    }
    return R_NONE;
}

static value *new_hvar(ppl_vm *V, const char *name)
{
    hvar *h = (hvar *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->name = xstrdup(name);
    v_int32(&h->v, 0);
    h->next = V->hvars;
    V->hvars = h;
    V->gen++;
    return &h->v;
}

/* resolve through the function's cache (settings are never cached) */
static int resolve_cached(ppl_vm *V, pfunc *f, int idx, value **slot, pfunc **fn)
{
    int k;
    ncache *c;
    if (!f->nc) {
        f->nc = (ncache *)calloc(f->nnames ? f->nnames : 1, sizeof(ncache));
        if (!f->nc) return resolve(V, f->names[idx], slot, fn);
    }
    c = &f->nc[idx];
    if (c->gen == V->gen + 1u && c->kind) {
        if (c->kind == R_VAR) { *slot = (value *)c->p; return R_VAR; }
        *fn = (pfunc *)c->p; return R_FUNC;
    }
    k = resolve(V, f->names[idx], slot, fn);
    if (k == R_VAR || k == R_FUNC) {
        c->gen = V->gen + 1u;
        c->kind = (uint8_t)k;
        c->p = k == R_VAR ? (void *)*slot : (void *)*fn;
    }
    return k;
}

/* ------------------------------------------------------------ indexing */

static int idx_list(ppl_vm *V, const value *c, int32_t i, value *out)
{
    plist *l = c->u.l;
    (void)V;
    if (i == 0) { if (!l->n) return E_INDEX; i = (int32_t)l->n; }
    if (i < 1 || (uint32_t)i > l->n) return E_INDEX;
    v_copy(out, &l->v[i - 1]);
    return 0;
}

int vm_index(ppl_vm *V, const value *c, int argc, const value *idx, value *out)
{
    int32_t i, j;
    int st;
    if (argc == 0) { v_copy(out, c); return 0; }
    if ((st = to_index(&idx[0], &i))) return st;
    switch (c->t) {
    case T_LIST: {
        value t;
        if ((st = idx_list(V, c, i, &t))) return st;
        if (argc == 1) { *out = t; return 0; }
        st = vm_index(V, &t, argc - 1, idx + 1, out);
        v_decref(&t);
        return st;
    }
    case T_STR:
        if (argc != 1) return E_BADTYPE;
        if (i < 1 || (uint32_t)i > c->u.s->len) return E_INDEX;
        v_int32(out, c->u.s->c[i - 1]);
        return 0;
    case T_MAT: {
        pmat *m = c->u.m;
        if (m->vec) {
            if (argc != 1) return E_INDEX;
            if (i < 1 || i > m->cols) return E_INDEX;
            *out = m->v[i - 1];
            return 0;
        }
        if (i < 1 || i > m->rows) return E_INDEX;
        if (argc == 1) {
            value r;
            if ((st = v_mat(&r, 1, m->cols, 1))) return st;
            memcpy(r.u.m->v, &m->v[(i - 1) * m->cols], m->cols * sizeof(value));
            *out = r;
            return 0;
        }
        if (argc != 2) return E_INDEX;
        if ((st = to_index(&idx[1], &j))) return st;
        if (j < 1 || j > m->cols) return E_INDEX;
        *out = m->v[(i - 1) * m->cols + (j - 1)];
        return 0;
    }
    default: return E_BADTYPE;
    }
}

static int scalar_elem(value *v)
{
    if (v->t == T_REAL || v->t == T_CPLX) return 0;
    if (v->t == T_INT) { real r; to_real(v, &r); v_real(v, &r); return 0; }
    return E_BADTYPE;
}

/* var(idx...) := val, in place (copy-on-write) */
static int set_index(ppl_vm *V, value *var, int argc, const value *idx, const value *val)
{
    int32_t i, j;
    int st;
    if (argc == 0) { v_set(var, val); return 0; }
    if ((st = to_index(&idx[0], &i))) return st;
    if (var->t == T_REAL && dec_is_zero(&var->u.r) && val->t != T_LIST) {
        /* an unset variable indexed: becomes a list */
        v_list(var, 0);
    }
    switch (var->t) {
    case T_LIST: {
        plist *l = list_own(var);
        if (!l) return E_MEM;
        if (i == 0 || (uint32_t)i == l->n + 1) {
            if (argc > 1) return E_INDEX;
            return list_push(l, val);
        }
        if (i < 1 || (uint32_t)i > l->n) return E_INDEX;
        if (argc == 1) { v_set(&l->v[i - 1], val); return 0; }
        return set_index(V, &l->v[i - 1], argc - 1, idx + 1, val);
    }
    case T_MAT: {
        pmat *m;
        value e;
        if (var->u.m->vec) {
            if (argc != 1) return E_INDEX;
            if (i < 1 || i > var->u.m->cols + 1) return E_INDEX;
            e = *val;
            if ((st = scalar_elem(&e))) return st;
            if (i == var->u.m->cols + 1) {
                value nv; int k;
                if ((st = v_mat(&nv, 1, i, 1))) return st;
                for (k = 0; k < i - 1; k++) nv.u.m->v[k] = var->u.m->v[k];
                nv.u.m->v[i - 1] = e;
                v_decref(var); *var = nv;
                return 0;
            }
            m = mat_own(var);
            if (!m) return E_MEM;
            m->v[i - 1] = e;
            return 0;
        }
        if (argc == 1) {
            /* replace a row with a vector */
            int k;
            if (val->t != T_MAT || !val->u.m->vec || val->u.m->cols != var->u.m->cols) return E_DIM;
            if (i < 1 || i > var->u.m->rows) return E_INDEX;
            m = mat_own(var);
            for (k = 0; k < m->cols; k++) m->v[(i - 1) * m->cols + k] = val->u.m->v[k];
            return 0;
        }
        if (argc != 2) return E_INDEX;
        if ((st = to_index(&idx[1], &j))) return st;
        e = *val;
        if ((st = scalar_elem(&e))) return st;
        if (i < 1 || j < 1 || i > var->u.m->rows + 1 || j > var->u.m->cols + 1) return E_INDEX;
        if (i > var->u.m->rows || j > var->u.m->cols) {
            /* grow by one row or column */
            int R = i > var->u.m->rows ? i : var->u.m->rows, Cc = j > var->u.m->cols ? j : var->u.m->cols, r, c;
            value nv;
            if ((st = v_mat(&nv, R, Cc, 0))) return st;
            for (r = 0; r < var->u.m->rows; r++)
                for (c = 0; c < var->u.m->cols; c++) nv.u.m->v[r * Cc + c] = var->u.m->v[r * var->u.m->cols + c];
            v_decref(var); *var = nv;
        }
        m = mat_own(var);
        if (!m) return E_MEM;
        m->v[(i - 1) * m->cols + (j - 1)] = e;
        return 0;
    }
    default: return E_BADTYPE;
    }
}

/* ------------------------------------------------------------ sysvars */

static int sys_store(ppl_vm *V, int id, const value *v)
{
    if (id <= SV_Z) {
        if (v->t != T_REAL && v->t != T_INT) return E_BADTYPE;
    } else if (id >= SV_L0 && id < SV_L0 + 10) {
        if (v->t != T_LIST) return E_BADTYPE;
    } else if (id >= SV_M0 && id < SV_M0 + 10) {
        if (v->t != T_MAT) return E_BADTYPE;
    } else if (id >= SV_Z0 && id < SV_Z0 + 10) {
        if (v->t != T_REAL && v->t != T_CPLX && v->t != T_INT) return E_BADTYPE;
    } else if (id >= SV_G0 && id < SV_G0 + 10) {
        return E_BADTYPE;   /* graphics: only BLIT and DIMGROB write them */
    }
    v_set(&V->sysvars[id], v);
    return 0;
}

/* ------------------------------------------------------------ frames */

static int push_frame(ppl_vm *V, pfunc *f, int argc, int thunk, uint32_t bp, uint32_t pc)
{
    frame *fr;
    int i;
    if (V->nframes >= VM_FRAMES) return E_MEM;
    if (!thunk) {
        if (!f->defined) return E_UNDEF;
        if (argc != f->nparams) return E_ARGC;
        if (V->sp + (f->nlocals - argc) + 64 >= VM_STACK) return E_MEM;
    }
    fr = &V->frames[V->nframes++];
    fr->fn = f;
    fr->pc = pc;
    fr->thunk = (uint8_t)thunk;
    fr->line = f->line;
    if (thunk) {
        fr->bp = bp;
        fr->sp0 = V->sp;
        v_none(&fr->last);
    } else {
        fr->bp = V->sp - (uint32_t)argc;
        fr->sp0 = fr->bp;
        v_int32(&fr->last, 0);
        for (i = argc; i < f->nlocals; i++) v_int32(&V->stack[V->sp++], 0);
    }
    return 0;
}

static void drop_frame(ppl_vm *V)
{
    frame *fr = &V->frames[V->nframes - 1];
    while (V->sp > fr->sp0) v_decref(&V->stack[--V->sp]);
    v_decref(&fr->last);
    while (V->nhandlers && V->handlers[V->nhandlers - 1].frame_depth >= (uint32_t)(V->nframes - 1)) V->nhandlers--;
    V->nframes--;
}

/* ------------------------------------------------------------ builtins */

static int do_fact(ppl_vm *V, const real *x, value *out)
{
    real t, one;
    int st;
    (void)V;
    if (dec_is_int(x) && x->s && !dec_is_zero(x)) return E_BADVAL;
    dec_from_int(&one, 1);
    dec_add(&t, x, &one);
    st = dec_gamma(&t, &t);
    if (st && !DEC_SOFT(st)) return dec_err(st);
    v_real(out, &t);
    return 0;
}

static int do_sqrt(ppl_vm *V, const real *x, value *out)
{
    real r;
    if (x->s && !dec_is_zero(x)) {
        cplx a, z;
        if (!V->set.complex_ok) return E_NONREAL;
        a.re = *x; dec_zero(&a.im);
        c_sqrt(&z, &a);
        return c_make(out, &z.re, &z.im);
    }
    dec_sqrt(&r, x);
    v_real(out, &r);
    return 0;
}

static int do_csqrt(ppl_vm *V, const cplx *x, value *out)
{
    cplx z;
    (void)V;
    c_sqrt(&z, x);
    return c_make(out, &z.re, &z.im);
}

/* ------------------------------------------------------------ the loop */

#define RD16(p) ((int)(((unsigned)(p)[0] << 8) | (p)[1]))
#define RDS16(p) ((int)(int16_t)(uint16_t)(((unsigned)(p)[0] << 8) | (p)[1]))

static int exec(ppl_vm *V, int base)
{
    frame *fr;
    const uint8_t *code;
    uint32_t pc;
    int err = 0;
    value *S = V->stack;

#define LOAD() do { fr = &V->frames[V->nframes - 1]; code = fr->fn->code; pc = fr->pc; } while (0)
#define SAVE() (fr->pc = pc)
#define FAIL(e) do { err = (e); goto error; } while (0)
#define NEED(n) do { if (V->sp + (n) >= VM_STACK) FAIL(E_MEM); } while (0)

    LOAD();
    for (;;) {
        uint8_t op = code[pc++];
        switch (op) {
        case BC_NOP: break;
        case BC_CONST: NEED(1); v_copy(&S[V->sp++], &fr->fn->k[RD16(code + pc)]); pc += 2; break;
        case BC_ZERO: NEED(1); v_int32(&S[V->sp++], 0); break;
        case BC_SINT: NEED(1); v_int32(&S[V->sp++], (int8_t)code[pc]); pc++; break;
        case BC_LLOAD: NEED(1); v_copy(&S[V->sp++], &S[fr->bp + code[pc]]); pc++; break;
        case BC_LSTORE: {
            value *d = &S[fr->bp + code[pc++]];
            v_decref(d);
            *d = S[--V->sp];
            break;
        }
        case BC_GLOAD: NEED(1); v_copy(&S[V->sp++], &fr->fn->prog->g[RD16(code + pc)].v); pc += 2; break;
        case BC_GSTORE: {
            value *d = &fr->fn->prog->g[RD16(code + pc)].v;
            pc += 2;
            v_decref(d);
            *d = S[--V->sp];
            break;
        }
        case BC_SLOAD: NEED(1); v_copy(&S[V->sp++], &V->sysvars[code[pc]]); pc++; break;
        case BC_SSTORE: {
            int id = code[pc++];
            int e = sys_store(V, id, &S[V->sp - 1]);
            v_decref(&S[--V->sp]);
            if (e) { SAVE(); FAIL(e); }
            break;
        }
        case BC_NLOAD: case BC_NSTORE: {
            int ni = RD16(code + pc);
            const char *nm = fr->fn->names[ni];
            value *slot = NULL;
            pfunc *fn = NULL;
            int k;
            pc += 2;
            SAVE();
            k = resolve_cached(V, fr->fn, ni, &slot, &fn);
            if (op == BC_NLOAD) {
                NEED(1);
                if (k == R_VAR) v_copy(&S[V->sp++], slot);
                else if (k == R_SETTING) vm_setting_get(V, nm, &S[V->sp++]);
                else if (k == R_FUNC) {
                    int e = push_frame(V, fn, 0, 0, 0, 0);
                    if (e) FAIL(e);
                    LOAD();
                } else { strncpy(V->errname, nm, sizeof V->errname - 1); FAIL(E_UNDEF); }
            } else {
                value v = S[--V->sp];
                int e = 0;
                if (k == R_VAR) { v_decref(slot); *slot = v; break; }
                if (k == R_SETTING) e = vm_setting_set(V, nm, &v);
                else if (k == R_NONE && fr->fn->home && !strchr(nm, '.')) {
                    value *s = new_hvar(V, nm);
                    if (!s) e = E_MEM; else { *s = v; break; }
                } else e = E_UNDEF;
                v_decref(&v);
                if (e) { strncpy(V->errname, nm, sizeof V->errname - 1); FAIL(e); }
            }
            break;
        }
        case BC_LSETIDX: case BC_GSETIDX: case BC_SSETIDX: case BC_NSETIDX: {
            value *var = NULL;
            int argc, e, id = -1;
            value tmp;
            if (op == BC_LSETIDX) { var = &S[fr->bp + code[pc]]; pc++; }
            else if (op == BC_GSETIDX) { var = &fr->fn->prog->g[RD16(code + pc)].v; pc += 2; }
            else if (op == BC_SSETIDX) { id = code[pc]; var = &V->sysvars[id]; pc++; }
            else {
                pfunc *fn = NULL;
                const char *nm = fr->fn->names[RD16(code + pc)];
                pc += 2;
                if (resolve(V, nm, &var, &fn) != R_VAR) { SAVE(); strncpy(V->errname, nm, sizeof V->errname - 1); FAIL(E_UNDEF); }
            }
            argc = code[pc++];
            SAVE();
            if (id >= 0) {
                /* typed system variables keep their type */
                v_copy(&tmp, var);
                e = set_index(V, &tmp, argc, &S[V->sp - argc], &S[V->sp - argc - 1]);
                if (!e) e = sys_store(V, id, &tmp);
                v_decref(&tmp);
            } else e = set_index(V, var, argc, &S[V->sp - argc], &S[V->sp - argc - 1]);
            {
                int k;
                for (k = 0; k <= argc; k++) v_decref(&S[--V->sp]);
            }
            if (e) FAIL(e);
            break;
        }
        case BC_INDEX: {
            int argc = code[pc++], e, k;
            value r;
            SAVE();
            e = vm_index(V, &S[V->sp - argc - 1], argc, &S[V->sp - argc], &r);
            for (k = 0; k <= argc; k++) v_decref(&S[--V->sp]);
            if (e) FAIL(e);
            S[V->sp++] = r;
            break;
        }
        case BC_CALLB: {
            const builtin *b = bi_get(RD16(code + pc));
            int argc = code[pc + 2], e, k;
            value r;
            pc += 3;
            SAVE();
            v_none(&r);
            if ((b->flags & BF_INPLACE) && argc && S[V->sp - argc].t == T_VREF) {
                /* editing command on a named variable: store the result back */
                value ref = S[V->sp - argc];
                vm_load_ref(V, &ref, &S[V->sp - argc]);
                e = b->fn(V, argc, &S[V->sp - argc], &r);
                if (!e) e = vm_store_ref(V, &ref, &r);
            } else e = b->fn(V, argc, &S[V->sp - argc], &r);
            for (k = 0; k < argc; k++) v_decref(&S[--V->sp]);
            if (e) { v_decref(&r); FAIL(e); }
            S[V->sp++] = r;
            break;
        }
        case BC_CALLF: {
            pfunc *f = fr->fn->prog->f[RD16(code + pc)];
            int argc = code[pc + 2], e;
            pc += 3;
            SAVE();
            e = push_frame(V, f, argc, 0, 0, 0);
            if (e) FAIL(e);
            LOAD();
            break;
        }
        case BC_CALLN: {
            int ni = RD16(code + pc);
            const char *nm = fr->fn->names[ni];
            int argc = code[pc + 2], e, k;
            value *slot = NULL;
            pfunc *fn = NULL;
            pc += 3;
            SAVE();
            k = resolve_cached(V, fr->fn, ni, &slot, &fn);
            if (k == R_FUNC) {
                e = push_frame(V, fn, argc, 0, 0, 0);
                if (e) FAIL(e);
                LOAD();
            } else if (k == R_VAR) {
                value r;
                e = vm_index(V, slot, argc, &S[V->sp - argc], &r);
                for (k = 0; k < argc; k++) v_decref(&S[--V->sp]);
                if (e) FAIL(e);
                S[V->sp++] = r;
            } else if (!strncmp(nm, "CAS.", 4)) {
                /* native helpers first, then the pluggable CAS */
                const builtin *b = bi_lookup(nm + 4);
                value r;
                v_none(&r);
                if (b && argc >= b->minargs && (b->maxargs < 0 || argc <= b->maxargs))
                    e = b->fn(V, argc, &S[V->sp - argc], &r);
                else if (V->cas) e = V->cas(V, nm + 4, argc, &S[V->sp - argc], &r);
                else e = E_UNSUPPORTED;
                for (k = 0; k < argc; k++) v_decref(&S[--V->sp]);
                if (e) { v_decref(&r); strncpy(V->errname, nm, sizeof V->errname - 1); FAIL(e); }
                S[V->sp++] = r;
            } else {
                strncpy(V->errname, nm, sizeof V->errname - 1);
                FAIL(E_UNDEF);
            }
            break;
        }
        case BC_BIN: {
            value r;
            int e = ar_binary(V, code[pc++], &S[V->sp - 2], &S[V->sp - 1], &r);
            if (e) { SAVE(); FAIL(e); }
            v_decref(&S[--V->sp]);
            v_decref(&S[V->sp - 1]);
            S[V->sp - 1] = r;
            break;
        }
        case BC_CMPKA: {
            value r, a2;
            int e = ar_binary(V, code[pc++], &S[V->sp - 2], &S[V->sp - 1], &r);
            if (e) { SAVE(); FAIL(e); }
            e = ar_binary(V, OP_AND, &S[V->sp - 3], &r, &a2);
            v_decref(&r);
            if (e) { SAVE(); FAIL(e); }
            v_decref(&S[V->sp - 3]);
            v_decref(&S[V->sp - 2]);
            S[V->sp - 3] = a2;
            S[V->sp - 2] = S[V->sp - 1];
            V->sp--;
            break;
        }
        case BC_NEG: case BC_NOT: case BC_FACT: case BC_SQ: case BC_CUBE: case BC_INV: case BC_SQRT: {
            value r, *a = &S[V->sp - 1];
            int e;
            switch (op) {
            case BC_NEG: e = ar_neg(V, a, &r); break;
            case BC_NOT: e = ar_not(V, a, &r); break;
            case BC_FACT: e = ar_map1(V, a, do_fact, NULL, &r); break;
            case BC_SQ: e = ar_binary(V, OP_MUL, a, a, &r); break;
            case BC_CUBE: {
                value t;
                e = ar_binary(V, OP_MUL, a, a, &t);
                if (!e) { e = ar_binary(V, OP_MUL, &t, a, &r); v_decref(&t); }
                break;
            }
            case BC_INV: { value one; v_int32(&one, 1); e = ar_binary(V, OP_DIV, &one, a, &r); break; }
            default: e = ar_map1(V, a, do_sqrt, do_csqrt, &r); break;
            }
            if (e) { SAVE(); FAIL(e); }
            v_decref(a);
            *a = r;
            break;
        }
        case BC_JMP: pc = (uint32_t)((int)pc + 2 + RDS16(code + pc)); break;
        case BC_JF: {
            int t, e = v_truth(&S[V->sp - 1], &t);
            if (e) { SAVE(); FAIL(e); }
            v_decref(&S[--V->sp]);
            if (!t) pc = (uint32_t)((int)pc + 2 + RDS16(code + pc));
            else pc += 2;
            break;
        }
        case BC_SC: {
            /* a scalar left operand that decides AND (false) or OR (true): the result
               replaces it and the right operand is skipped; lists etc. fall through */
            int t, sop = code[pc];
            if (!v_truth(&S[V->sp - 1], &t) && (sop == OP_AND ? !t : t)) {
                v_decref(&S[V->sp - 1]);
                v_int32(&S[V->sp - 1], t);
                pc = (uint32_t)((int)pc + 3 + RDS16(code + pc + 1));
            } else pc += 3;
            break;
        }
        case BC_POP: v_decref(&S[--V->sp]); break;
        case BC_DUP: NEED(1); v_copy(&S[V->sp], &S[V->sp - 1]); V->sp++; break;
        case BC_SETLAST: v_decref(&fr->last); fr->last = S[--V->sp]; break;
        case BC_RET: case BC_RETLAST: {
            value r;
            if (op == BC_RET) r = S[--V->sp];
            else { r = fr->last; v_none(&fr->last); }
            drop_frame(V);
            S[V->sp++] = r;
            if (V->nframes <= base) return 0;
            LOAD();
            break;
        }
        case BC_SUBRET: {
            value r = S[--V->sp];
            drop_frame(V);
            S[V->sp++] = r;
            if (V->nframes <= base) return 0;
            LOAD();
            break;
        }
        case BC_MKLIST: {
            int n = RD16(code + pc), k;
            plist *l;
            pc += 2;
            l = list_new(0);
            if (!l) { SAVE(); FAIL(E_MEM); }
            if (n > 4) { l->v = (value *)realloc(l->v, (size_t)n * sizeof(value)); l->cap = (uint32_t)n; }
            for (k = 0; k < n; k++) l->v[k] = S[V->sp - n + k];
            l->n = (uint32_t)n;
            V->sp -= (uint32_t)n;
            v_none(&S[V->sp]);
            S[V->sp].t = T_LIST; S[V->sp].u.l = l;
            V->sp++;
            break;
        }
        case BC_MKVEC: case BC_MKMAT: {
            int rows, cols, n, k, e = 0;
            value r, *el;
            if (op == BC_MKVEC) { rows = 1; cols = RD16(code + pc); pc += 2; }
            else { rows = RD16(code + pc); cols = RD16(code + pc + 2); pc += 4; }
            n = rows * cols;
            el = &S[V->sp - n];
            SAVE();
            if (op == BC_MKVEC && n > 0 && el[0].t == T_MAT && el[0].u.m->vec) {
                /* a vector of vectors is a matrix */
                int c = el[0].u.m->cols, i2;
                for (k = 0; k < n; k++) if (el[k].t != T_MAT || !el[k].u.m->vec || el[k].u.m->cols != c) e = E_DIM;
                if (!e && !(e = v_mat(&r, n, c, 0)))
                    for (k = 0; k < n; k++) for (i2 = 0; i2 < c; i2++) r.u.m->v[k * c + i2] = el[k].u.m->v[i2];
            } else {
                for (k = 0; k < n && !e; k++) e = scalar_elem(&el[k]);
                if (!e && !(e = v_mat(&r, rows, cols, op == BC_MKVEC)))
                    for (k = 0; k < n; k++) r.u.m->v[k] = el[k];
            }
            for (k = 0; k < n; k++) v_decref(&S[--V->sp]);
            if (e) FAIL(e);
            S[V->sp++] = r;
            break;
        }
        case BC_MKCPLX: {
            real re, im;
            value r;
            if (to_real(&S[V->sp - 2], &re) || to_real(&S[V->sp - 1], &im)) { SAVE(); FAIL(E_BADTYPE); }
            c_make(&r, &re, &im);
            v_decref(&S[--V->sp]); v_decref(&S[--V->sp]);
            S[V->sp++] = r;
            break;
        }
        case BC_TRY: {
            handler *h;
            if (V->nhandlers >= VM_HANDLERS) { SAVE(); FAIL(E_MEM); }
            h = &V->handlers[V->nhandlers++];
            h->frame_depth = (uint32_t)(V->nframes - 1);
            h->sp = V->sp;
            h->pc = (uint32_t)((int)pc + 2 + RDS16(code + pc));
            pc += 2;
            break;
        }
        case BC_ENDTRY: if (V->nhandlers) V->nhandlers--; break;
        case BC_THUNK: {
            int len = RD16(code + pc);
            NEED(1);
            pc += 2;
            v_none(&S[V->sp]);
            S[V->sp].t = T_THUNK;
            S[V->sp].u.th.pc = pc;
            S[V->sp].u.th.idx = (uint16_t)(V->nframes - 1);
            V->sp++;
            pc += (uint32_t)len;
            break;
        }
        case BC_VREF:
            NEED(1);
            v_none(&S[V->sp]);
            S[V->sp].t = T_VREF;
            S[V->sp].ib = code[pc];
            S[V->sp].u.th.idx = (uint16_t)RD16(code + pc + 1);
            S[V->sp].u.th.pc = (uint32_t)(V->nframes - 1);
            V->sp++;
            pc += 3;
            break;
        case BC_LINE: fr->line = RD16(code + pc); pc += 2; break;
        case BC_KILL: SAVE(); FAIL(E_KILL);
        default: SAVE(); FAIL(E_SYNTAX);
        }
        continue;

    error:
        V->errline = V->frames[V->nframes - 1].line;
        if (err != E_KILL && V->nhandlers) {
            handler *h = &V->handlers[V->nhandlers - 1];
            if ((int)h->frame_depth >= base) {
                handler hh = *h;
                value code_v;
                V->nhandlers--;
                while (V->nframes - 1 > (int)hh.frame_depth) drop_frame(V);
                while (V->sp > hh.sp) v_decref(&V->stack[--V->sp]);
                v_int32(&code_v, err);
                v_set(&V->sysvars[SV_ANS], &code_v);
                LOAD();
                pc = hh.pc;
                err = 0;
                continue;
            }
        }
        while (V->nframes > base) drop_frame(V);
        V->err = err;
        return err;
    }
#undef LOAD
#undef SAVE
#undef FAIL
#undef NEED
}

/* ------------------------------------------------------------ entry points */

int vm_run_func(ppl_vm *V, pfunc *f, int argc, const value *argv, value *out)
{
    int i, e, base = V->nframes;
    if (V->sp + argc + 8 >= VM_STACK) return E_MEM;
    for (i = 0; i < argc; i++) v_copy(&V->stack[V->sp++], &argv[i]);
    e = push_frame(V, f, argc, 0, 0, 0);
    if (e) { while (argc--) v_decref(&V->stack[--V->sp]); return e; }
    e = exec(V, base);
    if (e) return e;
    *out = V->stack[--V->sp];
    return 0;
}

int ppl_call(ppl_vm *V, const char *fname, int argc, const value *argv, value *out)
{
    pfunc *f = vm_find_func(V, fname, 1);
    if (!f) f = vm_find_func(V, fname, 0);
    if (!f) { strncpy(V->errname, fname, sizeof V->errname - 1); return E_UNDEF; }
    return vm_run_func(V, f, argc, argv, out);
}

int vm_call_value(ppl_vm *V, const char *name, int argc, value *argv, value *out)
{
    return ppl_call(V, name, argc, argv, out);
}

int vm_run_thunk(ppl_vm *V, const value *th, value *out)
{
    frame *parent;
    int e, base = V->nframes;
    if (th->t != T_THUNK) return E_BADTYPE;
    parent = &V->frames[th->u.th.idx];
    e = push_frame(V, parent->fn, 0, 1, parent->bp, th->u.th.pc);
    if (e) return e;
    e = exec(V, base);
    if (e) return e;
    *out = V->stack[--V->sp];
    return 0;
}

static value *ref_slot(ppl_vm *V, const value *ref, int create)
{
    frame *f = &V->frames[ref->u.th.pc];
    switch (ref->ib) {
    case VK_LOCAL: return &V->stack[f->bp + ref->u.th.idx];
    case VK_PROG: return &f->fn->prog->g[ref->u.th.idx].v;
    case VK_SYS: return &V->sysvars[ref->u.th.idx];
    case VK_NAME: {
        value *slot = NULL;
        pfunc *fn = NULL;
        const char *nm = f->fn->names[ref->u.th.idx];
        if (resolve(V, nm, &slot, &fn) == R_VAR) return slot;
        if (create) return new_hvar(V, nm);
        return NULL;
    }
    default: return NULL;
    }
}

int vm_store_ref(ppl_vm *V, const value *ref, const value *v)
{
    value *s;
    if (ref->t != T_VREF) return E_BADTYPE;
    if (ref->ib == VK_SYS) return sys_store(V, ref->u.th.idx, v);
    s = ref_slot(V, ref, 1);
    if (!s) return E_UNDEF;
    v_set(s, v);
    return 0;
}

int vm_load_ref(ppl_vm *V, const value *ref, value *out)
{
    value *s;
    if (ref->t != T_VREF) return E_BADTYPE;
    s = ref_slot(V, ref, 0);
    if (!s) { v_int32(out, 0); return 0; }
    v_copy(out, s);
    return 0;
}

int vm_eval_string(ppl_vm *V, const char *src, size_t n, value *out)
{
    parser P;
    node *b;
    pfunc *f;
    int e, errline = 0;
    char msg[100];
    size_t i;
    for (i = 0; i < n && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n'); i++) {}
    if (i == n) return E_SYNTAX;
    parser_init(&P, src, n);
    b = parse_home(&P);
    if (P.err) { V->errline = P.errline; parser_free(&P); return E_SYNTAX; }
    f = new_func(&home_prog, "(home)");
    if (!f) { parser_free(&P); return E_MEM; }
    f->home = 1;
    e = compile_function(V, &home_prog, f, NULL, 0, b, msg, sizeof msg, &errline);
    parser_free(&P);
    if (e) { free_func(f); return e; }
    e = vm_run_func(V, f, 0, NULL, out);
    free_func(f);
    return e;
}

int ppl_eval(ppl_vm *V, const char *src, value *out)
{
    int e = vm_eval_string(V, src, strlen(src), out);
    if (!e) v_set(&V->sysvars[SV_ANS], out);
    return e;
}
