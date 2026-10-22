/* ppl.c -- command-line front end of the PPL engine (host build).
 *
 *   ppl run FILE [FILE.ppl...] [EXPR...]  load programs; evaluate each EXPR (Home);
 *                              with none, call its first exported function
 *                              that takes no arguments
 *   ppl eval EXPR...           evaluate Home expressions
 *   ppl conform FILE.tsv       run a conformance table (see tests/conformance)
 *
 * Options: --ascii  write '-', 'E' and 'i' instead of the Prime's glyphs
 *          --png FILE  run/eval: write the screen G0 as a PNG at the end
 *          --crc  run/eval: print "crc32 XXXXXXXX" of G0 at the end (its RGB
 *                 bytes row by row; the golden-image tests compare it)
 *          --full   show every number in scientific format with 12 digits
 *          --away   ties round away from zero (default: to even, as the Prime)
 *          -v       conform: print every row
 */
#include "../src/vm.h"
#include "../src/gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int ascii_out, full_out;
static const char *png_out;
static int crc_out;

/* ------------------------------------------------------------ PNG */

static uint32_t crc_table[256];

static uint32_t crc32_of(uint32_t crc, const unsigned char *p, size_t n)
{
    size_t i;
    if (!crc_table[1]) {
        uint32_t c, k, j;
        for (k = 0; k < 256; k++) {
            for (c = k, j = 0; j < 8; j++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[k] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++) crc = crc_table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

static void png_chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
    unsigned char h[8];
    uint32_t crc;
    put32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    crc = crc32_of(0, (const unsigned char *)type, 4);
    crc = crc32_of(crc, data, n);
    put32(h, crc);
    fwrite(h, 1, 4, f);
}

/* an RGB PNG of a grob, with stored (uncompressed) deflate blocks */
static int write_png(const char *path, const grob *g)
{
    FILE *f = fopen(path, "wb");
    size_t raw_n = (size_t)g->h * (1 + 3 * (size_t)g->w), i, z_n, pos;
    unsigned char *raw, *z, ihdr[13];
    uint32_t s1 = 1, s2 = 0;
    int x, y;
    if (!f) return -1;
    raw = (unsigned char *)malloc(raw_n + 1);
    z = (unsigned char *)malloc(raw_n + raw_n / 65535 * 5 + 16);
    for (y = 0, i = 0; y < g->h; y++) {
        raw[i++] = 0;
        for (x = 0; x < g->w; x++) {
            uint32_t c = g->px[(long)y * g->w + x];
            raw[i++] = (unsigned char)(c >> 16); raw[i++] = (unsigned char)(c >> 8); raw[i++] = (unsigned char)c;
        }
    }
    z[0] = 0x78; z[1] = 0x01; z_n = 2;
    for (pos = 0; pos < raw_n || pos == 0;) {
        size_t n = raw_n - pos > 65535 ? 65535 : raw_n - pos;
        z[z_n++] = pos + n >= raw_n ? 1 : 0;
        z[z_n++] = (unsigned char)n; z[z_n++] = (unsigned char)(n >> 8);
        z[z_n++] = (unsigned char)~n; z[z_n++] = (unsigned char)(~n >> 8);
        memcpy(z + z_n, raw + pos, n);
        z_n += n; pos += n;
        if (!n) break;
    }
    for (i = 0; i < raw_n; i++) { s1 = (s1 + raw[i]) % 65521; s2 = (s2 + s1) % 65521; }
    put32(z + z_n, (s2 << 16) | s1); z_n += 4;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    put32(ihdr, (uint32_t)g->w); put32(ihdr + 4, (uint32_t)g->h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, z_n);
    png_chunk(f, "IEND", NULL, 0);
    free(raw); free(z);
    return fclose(f);
}

static void maybe_png(ppl_vm *V)
{
    grob *g;
    if (crc_out) {
        uint32_t crc = 0;
        int x, y;
        g = gfx_grob(V, 0);
        for (y = 0; g && y < g->h; y++)
            for (x = 0; x < g->w; x++) {
                uint32_t c = g->px[(long)y * g->w + x];
                unsigned char rgb[3];
                rgb[0] = (unsigned char)(c >> 16); rgb[1] = (unsigned char)(c >> 8); rgb[2] = (unsigned char)c;
                crc = crc32_of(crc, rgb, 3);
            }
        printf("crc32 %08X\n", (unsigned)crc);
    }
    if (!png_out) return;
    g = gfx_grob(V, 0);
    if (!g || write_png(png_out, g)) fprintf(stderr, "ppl: cannot write %s\n", png_out);
}

static uint32_t host_ticks(void) { return (uint32_t)(clock() / (CLOCKS_PER_SEC / 1000)); }

static void host_now(int *t)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    t[0] = tm->tm_year + 1900; t[1] = tm->tm_mon + 1; t[2] = tm->tm_mday;
    t[3] = tm->tm_hour; t[4] = tm->tm_min; t[5] = tm->tm_sec;
}

static void host_print(ppl_vm *V, const char *s) { (void)V; if (s) printf("%s\n", s); }

static void put_text(const char *s)
{
    if (!ascii_out) { fputs(s, stdout); return; }
    for (; *s; s++) {
        const unsigned char *u = (const unsigned char *)s;
        if (u[0] == 0xE2 && u[1] == 0x88 && u[2] == 0x92) { putchar('-'); s += 2; }
        else if (u[0] == 0xE1 && u[1] == 0xB4 && u[2] == 0x87) { putchar('E'); s += 2; }
        else if (u[0] == 0xEE && u[1] == 0x80 && u[2] == 0x83) { putchar('i'); s += 2; }
        else putchar(*s);
    }
}

static void show(ppl_vm *V, const value *v)
{
    sbuf b;
    sb_init(&b);
    v_to_text(V, &b, v, 1);
    sb_drop_nul(&b);
    put_text(b.p ? b.p : "");
    putchar('\n');
    sb_free(&b);
}

static char *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    *n = fread(buf, 1, (size_t)len, f);
    buf[*n] = 0;
    fclose(f);
    if (hpprgm_is((const uint8_t *)buf, *n)) {
        char *src;
        size_t sn;
        if (hpprgm_source((const uint8_t *)buf, *n, &src, &sn)) { free(buf); return NULL; }
        free(buf);
        *n = sn;
        return src;
    }
    /* UTF-16LE text (with a byte-order mark): convert to UTF-8 */
    if (*n >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) {
        size_t units = (*n - 2) / 2, i, k;
        uint16_t *u = (uint16_t *)malloc(units * sizeof(uint16_t) + 2);
        char *o;
        for (i = 0; i < units; i++) u[i] = (uint16_t)((unsigned char)buf[2 + 2 * i] | ((unsigned char)buf[3 + 2 * i] << 8));
        k = u16_to_utf8(NULL, u, (uint32_t)units);
        o = (char *)malloc(k + 1);
        u16_to_utf8(o, u, (uint32_t)units);
        o[k] = 0;
        free(u); free(buf);
        *n = k;
        return o;
    }
    return buf;
}

static void report_error(ppl_vm *V, int e, const char *msg)
{
    if (msg && *msg) fprintf(stderr, "Error: %s\n", msg);
    else if (V->errname[0] && e == E_UNDEF) fprintf(stderr, "Error: %s: %s (line %d)\n", ppl_errmsg(e), V->errname, V->errline);
    else fprintf(stderr, "Error: %s (line %d)\n", ppl_errmsg(e), V->errline);
}

static void setup(ppl_vm *V)
{
    ppl_init(V);
    V->ticks = host_ticks;
    V->print = host_print;
    V->now = host_now;
    if (full_out) { V->set.format = V->hset.format = FMT_SCI; V->set.digits = V->hset.digits = 11; }
}

static const char *base_name(const char *path, char *out, size_t n)
{
    const char *s = strrchr(path, '/'), *d;
    size_t k;
    s = s ? s + 1 : path;
    d = strrchr(s, '.');
    k = d ? (size_t)(d - s) : strlen(s);
    if (k >= n) k = n - 1;
    memcpy(out, s, k);
    out[k] = 0;
    return out;
}

static int cmd_run(int argc, char **argv)
{
    ppl_vm *V = (ppl_vm *)malloc(sizeof(ppl_vm));
    size_t n;
    char *src, name[64], msg[160];
    int e, i, rc = 0;
    if (argc < 1) { fprintf(stderr, "usage: ppl run FILE [EXPR...]\n"); return 2; }
    src = read_file(argv[0], &n);
    if (!src) { perror(argv[0]); return 2; }
    setup(V);
    e = ppl_load(V, base_name(argv[0], name, sizeof name), src, n, msg, sizeof msg);
    free(src);
    if (e) { report_error(V, e, msg); ppl_free(V); free(V); return 1; }
    if (msg[0]) fprintf(stderr, "Warning: %s\n", msg);
    /* more programs: the arguments after FILE that end in .ppl (companions) */
    while (argc > 1 && strlen(argv[1]) > 4 && !strcmp(argv[1] + strlen(argv[1]) - 4, ".ppl")) {
        argc--, argv++;
        if (!(src = read_file(argv[0], &n))) { perror(argv[0]); ppl_free(V); free(V); return 2; }
        e = ppl_load(V, base_name(argv[0], name, sizeof name), src, n, msg, sizeof msg);
        free(src);
        if (e) { report_error(V, e, msg); ppl_free(V); free(V); return 1; }
        if (msg[0]) fprintf(stderr, "Warning: %s\n", msg);
    }
    if (argc == 1) {
        pprog *p = V->progs;
        for (i = 0; i < p->nf; i++)
            if (p->f[i]->exported && p->f[i]->nparams == 0) {
                value r;
                e = ppl_call(V, p->f[i]->name, 0, NULL, &r);
                if (e) { report_error(V, e, NULL); rc = 1; }
                else { show(V, &r); v_decref(&r); }
                break;
            }
    }
    for (i = 1; i < argc; i++) {
        value r;
        e = ppl_eval(V, argv[i], &r);
        if (e) { report_error(V, e, NULL); rc = 1; continue; }
        show(V, &r);
        v_decref(&r);
    }
    maybe_png(V);
    ppl_free(V);
    free(V);
    return rc;
}

/* ppl names FILE...: the names each program leaves to run-time lookup
 * (not local, not its own, not a system variable, not a built-in) */
static int cmd_names(int argc, char **argv)
{
    int i, j, k;
    for (i = 0; i < argc; i++) {
        ppl_vm *V = (ppl_vm *)malloc(sizeof(ppl_vm));
        size_t n;
        char *src = read_file(argv[i], &n), name[64], msg[160];
        if (!src) continue;
        setup(V);
        if (!ppl_load(V, base_name(argv[i], name, sizeof name), src, n, msg, sizeof msg)) {
            pprog *p = V->progs;
            for (j = 0; j < p->nf; j++)
                for (k = 0; k < p->f[j]->nnames; k++) {
                    const char *nm = p->f[j]->names[k];
                    if (!vm_find_func(V, nm, 0)) printf("%s\t%s\n", name, nm);
                }
        }
        free(src);
        ppl_free(V);
        free(V);
    }
    return 0;
}

static int cmd_eval(int argc, char **argv)
{
    ppl_vm *V = (ppl_vm *)malloc(sizeof(ppl_vm));
    int i, rc = 0;
    setup(V);
    for (i = 0; i < argc; i++) {
        value r;
        int e = ppl_eval(V, argv[i], &r);
        if (e) { report_error(V, e, NULL); rc = 1; continue; }
        show(V, &r);
        v_decref(&r);
    }
    maybe_png(V);
    ppl_free(V);
    free(V);
    return rc;
}

/* ------------------------------------------------------------ conform */

typedef struct row { char *entry, *call, *answer, *type; } row;

static char *unescape(const char *s)
{
    /* the table writes newlines in programs as \n */
    char *o = (char *)malloc(strlen(s) + 1), *p = o;
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == 'n') { *p++ = '\n'; s++; }
        else *p++ = *s;
    }
    *p = 0;
    return o;
}

/* the text the harness records: STRING() of the answer, strings quoted
 * with inner quotes escaped */
static void answer_text(ppl_vm *V, const value *v, sbuf *out)
{
    sbuf b;
    sb_init(&b);
    if (v->t == T_STR) {
        size_t i;
        v_to_text(V, &b, v, 0);
        sb_puts(out, "\"");
        for (i = 0; i < b.n; i++) {
            if (b.p[i] == '"') sb_puts(out, "\\\"");
            else if (b.p[i] == '\n') sb_puts(out, "\\n");
            else sb_put(out, &b.p[i], 1);
        }
        sb_puts(out, "\"");
    } else {
        v_to_text(V, &b, v, 1);
        if (b.p) sb_put(out, b.p, b.n);
    }
    sb_free(&b);
}

static size_t utf8_prefix(const char *s, size_t chars)
{
    size_t i = 0, c = 0;
    while (s[i] && c < chars) {
        unsigned char u = (unsigned char)s[i];
        i += u < 0x80 ? 1 : u < 0xE0 ? 2 : u < 0xF0 ? 3 : 4;
        c++;
    }
    return i;
}

static int run_row(ppl_vm *V, const row *r, int k, sbuf *got)
{
    /* returns 1 pass, 0 fail */
    char msg[160], name[32];
    int e;
    char *call = unescape(r->call);
    snprintf(name, sizeof name, "T%d", k);
    if (!strcmp(r->answer, "*compiles*") || !strcmp(r->answer, "*does not compile*")) {
        e = ppl_load(V, name, call, strlen(call), msg, sizeof msg);
        sb_puts(got, e ? "*does not compile*" : "*compiles*");
        if (e) { sb_puts(got, " ("); sb_puts(got, msg); sb_puts(got, ")"); }
        free(call);
        return (e != 0) == !strcmp(r->answer, "*does not compile*");
    }
    {
        sbuf src;
        value v;
        sb_init(&src);
        if (strchr(call, ';')) { sb_puts(&src, "EXPORT ZX()\nBEGIN\n"); sb_puts(&src, call); sb_puts(&src, "\nEND;\n"); }
        else { sb_puts(&src, "EXPORT ZX()\nBEGIN\nRETURN "); sb_puts(&src, call); sb_puts(&src, ";\nEND;\n"); }
        e = ppl_load(V, name, src.p, src.n, msg, sizeof msg);
        sb_free(&src);
        free(call);
        if (e) { sb_puts(got, "*error* (compile: "); sb_puts(got, msg); sb_puts(got, ")"); return !strcmp(r->answer, "*error*"); }
        e = ppl_call(V, "ZX", 0, NULL, &v);
        if (e) {
            sb_puts(got, "*error* (");
            sb_puts(got, ppl_errmsg(e));
            sb_puts(got, ")");
            return !strcmp(r->answer, "*error*");
        }
        answer_text(V, &v, got);
        {
            int t = v.t < T_THUNK ? v.t : -1, ok;
            size_t want = strlen(r->answer), have = got->n;
            static const char cutmark[] = " (cut at 160 characters)";
            if (want > sizeof cutmark - 1 && !strcmp(r->answer + want - (sizeof cutmark - 1), cutmark))
                want -= sizeof cutmark - 1;   /* the harness kept 160 characters */
            /* the harness keeps 160 characters */
            size_t cut = utf8_prefix(got->p ? got->p : "", 160);
            if (have > cut) have = cut;
            ok = have == want && !memcmp(got->p, r->answer, want);
            if (ok && r->type[0] && atoi(r->type) != t) ok = 0;
            v_decref(&v);
            return ok;
        }
    }
}

static int cmd_conform(int argc, char **argv)
{
    FILE *f;
    char line[8192];
    row *rows = NULL;
    int nr = 0, cap = 0, i, verbose = 0, pass = 0, total = 0;
    const char *path = NULL;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else path = argv[i];
    }
    if (!path || !(f = fopen(path, "r"))) { fprintf(stderr, "usage: ppl conform FILE.tsv [-v]\n"); return 2; }
    while (fgets(line, sizeof line, f)) {
        char *p = line, *fld[4];
        int k;
        size_t L = strlen(line);
        if (line[0] == '#' || line[0] == '\n') continue;
        while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
        for (k = 0; k < 4; k++) {
            fld[k] = p;
            p = strchr(p, '\t');
            if (p) *p++ = 0; else { for (k++; k < 4; k++) fld[k] = (char *)""; break; }
        }
        if (nr == cap) { cap = cap ? cap * 2 : 256; rows = (row *)realloc(rows, (size_t)cap * sizeof(row)); }
        rows[nr].entry = strdup(fld[0]); rows[nr].call = strdup(fld[1]);
        rows[nr].answer = strdup(fld[2]); rows[nr].type = strdup(fld[3]);
        nr++;
    }
    fclose(f);
    /* entry by entry, each on a fresh machine; programs first */
    for (i = 0; i < nr;) {
        int j = i, pass_compile;
        ppl_vm *V = (ppl_vm *)malloc(sizeof(ppl_vm));
        while (j < nr && !strcmp(rows[j].entry, rows[i].entry)) j++;
        setup(V);
        V->print = NULL;
        for (pass_compile = 1; pass_compile >= 0; pass_compile--) {
            int k;
            for (k = i; k < j; k++) {
                int isprog = !strcmp(rows[k].answer, "*compiles*") || !strcmp(rows[k].answer, "*does not compile*");
                sbuf got;
                int ok;
                if (isprog != pass_compile) continue;
                sb_init(&got);
                ok = run_row(V, &rows[k], k, &got);
                total++; pass += ok;
                printf("%s\t%s\t%s\t%s\t%s\n", ok ? "PASS" : "FAIL", rows[k].entry, rows[k].call,
                       rows[k].answer, got.p ? got.p : "");
                (void)verbose;
                sb_free(&got);
            }
        }
        ppl_free(V);
        free(V);
        i = j;
    }
    fprintf(stderr, "conformance: %d/%d rows pass (%.1f%%)\n", pass, total, total ? 100.0 * pass / total : 0.0);
    return 0;
}

int main(int argc, char **argv)
{
    int i, k = 0;
    char **args = (char **)malloc((size_t)argc * sizeof(char *));
    extern int dec_round_mode;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ascii")) ascii_out = 1;
        else if (!strcmp(argv[i], "--away")) dec_round_mode = 0;
        else if (!strcmp(argv[i], "--full")) full_out = 1;
        else if (!strcmp(argv[i], "--png") && i + 1 < argc) png_out = argv[++i];
        else if (!strcmp(argv[i], "--crc")) crc_out = 1;
        else args[k++] = argv[i];
    }
    if (k < 1) {
        fprintf(stderr, "usage: ppl run FILE [EXPR...] | ppl eval EXPR... | ppl conform FILE.tsv [-v]\n");
        return 2;
    }
    if (!strcmp(args[0], "run")) return cmd_run(k - 1, args + 1);
    if (!strcmp(args[0], "eval")) return cmd_eval(k - 1, args + 1);
    if (!strcmp(args[0], "conform")) return cmd_conform(k - 1, args + 1);
    if (!strcmp(args[0], "names")) return cmd_names(k - 1, args + 1);
    fprintf(stderr, "unknown command %s\n", args[0]);
    return 2;
}
