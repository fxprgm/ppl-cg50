/* compile.c -- PPL AST to bytecode.
 *
 * Names resolve at compile time where they can: locals, then this
 * program's variables and functions, then the system variables (A-Z,
 * L0-L9, M0-M9, Z0-Z9, Ans), then the built-ins (exact case first, then
 * any case).  Anything else is looked up by name at run time (exported
 * variables and functions of other programs, Home variables, settings). */
#include "compile.h"
#include "bc.h"
#include <stdlib.h>
#include <string.h>

typedef struct loopctx {
    int brk[64]; int nbrk;
    int cont[64]; int ncont;
    int cont_target;           /* -1 until known */
    int try_depth;
} loopctx;

typedef struct comp {
    ppl_vm *V;
    pprog *prog;
    pfunc *fn;
    struct { const char *name; int slot; } loc[256];
    int nloc, nslots;
    loopctx loops[32]; int nloops;
    int try_depth;
    int err, errline;
    char msg[96];
    int line;
} comp;

static void cerr(comp *C, int line, const char *m)
{
    if (!C->err) {
        C->err = E_SYNTAX;
        C->errline = line;
        strncpy(C->msg, m, sizeof C->msg - 1);
    }
}

/* ------------------------------------------------------------ emitting */

static void emit(comp *C, int b)
{
    pfunc *f = C->fn;
    if (f->ncode == f->capcode) {
        uint32_t nc = f->capcode ? f->capcode * 2 : 64;
        uint8_t *n = (uint8_t *)realloc(f->code, nc);
        if (!n) { cerr(C, C->line, "out of memory"); return; }
        f->code = n; f->capcode = nc;
    }
    f->code[f->ncode++] = (uint8_t)b;
}

static void emit16(comp *C, int v) { emit(C, (v >> 8) & 0xFF); emit(C, v & 0xFF); }

static int here(comp *C) { return (int)C->fn->ncode; }

static int emit_jump(comp *C, int op) { emit(C, op); emit16(C, 0); return here(C) - 2; }

static void patch_to(comp *C, int at, int target)
{
    int off = target - (at + 2);
    if (off < -32768 || off > 32767) { cerr(C, C->line, "function too large"); return; }
    C->fn->code[at] = (uint8_t)((off >> 8) & 0xFF);
    C->fn->code[at + 1] = (uint8_t)(off & 0xFF);
}

static void patch(comp *C, int at) { patch_to(C, at, here(C)); }

static void emit_jump_to(comp *C, int op, int target)
{
    int at = emit_jump(C, op);
    patch_to(C, at, target);
}

static int add_const(comp *C, const value *v)
{
    pfunc *f = C->fn;
    if (f->nk == f->capk) {
        int nc = f->capk ? f->capk * 2 : 8;
        value *n = (value *)realloc(f->k, (size_t)nc * sizeof(value));
        if (!n) { cerr(C, C->line, "out of memory"); return 0; }
        f->k = n; f->capk = (uint16_t)nc;
    }
    v_copy(&f->k[f->nk], v);
    return f->nk++;
}

static int add_name(comp *C, const char *name)
{
    pfunc *f = C->fn;
    int i;
    for (i = 0; i < f->nnames; i++) if (!strcmp(f->names[i], name)) return i;
    if (f->nnames == f->capnames) {
        int nc = f->capnames ? f->capnames * 2 : 8;
        char **n = (char **)realloc(f->names, (size_t)nc * sizeof(char *));
        if (!n) { cerr(C, C->line, "out of memory"); return 0; }
        f->names = n; f->capnames = (uint16_t)nc;
    }
    f->names[f->nnames] = (char *)malloc(strlen(name) + 1);
    strcpy(f->names[f->nnames], name);
    return f->nnames++;
}

static void emit_const(comp *C, const value *v)
{
    if (v->t == T_REAL) {
        int32_t n;
        if (!dec_to_int32(&v->u.r, &n) && n >= -128 && n <= 127) {
            if (n == 0) emit(C, BC_ZERO);
            else { emit(C, BC_SINT); emit(C, n & 0xFF); }
            return;
        }
    }
    emit(C, BC_CONST);
    emit16(C, add_const(C, v));
}

/* ------------------------------------------------------------ names */

static int find_local(comp *C, const char *name)
{
    int i;
    for (i = C->nloc - 1; i >= 0; i--) if (C->loc[i].name && !strcmp(C->loc[i].name, name)) return C->loc[i].slot;
    return -1;
}

static int new_slot(comp *C, const char *name)
{
    int s = C->nslots++;
    if (s >= 250 || C->nloc >= 256) { cerr(C, C->line, "too many local variables"); return 0; }
    C->loc[C->nloc].name = name;
    C->loc[C->nloc].slot = s;
    C->nloc++;
    return s;
}

static int find_pvar(comp *C, const char *name)
{
    int i;
    for (i = 0; i < C->prog->ng; i++) if (!strcmp(C->prog->g[i].name, name)) return i;
    return -1;
}

static int find_pfunc(comp *C, const char *name)
{
    int i;
    for (i = 0; i < C->prog->nf; i++) if (!strcmp(C->prog->f[i]->name, name)) return i;
    return -1;
}

int sysvar_id(const char *n)
{
    if (n[0] >= 'A' && n[0] <= 'Z' && !n[1]) return SV_A + (n[0] - 'A');
    if (n[1] >= '0' && n[1] <= '9' && !n[2]) {
        if (n[0] == 'L') return SV_L0 + (n[1] - '0');
        if (n[0] == 'M') return SV_M0 + (n[1] - '0');
        if (n[0] == 'Z') return SV_Z0 + (n[1] - '0');
        if (n[0] == 'G') return SV_G0 + (n[1] - '0');
    }
    if (!strcmp(n, "Ans")) return SV_ANS;
    return -1;
}

const builtin *bi_lookup_any(const char *name)
{
    const builtin *b = bi_lookup(name);
    char up[64];
    size_t i, n = strlen(name);
    if (b || n >= sizeof up) return b;
    for (i = 0; i <= n; i++) {
        char c = name[i];
        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    return bi_lookup(up);
}

/* ------------------------------------------------------------ exprs */

static void cexpr(comp *C, node *n);

static void emit_vref(comp *C, node *n)
{
    int s;
    if (n->type != N_NAME) { cerr(C, n->line, "expected a variable name"); return; }
    emit(C, BC_VREF);
    if ((s = find_local(C, n->name)) >= 0) { emit(C, VK_LOCAL); emit16(C, s); }
    else if ((s = find_pvar(C, n->name)) >= 0) { emit(C, VK_PROG); emit16(C, s); }
    else if ((s = sysvar_id(n->name)) >= 0) { emit(C, VK_SYS); emit16(C, s); }
    else { emit(C, VK_NAME); emit16(C, add_name(C, n->name)); }
}

static void emit_thunk(comp *C, node *n)
{
    int at;
    emit(C, BC_THUNK);
    emit16(C, 0);
    at = here(C);
    cexpr(C, n);
    emit(C, BC_SUBRET);
    {
        int len = here(C) - at;
        C->fn->code[at - 2] = (uint8_t)((len >> 8) & 0xFF);
        C->fn->code[at - 1] = (uint8_t)(len & 0xFF);
    }
}

/* BF_REF1: a variable reference, or a list of them where a sub-list's first
 * element is the reference ({var, [types], {pos}} fields of INPUT) */
static void emit_reflist(comp *C, node *n, int depth)
{
    int i;
    if (n->type == N_NAME) { emit_vref(C, n); return; }
    if (n->type != N_LIST || depth > 1) { cexpr(C, n); return; }
    for (i = 0; i < n->nkids; i++) {
        if (i == 0 || depth == 0) emit_reflist(C, n->kids[i], depth + 1);
        else cexpr(C, n->kids[i]);
    }
    emit(C, BC_MKLIST);
    emit16(C, n->nkids);
}

static void call_builtin(comp *C, node *n, const builtin *b, int argc)
{
    int i;
    if (b->minargs > argc || (b->maxargs >= 0 && argc > b->maxargs)) {
        cerr(C, n->line, "wrong number of arguments");
        return;
    }
    for (i = 0; i < argc; i++) {
        if (i == 0 && (b->flags & BF_QUOTE1)) emit_thunk(C, n->kids[i]);
        else if (i == 0 && (b->flags & BF_REF1)) emit_reflist(C, n->kids[i], 0);
        else if (i == 0 && (b->flags & BF_INPLACE) && n->kids[i]->type == N_NAME
                 && (find_local(C, n->kids[i]->name) >= 0 || find_pvar(C, n->kids[i]->name) >= 0
                     || sysvar_id(n->kids[i]->name) >= 0)) emit_vref(C, n->kids[i]);
        else if (i == 1 && (b->flags & BF_QUOTE2)) emit_vref(C, n->kids[i]);
        else cexpr(C, n->kids[i]);
    }
    emit(C, BC_CALLB);
    emit16(C, bi_id(b));
    emit(C, argc);
}

static void cname(comp *C, node *n)
{
    int s;
    const builtin *b;
    const char *nm = n->name;
    if ((s = find_local(C, nm)) >= 0) { emit(C, BC_LLOAD); emit(C, s); return; }
    if ((s = find_pvar(C, nm)) >= 0) { emit(C, BC_GLOAD); emit16(C, s); return; }
    if ((s = find_pfunc(C, nm)) >= 0) { emit(C, BC_CALLF); emit16(C, s); emit(C, 0); return; }
    if ((s = sysvar_id(nm)) >= 0) { emit(C, BC_SLOAD); emit(C, s); return; }
    if (!strcmp(nm, "i")) {
        value v; real z, o;
        dec_zero(&z); dec_from_int(&o, 1);
        v_none(&v); v.t = T_CPLX; v.u.c.re = z; v.u.c.im = o;
        emit_const(C, &v);
        return;
    }
    if (!strcmp(nm, "e")) { value v; real r; dec_e(&r); v_real(&v, &r); emit_const(C, &v); return; }
    if (!strcmp(nm, "\xCF\x80")) { value v; real r; dec_pi(&r); v_real(&v, &r); emit_const(C, &v); return; }
    if ((b = bi_lookup(nm)) != NULL || (!strchr(nm, '.') && (b = bi_lookup_any(nm)) != NULL)) {
        if (b->minargs > 0) { cerr(C, n->line, "missing arguments"); return; }
        emit(C, BC_CALLB); emit16(C, bi_id(b)); emit(C, 0);
        return;
    }
    emit(C, BC_NLOAD);
    emit16(C, add_name(C, nm));
}

static void ccall(comp *C, node *n)
{
    int s, i;
    const builtin *b;
    const char *nm = n->name;
    if ((s = find_local(C, nm)) >= 0) {
        emit(C, BC_LLOAD); emit(C, s);
        for (i = 0; i < n->nkids; i++) cexpr(C, n->kids[i]);
        emit(C, BC_INDEX); emit(C, n->nkids);
        return;
    }
    if ((s = find_pvar(C, nm)) >= 0) {
        emit(C, BC_GLOAD); emit16(C, s);
        for (i = 0; i < n->nkids; i++) cexpr(C, n->kids[i]);
        emit(C, BC_INDEX); emit(C, n->nkids);
        return;
    }
    if ((s = find_pfunc(C, nm)) >= 0) {
        for (i = 0; i < n->nkids; i++) cexpr(C, n->kids[i]);
        emit(C, BC_CALLF); emit16(C, s); emit(C, n->nkids);
        return;
    }
    if ((s = sysvar_id(nm)) >= 0) {
        emit(C, BC_SLOAD); emit(C, s);
        for (i = 0; i < n->nkids; i++) cexpr(C, n->kids[i]);
        emit(C, BC_INDEX); emit(C, n->nkids);
        return;
    }
    if ((b = bi_lookup(nm)) != NULL || (!strchr(nm, '.') && (b = bi_lookup_any(nm)) != NULL)) {
        call_builtin(C, n, b, n->nkids);
        return;
    }
    for (i = 0; i < n->nkids; i++) cexpr(C, n->kids[i]);
    emit(C, BC_CALLN); emit16(C, add_name(C, nm)); emit(C, n->nkids);
}

static void cassign(comp *C, node *n)
{
    node *t = n->a;
    int s, i;
    cexpr(C, n->b);
    emit(C, BC_DUP);
    if (t->type == N_NAME) {
        const char *nm = t->name;
        if ((s = find_local(C, nm)) >= 0) { emit(C, BC_LSTORE); emit(C, s); }
        else if ((s = find_pvar(C, nm)) >= 0) { emit(C, BC_GSTORE); emit16(C, s); }
        else if (find_pfunc(C, nm) >= 0) cerr(C, t->line, "cannot assign to a function");
        else if ((s = sysvar_id(nm)) >= 0) { emit(C, BC_SSTORE); emit(C, s); }
        else { emit(C, BC_NSTORE); emit16(C, add_name(C, nm)); }
        return;
    }
    /* indexed */
    for (i = 0; i < t->nkids; i++) cexpr(C, t->kids[i]);
    if ((s = find_local(C, t->name)) >= 0) { emit(C, BC_LSETIDX); emit(C, s); }
    else if ((s = find_pvar(C, t->name)) >= 0) { emit(C, BC_GSETIDX); emit16(C, s); }
    else if ((s = sysvar_id(t->name)) >= 0) { emit(C, BC_SSETIDX); emit(C, s); }
    else { emit(C, BC_NSETIDX); emit16(C, add_name(C, t->name)); }
    emit(C, t->nkids);
}

static void cexpr(comp *C, node *n)
{
    int i;
    if (C->err) return;
    switch (n->type) {
    case N_NUM: emit_const(C, &n->lit); break;
    case N_STR: {
        value v;
        v_str_utf8(&v, n->name ? n->name : "", n->nlen);
        emit(C, BC_CONST); emit16(C, add_const(C, &v));
        v_decref(&v);
        break;
    }
    case N_NAME: cname(C, n); break;
    case N_SYM: {
        value v, t;
        psym *y = (psym *)malloc(sizeof(psym));
        v_str_utf8(&t, n->name ? n->name : "", n->nlen);
        if (!y) { cerr(C, n->line, "out of memory"); break; }
        y->h.rc = 1; y->text = t.u.s;
        v_none(&v); v.t = T_SYM; v.u.y = y;
        emit(C, BC_CONST); emit16(C, add_const(C, &v));
        v_decref(&v);
        break;
    }
    case N_CALL: ccall(C, n); break;
    case N_LIST:
        for (i = 0; i < n->nkids; i++) cexpr(C, n->kids[i]);
        emit(C, BC_MKLIST); emit16(C, n->nkids);
        break;
    case N_VEC:
        for (i = 0; i < n->nkids; i++) cexpr(C, n->kids[i]);
        emit(C, BC_MKVEC); emit16(C, n->nkids);
        break;
    case N_MAT: {
        int cols = n->nkids ? n->kids[0]->nkids : 0, j;
        for (i = 0; i < n->nkids; i++) {
            if (n->kids[i]->nkids != cols) { cerr(C, n->line, "rows of different lengths"); return; }
            for (j = 0; j < cols; j++) cexpr(C, n->kids[i]->kids[j]);
        }
        emit(C, BC_MKMAT); emit16(C, n->nkids); emit16(C, cols);
        break;
    }
    case N_CPLX: cexpr(C, n->a); cexpr(C, n->b); emit(C, BC_MKCPLX); break;
    case N_NEG:
        if (n->a->type == N_NUM && n->a->lit.t == T_REAL) {
            value v = n->a->lit;
            dec_neg(&v.u.r, &n->a->lit.u.r);
            emit_const(C, &v);
        } else { cexpr(C, n->a); emit(C, BC_NEG); }
        break;
    case N_NOT: cexpr(C, n->a); emit(C, BC_NOT); break;
    case N_BIN:
        cexpr(C, n->a);
        if (n->op == OP_AND || n->op == OP_OR) {
            /* short-circuit, as on the Prime: 0 AND (A:=1) and 1 OR (A:=1) leave A alone */
            int j;
            emit(C, BC_SC); emit(C, n->op);
            emit16(C, 0); j = here(C) - 2;
            cexpr(C, n->b); emit(C, BC_BIN); emit(C, n->op);
            patch(C, j);
            break;
        }
        cexpr(C, n->b); emit(C, BC_BIN); emit(C, n->op);
        break;
    case N_CHAIN:
        emit(C, BC_SINT); emit(C, 1);
        cexpr(C, n->kids[0]);
        for (i = 1; i < n->nkids; i++) {
            cexpr(C, n->kids[i]);
            emit(C, BC_CMPKA); emit(C, n->ops[i - 1]);
        }
        emit(C, BC_POP);
        break;
    case N_POST:
        cexpr(C, n->a);
        switch (n->op) {
        case P_BANG: emit(C, BC_FACT); break;
        case P_SQ: emit(C, BC_SQ); break;
        case P_CUBE: emit(C, BC_CUBE); break;
        default: emit(C, BC_INV); break;
        }
        break;
    case N_SQRT: cexpr(C, n->a); emit(C, BC_SQRT); break;
    case N_ASSIGN: cassign(C, n); break;
    default: cerr(C, n->line, "syntax error"); break;
    }
}

/* ------------------------------------------------------------ statements */

static void cstmt(comp *C, node *n);

static void cblock(comp *C, node *b)
{
    int i;
    if (!b) return;
    if (b->type != N_BLOCK) { cstmt(C, b); return; }
    for (i = 0; i < b->nkids && !C->err; i++) cstmt(C, b->kids[i]);
}

static void store_name(comp *C, node *t)
{
    int s;
    if ((s = find_local(C, t->name)) >= 0) { emit(C, BC_LSTORE); emit(C, s); }
    else if ((s = find_pvar(C, t->name)) >= 0) { emit(C, BC_GSTORE); emit16(C, s); }
    else if ((s = sysvar_id(t->name)) >= 0) { emit(C, BC_SSTORE); emit(C, s); }
    else { emit(C, BC_NSTORE); emit16(C, add_name(C, t->name)); }
}

static loopctx *push_loop(comp *C)
{
    loopctx *L;
    if (C->nloops >= 32) { cerr(C, C->line, "loops nested too deep"); return &C->loops[0]; }
    L = &C->loops[C->nloops++];
    L->nbrk = L->ncont = 0;
    L->cont_target = -1;
    L->try_depth = C->try_depth;
    return L;
}

static void pop_loop(comp *C, int cont_target)
{
    loopctx *L = &C->loops[C->nloops - 1];
    int i;
    for (i = 0; i < L->ncont; i++) patch_to(C, L->cont[i], cont_target);
    for (i = 0; i < L->nbrk; i++) patch(C, L->brk[i]);
    C->nloops--;
}

static void cstmt(comp *C, node *n)
{
    int i;
    if (C->err) return;
    if (n->type != N_BLOCK && n->line != C->line && n->line > 0) {
        C->line = n->line;
        emit(C, BC_LINE); emit16(C, n->line > 65535 ? 65535 : n->line);
    }
    switch (n->type) {
    case N_BLOCK: cblock(C, n); break;
    case N_EXPR:
        cexpr(C, n->a);
        emit(C, BC_SETLAST);
        break;
    case N_LOCAL:
        for (i = 0; i < n->nkids; i++) {
            node *d = n->kids[i];
            int s;
            if (d->a) cexpr(C, d->a); else emit(C, BC_ZERO);
            s = new_slot(C, d->name);
            emit(C, BC_LSTORE); emit(C, s);
        }
        break;
    case N_IF: {
        int jf, je;
        cexpr(C, n->a);
        jf = emit_jump(C, BC_JF);
        cblock(C, n->b);
        if (n->c) {
            je = emit_jump(C, BC_JMP);
            patch(C, jf);
            cblock(C, n->c);
            patch(C, je);
        } else patch(C, jf);
        break;
    }
    case N_CASE: {
        int ends[128], ne = 0;
        for (i = 0; i < n->nkids; i++) {
            node *br = n->kids[i];
            int jf;
            cexpr(C, br->a);
            jf = emit_jump(C, BC_JF);
            cblock(C, br->b);
            if (ne < 128) ends[ne++] = emit_jump(C, BC_JMP);
            patch(C, jf);
        }
        if (n->c) cblock(C, n->c);
        for (i = 0; i < ne; i++) patch(C, ends[i]);
        break;
    }
    case N_FOR: {
        int sl_end = new_slot(C, NULL), sl_step = new_slot(C, NULL), top, jf, cont;
        loopctx *L;
        cexpr(C, n->b);
        store_name(C, n->a);
        cexpr(C, n->c);
        emit(C, BC_LSTORE); emit(C, sl_end);
        if (n->d) cexpr(C, n->d); else { emit(C, BC_SINT); emit(C, 1); }
        emit(C, BC_LSTORE); emit(C, sl_step);
        top = here(C);
        cexpr(C, n->a);
        emit(C, BC_LLOAD); emit(C, sl_end);
        emit(C, BC_BIN); emit(C, n->op ? OP_GE : OP_LE);
        jf = emit_jump(C, BC_JF);
        L = push_loop(C);
        (void)L;
        cblock(C, n->kids[0]);
        cont = here(C);
        cexpr(C, n->a);
        emit(C, BC_LLOAD); emit(C, sl_step);
        emit(C, BC_BIN); emit(C, n->op ? OP_SUB : OP_ADD);
        store_name(C, n->a);
        emit_jump_to(C, BC_JMP, top);
        patch(C, jf);
        pop_loop(C, cont);
        break;
    }
    case N_WHILE: {
        int top = here(C), jf;
        cexpr(C, n->a);
        jf = emit_jump(C, BC_JF);
        push_loop(C);
        cblock(C, n->b);
        emit_jump_to(C, BC_JMP, top);
        patch(C, jf);
        pop_loop(C, top);
        break;
    }
    case N_REPEAT: {
        int top = here(C), cont;
        push_loop(C);
        cblock(C, n->b);
        cont = here(C);
        cexpr(C, n->a);
        emit_jump_to(C, BC_JF, top);
        pop_loop(C, cont);
        break;
    }
    case N_IFERR: {
        int tr, je, saved = C->line;
        emit(C, BC_TRY); emit16(C, 0);
        tr = here(C) - 2;
        C->try_depth++;
        cblock(C, n->a);
        C->try_depth--;
        emit(C, BC_ENDTRY);
        if (n->c) cblock(C, n->c);
        je = emit_jump(C, BC_JMP);
        patch(C, tr);
        C->line = saved;
        cblock(C, n->b);
        patch(C, je);
        break;
    }
    case N_BREAK: case N_CONTINUE: {
        int levels = n->op ? n->op : 1, k;
        loopctx *L;
        if (!C->nloops) {
            /* outside any loop: leaves the function (programs use it to quit) */
            emit(C, BC_RETLAST);
            break;
        }
        if (levels > C->nloops) levels = C->nloops;
        L = &C->loops[C->nloops - levels];
        for (k = C->try_depth; k > L->try_depth; k--) emit(C, BC_ENDTRY);
        if (n->type == N_BREAK) { if (L->nbrk < 64) L->brk[L->nbrk++] = emit_jump(C, BC_JMP); }
        else { if (L->ncont < 64) L->cont[L->ncont++] = emit_jump(C, BC_JMP); }
        break;
    }
    case N_RETURN:
        if (n->a) cexpr(C, n->a); else emit(C, BC_ZERO);
        emit(C, BC_RET);
        break;
    case N_KILL: emit(C, BC_KILL); break;
    case N_NOP: break;
    default: cexpr(C, n); emit(C, BC_SETLAST); break;
    }
}

/* ------------------------------------------------------------ functions */

static void comp_init(comp *C, ppl_vm *V, pprog *prog, pfunc *fn)
{
    memset(C, 0, sizeof *C);
    C->V = V; C->prog = prog; C->fn = fn;
}

int compile_function(ppl_vm *V, pprog *prog, pfunc *fn, char **params, int np, node *body,
                     char *msg, size_t msgn, int *errline)
{
    comp C;
    int i;
    comp_init(&C, V, prog, fn);
    C.line = -1;
    for (i = 0; i < np; i++) new_slot(&C, params[i]);
    fn->nparams = (uint8_t)np;
    cblock(&C, body);
    emit(&C, BC_RETLAST);
    fn->nlocals = (uint8_t)C.nslots;
    fn->defined = 1;
    if (C.err) {
        if (msg) { strncpy(msg, C.msg, msgn - 1); msg[msgn - 1] = 0; }
        if (errline) *errline = C.errline;
    }
    return C.err;
}

/* the initialisers of program variables: one hidden function */
int compile_inits(ppl_vm *V, pprog *prog, pfunc *fn, topdecl *d, int nd, char *msg, size_t msgn, int *errline)
{
    comp C;
    int i;
    comp_init(&C, V, prog, fn);
    for (i = 0; i < nd && !C.err; i++) {
        if (d[i].kind == 2 && d[i].body) {
            int s = find_pvar(&C, d[i].name);
            C.line = d[i].line;
            cexpr(&C, d[i].body);
            emit(&C, BC_GSTORE); emit16(&C, s);
        }
    }
    emit(&C, BC_ZERO);
    emit(&C, BC_RET);
    fn->nlocals = (uint8_t)C.nslots;
    fn->defined = 1;
    if (C.err) {
        if (msg) { strncpy(msg, C.msg, msgn - 1); msg[msgn - 1] = 0; }
        if (errline) *errline = C.errline;
    }
    return C.err;
}
