/* files.c — file reading/writing, autosave. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char *read_whole(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 65536, len = 0;
    char  *buf = malloc(cap);
    if (!buf) { fclose(f); return NULL; }
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            char *p = realloc(buf, cap);
            if (!p) { free(buf); fclose(f); return NULL; }
            buf = p;
        }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
    }
    fclose(f);
    buf[len < cap ? len : cap - 1] = '\0';
    *out_len = len;
    return buf;
}

int file_load(Editor *e, const char *path)
{
    size_t len = 0;
    char  *data = read_whole(path, &len);
    if (!data) return 0;

    size_t   cap = 256, n = 0;
    char   **lines = malloc(cap * sizeof(char *));
    if (!lines) { free(data); return 0; }

    size_t start = 0;
    size_t i = 0;
    int    crlf = 0;
    while (i <= len) {
        if (i == len) {
            if (i > start) {
                size_t l = i - start;
                if (l && data[start + l - 1] == '\r') { l--; crlf = 1; }
                if (n == cap) { cap *= 2; lines = realloc(lines, cap * sizeof(char *)); }
                lines[n++] = str_dupn(data + start, l);
            }
            break;
        }
        if (data[i] == '\n') {
            size_t l = i - start;
            if (l && data[start + l - 1] == '\r') { l--; crlf = 1; }
            if (n == cap) { cap *= 2; lines = realloc(lines, cap * sizeof(char *)); }
            lines[n++] = str_dupn(data + start, l);
            start = i + 1;
        }
        i++;
    }
    free(data);

    if (n == 0) {
        if (cap < 1) { cap = 1; lines = realloc(lines, cap * sizeof(char *)); }
        lines[n++] = str_dupz("");
    }

    buf_clear(&e->buf);
    buf_replace(&e->buf, 0, 0, lines, n);
    free(lines);

    e->crlf = crlf;
    snprintf(e->path, sizeof e->path, "%s", path);
    e->has_path = 1;
    e->modified = 0;
    e->cy = e->cx = 0;
    e->top = e->left = 0;
    e->want_cx = 0;
    chg_clear(&e->undo);
    chg_clear(&e->redo);
    return 1;
}

int file_save(Editor *e, const char *path)
{
    if (!path || !*path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    const char *nl = e->crlf ? "\r\n" : "\n";
    int         empty = (e->buf.n == 1 && e->buf.lines[0].len == 0);

    for (size_t i = 0; i < e->buf.n; i++) {
        if (i) fwrite(nl, 1, strlen(nl), f);
        if (e->buf.lines[i].len) fwrite(e->buf.lines[i].data, 1, e->buf.lines[i].len, f);
    }
    if (!empty) fwrite(nl, 1, strlen(nl), f);
    fclose(f);

    if (path == e->path) {
        e->modified = 0;
    } else {
        snprintf(e->path, sizeof e->path, "%s", path);
        e->has_path = 1;
        e->modified = 0;
    }
    return 1;
}

/* ------------------------------------------------------------- Autosave */

static void cache_dir(char *out, size_t n)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) {
        snprintf(out, n, "%s/rudy", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(out, n, "%s/.cache/rudy", home);
        return;
    }
    snprintf(out, n, "/tmp/rudy");
}

static void autosave_names(const char *path, char *data, size_t dn, char *meta, size_t mn)
{
    unsigned long h = 1469598103934665603UL;
    for (const char *p = path; p && *p; p++) {
        h ^= (unsigned char)*p;
        h *= 1099511628211UL;
    }
    char dir[512];
    cache_dir(dir, sizeof dir);
    mkdir(dir, 0700);
    snprintf(data, dn, "%s/autosave.%08lx", dir, h & 0xffffffffUL);
    snprintf(meta, mn, "%s/autosave.%08lx.path", dir, h & 0xffffffffUL);
}

void autosave(Editor *e)
{
    if (!e->modified || !e->has_path) return;
    char d[PATH_MAX_LEN], m[PATH_MAX_LEN];
    autosave_names(e->path, d, sizeof d, m, sizeof m);

    FILE *f = fopen(d, "wb");
    if (!f) return;
    for (size_t i = 0; i < e->buf.n; i++) {
        if (i) fputc('\n', f);
        if (e->buf.lines[i].len) fwrite(e->buf.lines[i].data, 1, e->buf.lines[i].len, f);
    }
    fclose(f);

    f = fopen(m, "wb");
    if (f) {
        fprintf(f, "%s\n", e->path);
        fclose(f);
    }
}

int autosave_recover(Editor *e)
{
    if (!e->has_path) return 0;
    char d[PATH_MAX_LEN], m[PATH_MAX_LEN];
    autosave_names(e->path, d, sizeof d, m, sizeof m);

    FILE *f = fopen(d, "rb");
    if (!f) return 0;
    fclose(f);

    if (!file_load(e, d)) return 0;
    e->modified = 1;
    return 1;
}
