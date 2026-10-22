/* edit.c — editing operations, undo/redo, clipboard, cursor. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------------------------------------------------- Initialization */

void edit_init(Editor *e)
{
    buf_init(&e->buf);
    chg_init(&e->undo);
    chg_init(&e->redo);
    str_init(&e->prompt_input);
    e->tabsize      = 4;
    e->syntax_on    = 1;
    e->show_numbers = 1;
    e->want_cx      = 0;
    e->top          = 0;
    e->left         = 0;
    e->cur_match_line = -1;

    char *empty = str_dupz("");
    buf_replace(&e->buf, 0, 0, &empty, 1);
}

void edit_free(Editor *e)
{
    buf_free(&e->buf);
    chg_free(&e->undo);
    chg_free(&e->redo);
    for (size_t i = 0; i < e->clip_n; i++) free(e->clip[i]);
    free(e->clip);
    e->clip = NULL;
    e->clip_n = e->clip_cap = 0;
    str_free(&e->prompt_input);
    if (e->re_valid) { regfree(&e->re); e->re_valid = 0; }
}

/* ------------------------------------------------------- Applying a change */

/* Replaces count lines starting at start with nrepl new ones.
 * Ownership of repl[0..nrepl) is taken over by this function (it frees them);
 * the repl array itself stays owned by the caller. */
static void apply_change(Editor *e, size_t start, size_t count, char **repl, size_t nrepl,
                         int new_cy, int new_cx, int kind)
{
    Change c;
    memset(&c, 0, sizeof c);
    c.old_start = start;
    c.old_count = count;
    c.old_cy = e->cy;
    c.old_cx = e->cx;

    c.old_lines = malloc((count ? count : 1) * sizeof(char *));
    for (size_t i = 0; i < count; i++) c.old_lines[i] = buf_line_dup(&e->buf, start + i);

    buf_replace(&e->buf, start, count, repl, nrepl);

    c.new_start = start;
    c.new_count = nrepl;
    c.new_lines = malloc((nrepl ? nrepl : 1) * sizeof(char *));
    for (size_t i = 0; i < nrepl; i++) c.new_lines[i] = buf_line_dup(&e->buf, start + i);
    c.new_cy = new_cy;
    c.new_cx = new_cx;
    c.kind = kind;

    e->cy = new_cy;
    e->cx = new_cx;
    e->modified = 1;
    chg_clear(&e->redo);

    /* Merge consecutive insertions into one entry so undo works word-wise. */
    if (kind == CH_INSERT && e->undo.n > 0) {
        Change *last = &e->undo.items[e->undo.n - 1];
        if (last->kind == CH_INSERT && last->old_start == c.old_start &&
            last->new_start == c.new_start && last->old_count == c.old_count) {
            for (size_t i = 0; i < last->new_count; i++) free(last->new_lines[i]);
            free(last->new_lines);
            last->new_lines = c.new_lines;
            last->new_count = c.new_count;
            last->new_cy = c.new_cy;
            last->new_cx = c.new_cx;
            for (size_t i = 0; i < c.old_count; i++) free(c.old_lines[i]);
            free(c.old_lines);
            for (size_t i = 0; i < nrepl; i++) free(repl[i]);
            return;
        }
    }

    chg_push(&e->undo, &c);
    for (size_t i = 0; i < nrepl; i++) free(repl[i]);
}

static char *line_new(const char *s, size_t len)
{
    char *r = malloc(len + 1);
    if (!r) { fputs("rudy: out of memory\n", stderr); exit(1); }
    if (len) memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

/* -------------------------------------------------------------- Insertion */

void edit_insert_text(Editor *e, const char *s, size_t len)
{
    if (len == 0) return;
    if (e->sel_active) edit_delete_selection(e);

    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;
    if ((size_t)e->cx > llen) e->cx = (int)llen;

    char *nl = malloc(llen + len + 1);
    memcpy(nl, ls, (size_t)e->cx);
    memcpy(nl + e->cx, s, len);
    memcpy(nl + e->cx + len, ls + e->cx, llen - (size_t)e->cx);
    nl[llen + len] = '\0';

    char *repl[1] = { nl };
    apply_change(e, li, 1, repl, 1, e->cy, e->cx + (int)len, CH_INSERT);
}

void edit_insert_char(Editor *e, int cp)
{
    char buf[8];
    int  n = utf8_encode(cp, buf);
    if (n > 0) edit_insert_text(e, buf, (size_t)n);
}

void edit_backspace(Editor *e)
{
    if (e->sel_active) { edit_delete_selection(e); return; }

    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;

    if (e->cx > 0) {
        size_t p = utf8_prev(ls, (size_t)e->cx);
        char  *nl = malloc(llen + 1);
        memcpy(nl, ls, p);
        memcpy(nl + p, ls + e->cx, llen - (size_t)e->cx);
        nl[llen - ((size_t)e->cx - p)] = '\0';
        char *repl[1] = { nl };
        apply_change(e, li, 1, repl, 1, e->cy, (int)p, CH_DELETE);
        return;
    }

    if (li > 0) {
        size_t prev = li - 1;
        const char *ps = e->buf.lines[prev].data;
        size_t      plen = e->buf.lines[prev].len;
        char *nl = malloc(plen + llen + 1);
        memcpy(nl, ps, plen);
        memcpy(nl + plen, ls, llen);
        nl[plen + llen] = '\0';
        char *repl[1] = { nl };
        apply_change(e, prev, 2, repl, 1, (int)prev, (int)plen, CH_DELETE);
    }
}

void edit_delete_char(Editor *e)
{
    if (e->sel_active) { edit_delete_selection(e); return; }

    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;

    if ((size_t)e->cx < llen) {
        size_t p = utf8_next(ls, (size_t)e->cx, llen);
        char  *nl = malloc(llen + 1);
        memcpy(nl, ls, (size_t)e->cx);
        memcpy(nl + e->cx, ls + p, llen - p);
        nl[llen - (p - (size_t)e->cx)] = '\0';
        char *repl[1] = { nl };
        apply_change(e, li, 1, repl, 1, e->cy, e->cx, CH_DELETE);
        return;
    }

    if (li + 1 < e->buf.n) {
        const char *ns = e->buf.lines[li + 1].data;
        size_t      nlen = e->buf.lines[li + 1].len;
        char *nl = malloc(llen + nlen + 1);
        memcpy(nl, ls, llen);
        memcpy(nl + llen, ns, nlen);
        nl[llen + nlen] = '\0';
        char *repl[1] = { nl };
        apply_change(e, li, 2, repl, 1, (int)li, e->cx, CH_DELETE);
    }
}

/* Leading indentation of the line before the cursor (spaces/tabs only). */
static size_t indent_of(const char *s, size_t len, size_t *out_indent)
{
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    if (out_indent) *out_indent = i;
    return i;
}

void edit_newline(Editor *e)
{
    if (e->sel_active) edit_delete_selection(e);

    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;
    if ((size_t)e->cx > llen) e->cx = (int)llen;

    size_t ind = indent_of(ls, llen, NULL);

    /* smart indent: one extra level after an opening bracket */
    int extra = 0;
    if ((size_t)e->cx > 0) {
        char prev = ls[e->cx - 1];
        if (prev == '{' || prev == '(' || prev == '[') extra = 1;
    }

    char *head = line_new(ls, (size_t)e->cx);
    size_t taillen = llen - (size_t)e->cx;
    char  *tail = malloc(ind + (size_t)extra * (size_t)e->tabsize + taillen + 1);
    memcpy(tail, ls, ind);                       /* original indentation */
    size_t tl = ind;
    for (size_t i = 0; i < (size_t)extra * (size_t)e->tabsize; i++) tail[tl++] = ' ';
    memcpy(tail + tl, ls + e->cx, taillen);       /* rest of the line */
    tl += taillen;
    tail[tl] = '\0';

    char *repl[2] = { head, tail };
    apply_change(e, li, 1, repl, 2, (int)li + 1, (int)(ind + (size_t)extra * (size_t)e->tabsize),
                 CH_OTHER);
}

void edit_indent(Editor *e)
{
    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;
    if ((size_t)e->cx > llen) e->cx = (int)llen;

    int col = utf8_off_to_col(ls, llen, (size_t)e->cx, e->tabsize);
    int add = e->tabsize - (col % e->tabsize);

    char *nl = malloc(llen + (size_t)add + 1);
    memcpy(nl, ls, (size_t)e->cx);
    for (int i = 0; i < add; i++) nl[e->cx + i] = ' ';
    memcpy(nl + e->cx + add, ls + e->cx, llen - (size_t)e->cx);
    nl[llen + add] = '\0';

    char *repl[1] = { nl };
    apply_change(e, li, 1, repl, 1, e->cy, e->cx + add, CH_OTHER);
}

void edit_outdent(Editor *e)
{
    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;

    size_t ind = indent_of(ls, llen, NULL);
    if (ind == 0) return;

    size_t remove = 0;
    while (remove < (size_t)e->tabsize && remove < ind) {
        if (ls[remove] == ' ') remove++;
        else if (ls[remove] == '\t') { remove++; break; }
        else break;
    }

    char *nl = malloc(llen + 1);
    memcpy(nl, ls + remove, llen - remove);
    nl[llen - remove] = '\0';

    char *repl[1] = { nl };
    apply_change(e, li, 1, repl, 1, e->cy, e->cx > (int)remove ? e->cx - (int)remove : 0, CH_OTHER);
}

/* Toggle the comment marker of the current line (language specific). */
void edit_toggle_comment(Editor *e)
{
    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;

    const char *mark = e->lang && e->lang->line_comment ? e->lang->line_comment : "//";
    size_t      mlen = strlen(mark);
    size_t      ind = indent_of(ls, llen, NULL);

    int commented = (llen >= ind + mlen && memcmp(ls + ind, mark, mlen) == 0);

    char *nl = malloc(llen + mlen + 2);
    size_t n = 0;
    if (commented) {
        memcpy(nl, ls, ind);
        memcpy(nl + ind, ls + ind + mlen, llen - ind - mlen);
        n = llen - mlen;
    } else {
        memcpy(nl, ls, ind);
        memcpy(nl + ind, mark, mlen);
        memcpy(nl + ind + mlen, ls + ind, llen - ind);
        n = llen + mlen;
    }
    nl[n] = '\0';

    char *repl[1] = { nl };
    apply_change(e, li, 1, repl, 1, e->cy, e->cx + (commented ? 0 : (int)mlen), CH_OTHER);
}

/* -------------------------------------------------------------- Clipboard */

static void clip_set(Editor *e, char **lines, size_t n)
{
    for (size_t i = 0; i < e->clip_n; i++) free(e->clip[i]);
    e->clip_n = 0;
    if (n == 0) return;
    if (n > e->clip_cap) {
        size_t cap = n * 2;
        char **p = realloc(e->clip, cap * sizeof(char *));
        if (!p) { fputs("rudy: out of memory\n", stderr); exit(1); }
        e->clip = p;
        e->clip_cap = cap;
    }
    for (size_t i = 0; i < n; i++) e->clip[e->clip_n++] = lines[i];
}

static void copy_range(Editor *e, size_t from, size_t to)
{
    size_t n = to - from + 1;
    char **lines = malloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) lines[i] = buf_line_dup(&e->buf, from + i);
    clip_set(e, lines, n);
}

void edit_copy_line(Editor *e)
{
    int sly, slx, ely, elx;
    if (sel_range(e, &sly, &slx, &ely, &elx)) {
        copy_range(e, (size_t)sly, (size_t)ely);
        screen_message(e, "Copied", 0);
    } else {
        copy_range(e, (size_t)e->cy, (size_t)e->cy);
        screen_message(e, "Line copied", 0);
    }
}

void edit_cut_line(Editor *e)
{
    int sly, slx, ely, elx;
    if (sel_range(e, &sly, &slx, &ely, &elx)) {
        copy_range(e, (size_t)sly, (size_t)ely);
        edit_delete_selection(e);
        return;
    }
    copy_range(e, (size_t)e->cy, (size_t)e->cy);
    size_t li = (size_t)e->cy;
    char  *repl[1] = { line_new("", 0) };
    apply_change(e, li, 1, repl, 1, (int)li, 0, CH_DELETE);
}

void edit_delete_selection(Editor *e)
{
    int sly, slx, ely, elx;
    if (!sel_range(e, &sly, &slx, &ely, &elx)) return;

    size_t li = (size_t)sly;
    const char *s = e->buf.lines[li].data;
    size_t      slen = e->buf.lines[li].len;
    const char *t = e->buf.lines[ely].data;
    size_t      tlen = e->buf.lines[ely].len;
    if ((size_t)slx > slen) slx = (int)slen;
    if ((size_t)elx > tlen) elx = (int)tlen;

    if (sly == ely) {
        char *nl = malloc(slen + 1);
        memcpy(nl, s, (size_t)slx);
        memcpy(nl + slx, s + elx, slen - (size_t)elx);
        nl[slen - ((size_t)elx - (size_t)slx)] = '\0';
        char *repl[1] = { nl };
        apply_change(e, li, 1, repl, 1, sly, slx, CH_DELETE);
    } else {
        size_t total = slen - (size_t)slx + tlen - (size_t)elx;
        char  *nl = malloc(total + 1);
        memcpy(nl, s + slx, slen - (size_t)slx);
        memcpy(nl + (slen - (size_t)slx), t, tlen - (size_t)elx);
        nl[total] = '\0';
        char *repl[1] = { nl };
        apply_change(e, li, (size_t)(ely - sly + 1), repl, 1, sly, slx, CH_DELETE);
    }
    sel_clear(e);
}

void edit_paste(Editor *e)
{
    if (e->clip_n == 0) { screen_message(e, "Clipboard is empty", 1); return; }
    if (e->sel_active) edit_delete_selection(e);

    size_t li = (size_t)e->cy;
    if (li >= e->buf.n) return;
    const char *ls = e->buf.lines[li].data;
    size_t      llen = e->buf.lines[li].len;
    if ((size_t)e->cx > llen) e->cx = (int)llen;

    size_t headlen = (size_t)e->cx;
    size_t taillen = llen - headlen;
    size_t n = e->clip_n;

    char **repl = malloc((n + 1) * sizeof(char *));
    size_t outn;

    char *first = malloc(headlen + strlen(e->clip[0]) + 1);
    memcpy(first, ls, headlen);
    memcpy(first + headlen, e->clip[0], strlen(e->clip[0]) + 1);
    repl[0] = first;

    if (n == 1) {
        char *last = malloc(taillen + 1);
        memcpy(last, ls + headlen, taillen);
        last[taillen] = '\0';
        repl[1] = last;
        outn = 2;
    } else {
        for (size_t i = 1; i + 1 < n; i++) repl[i] = str_dupz(e->clip[i]);
        char *last = malloc(strlen(e->clip[n - 1]) + taillen + 1);
        memcpy(last, e->clip[n - 1], strlen(e->clip[n - 1]));
        memcpy(last + strlen(e->clip[n - 1]), ls + headlen, taillen);
        last[strlen(e->clip[n - 1]) + taillen] = '\0';
        repl[n - 1] = last;
        outn = n;
    }

    int new_cy = (int)li + (int)outn - 1;
    int new_cx = (int)strlen(repl[outn - 1]);
    apply_change(e, li, 1, repl, outn, new_cy, new_cx, CH_OTHER);
    free(repl);
}

/* ------------------------------------------------------------ Undo/redo */

void edit_undo(Editor *e)
{
    if (e->undo.n == 0) { screen_message(e, "Nothing to undo", 1); return; }
    Change c = e->undo.items[--e->undo.n];

    buf_replace(&e->buf, c.new_start, c.new_count, c.old_lines, c.old_count);
    e->cy = c.old_cy;
    e->cx = c.old_cx;
    e->modified = 1;

    Change r;
    r.old_start = c.new_start; r.old_count = c.new_count; r.old_lines = c.new_lines;
    r.new_start = c.old_start; r.new_count = c.old_count; r.new_lines = c.old_lines;
    r.old_cy = c.new_cy; r.old_cx = c.new_cx;
    r.new_cy = c.old_cy; r.new_cx = c.old_cx;
    r.kind = c.kind;
    chg_push(&e->redo, &r);
    screen_message(e, "Undo", 0);
}

void edit_redo(Editor *e)
{
    if (e->redo.n == 0) { screen_message(e, "Nothing to redo", 1); return; }
    Change c = e->redo.items[--e->redo.n];

    buf_replace(&e->buf, c.new_start, c.new_count, c.old_lines, c.old_count);
    e->cy = c.new_cy;
    e->cx = c.new_cx;
    e->modified = 1;

    Change r;
    r.old_start = c.new_start; r.old_count = c.new_count; r.old_lines = c.new_lines;
    r.new_start = c.old_start; r.new_count = c.old_count; r.new_lines = c.old_lines;
    r.old_cy = c.new_cy; r.old_cx = c.new_cx;
    r.new_cy = c.old_cy; r.new_cx = c.old_cx;
    r.kind = c.kind;
    chg_push(&e->undo, &r);
    screen_message(e, "Redo", 0);
}

void edit_select_all(Editor *e)
{
    e->sel_active = 1;
    e->sel_ay = 0;
    e->sel_ax = 0;
    e->sel_by = (int)e->buf.n - 1;
    e->sel_bx = (int)e->buf.lines[e->buf.n - 1].len;
}

/* ------------------------------------------------------------ Replacement */

/* Literal replacement of every occurrence of a substring. */
int search_replace_all(Editor *e, const char *from, const char *to)
{
    size_t flen = from ? strlen(from) : 0;
    if (!flen) return 0;
    size_t tlen = to ? strlen(to) : 0;

    int    count = 0;
    size_t i = 0;
    while (i < e->buf.n) {
        const char *s = e->buf.lines[i].data ? e->buf.lines[i].data : "";
        size_t      len = e->buf.lines[i].len;

        Str out;
        str_init(&out);
        size_t p = 0;
        int    changed = 0;
        while (p < len) {
            if (p + flen <= len && memcmp(s + p, from, flen) == 0) {
                if (tlen) str_append(&out, to, tlen);
                p += flen;
                changed = 1;
                count++;
            } else {
                str_putc(&out, s[p]);
                p++;
            }
        }
        if (changed) {
            char *nl = str_dupn(out.data ? out.data : "", out.len);
            char *repl[1] = { nl };
            apply_change(e, i, 1, repl, 1, (int)i, e->cx, CH_OTHER);
        }
        str_free(&out);
        i++;
    }
    if ((size_t)e->cy >= e->buf.n) e->cy = (int)e->buf.n - 1;
    return count;
}

/* Global regular expression replacement. */
int search_replace_regex(Editor *e, const char *pat, const char *rep)
{
    if (!search_set_pattern(e, pat, 0)) return 0;

    int    count = 0;
    size_t i = 0;
    while (i < e->buf.n) {
        const char *s = e->buf.lines[i].data ? e->buf.lines[i].data : "";
        size_t      len = e->buf.lines[i].len;

        Str out;
        str_init(&out);
        size_t off = 0;
        int    changed = 0;
        while (off <= len) {
            regmatch_t m;
            int        flags = (off > 0) ? REG_NOTBOL : 0;
            if (regexec(&e->re, s + off, 1, &m, flags) != 0) break;
            size_t so = off + (size_t)m.rm_so;
            size_t eo = off + (size_t)m.rm_eo;
            if (eo == so) {
                if (eo >= len) break;
                size_t nx = utf8_next(s, eo, len);
                str_append(&out, s + off, nx - off);
                off = nx;
                continue;
            }
            str_append(&out, s + off, so - off);
            if (rep) str_puts(&out, rep);
            off = eo;
            changed = 1;
            count++;
        }
        if (changed) {
            str_append(&out, s + off, len - off);
            char *nl = str_dupn(out.data ? out.data : "", out.len);
            char *repl[1] = { nl };
            apply_change(e, i, 1, repl, 1, (int)i, e->cx, CH_OTHER);
        }
        str_free(&out);
        i++;
    }
    if ((size_t)e->cy >= e->buf.n) e->cy = (int)e->buf.n - 1;
    return count;
}

/* ---------------------------------------------------------------- Cursor */

void sel_clear(Editor *e)
{
    e->sel_active = 0;
}

int sel_range(Editor *e, int *sly, int *slx, int *ely, int *elx)
{
    if (!e->sel_active) return 0;
    int ay = e->sel_ay, ax = e->sel_ax, by = e->sel_by, bx = e->sel_bx;
    if (ay > by || (ay == by && ax > bx)) {
        int t;
        t = ay; ay = by; by = t;
        t = ax; ax = bx; bx = t;
    }
    *sly = ay; *slx = ax; *ely = by; *elx = bx;
    return 1;
}

static void sel_head(Editor *e, int extend)
{
    if (extend) {
        if (!e->sel_active) {
            e->sel_active = 1;
            e->sel_ay = e->cy;
            e->sel_ax = e->cx;
        }
        e->sel_by = e->cy;
        e->sel_bx = e->cx;
    } else {
        sel_clear(e);
    }
}

static const char *cur_line(const Editor *e)
{
    if (e->buf.n == 0 || (size_t)e->cy >= e->buf.n) return "";
    return e->buf.lines[e->cy].data ? e->buf.lines[e->cy].data : "";
}

static size_t cur_len(const Editor *e)
{
    if (e->buf.n == 0 || (size_t)e->cy >= e->buf.n) return 0;
    return e->buf.lines[e->cy].len;
}

/* want_cx keeps the screen column (not the byte offset). */
static int want_col(const Editor *e) { return e->want_cx; }

static void set_want_col(Editor *e)
{
    e->want_cx = utf8_off_to_col(cur_line(e), cur_len(e), (size_t)e->cx, e->tabsize);
}

void move_left(Editor *e, int extend)
{
    sel_head(e, extend);
    if (e->cx > 0) {
        e->cx = (int)utf8_prev(cur_line(e), (size_t)e->cx);
    } else if (e->cy > 0) {
        e->cy--;
        e->cx = (int)cur_len(e);
    }
    set_want_col(e);
    view_update(e);
}

void move_right(Editor *e, int extend)
{
    sel_head(e, extend);
    size_t len = cur_len(e);
    if ((size_t)e->cx < len) {
        e->cx = (int)utf8_next(cur_line(e), (size_t)e->cx, len);
    } else if ((size_t)e->cy + 1 < e->buf.n) {
        e->cy++;
        e->cx = 0;
    }
    set_want_col(e);
    view_update(e);
}

void move_up(Editor *e, int extend)
{
    sel_head(e, extend);
    if (e->cy > 0) e->cy--;
    int col = want_col(e);
    e->cx = (int)utf8_col_to_off(cur_line(e), cur_len(e), col, e->tabsize);
    view_update(e);
}

void move_down(Editor *e, int extend)
{
    sel_head(e, extend);
    if ((size_t)e->cy + 1 < e->buf.n) e->cy++;
    int col = want_col(e);
    e->cx = (int)utf8_col_to_off(cur_line(e), cur_len(e), col, e->tabsize);
    view_update(e);
}

void move_home(Editor *e, int extend)
{
    sel_head(e, extend);
    e->cx = 0;
    e->want_cx = 0;
    view_update(e);
}

void move_end(Editor *e, int extend)
{
    sel_head(e, extend);
    e->cx = (int)cur_len(e);
    set_want_col(e);
    view_update(e);
}

void move_word_left(Editor *e, int extend)
{
    sel_head(e, extend);
    const char *s = cur_line(e);
    size_t      p = (size_t)e->cx;
    while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t')) p--;
    while (p > 0) {
        size_t q = utf8_prev(s, p);
        unsigned char c = (unsigned char)s[q];
        if (isalnum(c) || c == '_') p = q;
        else break;
    }
    e->cx = (int)p;
    set_want_col(e);
    view_update(e);
}

void move_word_right(Editor *e, int extend)
{
    sel_head(e, extend);
    const char *s = cur_line(e);
    size_t      len = cur_len(e);
    size_t      p = (size_t)e->cx;
    while (p < len && (s[p] == ' ' || s[p] == '\t')) p++;
    while (p < len) {
        size_t q = utf8_next(s, p, len);
        unsigned char c = (unsigned char)s[p];
        if (isalnum(c) || c == '_') p = q;
        else break;
    }
    e->cx = (int)p;
    set_want_col(e);
    view_update(e);
}

void move_page_up(Editor *e)
{
    int rows = e->rows - 3;
    if (rows < 1) rows = 1;
    e->cy -= rows;
    if (e->cy < 0) e->cy = 0;
    e->top -= rows;
    if (e->top < 0) e->top = 0;
    int col = want_col(e);
    e->cx = (int)utf8_col_to_off(cur_line(e), cur_len(e), col, e->tabsize);
    view_update(e);
}

void move_page_down(Editor *e)
{
    int rows = e->rows - 3;
    if (rows < 1) rows = 1;
    e->cy += rows;
    if ((size_t)e->cy >= e->buf.n) e->cy = (int)e->buf.n - 1;
    if (e->cy < 0) e->cy = 0;
    e->top += rows;
    int col = want_col(e);
    e->cx = (int)utf8_col_to_off(cur_line(e), cur_len(e), col, e->tabsize);
    view_update(e);
}

void move_to_line(Editor *e, int line)
{
    if (line < 1) line = 1;
    if ((size_t)line > e->buf.n) line = (int)e->buf.n;
    e->cy = line - 1;
    e->cx = 0;
    e->want_cx = 0;
    view_update(e);
}

void view_update(Editor *e)
{
    int rows = e->rows - 3;
    if (rows < 1) rows = 1;
    int gutter = gutter_width(e);
    int cols = e->cols - gutter;
    if (cols < 1) cols = 1;

    if (e->cy < e->top) e->top = e->cy;
    if (e->cy >= e->top + rows) e->top = e->cy - rows + 1;
    if (e->top < 0) e->top = 0;

    int col = utf8_off_to_col(cur_line(e), cur_len(e), (size_t)e->cx, e->tabsize);
    if (col < e->left) e->left = col;
    if (col >= e->left + cols) e->left = col - cols + 1;
    if (e->left < 0) e->left = 0;
}
