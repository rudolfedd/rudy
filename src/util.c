/* util.c — strings, word sets, UTF-8. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* ------------------------------------------------------------------ Str */

void str_init(Str *s) { s->data = NULL; s->len = 0; s->cap = 0; }

void str_free(Str *s) { free(s->data); s->data = NULL; s->len = s->cap = 0; }

void str_clear(Str *s) { s->len = 0; if (s->data) s->data[0] = '\0'; }

void str_reserve(Str *s, size_t need)
{
    if (s->data && s->len + need + 1 <= s->cap) return;
    size_t cap = s->cap ? s->cap : 64;
    while (cap < s->len + need + 1) cap *= 2;
    char *p = realloc(s->data, cap);
    if (!p) { fputs("rudy: out of memory\n", stderr); exit(1); }
    s->data = p;
    s->cap  = cap;
}

void str_append(Str *s, const char *p, size_t n)
{
    str_reserve(s, n);
    memcpy(s->data + s->len, p, n);
    s->len += n;
    s->data[s->len] = '\0';
}

void str_puts(Str *s, const char *p) { str_append(s, p, strlen(p)); }

void str_putc(Str *s, char c) { str_append(s, &c, 1); }

void str_printf(Str *s, const char *fmt, ...)
{
    char    tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { str_append(s, tmp, (size_t)n); return; }
    char *big = malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    str_append(s, big, (size_t)n);
    free(big);
}

char *str_dupn(const char *p, size_t n)
{
    char *r = malloc(n + 1);
    if (!r) { fputs("rudy: out of memory\n", stderr); exit(1); }
    if (n) memcpy(r, p, n);
    r[n] = '\0';
    return r;
}

char *str_dupz(const char *p) { return str_dupn(p, p ? strlen(p) : 0); }

/* -------------------------------------------------------------- WordSet */

static unsigned long ws_hash(const char *w, size_t len)
{
    unsigned long h = 1469598103934665603UL;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)w[i];
        h *= 1099511628211UL;
    }
    return h;
}

static void ws_insert(WordSet *ws, char *word)
{
    size_t mask = ws->cap - 1;
    size_t i = ws_hash(word, strlen(word)) & mask;
    while (ws->keys[i]) {
        if (strcmp(ws->keys[i], word) == 0) { free(word); return; }
        i = (i + 1) & mask;
    }
    ws->keys[i] = word;
    ws->n++;
}

void ws_init(WordSet *ws, const char *words)
{
    memset(ws, 0, sizeof *ws);
    if (!words || !*words) { ws->cap = 16; ws->keys = calloc(ws->cap, sizeof(char *)); return; }

    size_t count = 0;
    for (const char *p = words; *p;) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        count++;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
    }
    size_t cap = 16;
    while (cap < count * 2) cap *= 2;
    ws->keys = calloc(cap, sizeof(char *));
    if (!ws->keys) { fputs("rudy: out of memory\n", stderr); exit(1); }
    ws->cap = cap;

    for (const char *p = words; *p;) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        ws_insert(ws, str_dupn(s, (size_t)(p - s)));
    }
}

void ws_free(WordSet *ws)
{
    if (!ws->keys) return;
    for (size_t i = 0; i < ws->cap; i++) free(ws->keys[i]);
    free(ws->keys);
    ws->keys = NULL;
    ws->cap = ws->n = 0;
}

int ws_has(const WordSet *ws, const char *w, size_t len)
{
    if (!ws || !ws->keys || !ws->cap) return 0;
    size_t mask = ws->cap - 1;
    size_t i = ws_hash(w, len) & mask;
    while (ws->keys[i]) {
        if (strlen(ws->keys[i]) == len && memcmp(ws->keys[i], w, len) == 0) return 1;
        i = (i + 1) & mask;
    }
    return 0;
}

/* ------------------------------------------------------------------ utf8 */

int utf8_encode(int cp, char *out)
{
    if (cp < 0) cp = 0xFFFD;
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int utf8_decode(const char *s, size_t len, int *cp){
    if (len == 0) { *cp = 0; return 0; }
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) {
        if (len < 2 || ((unsigned char)s[1] & 0xC0) != 0x80) { *cp = c; return 1; }
        *cp = ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        if (len < 3 || ((unsigned char)s[1] & 0xC0) != 0x80 ||
            ((unsigned char)s[2] & 0xC0) != 0x80) { *cp = c; return 1; }
        *cp = ((c & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) |
              ((unsigned char)s[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        if (len < 4 || ((unsigned char)s[1] & 0xC0) != 0x80 ||
            ((unsigned char)s[2] & 0xC0) != 0x80 ||
            ((unsigned char)s[3] & 0xC0) != 0x80) { *cp = c; return 1; }
        *cp = ((c & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) |
              (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
        return 4;
    }
    *cp = c;
    return 1;
}

int utf8_width(int cp)
{
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
    /* combining marks */
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F))
        return 0;
    /* wide blocks: CJK, Hangul, fullwidth */
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

size_t utf8_prev(const char *s, size_t pos)
{
    if (pos == 0) return 0;
    size_t p = pos - 1;
    while (p > 0 && ((unsigned char)s[p] & 0xC0) == 0x80) p--;
    return p;
}

size_t utf8_next(const char *s, size_t pos, size_t len)
{
    if (pos >= len) return len;
    int    cp;
    size_t n = utf8_decode(s + pos, len - pos, &cp);
    return pos + (n ? n : 1);
}

size_t utf8_len(const char *s, size_t len)
{
    size_t p = 0, n = 0;
    while (p < len) { p = utf8_next(s, p, len); n++; }
    return n;
}

int utf8_off_to_col(const char *s, size_t len, size_t off, int tabsize)
{
    if (off > len) off = len;
    int    col = 0;
    size_t p = 0;
    while (p < off) {
        if (s[p] == '\t') {
            col += tabsize - (col % tabsize);
            p++;
        } else {
            int cp;
            size_t n = utf8_decode(s + p, len - p, &cp);
            col += utf8_width(cp);
            p += n ? n : 1;
        }
    }
    return col;
}

size_t utf8_col_to_off(const char *s, size_t len, int col, int tabsize)
{
    int    c = 0;
    size_t p = 0;
    while (p < len && c < col) {
        if (s[p] == '\t') {
            c += tabsize - (c % tabsize);
            p++;
        } else {
            int cp;
            size_t n = utf8_decode(s + p, len - p, &cp);
            c += utf8_width(cp);
            p += n ? n : 1;
        }
    }
    return p;
}
