/* buffer.c — text buffer (array of lines) and undo stack. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>

static void line_set(Line *l, const char *s, size_t len)
{
    if (l->cap < len + 1) {
        size_t cap = len + 64;
        char  *p = realloc(l->data, cap);
        if (!p) { fputs("rudy: out of memory\n", stderr); exit(1); }
        l->data = p;
        l->cap  = cap;
    }
    if (len) memcpy(l->data, s, len);
    l->data[len] = '\0';
    l->len = len;
}

void buf_init(Buffer *b)
{
    b->lines = NULL;
    b->n = b->cap = 0;
    b->version = 1;
}

void buf_free(Buffer *b)
{
    for (size_t i = 0; i < b->n; i++) free(b->lines[i].data);
    free(b->lines);
    b->lines = NULL;
    b->n = b->cap = 0;
}

void buf_clear(Buffer *b)
{
    for (size_t i = 0; i < b->n; i++) free(b->lines[i].data);
    b->n = 0;
    b->version++;
}

char *buf_line_dup(const Buffer *b, size_t idx)
{
    if (idx >= b->n) return str_dupz("");
    return str_dupn(b->lines[idx].data, b->lines[idx].len);
}

void buf_set_line(Buffer *b, size_t idx, const char *s, size_t len)
{
    if (idx >= b->n) return;
    line_set(&b->lines[idx], s, len);
    b->version++;
}

/* Replaces count lines starting at start with nrepl new lines.
 * repl[] — malloc'ed strings, they are copied into the buffer; ownership stays with the caller. */
void buf_replace(Buffer *b, size_t start, size_t count, char **repl, size_t nrepl)
{
    if (start > b->n) start = b->n;
    if (start + count > b->n) count = b->n - start;

    size_t tail  = b->n - start - count;
    size_t newn  = start + nrepl + tail;

    Line  *nl = malloc((newn ? newn : 1) * sizeof(Line));
    if (!nl) { fputs("rudy: out of memory\n", stderr); exit(1); }

    for (size_t i = 0; i < start; i++) nl[i] = b->lines[i];
    for (size_t i = 0; i < nrepl; i++) {
        const char *s = repl[i] ? repl[i] : "";
        size_t      l = strlen(s);
        Line       *ln = &nl[start + i];
        ln->data = malloc(l + 1);
        if (!ln->data) { fputs("rudy: out of memory\n", stderr); exit(1); }
        memcpy(ln->data, s, l + 1);
        ln->len = l;
        ln->cap = l + 1;
    }
    for (size_t i = 0; i < tail; i++) nl[start + nrepl + i] = b->lines[start + count + i];
    for (size_t i = 0; i < count; i++) free(b->lines[start + i].data);

    free(b->lines);
    b->lines = nl;
    b->n = newn;
    b->cap = newn;
    b->version++;
}

/* --------------------------------------------------------------- Undo */

void chg_init(ChangeStack *cs) { cs->items = NULL; cs->n = cs->cap = 0; }

void chg_clear(ChangeStack *cs)
{
    for (size_t i = 0; i < cs->n; i++) {
        for (size_t k = 0; k < cs->items[i].old_count; k++) free(cs->items[i].old_lines[k]);
        for (size_t k = 0; k < cs->items[i].new_count; k++) free(cs->items[i].new_lines[k]);
        free(cs->items[i].old_lines);
        free(cs->items[i].new_lines);
    }
    free(cs->items);
    cs->items = NULL;
    cs->n = cs->cap = 0;
}

void chg_free(ChangeStack *cs) { chg_clear(cs); }

void chg_push(ChangeStack *cs, Change *c)
{
    if (cs->n == cs->cap) {
        size_t cap = cs->cap ? cs->cap * 2 : 32;
        Change *p = realloc(cs->items, cap * sizeof(Change));
        if (!p) { fputs("rudy: out of memory\n", stderr); exit(1); }
        cs->items = p;
        cs->cap = cap;
    }
    cs->items[cs->n++] = *c;
}
