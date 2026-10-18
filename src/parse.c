/* parse.c -- PPL lexer and recursive-descent parser.
 *
 * Precedence, highest first (HP Prime User Guide, "Algebraic precedence"):
 *   postfix ! and squares, prefix square root; NTHROOT; ^; negation with
 *   * / MOD; + -; relations (chained: 6<8<3 is 0); AND, NOT; OR, XOR;
 *   := and the store arrow.  Operators of one level group left to right.
 */
#include "parse.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ arena */

typedef struct arena_blk { struct arena_blk *next; size_t used, cap; char data[1]; } arena_blk;

void *pa_alloc(parser *P, size_t n)
{
    arena_blk *b = P->blocks;
    n = (n + 7) & ~(size_t)7;
    if (!b || b->used + n > b->cap) {
        size_t cap = n > 16000 ? n : 16000;
        arena_blk *nb = (arena_blk *)malloc(sizeof(arena_blk) + cap);
        if (!nb) return NULL;
        nb->next = b; nb->used = 0; nb->cap = cap;
        P->blocks = b = nb;
    }
    {
        void *p = b->data + b->used;
        b->used += n;
        memset(p, 0, n);
        return p;
    }
}

static char *pa_strdup(parser *P, const char *s, size_t n)
{
    char *d = (char *)pa_alloc(P, n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

void parser_init(parser *P, const char *src, size_t n)
{
    memset(P, 0, sizeof *P);
    P->src = P->p = src;
    P->end = src + n;
    P->line = 1;
}

static void free_lits(node *n);

void parser_free(parser *P)
{
    arena_blk *b = P->blocks;
    int i;
    for (i = 0; i < P->ndecls; i++) if (P->decls[i].body) free_lits(P->decls[i].body);
    while (b) { arena_blk *nx = b->next; free(b); b = nx; }
    free(P->decls);
    P->blocks = NULL;
}

/* literal values in the tree own references (strings of matrices are not
 * refcounted; only N_LIT holds objects) */
static void free_lits(node *n)
{
    int i;
    if (!n) return;
    if (n->type == N_LIT) v_decref(&n->lit);
    free_lits(n->a); free_lits(n->b); free_lits(n->c); free_lits(n->d);
    for (i = 0; i < n->nkids; i++) free_lits(n->kids[i]);
}

static void perr(parser *P, const char *msg)
{
    if (!P->err) {
        P->err = E_SYNTAX;
        P->errline = P->tok.line ? P->tok.line : P->line;
        strncpy(P->msg, msg, sizeof P->msg - 1);
    }
}

/* ------------------------------------------------------------ lexer */

static uint32_t peekcp(const parser *P, const char *p, int *len)
{
    const unsigned char *s = (const unsigned char *)p;
    if (p >= P->end) { *len = 0; return 0; }
    if (s[0] < 0x80) { *len = 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0 && p + 1 < P->end) { *len = 2; return ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); }
    if ((s[0] & 0xF0) == 0xE0 && p + 2 < P->end) { *len = 3; return ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
    if ((s[0] & 0xF8) == 0xF0 && p + 3 < P->end) { *len = 4; return ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F); }
    *len = 1;
    return s[0];
}

static int is_opglyph(uint32_t c)
{
    switch (c) {
    case 0x2212: case 0x2260: case 0x2264: case 0x2265: case 0x25B6: case 0x25BA:
    case 0x221A: case 0x00B2: case 0x00B3: case 0x00D7: case 0x00F7: case 0x22C5:
    case 0x207B: case 0x00B9: case 0x00A0: case 0x2192: case 0x1D07: case 0x2148:
    case 0xE003: case 0x00B7: case 0x2009: case 0x200A: case 0x202F:
        return 1;
    default: return 0;
    }
}

static int is_idstart(uint32_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (c >= 0x80 && !is_opglyph(c));
}

static int is_idchar(uint32_t c)
{
    return is_idstart(c) || (c >= '0' && c <= '9');
}

static const struct { const char *s; int kw; } KWS[] = {
    {"BEGIN", KW_BEGIN}, {"END", KW_END}, {"LOCAL", KW_LOCAL}, {"EXPORT", KW_EXPORT},
    {"IF", KW_IF}, {"THEN", KW_THEN}, {"ELSE", KW_ELSE}, {"CASE", KW_CASE},
    {"DEFAULT", KW_DEFAULT}, {"FOR", KW_FOR}, {"FROM", KW_FROM}, {"TO", KW_TO},
    {"DOWNTO", KW_DOWNTO}, {"STEP", KW_STEP}, {"DO", KW_DO}, {"WHILE", KW_WHILE},
    {"REPEAT", KW_REPEAT}, {"UNTIL", KW_UNTIL}, {"BREAK", KW_BREAK},
    {"CONTINUE", KW_CONTINUE}, {"RETURN", KW_RETURN}, {"IFERR", KW_IFERR},
    {"AND", KW_AND}, {"OR", KW_OR}, {"NOT", KW_NOT}, {"XOR", KW_XOR},
    {"MOD", KW_MOD}, {"NTHROOT", KW_NTHROOT}, {"KILL", KW_KILL},
    {"and", KW_AND}, {"or", KW_OR}, {"not", KW_NOT}, {"xor", KW_XOR}, {"mod", KW_MOD},
    {"VIEW", KW_VIEW}, {"KEY", KW_KEY},
    {NULL, 0}
};

static int skip_line_until(parser *P, const char *mark)
{
    /* skip to a line starting (after spaces) with mark, case-insensitive;
     * consume that line */
    size_t ml = strlen(mark);
    while (P->p < P->end) {
        const char *q = P->p;
        while (q < P->end && (*q == ' ' || *q == '\t')) q++;
        if ((size_t)(P->end - q) >= ml) {
            size_t i;
            for (i = 0; i < ml; i++) {
                char a = q[i], b = mark[i];
                if (a >= 'a' && a <= 'z') a = (char)(a - 32);
                if (b >= 'a' && b <= 'z') b = (char)(b - 32);
                if (a != b) break;
            }
            if (i == ml) {
                while (P->p < P->end && *P->p != '\n') P->p++;
                return 1;
            }
        }
        while (P->p < P->end && *P->p != '\n') P->p++;
        if (P->p < P->end) { P->p++; P->line++; }
    }
    return 0;
}

static int lex_int(parser *P, token *t)
{
    /* after '#': digits [':' bits] [bodh] */
    const char *s = P->p, *q;
    char buf[80];
    int n = 0, base = B_DEFAULT, bits = 0, radix, i;
    uint64_t v = 0;
    q = s;
    while (q < P->end && ((*q >= '0' && *q <= '9') || (*q >= 'A' && *q <= 'F') || (*q >= 'a' && *q <= 'f'))) {
        if (n < 79) buf[n++] = *q;
        q++;
    }
    if (q < P->end && *q == ':') {
        const char *r = q + 1;
        int b = 0;
        while (r < P->end && *r >= '0' && *r <= '9') { b = b * 10 + (*r - '0'); r++; }
        if (r > q + 1) { bits = b; q = r; }
    }
    if (q < P->end && (*q == 'h' || *q == 'o')) { base = *q == 'h' ? B_HEX : B_OCT; q++; }
    else if (n && !bits && (buf[n - 1] == 'b' || buf[n - 1] == 'd') && (q >= P->end || !is_idchar((unsigned char)*q))) {
        /* trailing lower-case b/d is a base mark */
        base = buf[n - 1] == 'b' ? B_BIN : B_DEC;
        n--;
    } else if (q < P->end && (*q == 'b' || *q == 'd')) { base = *q == 'b' ? B_BIN : B_DEC; q++; }
    if (!n) return -1;
    radix = base == B_BIN ? 2 : base == B_OCT ? 8 : base == B_DEC ? 10 : 16;
    for (i = 0; i < n; i++) {
        int d = buf[i] <= '9' ? buf[i] - '0' : (buf[i] | 32) - 'a' + 10;
        if (d >= radix) return -1;
        v = v * (uint64_t)radix + (uint64_t)d;
    }
    P->p = q;
    v_none(&t->iv);
    t->iv.t = T_INT;
    t->iv.ib = (uint8_t)base;
    t->iv.iw = (uint8_t)(bits > 64 ? 64 : bits);
    t->iv.u.i.hi = (uint32_t)(v >> 32);
    t->iv.u.i.lo = (uint32_t)v;
    t->kind = TK_INT;
    return 0;
}

static void lex(parser *P, token *t)
{
    uint32_t c;
    int len;
again:
    memset(t, 0, sizeof *t);
    for (;;) {
        if (P->p >= P->end) { t->kind = TK_EOF; t->line = P->line; return; }
        c = peekcp(P, P->p, &len);
        if (c == '\n') { P->line++; P->p++; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == 0xA0 || c == 0x2009 || c == 0x200A || c == 0x202F || c == 0xFEFF) { P->p += len; continue; }
        if (c == '/' && P->p + 1 < P->end && P->p[1] == '/') { while (P->p < P->end && *P->p != '\n') P->p++; continue; }
        if (c == '/' && P->p + 1 < P->end && P->p[1] == '*') {
            P->p += 2;
            while (P->p + 1 < P->end && !(P->p[0] == '*' && P->p[1] == '/')) { if (*P->p == '\n') P->line++; P->p++; }
            P->p += 2;
            continue;
        }
        break;
    }
    t->line = P->line;
    t->s = P->p;
    /* numbers */
    if ((c >= '0' && c <= '9') || (c == '.' && P->p + 1 < P->end && P->p[1] >= '0' && P->p[1] <= '9')) {
        char buf[96];
        int n = 0;
        const char *q = P->p;
        while (q < P->end && ((*q >= '0' && *q <= '9') || *q == '.')) { if (n < 90) buf[n++] = *q; q++; }
        {
            /* exponent: E e or U+1D07, optional sign (+ - U+2212) */
            int el = 0;
            uint32_t ec = peekcp(P, q, &el);
            if (ec == 'E' || ec == 'e' || ec == 0x1D07) {
                const char *r = q + el;
                int sl = 0, neg = 0;
                uint32_t sc = peekcp(P, r, &sl);
                if (sc == '-' || sc == 0x2212) { neg = 1; r += sl; }
                else if (sc == '+') r += sl;
                if (r < P->end && *r >= '0' && *r <= '9') {
                    if (n < 90) buf[n++] = 'E';
                    if (neg && n < 90) buf[n++] = '-';
                    while (r < P->end && *r >= '0' && *r <= '9') { if (n < 90) buf[n++] = *r; r++; }
                    q = r;
                }
            }
        }
        buf[n] = 0;
        P->p = q;
        t->kind = TK_NUM;
        dec_from_str(&t->num, buf, NULL);
        t->len = (int)(P->p - t->s);
        return;
    }
    if (c == '#') {
        const char *q = P->p + 1;
        char w[8];
        int n = 0;
        while (q < P->end && n < 7 && ((*q | 32) >= 'a' && (*q | 32) <= 'z')) { w[n++] = (char)(*q | 32); q++; }
        w[n] = 0;
        if (!strcmp(w, "pragma") || !strcmp(w, "end")) { while (P->p < P->end && *P->p != '\n') P->p++; goto again; }
        if (!strcmp(w, "cas")) { skip_line_until(P, "#end"); goto again; }
        if (!strcmp(w, "python")) { skip_line_until(P, "#END"); goto again; }
        P->p++;
        if (lex_int(P, t)) { t->kind = TK_OP; t->op = 0; perr(P, "bad integer literal"); }
        t->len = (int)(P->p - t->s);
        return;
    }
    if (c == '"') {
        char *buf;
        size_t n = 0, cap = 64;
        const char *q = P->p + 1;
        buf = (char *)malloc(cap);
        for (;;) {
            char ch;
            if (q >= P->end) { perr(P, "unterminated string"); break; }
            ch = *q++;
            if (ch == '"') {
                if (q < P->end && *q == '"') { ch = '"'; q++; }
                else break;
            } else if (ch == '\\' && q < P->end) {
                char e = *q++;
                ch = e == 'n' ? '\n' : e == 't' ? '\t' : e;
            } else if (ch == '\n') P->line++;
            if (n + 2 > cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
            buf[n++] = ch;
        }
        P->p = q;
        t->kind = TK_STR;
        t->str = pa_strdup(P, buf, n);
        t->slen = n;
        free(buf);
        t->len = (int)(P->p - t->s);
        return;
    }
    {
        int l2 = 0;
        uint32_t c2 = c == 0x2192 ? peekcp(P, P->p + len, &l2) : 0;
        if (c == 0x2192 && !is_idstart(c2)) c2 = 0;
        if (!is_idstart(c) && !c2) goto operators;
    }
    {
        const char *q = P->p;
        int i;
        for (;;) {
            int l;
            uint32_t d = peekcp(P, q, &l);
            if (q >= P->end) break;
            if (is_idchar(d) || d == 0x2192) { q += l; continue; }
            if (d == '.' && q + 1 < P->end) {
                int l2;
                uint32_t d2 = peekcp(P, q + 1, &l2);
                if (is_idstart(d2)) { q += 1; continue; }
            }
            break;
        }
        t->kind = TK_ID;
        t->str = pa_strdup(P, P->p, (size_t)(q - P->p));
        t->slen = (size_t)(q - P->p);
        P->p = q;
        for (i = 0; KWS[i].s; i++) if (!strcmp(KWS[i].s, t->str)) { t->kind = TK_KW; t->op = KWS[i].kw; break; }
        t->len = (int)(P->p - t->s);
        return;
    }
operators:
    t->kind = TK_OP;
    {
        const char *q = P->p;
        char c1 = q + 1 < P->end ? q[1] : 0;
        switch (c) {
        case ':': if (c1 == '=') { t->op = P_ASSIGN; P->p += 2; } else { t->op = P_COLON; P->p++; } break;
        case '=': if (c1 == '=') { t->op = P_EQ; P->p += 2; } else if (c1 == '>') { t->op = P_STORE; P->p += 2; } else { t->op = P_EQ1; P->p++; } break;
        case '<': if (c1 == '>') { t->op = P_NE; P->p += 2; } else if (c1 == '=') { t->op = P_LE; P->p += 2; } else { t->op = P_LT; P->p++; } break;
        case '>': if (c1 == '=') { t->op = P_GE; P->p += 2; } else { t->op = P_GT; P->p++; } break;
        case '!': if (c1 == '=' && !(q + 2 < P->end && q[2] == '=')) { t->op = P_NE; P->p += 2; } else { t->op = P_BANG; P->p++; } break;
        case '+': t->op = P_PLUS; P->p++; break;
        case '-': t->op = P_MINUS; P->p++; break;
        case '*': t->op = P_MUL; P->p++; break;
        case '/': t->op = P_DIV; P->p++; break;
        case '^': t->op = P_POW; P->p++; break;
        case '(': t->op = P_LPAR; P->p++; break;
        case ')': t->op = P_RPAR; P->p++; break;
        case '{': t->op = P_LBRACE; P->p++; break;
        case '}': t->op = P_RBRACE; P->p++; break;
        case '[': t->op = P_LBRACK; P->p++; break;
        case ']': t->op = P_RBRACK; P->p++; break;
        case ',': t->op = P_COMMA; P->p++; break;
        case ';': t->op = P_SEMI; P->p++; break;
        case '|': t->op = P_PIPE; P->p++; break;
        case '&': t->op = P_AMP; P->p++; break;
        case 0x2212: t->op = P_MINUS; P->p += len; break;
        case 0x2260: t->op = P_NE; P->p += len; break;
        case 0x2264: t->op = P_LE; P->p += len; break;
        case 0x2265: t->op = P_GE; P->p += len; break;
        case 0x25B6: case 0x25BA: case 0x2192: t->op = P_STORE; P->p += len; break;
        case 0x221A: t->op = P_SQRT; P->p += len; break;
        case 0x00B2: t->op = P_SQ; P->p += len; break;
        case 0x00B3: t->op = P_CUBE; P->p += len; break;
        case 0x00D7: case 0x22C5: case 0x00B7: t->op = P_MUL; P->p += len; break;
        case 0x00F7: t->op = P_DIV; P->p += len; break;
        case 0x2148: case 0xE003:
            t->kind = TK_ID; t->str = pa_strdup(P, "i", 1); t->slen = 1; P->p += len; break;
        case 0x207B: {
            int l2;
            uint32_t c2 = peekcp(P, P->p + len, &l2);
            if (c2 == 0x00B9) { t->op = P_INV; P->p += len + l2; }
            else { t->op = 0; P->p += len; perr(P, "unexpected character"); }
            break;
        }
        default:
            t->op = 0; P->p += len ? len : 1;
            perr(P, "unexpected character");
        }
    }
    t->len = (int)(P->p - t->s);
}

static void next(parser *P)
{
    if (P->has_peek) { P->tok = P->peek; P->has_peek = 0; }
    else lex(P, &P->tok);
}

static int is_op(parser *P, int op) { return P->tok.kind == TK_OP && P->tok.op == op; }
static int is_kw(parser *P, int kw) { return P->tok.kind == TK_KW && P->tok.op == kw; }

static int accept_op(parser *P, int op) { if (is_op(P, op)) { next(P); return 1; } return 0; }
static int accept_kw(parser *P, int kw) { if (is_kw(P, kw)) { next(P); return 1; } return 0; }

static void expect_op(parser *P, int op, const char *what)
{
    if (!accept_op(P, op)) {
        char m[64];
        strcpy(m, "expected ");
        strncat(m, what, 40);
        perr(P, m);
    }
}

static void expect_kw(parser *P, int kw, const char *what)
{
    if (!accept_kw(P, kw)) {
        char m[64];
        strcpy(m, "expected ");
        strncat(m, what, 40);
        perr(P, m);
    }
}

/* ------------------------------------------------------------ nodes */

static node *mk(parser *P, int type)
{
    node *n = (node *)pa_alloc(P, sizeof(node));
    if (!n) { perr(P, "out of memory"); static node dummy; return &dummy; }
    n->type = (uint8_t)type;
    n->line = P->tok.line;
    return n;
}

typedef struct nvec { node **v; int n, cap; } nvec;

static void nv_push(parser *P, nvec *v, node *n)
{
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 4;
        node **nv = (node **)pa_alloc(P, (size_t)nc * sizeof(node *));
        if (!nv) { perr(P, "out of memory"); return; }
        if (v->n) memcpy(nv, v->v, (size_t)v->n * sizeof(node *));
        v->v = nv; v->cap = nc;
    }
    v->v[v->n++] = n;
}

static node *expr(parser *P);
static node *stmts(parser *P, int t1, int t2, int t3);

static void args(parser *P, node *call)
{
    /* after '(' ; fills kids, consumes ')' */
    nvec v = {0, 0, 0};
    if (!is_op(P, P_RPAR)) {
        do {
            nv_push(P, &v, expr(P));
        } while (accept_op(P, P_COMMA) && !P->err);
    }
    expect_op(P, P_RPAR, "')'");
    call->kids = v.v; call->nkids = v.n;
}

static node *primary(parser *P)
{
    node *n;
    token t = P->tok;
    if (P->err) return mk(P, N_NOP);
    switch (t.kind) {
    case TK_NUM:
        n = mk(P, N_NUM); v_real(&n->lit, &t.num); next(P); return n;
    case TK_INT:
        n = mk(P, N_NUM); n->lit = t.iv; next(P); return n;
    case TK_STR:
        n = mk(P, N_STR); n->name = t.str; n->nlen = t.slen; next(P); return n;
    case TK_ID:
        next(P);
        if (is_op(P, P_LPAR)) {
            n = mk(P, N_CALL);
            n->name = t.str; n->line = t.line;
            next(P);
            args(P, n);
            if (is_op(P, P_LPAR)) perr(P, "a call's result cannot be indexed");
            return n;
        }
        n = mk(P, N_NAME); n->name = t.str; n->line = t.line;
        return n;
    case TK_OP:
        switch (t.op) {
        case P_LPAR: {
            node *a;
            next(P);
            a = expr(P);
            if (accept_op(P, P_COMMA)) {
                n = mk(P, N_CPLX);
                n->a = a; n->b = expr(P);
                expect_op(P, P_RPAR, "')'");
                return n;
            }
            expect_op(P, P_RPAR, "')'");
            return a;
        }
        case P_LBRACE: {
            nvec v = {0, 0, 0};
            next(P);
            n = mk(P, N_LIST);
            if (!is_op(P, P_RBRACE)) {
                do nv_push(P, &v, expr(P)); while (accept_op(P, P_COMMA) && !P->err);
            }
            expect_op(P, P_RBRACE, "'}'");
            n->kids = v.v; n->nkids = v.n;
            return n;
        }
        case P_LBRACK: {
            nvec v = {0, 0, 0};
            next(P);
            if (is_op(P, P_LBRACK)) {
                /* matrix: rows of equal length */
                n = mk(P, N_MAT);
                do {
                    node *row = mk(P, N_VEC);
                    nvec r = {0, 0, 0};
                    expect_op(P, P_LBRACK, "'['");
                    if (!is_op(P, P_RBRACK)) {
                        do nv_push(P, &r, expr(P)); while (accept_op(P, P_COMMA) && !P->err);
                    }
                    expect_op(P, P_RBRACK, "']'");
                    row->kids = r.v; row->nkids = r.n;
                    nv_push(P, &v, row);
                } while (accept_op(P, P_COMMA) && !P->err);
                expect_op(P, P_RBRACK, "']'");
            } else {
                n = mk(P, N_VEC);
                if (!is_op(P, P_RBRACK)) {
                    do nv_push(P, &v, expr(P)); while (accept_op(P, P_COMMA) && !P->err);
                }
                expect_op(P, P_RBRACK, "']'");
            }
            n->kids = v.v; n->nkids = v.n;
            return n;
        }
        default: break;
        }
        break;
    default: break;
    }
    perr(P, t.kind == TK_EOF ? "unexpected end" : "syntax error");
    return mk(P, N_NOP);
}

/* prefix square root binds to the next operand only; postfix ! and squares
 * then apply to the result (same level, left to right) */
static node *level2(parser *P)
{
    node *n;
    if (is_op(P, P_SQRT)) {
        node *s = mk(P, N_SQRT);
        next(P);
        if (is_op(P, P_SQRT)) s->a = level2(P);
        else if (is_op(P, P_MINUS)) { node *g = mk(P, N_NEG); next(P); g->a = level2(P); s->a = g; }
        else s->a = primary(P);
        n = s;
    } else n = primary(P);
    for (;;) {
        if (is_op(P, P_BANG) || is_op(P, P_SQ) || is_op(P, P_CUBE) || is_op(P, P_INV)) {
            node *p = mk(P, N_POST);
            p->op = (uint8_t)P->tok.op;
            p->a = n;
            next(P);
            n = p;
        } else break;
    }
    return n;
}

static node *signed_operand(parser *P, node *(*f)(parser *))
{
    if (is_op(P, P_MINUS)) { node *g = mk(P, N_NEG); next(P); g->a = signed_operand(P, f); return g; }
    if (is_op(P, P_PLUS)) { next(P); return signed_operand(P, f); }
    return f(P);
}

static node *rootexpr(parser *P)
{
    node *n = level2(P);
    while (is_kw(P, KW_NTHROOT)) {
        node *b = mk(P, N_BIN);
        next(P);
        b->op = OP_NTHROOT; b->a = n; b->b = signed_operand(P, level2);
        n = b;
    }
    return n;
}

static node *powexpr(parser *P)
{
    node *n = rootexpr(P);
    while (is_op(P, P_POW)) {
        node *b = mk(P, N_BIN);
        next(P);
        b->op = OP_POW; b->a = n; b->b = signed_operand(P, rootexpr);
        n = b;
    }
    return n;
}

static node *negterm(parser *P)
{
    if (is_op(P, P_MINUS)) { node *g = mk(P, N_NEG); next(P); g->a = negterm(P); return g; }
    if (is_op(P, P_PLUS)) { next(P); return negterm(P); }
    return powexpr(P);
}

static node *mulexpr(parser *P)
{
    node *n = negterm(P);
    for (;;) {
        int op;
        if (is_op(P, P_MUL)) op = OP_MUL;
        else if (is_op(P, P_DIV)) op = OP_DIV;
        else if (is_kw(P, KW_MOD)) op = OP_MOD;
        else break;
        {
            node *b = mk(P, N_BIN);
            next(P);
            b->op = (uint8_t)op; b->a = n; b->b = negterm(P);
            n = b;
        }
    }
    return n;
}

static node *addexpr(parser *P)
{
    node *n = mulexpr(P);
    for (;;) {
        int op;
        if (is_op(P, P_PLUS)) op = OP_ADD;
        else if (is_op(P, P_MINUS)) op = OP_SUB;
        else break;
        {
            node *b = mk(P, N_BIN);
            next(P);
            b->op = (uint8_t)op; b->a = n; b->b = mulexpr(P);
            n = b;
        }
    }
    return n;
}

static int relop(parser *P)
{
    if (P->tok.kind != TK_OP) return -1;
    switch (P->tok.op) {
    case P_EQ: case P_EQ1: return OP_EQ;
    case P_NE: return OP_NE;
    case P_LT: return OP_LT;
    case P_LE: return OP_LE;
    case P_GT: return OP_GT;
    case P_GE: return OP_GE;
    default: return -1;
    }
}

static node *relexpr(parser *P)
{
    node *n = addexpr(P);
    int op = relop(P);
    if (op < 0) return n;
    {
        nvec v = {0, 0, 0};
        uint8_t ops[64];
        int k = 0;
        nv_push(P, &v, n);
        while ((op = relop(P)) >= 0 && k < 63) {
            next(P);
            ops[k++] = (uint8_t)op;
            nv_push(P, &v, addexpr(P));
        }
        if (k == 1) {
            node *b = mk(P, N_BIN);
            b->op = ops[0]; b->a = v.v[0]; b->b = v.v[1];
            return b;
        }
        n = mk(P, N_CHAIN);
        n->kids = v.v; n->nkids = v.n;
        n->ops = (uint8_t *)pa_alloc(P, (size_t)k);
        memcpy(n->ops, ops, (size_t)k);
        return n;
    }
}

static node *notexpr(parser *P)
{
    if (is_kw(P, KW_NOT)) {
        node *g = mk(P, N_NOT);
        next(P);
        g->a = notexpr(P);
        return g;
    }
    return relexpr(P);
}

static node *andexpr(parser *P)
{
    node *n = notexpr(P);
    while (is_kw(P, KW_AND)) {
        node *b = mk(P, N_BIN);
        next(P);
        b->op = OP_AND; b->a = n; b->b = notexpr(P);
        n = b;
    }
    return n;
}

static node *orexpr(parser *P)
{
    node *n = andexpr(P);
    for (;;) {
        int op;
        if (is_kw(P, KW_OR)) op = OP_OR;
        else if (is_kw(P, KW_XOR)) op = OP_XOR;
        else break;
        {
            node *b = mk(P, N_BIN);
            next(P);
            b->op = (uint8_t)op; b->a = n; b->b = andexpr(P);
            n = b;
        }
    }
    return n;
}

static int is_lvalue(const node *n) { return n->type == N_NAME || n->type == N_CALL; }

static node *expr(parser *P)
{
    node *n = orexpr(P);
    if (is_op(P, P_ASSIGN)) {
        node *a = mk(P, N_ASSIGN);
        if (!is_lvalue(n)) perr(P, "cannot assign to this");
        next(P);
        a->a = n; a->b = expr(P);
        return a;
    }
    while (is_op(P, P_STORE)) {
        node *a = mk(P, N_ASSIGN), *t;
        next(P);
        t = level2(P);
        if (!is_lvalue(t)) perr(P, "cannot store into this");
        a->a = t; a->b = n;
        n = a;
    }
    return n;
}

/* ------------------------------------------------------------ statements */

static node *local_decl(parser *P)
{
    /* after LOCAL: name [:= expr] {, name [:= expr]} */
    node *n = mk(P, N_LOCAL);
    nvec v = {0, 0, 0};
    do {
        node *d;
        if (P->tok.kind != TK_ID) { perr(P, "expected a name"); break; }
        d = mk(P, N_NAME);
        d->name = P->tok.str;
        next(P);
        if (accept_op(P, P_ASSIGN)) d->a = expr(P);
        nv_push(P, &v, d);
    } while (accept_op(P, P_COMMA) && !P->err);
    n->kids = v.v; n->nkids = v.n;
    if (v.n > 8) perr(P, "more than 8 variables in one LOCAL");
    return n;
}

static node *statement(parser *P)
{
    node *n;
    int line = P->tok.line;
    if (P->tok.kind == TK_KW) {
        switch (P->tok.op) {
        case KW_BEGIN:
            next(P);
            n = stmts(P, KW_END, 0, 0);
            expect_kw(P, KW_END, "END");
            return n;
        case KW_LOCAL:
            next(P);
            return local_decl(P);
        case KW_IF:
            n = mk(P, N_IF);
            next(P);
            n->a = expr(P);
            expect_kw(P, KW_THEN, "THEN");
            n->b = stmts(P, KW_ELSE, KW_END, 0);
            if (accept_kw(P, KW_ELSE)) n->c = stmts(P, KW_END, 0, 0);
            expect_kw(P, KW_END, "END");
            return n;
        case KW_CASE: {
            nvec v = {0, 0, 0};
            n = mk(P, N_CASE);
            next(P);
            for (;;) {
                while (accept_op(P, P_SEMI)) {}
                if (is_kw(P, KW_IF)) {
                    node *br = mk(P, N_IF);
                    next(P);
                    br->a = expr(P);
                    expect_kw(P, KW_THEN, "THEN");
                    br->b = stmts(P, KW_END, 0, 0);
                    expect_kw(P, KW_END, "END");
                    nv_push(P, &v, br);
                    if (P->err) break;
                    continue;
                }
                break;
            }
            if (accept_kw(P, KW_DEFAULT)) n->c = stmts(P, KW_END, 0, 0);
            expect_kw(P, KW_END, "END");
            n->kids = v.v; n->nkids = v.n;
            return n;
        }
        case KW_FOR:
            n = mk(P, N_FOR);
            next(P);
            if (P->tok.kind != TK_ID) { perr(P, "expected a loop variable"); return n; }
            n->a = mk(P, N_NAME);
            n->a->name = P->tok.str;
            next(P);
            if (!accept_kw(P, KW_FROM)) expect_op(P, P_ASSIGN, "FROM");
            n->b = expr(P);
            if (accept_kw(P, KW_DOWNTO)) n->op = 1;
            else expect_kw(P, KW_TO, "TO");
            n->c = expr(P);
            if (accept_kw(P, KW_STEP)) n->d = expr(P);
            expect_kw(P, KW_DO, "DO");
            {
                node *body = stmts(P, KW_END, 0, 0);
                n->kids = (node **)pa_alloc(P, sizeof(node *));
                n->kids[0] = body; n->nkids = 1;
            }
            expect_kw(P, KW_END, "END");
            return n;
        case KW_WHILE:
            n = mk(P, N_WHILE);
            next(P);
            n->a = expr(P);
            expect_kw(P, KW_DO, "DO");
            n->b = stmts(P, KW_END, 0, 0);
            expect_kw(P, KW_END, "END");
            return n;
        case KW_REPEAT:
            n = mk(P, N_REPEAT);
            next(P);
            n->b = stmts(P, KW_UNTIL, 0, 0);
            expect_kw(P, KW_UNTIL, "UNTIL");
            n->a = expr(P);
            return n;
        case KW_IFERR:
            n = mk(P, N_IFERR);
            next(P);
            n->a = stmts(P, KW_THEN, 0, 0);
            expect_kw(P, KW_THEN, "THEN");
            n->b = stmts(P, KW_ELSE, KW_END, 0);
            if (accept_kw(P, KW_ELSE)) n->c = stmts(P, KW_END, 0, 0);
            expect_kw(P, KW_END, "END");
            return n;
        case KW_BREAK:
            n = mk(P, N_BREAK);
            next(P);
            n->op = 1;
            if (P->tok.kind == TK_NUM) {
                int32_t k;
                if (!dec_to_int32(&P->tok.num, &k) && k >= 1 && k < 100) n->op = (uint8_t)k;
                next(P);
            }
            return n;
        case KW_CONTINUE:
            n = mk(P, N_CONTINUE);
            next(P);
            return n;
        case KW_RETURN:
            n = mk(P, N_RETURN);
            next(P);
            if (!is_op(P, P_SEMI) && !(P->tok.kind == TK_KW && (P->tok.op == KW_END || P->tok.op == KW_ELSE || P->tok.op == KW_UNTIL || P->tok.op == KW_DEFAULT)) && P->tok.kind != TK_EOF)
                n->a = expr(P);
            return n;
        case KW_KILL:
            n = mk(P, N_KILL);
            next(P);
            return n;
        default: break;
        }
    }
    n = mk(P, N_EXPR);
    n->line = line;
    n->a = expr(P);
    return n;
}

static int at_end(parser *P, int t1, int t2, int t3)
{
    if (P->tok.kind == TK_EOF) return 1;
    if (P->tok.kind != TK_KW) return 0;
    return P->tok.op == t1 || (t2 && P->tok.op == t2) || (t3 && P->tok.op == t3);
}

static node *stmts(parser *P, int t1, int t2, int t3)
{
    node *b = mk(P, N_BLOCK);
    nvec v = {0, 0, 0};
    for (;;) {
        while (accept_op(P, P_SEMI)) {}
        if (P->err || at_end(P, t1, t2, t3)) break;
        nv_push(P, &v, statement(P));
        if (P->err) break;
        if (!accept_op(P, P_SEMI) && !at_end(P, t1, t2, t3)) { perr(P, "expected ';'"); break; }
    }
    b->kids = v.v; b->nkids = v.n;
    return b;
}

/* ------------------------------------------------------------ top level */

static topdecl *new_decl(parser *P)
{
    if (P->ndecls == P->capdecls) {
        int nc = P->capdecls ? P->capdecls * 2 : 16;
        topdecl *nd = (topdecl *)realloc(P->decls, (size_t)nc * sizeof(topdecl));
        if (!nd) { perr(P, "out of memory"); return NULL; }
        P->decls = nd; P->capdecls = nc;
    }
    memset(&P->decls[P->ndecls], 0, sizeof(topdecl));
    return &P->decls[P->ndecls++];
}

static void top_function(parser *P, char *name, int exported, int line)
{
    /* after the name, at '(' */
    char *params[64];
    int np = 0, i;
    topdecl *d;
    next(P);   /* ( */
    if (!is_op(P, P_RPAR)) {
        do {
            if (P->tok.kind != TK_ID) { perr(P, "expected a parameter name"); return; }
            if (np < 64) params[np++] = P->tok.str;
            next(P);
        } while (accept_op(P, P_COMMA));
    }
    expect_op(P, P_RPAR, "')'");
    if (P->err) return;
    d = new_decl(P);
    if (!d) return;
    d->name = name; d->exported = exported; d->line = line;
    d->nparams = np;
    d->params = (char **)pa_alloc(P, (size_t)(np ? np : 1) * sizeof(char *));
    for (i = 0; i < np; i++) d->params[i] = params[i];
    if (accept_op(P, P_SEMI)) { d->kind = 3; return; }
    d->kind = 1;
    expect_kw(P, KW_BEGIN, "BEGIN");
    d->body = stmts(P, KW_END, 0, 0);
    expect_kw(P, KW_END, "END");
    expect_op(P, P_SEMI, "';' after END");
}

static void top_vars(parser *P, char *first, int exported, int line)
{
    char *name = first;
    for (;;) {
        topdecl *d = new_decl(P);
        if (!d) return;
        d->kind = 2; d->name = name; d->exported = exported; d->line = line;
        if (accept_op(P, P_ASSIGN)) d->body = expr(P);
        if (!accept_op(P, P_COMMA)) break;
        if (P->tok.kind != TK_ID) { perr(P, "expected a name"); return; }
        name = P->tok.str;
        next(P);
    }
    expect_op(P, P_SEMI, "';'");
}

int parse_program(parser *P)
{
    next(P);
    while (!P->err && P->tok.kind != TK_EOF) {
        int exported = 0, line = P->tok.line;
        char *name;
        if (accept_op(P, P_SEMI)) continue;
        if (accept_kw(P, KW_VIEW)) {
            /* VIEW "title", function heading: the menu entry is ignored */
            if (P->tok.kind == TK_STR) next(P);
            accept_op(P, P_COMMA);
        }
        if (accept_kw(P, KW_EXPORT)) exported = 1;
        else if (accept_kw(P, KW_LOCAL)) exported = 0;
        else if (accept_kw(P, KW_KEY)) exported = 0;
        if (P->tok.kind != TK_ID) { perr(P, "expected a declaration"); break; }
        name = P->tok.str;
        next(P);
        if (is_op(P, P_LPAR)) top_function(P, name, exported, line);
        else top_vars(P, name, exported, line);
    }
    return P->err;
}

node *parse_home(parser *P)
{
    node *b;
    next(P);
    b = stmts(P, 0, 0, 0);
    if (!P->err && P->tok.kind != TK_EOF) perr(P, "syntax error");
    return b;
}
