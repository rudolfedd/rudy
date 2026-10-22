/* search.c — regular expression search. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>

int search_set_pattern(Editor *e, const char *pat, int icase)
{
    if (e->re_valid) { regfree(&e->re); e->re_valid = 0; }
    e->has_pattern = 0;
    e->cur_match_line = -1;
    e->pattern[0] = '\0';
    if (!pat || !*pat) return 0;

    int flags = REG_EXTENDED | REG_NEWLINE;
    if (icase) flags |= REG_ICASE;

    int rc = regcomp(&e->re, pat, flags);
    if (rc != 0) {
        char buf[256];
        regerror(rc, &e->re, buf, sizeof buf);
        screen_message(e, buf, 1);
        return 0;
    }
    e->re_valid = 1;
    e->has_pattern = 1;
    e->re_icase = icase;
    snprintf(e->pattern, sizeof e->pattern, "%s", pat);
    return 1;
}

void search_clear(Editor *e)
{
    if (e->re_valid) { regfree(&e->re); e->re_valid = 0; }
    e->has_pattern = 0;
    e->cur_match_line = -1;
    e->pattern[0] = '\0';
}

int search_line_matches(Editor *e, size_t line, Match *out, int max)
{
    if (!e->has_pattern || !e->re_valid || max <= 0) return 0;
    if (line >= e->buf.n) return 0;

    const char *s = e->buf.lines[line].data ? e->buf.lines[line].data : "";
    size_t      len = e->buf.lines[line].len;

    int    n = 0;
    size_t off = 0;
    while (off <= len && n < max) {
        regmatch_t m;
        int        flags = (off > 0) ? REG_NOTBOL : 0;
        if (regexec(&e->re, s + off, 1, &m, flags) != 0) break;
        size_t so = off + (size_t)m.rm_so;
        size_t eo = off + (size_t)m.rm_eo;
        if (eo == so) {
            if (eo >= len) break;
            off = utf8_next(s, eo, len);
            continue;
        }
        out[n].so = so;
        out[n].eo = eo;
        n++;
        off = eo;
    }
    return n;
}

int search_next(Editor *e, int dir)
{
    if (!e->has_pattern || !e->re_valid) {
        screen_message(e, "No search pattern (^W)", 1);
        return 0;
    }
    size_t nlines = e->buf.n;
    if (nlines == 0) return 0;

    int    start = e->cy;
    size_t startoff = (size_t)e->cx;

    for (size_t step = 0; step <= nlines; step++) {
        int line;
        if (dir > 0)
            line = (int)(((size_t)start + step) % nlines);
        else
            line = (int)(((size_t)start + nlines * 2 - step) % nlines);

        Match ms[256];
        int   nm = search_line_matches(e, (size_t)line, ms, 256);

        if (dir > 0) {
            for (int i = 0; i < nm; i++) {
                if (step == 0 && ms[i].so < startoff) continue;
                e->cy = line;
                e->cx = (int)ms[i].so;
                e->cur_match_line = line;
                e->cur_match_so = ms[i].so;
                e->cur_match_eo = ms[i].eo;
                view_update(e);
                return 1;
            }
        } else {
            for (int i = nm - 1; i >= 0; i--) {
                if (step == 0 && ms[i].so >= startoff) continue;
                e->cy = line;
                e->cx = (int)ms[i].so;
                e->cur_match_line = line;
                e->cur_match_so = ms[i].so;
                e->cur_match_eo = ms[i].eo;
                view_update(e);
                return 1;
            }
        }
    }
    screen_message(e, "No matches", 1);
    e->cur_match_line = -1;
    return 0;
}
