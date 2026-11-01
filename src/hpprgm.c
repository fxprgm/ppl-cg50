/* hpprgm.c -- the source text of an HP Prime .hpprgm program file.
 *
 * The file is a little-endian container of length-prefixed records
 * (magic 7C 61 8A B2).  The source is a record [u32 length][u32 tag]
 * [UTF-16LE text][NUL], possibly after a compiled block, so not aligned.
 * It is found by its shape: try every byte offset, keep payloads that are
 * UTF-16LE text ending in NUL and nearly all printable, take the largest;
 * of candidates ending at the same place take the innermost (a wrapping
 * record can also look like text).  Format described by hp-prime-kit
 * (docs/topics/formats.md, MIT); this code is ours.  Bytes are read one by
 * one, so it runs on big-endian machines. */
#include "vm.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int text_like(const uint8_t *p, uint32_t units)
{
    uint32_t i, bad = 0;
    if (units < 1) return 0;
    if (p[2 * (units - 1)] || p[2 * (units - 1) + 1]) return 0;   /* ends in NUL */
    for (i = 0; i + 1 < units; i++) {
        uint32_t c = (uint32_t)p[2 * i] | ((uint32_t)p[2 * i + 1] << 8);
        if (c == 0) return 0;
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') bad++;
        if (c >= 0xFFFE) bad++;
    }
    return bad * 50 <= units;
}

/* where UTF-16LE text starts in a file: 2 after a byte-order mark, 0 for text without one
 * (HP's older .hpprgm files are only the program text in UTF-16LE: breakout.hpprgm, 2014),
 * -1 when the file is not UTF-16LE.  Without a mark: UTF-8 has no NUL bytes, while UTF-16LE
 * program text has one in the high byte of nearly every character. */
int utf16le_start(const uint8_t *buf, size_t n)
{
    size_t i, m = n < 1024 ? n : 1024, pairs = m / 2, zeros = 0;
    if (n >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) return 2;
    if (pairs < 2 || !buf[0]) return -1;
    for (i = 0; i < pairs; i++) {
        if (!buf[2 * i]) return -1;            /* a NUL low byte: not text */
        if (!buf[2 * i + 1]) zeros++;
    }
    return zeros * 4 >= pairs * 3 ? 0 : -1;
}

int hpprgm_is(const uint8_t *buf, size_t n)
{
    return n >= 12 && buf[0] == 0x7C && buf[1] == 0x61 && buf[2] == 0x8A && buf[3] == 0xB2;
}

/* -> malloc'd UTF-8 source in *out (length *outn); 0 or E_SYNTAX */
int hpprgm_source(const uint8_t *buf, size_t n, char **out, size_t *outn)
{
    size_t o, best_start = 0, best_len = 0, best_end = 0;
    uint16_t *u;
    uint32_t units, i;
    if (!hpprgm_is(buf, n)) return E_SYNTAX;
    for (o = 12; o + 10 <= n; o++) {
        uint32_t len = rd32le(buf + o);
        size_t end;
        if (len < 6 || (len & 1) || len > n - o - 4) continue;
        end = o + 4 + len;
        if (!text_like(buf + o + 8, (len - 4) / 2)) continue;
        if (len > best_len) { best_start = o; best_len = len; best_end = end; }
    }
    /* innermost record ending where the largest ends */
    for (o = best_start + 1; best_len && o + 10 <= best_end; o++) {
        uint32_t len = rd32le(buf + o);
        if (len >= 6 && !(len & 1) && o + 4 + len == best_end && text_like(buf + o + 8, (len - 4) / 2))
            best_start = o;
    }
    if (!best_len) return E_SYNTAX;
    units = (uint32_t)((best_end - (best_start + 8)) / 2) - 1;   /* without the NUL */
    u = (uint16_t *)malloc((units + 1) * sizeof(uint16_t));
    if (!u) return E_MEM;
    for (i = 0; i < units; i++) u[i] = (uint16_t)(buf[best_start + 8 + 2 * i] | (buf[best_start + 9 + 2 * i] << 8));
    *outn = u16_to_utf8(NULL, u, units);
    *out = (char *)malloc(*outn + 1);
    if (!*out) { free(u); return E_MEM; }
    u16_to_utf8(*out, u, units);
    (*out)[*outn] = 0;
    free(u);
    return 0;
}
