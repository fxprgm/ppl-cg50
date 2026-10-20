/* parse.h -- PPL lexer and parser (source text -> AST). */
#ifndef PPL_PARSE_H
#define PPL_PARSE_H

#include "vm.h"

/* ---- tokens ---- */
enum {
    TK_EOF = 0, TK_NUM, TK_INT, TK_STR, TK_ID, TK_KW, TK_OP, TK_UNIT, TK_SYM
};

/* keywords */
enum {
    KW_BEGIN = 1, KW_END, KW_LOCAL, KW_EXPORT, KW_IF, KW_THEN, KW_ELSE,
    KW_CASE, KW_DEFAULT, KW_FOR, KW_FROM, KW_TO, KW_DOWNTO, KW_STEP, KW_DO,
    KW_WHILE, KW_REPEAT, KW_UNTIL, KW_BREAK, KW_CONTINUE, KW_RETURN,
    KW_IFERR, KW_AND, KW_OR, KW_NOT, KW_XOR, KW_MOD, KW_NTHROOT, KW_KILL,
    KW_VIEW, KW_KEY, KW_CONST, KW_ICON
};

/* operator tokens (TK_OP, tok.op) */
enum {
    P_ASSIGN = 1, P_STORE, P_EQ, P_EQ1, P_NE, P_LT, P_LE, P_GT, P_GE,
    P_PLUS, P_MINUS, P_MUL, P_DIV, P_POW, P_LPAR, P_RPAR, P_LBRACE,
    P_RBRACE, P_LBRACK, P_RBRACK, P_COMMA, P_SEMI, P_BANG, P_SQ, P_CUBE,
    P_SQRT, P_PI, P_COLON, P_INV, P_PIPE, P_AMP, P_DEG,
    P_EMUL, P_EDIV, P_EPOW, P_EADD, P_ESUB   /* .* ./ .^ .+ .- (element by element) */
};

typedef struct token {
    int kind, op, line;
    const char *s; int len;      /* source text of the token */
    real num;                    /* TK_NUM */
    uint8_t dms;                 /* TK_NUM: a degrees-minutes-seconds literal 13°15′30″ */
    value iv;                    /* TK_INT */
    char *str; size_t slen;      /* TK_STR (UTF-8, unescaped), TK_ID (name) */
} token;

/* ---- AST ---- */
enum {
    N_NUM, N_LIT, N_STR, N_NAME, N_CALL, N_LIST, N_VEC, N_MAT, N_CPLX,
    N_NEG, N_NOT, N_BIN, N_CHAIN, N_POST, N_SQRT, N_ASSIGN,
    N_BLOCK, N_IF, N_CASE, N_FOR, N_WHILE, N_REPEAT, N_IFERR, N_BREAK,
    N_CONTINUE, N_RETURN, N_LOCAL, N_KILL, N_EXPR, N_NOP, N_SYM
};

typedef struct node {
    uint8_t type;
    uint8_t op;          /* N_BIN: OP_*, N_POST: P_*, N_FOR: 1 = DOWNTO */
    int line;
    value lit;           /* N_NUM / N_LIT */
    char *name;          /* N_NAME, N_CALL, N_STR(UTF-8) */
    size_t nlen;
    struct node *a, *b, *c, *d;
    struct node **kids; int nkids;
    uint8_t *ops;        /* N_CHAIN: operators between kids */
} node;

typedef struct topdecl {
    int kind;            /* 1 function, 2 variable, 3 forward declaration */
    int exported;
    char *name;
    char **params; int nparams;
    node *body;          /* function: N_BLOCK; variable: initial value or NULL */
    int line;
} topdecl;

typedef struct parser {
    const char *src, *p, *end;
    int line;
    token tok, peek; int has_peek;
    char *arena; size_t used, cap;     /* bump allocator for the AST */
    struct arena_blk *blocks;
    int err; int errline; char msg[96];
    int dcomma;                        /* #pragma mode(separator(,;...)): decimal comma, ';' separates */
    int depth;                         /* bracket depth (decimal-comma mode: ';' inside is ',') */
    topdecl *decls; int ndecls, capdecls;
} parser;

void parser_init(parser *P, const char *src, size_t n);
void parser_free(parser *P);
int  parse_program(parser *P);          /* fills P->decls */
node *parse_home(parser *P);            /* statements of a Home line */
void *pa_alloc(parser *P, size_t n);

#endif
