/* highlight.c — tokenizer and highlighting cache. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { M_NORMAL = 0, M_BLOCK, M_RAW, M_TEXTBLK, M_VERBATIM };

static int ident_start(int c) { return isalpha(c) || c == '_' || c == '$'; }
static int ident_cont(int c)  { return isalnum(c) || c == '_' || c == '$'; }

/* ---------------------------------------------------------------- Span */

static void span_add(SpanList *o, int start, int len, TokType t)
{
    if (len <= 0) return;
    if (o->n > 0) {
        Span *last = &o->v[o->n - 1];
        if (last->type == t && last->start + last->len == start) {
            last->len += len;
            return;
        }
    }
    if (o->n == o->cap) {
        int    cap = o->cap ? o->cap * 2 : 32;
        Span  *p = realloc(o->v, (size_t)cap * sizeof(Span));
        if (!p) { fputs("rudy: out of memory\n", stderr); exit(1); }
        o->v = p;
        o->cap = cap;
    }
    o->v[o->n].start = start;
    o->v[o->n].len = len;
    o->v[o->n].type = t;
    o->n++;
}

static const char *memfind(const char *hay, size_t hlen, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || hlen < nlen) return NULL;
    for (size_t i = 0; i + nlen <= hlen; i++)
        if (memcmp(hay + i, needle, nlen) == 0) return hay + i;
    return NULL;
}

/* End of a Rust raw string: quote + hashes '#' characters. */
static size_t raw_end(const char *s, size_t len, int hashes)
{
    for (size_t p = 0; p < len; p++) {
        if (s[p] != '"') continue;
        size_t k = p + 1;
        int    c = 0;
        while (k < len && s[k] == '#' && c < hashes) { k++; c++; }
        if (c == hashes) return p + 1 + (size_t)hashes;
    }
    return (size_t)-1;
}

static int only_ws_before(const char *s, size_t i)
{
    for (size_t k = 0; k < i; k++)
        if (s[k] != ' ' && s[k] != '\t') return 0;
    return 1;
}

/* -------------------------------------------------------- Main tokenizer */

void hl_line(const Language *L, const char *s, size_t len, HLState *st, SpanList *out)
{
    size_t i = 0;
    int    mode = st->mode;
    int    hashes = st->hashes;
    const size_t lclen = L->line_comment ? strlen(L->line_comment) : 0;
    const size_t bolen = L->block_open ? strlen(L->block_open) : 0;

    /* continuation of multi-line constructs */
    if (mode == M_BLOCK) {
        const char *e = memfind(s, len, L->block_close ? L->block_close : "*/");
        if (!e) {
            span_add(out, 0, (int)len, T_COMMENT);
            st->mode = M_BLOCK;
            return;
        }
        size_t n = (size_t)(e - s) + strlen(L->block_close);
        span_add(out, 0, (int)n, T_COMMENT);
        i = n;
        mode = M_NORMAL;
    } else if (mode == M_RAW) {
        size_t e = raw_end(s, len, hashes);
        if (e == (size_t)-1) {
            span_add(out, 0, (int)len, T_STRING);
            st->mode = M_RAW;
            return;
        }
        span_add(out, 0, (int)e, T_STRING);
        i = e;
        mode = M_NORMAL;
    } else if (mode == M_TEXTBLK) {
        const char *e = memfind(s, len, "\"\"\"");
        if (!e) {
            span_add(out, 0, (int)len, T_STRING);
            st->mode = M_TEXTBLK;
            return;
        }
        size_t n = (size_t)(e - s) + 3;
        span_add(out, 0, (int)n, T_STRING);
        i = n;
        mode = M_NORMAL;
    } else if (mode == M_VERBATIM) {
        size_t p = 0;
        while (p < len) {
            if (s[p] == '"') {
                if (p + 1 < len && s[p + 1] == '"') { p += 2; continue; }
                break;
            }
            p++;
        }
        if (p == len) {
            span_add(out, 0, (int)len, T_STRING);
            st->mode = M_VERBATIM;
            return;
        }
        span_add(out, 0, (int)(p + 1), T_STRING);
        i = p + 1;
        mode = M_NORMAL;
    }
    st->mode = mode;
    st->hashes = hashes;

    while (i < len) {
        char c = s[i];

        if (c == ' ' || c == '\t') { i++; continue; }

        /* single line comment */
        if (lclen && i + lclen <= len && memcmp(s + i, L->line_comment, lclen) == 0) {
            span_add(out, (int)i, (int)(len - i), T_COMMENT);
            break;
        }

        /* block comment */
        if (bolen && i + bolen <= len && memcmp(s + i, L->block_open, bolen) == 0) {
            const char *e = memfind(s + i, len - i, L->block_close);
            if (!e) {
                span_add(out, (int)i, (int)(len - i), T_COMMENT);
                st->mode = M_BLOCK;
                break;
            }
            size_t n = (size_t)(e - (s + i)) + strlen(L->block_close);
            span_add(out, (int)i, (int)n, T_COMMENT);
            i += n;
            continue;
        }

        /* preprocessor directive at the beginning of a line */
        if ((L->flags & LF_PREPROC) && c == '#' && only_ws_before(s, i)) {
            size_t j = i + 1;
            while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
            size_t ns = j;
            while (j < len && ident_cont((unsigned char)s[j])) j++;
            size_t wlen = j - ns;
            span_add(out, (int)i, (int)(j - i), T_PREPROC);
            if (wlen) {
                const char *w = s + ns;
                int is_include = (wlen == 7 && !memcmp(w, "include", 7)) ||
                                 (wlen == 6 && !memcmp(w, "import", 6)) ||
                                 (wlen == 12 && !memcmp(w, "include_next", 12));
                int is_define = (wlen == 6 && !memcmp(w, "define", 6));
                if (is_include) {
                    size_t k = j;
                    while (k < len && (s[k] == ' ' || s[k] == '\t')) k++;
                    if (k < len && s[k] == '<') {
                        size_t e = k;
                        while (e < len && s[e] != '>') e++;
                        span_add(out, (int)k, (int)(e < len ? e - k + 1 : len - k), T_STRING);
                        i = (e < len) ? e + 1 : len;
                        continue;
                    }
                } else if (is_define) {
                    size_t k = j;
                    while (k < len && (s[k] == ' ' || s[k] == '\t')) k++;
                    size_t ms = k;
                    while (k < len && ident_cont((unsigned char)s[k])) k++;
                    if (k > ms) {
                        span_add(out, (int)ms, (int)(k - ms), T_CONST);
                        i = k;
                        continue;
                    }
                }
            }
            i = j;
            continue;
        }

        /* Rust: attribute #[...] */
        if ((L->flags & LF_RAWSTR) && c == '#' && i + 1 < len && s[i + 1] == '[') {
            size_t p = i + 1;
            int    depth = 0;
            while (p < len) {
                if (s[p] == '[') depth++;
                else if (s[p] == ']') { depth--; if (!depth) { p++; break; } }
                p++;
            }
            span_add(out, (int)i, (int)(p - i), T_ATTR);
            i = p;
            continue;
        }

        /* Rust: raw string r"..." / r#"..."# */
        if ((L->flags & LF_RAWSTR) && c == 'r' && (i == 0 || !ident_cont((unsigned char)s[i - 1]))) {
            size_t j = i + 1;
            int    h = 0;
            while (j < len && s[j] == '#') { h++; j++; }
            if (j < len && s[j] == '"') {
                size_t e = raw_end(s + j, len - j, h);
                if (e == (size_t)-1) {
                    span_add(out, (int)i, (int)(len - i), T_STRING);
                    st->mode = M_RAW;
                    st->hashes = h;
                    break;
                }
                span_add(out, (int)i, (int)(e + 1), T_STRING);
                i = i + e + 1;
                continue;
            }
        }

        /* C#: @"...", Java: @Annotation */
        if (c == '@' && i + 1 < len) {
            if ((L->flags & LF_VERBATIM) && s[i + 1] == '"') {
                size_t p = i + 2;
                int    closed = -1;
                while (p < len) {
                    if (s[p] == '"') {
                        if (p + 1 < len && s[p + 1] == '"') { p += 2; continue; }
                        closed = (int)p;
                        break;
                    }
                    p++;
                }
                if (closed < 0) {
                    span_add(out, (int)i, (int)(len - i), T_STRING);
                    st->mode = M_VERBATIM;
                    break;
                }
                span_add(out, (int)i, (int)(closed - i + 1), T_STRING);
                i = (size_t)closed + 1;
                continue;
            }
            if ((L->flags & LF_CAPTYPE) && ident_start((unsigned char)s[i + 1])) {
                size_t p = i + 1;
                while (p < len && (ident_cont((unsigned char)s[p]) || s[p] == '.')) p++;
                span_add(out, (int)i, (int)(p - i), T_ATTR);
                i = p;
                continue;
            }
        }

        /* Java: text block """...""" */
        if ((L->flags & LF_TEXTBLK) && c == '"' && i + 2 < len &&
            s[i + 1] == '"' && s[i + 2] == '"') {
            const char *e = memfind(s + i + 3, len - i - 3, "\"\"\"");
            if (!e) {
                span_add(out, (int)i, (int)(len - i), T_STRING);
                st->mode = M_TEXTBLK;
                break;
            }
            size_t n = (size_t)(e - (s + i)) + 3;
            span_add(out, (int)i, (int)n, T_STRING);
            i += n;
            continue;
        }

        /* string */
        if (c == '"') {
            size_t p = i + 1;
            while (p < len) {
                if (s[p] == '\\' && p + 1 < len) { p += 2; continue; }
                if (s[p] == '"') { p++; break; }
                p++;
            }
            span_add(out, (int)i, (int)(p - i), T_STRING);
            i = p;
            continue;
        }

        /* character literal or Rust lifetime */
        if (c == '\'') {
            if ((L->flags & LF_LIFETIME) && i + 1 < len && ident_start((unsigned char)s[i + 1])) {
                size_t p = i + 1;
                while (p < len && ident_cont((unsigned char)s[p])) p++;
                if (!(p < len && s[p] == '\'')) {
                    span_add(out, (int)i, (int)(p - i), T_CONST);
                    i = p;
                    continue;
                }
            }
            size_t p = i + 1;
            while (p < len) {
                if (s[p] == '\\' && p + 1 < len) { p += 2; continue; }
                if (s[p] == '\'') { p++; break; }
                p++;
            }
            span_add(out, (int)i, (int)(p - i), T_CHAR);
            i = p;
            continue;
        }

        /* NASM: directives %define */
        if ((L->flags & LF_ASM) && c == '%') {
            size_t p = i + 1;
            while (p < len && (ident_cont((unsigned char)s[p]) || s[p] == '%')) p++;
            span_add(out, (int)i, (int)(p - i), T_DIRECTIVE);
            i = p;
            continue;
        }

        /* NASM: $FFh */
        if ((L->flags & LF_ASM) && c == '$') {
            size_t p = i + 1;
            while (p < len && isxdigit((unsigned char)s[p])) p++;
            if (p < len && (s[p] == 'h' || s[p] == 'H')) p++;
            span_add(out, (int)i, (int)(p - i), T_NUMBER);
            i = p;
            continue;
        }

        /* number */
        if (isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < len && isdigit((unsigned char)s[i + 1]))) {
            size_t p = i;
            if (c == '.') p++;
            while (p < len) {
                char d = s[p];
                if (isalnum((unsigned char)d) || d == '.' || d == '_') { p++; continue; }
                if ((d == '+' || d == '-') && p > i && (s[p - 1] == 'e' || s[p - 1] == 'E')) { p++; continue; }
                break;
            }
            span_add(out, (int)i, (int)(p - i), T_NUMBER);
            i = p;
            continue;
        }

        /* identifier */
        if (ident_start((unsigned char)c)) {
            size_t p = i;
            while (p < len && ident_cont((unsigned char)s[p])) p++;
            size_t      wlen = p - i;
            const char *w = s + i;
            TokType     t = T_IDENT;

            if (ws_has(&L->kw, w, wlen)) t = T_KEYWORD;
            else if (ws_has(&L->ty, w, wlen)) t = T_TYPE;
            else if (ws_has(&L->cn, w, wlen)) t = T_CONST;
            else if (ws_has(&L->bi, w, wlen)) t = T_BUILTIN;
            else if (ws_has(&L->rg, w, wlen)) t = T_REG;
            else if (ws_has(&L->lb, w, wlen)) t = T_LABEL;
            else if (p < len && s[p] == '(' && t == T_IDENT) t = T_FUNC;
            else if ((L->flags & LF_ASM) && p < len && s[p] == ':') t = T_LABEL;
            else if ((L->flags & LF_CAPTYPE) && isupper((unsigned char)c)) {
                int allcaps = 1;
                for (size_t k = i; k < p; k++)
                    if (islower((unsigned char)s[k])) { allcaps = 0; break; }
                t = allcaps ? T_CONST : T_TYPE;
            }
            if ((L->flags & LF_RAWSTR) && p < len && s[p] == '!' && t == T_IDENT) t = T_MACRO;

            span_add(out, (int)i, (int)wlen, t);
            i = p;
            continue;
        }

        /* operators and punctuation */
        span_add(out, (int)i, 1, T_OPERATOR);
        i++;
    }
}

/* ---------------------------------------------------------------- Cache */

static struct {
    Buffer         *buf;
    unsigned long  version;
    const Language *lang;
    size_t          built;
    SpanList       *spans;
    HLState         state;
} CACHE;

void hl_cache_invalidate(void)
{
    CACHE.buf = NULL;
    CACHE.built = 0;
}

static void cache_reset(Buffer *b, const Language *L)
{
    for (size_t i = 0; i < CACHE.built; i++) {
        free(CACHE.spans[i].v);
    }
    free(CACHE.spans);
    CACHE.spans = NULL;
    CACHE.built = 0;
    CACHE.buf = b;
    CACHE.version = b->version;
    CACHE.lang = L;
    CACHE.state.mode = M_NORMAL;
    CACHE.state.hashes = 0;
}

const SpanList *hl_spans_for(Buffer *b, const Language *L, size_t line)
{
    if (!CACHE.buf || CACHE.buf != b || CACHE.version != b->version || CACHE.lang != L)
        cache_reset(b, L);

    if (line >= CACHE.built) {
        /* recompute the missing lines */
        size_t need = line + 1;
        SpanList *ns = realloc(CACHE.spans, need * sizeof(SpanList));
        if (!ns) { fputs("rudy: out of memory\n", stderr); exit(1); }
        CACHE.spans = ns;
        for (size_t i = CACHE.built; i < need; i++) {
            CACHE.spans[i].v = NULL;
            CACHE.spans[i].n = CACHE.spans[i].cap = 0;
        }
        for (size_t i = CACHE.built; i < need; i++) {
            const char *s = b->lines[i].data ? b->lines[i].data : "";
            hl_line(L, s, b->lines[i].len, &CACHE.state, &CACHE.spans[i]);
        }
        CACHE.built = need;
    }
    return &CACHE.spans[line];
}
