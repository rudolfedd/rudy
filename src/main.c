/* main.c — editor loop, key handling, commands. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

/* ----------------------------------------------------------------- Keys */

enum {
    K_NONE = 0, K_CHAR, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END,
    K_PGUP, K_PGDN, K_DELETE, K_INSERT, K_TAB, K_BACKSPACE, K_ENTER,
    K_ESC, K_FN, K_CTRL, K_ALT, K_SHTAB, K_UNKNOWN,
    K_C_UP, K_C_DOWN, K_C_LEFT, K_C_RIGHT,
    K_A_UP, K_A_DOWN, K_A_LEFT, K_A_RIGHT
};

#define MOD_SHIFT 1
#define MOD_ALT   2
#define MOD_CTRL  4

typedef struct { int type; int ch; int mod; } Key;

static volatile sig_atomic_t g_resized = 0;

static void on_winch(int sig) { (void)sig; g_resized = 1; }

static int read_byte(unsigned char *c)
{
    ssize_t n = read(STDIN_FILENO, c, 1);
    return n == 1;
}

static Key parse_csi(const char *s, int n)
{
    int params[4] = { 0, 0, 0, 0 };
    int np = 0;
    int i = 1;
    while (i < n && np < 4) {
        if (s[i] >= '0' && s[i] <= '9') {
            int v = 0;
            while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
            params[np++] = v;
        } else if (s[i] == ';') {
            i++;
            if (np < 4) params[np++] = 0;
        } else break;
    }
    if (i >= n) return (Key){ K_UNKNOWN, 0, 0 };
    char final = s[i];

    int mod = 0;
    if (np >= 2 && params[1] > 0) {
        int m = params[1] - 1;
        if (m & 1) mod |= MOD_SHIFT;
        if (m & 2) mod |= MOD_ALT;
        if (m & 4) mod |= MOD_CTRL;
    }
    int p1 = np >= 1 ? params[0] : 0;

    switch (final) {
    case 'A':
        if (mod & MOD_CTRL) return (Key){ K_C_UP, 0, mod };
        if (mod & MOD_ALT) return (Key){ K_A_UP, 0, mod };
        return (Key){ K_UP, 0, mod };
    case 'B':
        if (mod & MOD_CTRL) return (Key){ K_C_DOWN, 0, mod };
        if (mod & MOD_ALT) return (Key){ K_A_DOWN, 0, mod };
        return (Key){ K_DOWN, 0, mod };
    case 'C':
        if (mod & MOD_CTRL) return (Key){ K_C_RIGHT, 0, mod };
        if (mod & MOD_ALT) return (Key){ K_A_RIGHT, 0, mod };
        return (Key){ K_RIGHT, 0, mod };
    case 'D':
        if (mod & MOD_CTRL) return (Key){ K_C_LEFT, 0, mod };
        if (mod & MOD_ALT) return (Key){ K_A_LEFT, 0, mod };
        return (Key){ K_LEFT, 0, mod };
    case 'H': return (Key){ K_HOME, 0, mod };
    case 'F': return (Key){ K_END, 0, mod };
    case 'Z': return (Key){ K_SHTAB, 0, 0 };
    case '~':
        switch (p1) {
        case 1: case 7: return (Key){ K_HOME, 0, mod };
        case 2: return (Key){ K_INSERT, 0, mod };
        case 3: return (Key){ K_DELETE, 0, mod };
        case 4: case 8: return (Key){ K_END, 0, mod };
        case 5: return (Key){ K_PGUP, 0, mod };
        case 6: return (Key){ K_PGDN, 0, mod };
        case 11: return (Key){ K_FN, 1, 0 };
        case 12: return (Key){ K_FN, 2, 0 };
        case 13: return (Key){ K_FN, 3, 0 };
        case 14: return (Key){ K_FN, 4, 0 };
        case 15: return (Key){ K_FN, 5, 0 };
        case 17: return (Key){ K_FN, 6, 0 };
        case 18: return (Key){ K_FN, 7, 0 };
        case 19: return (Key){ K_FN, 8, 0 };
        case 20: return (Key){ K_FN, 9, 0 };
        case 21: return (Key){ K_FN, 10, 0 };
        case 23: return (Key){ K_FN, 11, 0 };
        case 24: return (Key){ K_FN, 12, 0 };
        default: return (Key){ K_UNKNOWN, 0, 0 };
        }
    default:
        if ((mod & MOD_ALT) &&
            ((final >= 'a' && final <= 'z') || (final >= 'A' && final <= 'Z')))
            return (Key){ K_ALT, (mod & MOD_SHIFT) ? final : (int)tolower(final), mod };
        return (Key){ K_UNKNOWN, 0, 0 };
    }
}

static Key read_key(void)
{
    unsigned char c;
    if (!read_byte(&c)) return (Key){ K_NONE, 0, 0 };

    if (c == 0x1b) {
        struct pollfd p;
        p.fd = STDIN_FILENO;
        p.events = POLLIN;
        p.revents = 0;
        if (poll(&p, 1, 50) <= 0) return (Key){ K_ESC, 0, 0 };

        unsigned char b;
        if (!read_byte(&b)) return (Key){ K_ESC, 0, 0 };
        if (b == '[') {
            char seq[40];
            int  n = 0;
            seq[n++] = '[';
            for (;;) {
                if (!read_byte(&b)) break;
                if (n < 30) seq[n++] = (char)b;
                if (b >= 0x40 && b <= 0x7E) break;
            }
            return parse_csi(seq, n);
        }
        if (b == 'O') {
            if (!read_byte(&b)) return (Key){ K_ESC, 0, 0 };
            switch (b) {
            case 'A': return (Key){ K_UP, 0, 0 };
            case 'B': return (Key){ K_DOWN, 0, 0 };
            case 'C': return (Key){ K_RIGHT, 0, 0 };
            case 'D': return (Key){ K_LEFT, 0, 0 };
            case 'H': return (Key){ K_HOME, 0, 0 };
            case 'F': return (Key){ K_END, 0, 0 };
            case 'P': return (Key){ K_FN, 1, 0 };
            case 'Q': return (Key){ K_FN, 2, 0 };
            case 'R': return (Key){ K_FN, 3, 0 };
            case 'S': return (Key){ K_FN, 4, 0 };
            }
            return (Key){ K_UNKNOWN, 0, 0 };
        }
        if (b == 0x1b) return (Key){ K_ESC, 0, 0 };
        if (b < 0x20) return (Key){ K_ALT, b + 'A' - 1, 0 };
        return (Key){ K_ALT, b, 0 };
    }

    if (c == 0x0d || c == 0x0a) return (Key){ K_ENTER, 0, 0 };
    if (c == 0x09) return (Key){ K_TAB, 0, 0 };
    if (c == 0x7f || c == 0x08) return (Key){ K_BACKSPACE, 0, 0 };
    if (c < 0x20) return (Key){ K_CTRL, c + 'A' - 1, 0 };
    return (Key){ K_CHAR, c, 0 };
}

/* -------------------------------------------------------------- Prompts */

enum { PROMPT_TEXT = 0, PROMPT_CONFIRM = 1 };

static void prompt_start(Editor *e, const char *label, int type,
                         void (*cb)(Editor *e, int ok, const char *text))
{
    e->prompt_active = 1;
    e->prompt_type = type;
    snprintf(e->prompt_label, sizeof e->prompt_label, "%s", label);
    str_clear(&e->prompt_input);
    e->prompt_cursor = 0;
    e->prompt_cb = cb;
    e->message[0] = '\0';
}

static void prompt_finish(Editor *e, int ok)
{
    char *text = NULL;
    if (ok) text = str_dupz(e->prompt_input.data ? e->prompt_input.data : "");
    void (*cb)(Editor *, int, const char *) = e->prompt_cb;
    e->prompt_active = 0;
    e->prompt_cb = NULL;
    if (cb) cb(e, ok, text);
    free(text);
}

static void prompt_insert(Editor *e, const char *s, size_t n)
{
    size_t cur = (size_t)e->prompt_cursor;
    if (cur > e->prompt_input.len) cur = e->prompt_input.len;
    str_reserve(&e->prompt_input, n);
    memmove(e->prompt_input.data + cur + n, e->prompt_input.data + cur,
            e->prompt_input.len - cur);
    memcpy(e->prompt_input.data + cur, s, n);
    e->prompt_input.len += n;
    e->prompt_input.data[e->prompt_input.len] = '\0';
    e->prompt_cursor = (int)(cur + n);
}

static void prompt_del(Editor *e, int back)
{
    size_t cur = (size_t)e->prompt_cursor;
    if (cur > e->prompt_input.len) cur = e->prompt_input.len;
    if (back) {
        if (cur == 0) return;
        size_t p = utf8_prev(e->prompt_input.data, cur);
        memmove(e->prompt_input.data + p, e->prompt_input.data + cur,
                e->prompt_input.len - cur);
        e->prompt_input.len -= (cur - p);
        e->prompt_input.data[e->prompt_input.len] = '\0';
        e->prompt_cursor = (int)p;
    } else {
        if (cur >= e->prompt_input.len) return;
        size_t n = utf8_next(e->prompt_input.data, cur, e->prompt_input.len);
        memmove(e->prompt_input.data + cur, e->prompt_input.data + n,
                e->prompt_input.len - n);
        e->prompt_input.len -= (n - cur);
        e->prompt_input.data[e->prompt_input.len] = '\0';
    }
}

static void prompt_key(Editor *e, Key k)
{
    switch (k.type) {
    case K_ENTER: prompt_finish(e, 1); return;
    case K_ESC:   prompt_finish(e, 0); return;
    case K_BACKSPACE: prompt_del(e, 1); return;
    case K_DELETE: prompt_del(e, 0); return;
    case K_LEFT:
        if (e->prompt_cursor > 0)
            e->prompt_cursor = (int)utf8_prev(e->prompt_input.data, (size_t)e->prompt_cursor);
        return;
    case K_RIGHT:
        if ((size_t)e->prompt_cursor < e->prompt_input.len)
            e->prompt_cursor = (int)utf8_next(e->prompt_input.data, (size_t)e->prompt_cursor,
                                              e->prompt_input.len);
        return;
    case K_HOME: e->prompt_cursor = 0; return;
    case K_END:  e->prompt_cursor = (int)e->prompt_input.len; return;
    case K_CTRL:
        if (k.ch == 'U') { str_clear(&e->prompt_input); e->prompt_cursor = 0; }
        else if (k.ch == 'W') {
            int c = e->prompt_cursor;
            while (c > 0 && e->prompt_input.data[c - 1] == ' ') c--;
            while (c > 0 && e->prompt_input.data[c - 1] != ' ') c--;
            e->prompt_cursor = c;
            prompt_del(e, 0);
        } else if (k.ch == 'A') e->prompt_cursor = 0;
        else if (k.ch == 'E') e->prompt_cursor = (int)e->prompt_input.len;
        return;
    case K_CHAR: {
        char b[8];
        int  n = utf8_encode(k.ch, b);
        prompt_insert(e, b, (size_t)n);
        return;
    }
    default:
        return;
    }
}

/* -------------------------------------------------------------- Commands */

enum { PEND_NONE = 0, PEND_OPEN, PEND_QUIT, PEND_SAVEAS };
static int  g_pending = PEND_NONE;
static char g_pending_path[PATH_MAX_LEN];

static void do_open(Editor *e, const char *path);
static void do_save_as(Editor *e, const char *path);
static void cmd_save(Editor *e);

static void confirm_cb(Editor *e, int ok, const char *text)
{
    int pend = g_pending;
    g_pending = PEND_NONE;
    if (!ok) return;

    int yes = 0, no = 0;
    if (text && text[0]) {
        if (strchr("yYdD", text[0])) yes = 1;
        else if (strchr("nN", text[0])) no = 1;
    }
    if (!yes && !no) return;            /* empty answer — cancel */

    if (yes && e->modified && e->has_path) {
        if (!file_save(e, e->path)) {
            screen_message(e, "Save error", 1);
            return;
        }
    }

    switch (pend) {
    case PEND_OPEN:   do_open(e, g_pending_path); break;
    case PEND_SAVEAS: do_save_as(e, g_pending_path); break;
    case PEND_QUIT:
        if (yes) e->quit_request = 1;
        else if (no) e->quit_request = 1;
        break;
    default: break;
    }
}

static void cb_open(Editor *e, int ok, const char *text)
{
    if (!ok || !text || !*text) return;
    snprintf(g_pending_path, sizeof g_pending_path, "%s", text);
    if (e->modified) {
        g_pending = PEND_OPEN;
        prompt_start(e, "Save changes? (y/n) ", PROMPT_CONFIRM, confirm_cb);
        return;
    }
    do_open(e, text);
}

static void do_open(Editor *e, const char *path)
{
    if (!file_load(e, path)) {
        screen_message(e, "Cannot open file", 1);
        return;
    }
    e->lang = lang_by_path(e->path);
    snprintf(e->lang_name, sizeof e->lang_name, "%s", e->lang ? e->lang->name : "Text");
    screen_message(e, "File opened", 0);
}

static void cmd_open(Editor *e)
{
    prompt_start(e, "Open file: ", PROMPT_TEXT, cb_open);
}

static void cb_saveas(Editor *e, int ok, const char *text)
{
    if (!ok || !text || !*text) return;
    do_save_as(e, text);
}

static void do_save_as(Editor *e, const char *path)
{
    if (!file_save(e, path)) {
        screen_message(e, "Save error", 1);
        return;
    }
    e->lang = lang_by_path(e->path);
    snprintf(e->lang_name, sizeof e->lang_name, "%s", e->lang ? e->lang->name : "Text");
    screen_message(e, "Saved", 0);
}

static void cmd_save(Editor *e)
{
    if (!e->has_path) {
        g_pending = PEND_SAVEAS;
        prompt_start(e, "Save as: ", PROMPT_TEXT, cb_saveas);
        return;
    }
    if (file_save(e, e->path)) screen_message(e, "Saved", 0);
    else screen_message(e, "Save error", 1);
}

static void cmd_quit(Editor *e)
{
    if (e->modified) {
        g_pending = PEND_QUIT;
        prompt_start(e, "Save changes? (y/n) ", PROMPT_CONFIRM, confirm_cb);
        return;
    }
    e->quit_request = 1;
}

static void cb_find(Editor *e, int ok, const char *text)
{
    if (!ok || !text) return;
    if (!search_set_pattern(e, text, 0)) return;
    if (!search_next(e, +1)) screen_message(e, "No matches", 1);
}

static void cmd_find(Editor *e)
{
    prompt_start(e, "Search (regexp): ", PROMPT_TEXT, cb_find);
}

/* Replace: search -> replacement -> replace all */
static char g_find_buf[512];
static char g_repl_buf[512];

static void cb_repl2(Editor *e, int ok, const char *text)
{
    if (!ok) return;
    snprintf(g_repl_buf, sizeof g_repl_buf, "%s", text ? text : "");
    int  n = search_replace_all(e, g_find_buf, g_repl_buf);
    char msg[128];
    snprintf(msg, sizeof msg, "Replaced: %d", n);
    screen_message(e, msg, 0);
}

static void cb_repl1(Editor *e, int ok, const char *text)
{
    if (!ok || !text) return;
    snprintf(g_find_buf, sizeof g_find_buf, "%s", text);
    prompt_start(e, "Replace with: ", PROMPT_TEXT, cb_repl2);
}

static void cmd_replace(Editor *e)
{
    prompt_start(e, "Find: ", PROMPT_TEXT, cb_repl1);
}

static void cb_goto(Editor *e, int ok, const char *text)
{
    if (!ok || !text) return;
    int n = atoi(text);
    if (n <= 0) return;
    move_to_line(e, n);
}

static void cmd_goto(Editor *e)
{
    prompt_start(e, "Go to line: ", PROMPT_TEXT, cb_goto);
}

/* --------------------------------------------------------- Command line */

static void run_command(Editor *e, const char *cmd);

static void cb_cmd(Editor *e, int ok, const char *text)
{
    if (!ok || !text) return;
    run_command(e, text);
}

static void cmd_line(Editor *e)
{
    prompt_start(e, ":", PROMPT_TEXT, cb_cmd);
}

static void run_command(Editor *e, const char *cmd)
{
    while (*cmd == ' ' || *cmd == ':') cmd++;
    if (!*cmd) return;

    if (!strncmp(cmd, "w ", 2)) { do_save_as(e, cmd + 2); return; }
    if (!strcmp(cmd, "w") || !strcmp(cmd, "write")) { cmd_save(e); return; }
    if (!strcmp(cmd, "q!") || !strcmp(cmd, "quit!")) {
        e->modified = 0;
        e->quit_request = 1;
        return;
    }
    if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) { cmd_quit(e); return; }
    if (!strcmp(cmd, "wq") || !strcmp(cmd, "x")) {
        if (e->has_path && file_save(e, e->path)) e->quit_request = 1;
        else cmd_save(e);
        return;
    }
    if (!strncmp(cmd, "o ", 2)) { do_open(e, cmd + 2); return; }
    if (!strncmp(cmd, "e ", 2)) { do_open(e, cmd + 2); return; }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "h")) { e->help_active = 1; e->help_scroll = 0; return; }
    if (!strcmp(cmd, "recover")) {
        if (autosave_recover(e)) screen_message(e, "Recovered from autosave", 0);
        else screen_message(e, "No autosave found", 1);
        return;
    }
    if (!strncmp(cmd, "set ", 4)) {
        const char *opt = cmd + 4;
        if (!strncmp(opt, "syntax=", 7)) {
            const char *v = opt + 7;
            if (!strcmp(v, "off") || !strcmp(v, "none")) {
                e->syntax_on = 0;
                screen_message(e, "Syntax highlighting off", 0);
            } else {
                const Language *L = lang_by_name(v);
                if (L) {
                    e->lang = L;
                    e->syntax_on = 1;
                    snprintf(e->lang_name, sizeof e->lang_name, "%s", L->name);
                    char msg[64];
                    snprintf(msg, sizeof msg, "Language: %s", L->name);
                    screen_message(e, msg, 0);
                } else {
                    screen_message(e, "Unknown language", 1);
                }
            }
        } else if (!strncmp(opt, "tabsize=", 8)) {
            e->tabsize = atoi(opt + 8);
            if (e->tabsize < 1) e->tabsize = 1;
            if (e->tabsize > 16) e->tabsize = 16;
            char msg[64];
            snprintf(msg, sizeof msg, "Tab size: %d", e->tabsize);
            screen_message(e, msg, 0);
        } else if (!strcmp(opt, "numbers")) {
            e->show_numbers = 1;
        } else if (!strcmp(opt, "nonumber")) {
            e->show_numbers = 0;
        } else {
            screen_message(e, "Unknown option", 1);
        }
        return;
    }
    if (cmd[0] == 's' && cmd[1] == '/') {
        const char *p = cmd + 2;
        const char *from = p;
        while (*p && *p != '/') p++;
        if (*p != '/') { screen_message(e, "Usage: :s/pattern/replacement/g", 1); return; }
        char pat[256];
        size_t fl = (size_t)(p - from);
        if (fl >= sizeof pat) fl = sizeof pat - 1;
        memcpy(pat, from, fl);
        pat[fl] = '\0';
        const char *rep = ++p;
        while (*p && *p != '/') p++;
        char rep_buf[256];
        size_t rl = (size_t)(p - rep);
        if (rl >= sizeof rep_buf) rl = sizeof rep_buf - 1;
        memcpy(rep_buf, rep, rl);
        rep_buf[rl] = '\0';
        int global = (*p == '/' && p[1] == 'g');
        int n = 0;
        if (global) {
            n = search_replace_regex(e, pat, rep_buf);
        } else if (search_set_pattern(e, pat, 0)) {
            if (search_next(e, +1)) n = 1;
        }
        char msg[128];
        snprintf(msg, sizeof msg, "Replaced: %d", n);
        screen_message(e, msg, 0);
        return;
    }
    screen_message(e, "Unknown command", 1);
}

/* --------------------------------------------------------- Key handling */

static void handle_key(Editor *e, Key k)
{
    if (e->help_active) {
        if (k.type == K_UP)   { if (e->help_scroll > 0) e->help_scroll--; return; }
        if (k.type == K_DOWN) { e->help_scroll++; return; }
        if (k.type == K_PGUP) { e->help_scroll -= 10; if (e->help_scroll < 0) e->help_scroll = 0; return; }
        if (k.type == K_PGDN) { e->help_scroll += 10; return; }
        e->help_active = 0;
        return;
    }

    switch (k.type) {
    case K_UP:    move_up(e, k.mod & MOD_SHIFT); return;
    case K_DOWN:  move_down(e, k.mod & MOD_SHIFT); return;
    case K_LEFT:  move_left(e, k.mod & MOD_SHIFT); return;
    case K_RIGHT: move_right(e, k.mod & MOD_SHIFT); return;
    case K_C_UP:    move_up(e, 0); return;
    case K_C_DOWN:  move_down(e, 0); return;
    case K_C_LEFT:  move_word_left(e, 0); return;
    case K_C_RIGHT: move_word_right(e, 0); return;
    case K_A_UP:    move_up(e, 0); return;
    case K_A_DOWN:  move_down(e, 0); return;
    case K_A_LEFT:  move_word_left(e, 0); return;
    case K_A_RIGHT: move_word_right(e, 0); return;
    case K_HOME:
        if (k.mod & MOD_CTRL) { move_to_line(e, 1); return; }
        move_home(e, k.mod & MOD_SHIFT);
        return;
    case K_END:
        if (k.mod & MOD_CTRL) { move_to_line(e, (int)e->buf.n); return; }
        move_end(e, k.mod & MOD_SHIFT);
        return;
    case K_PGUP:  move_page_up(e); return;
    case K_PGDN:  move_page_down(e); return;
    case K_TAB:   edit_indent(e); return;
    case K_SHTAB: edit_outdent(e); return;
    case K_ENTER: edit_newline(e); return;
    case K_BACKSPACE: edit_backspace(e); return;
    case K_DELETE: edit_delete_char(e); return;
    case K_INSERT: return;
    case K_ESC:   return;

    case K_FN:
        switch (k.ch) {
        case 1: e->help_active = 1; e->help_scroll = 0; return;
        case 2: cmd_open(e); return;
        case 3: cmd_save(e); return;
        case 4: cmd_find(e); return;
        case 5: cmd_goto(e); return;
        case 6: edit_copy_line(e); return;
        case 7: edit_cut_line(e); return;
        case 8: edit_paste(e); return;
        case 9: case 10: cmd_quit(e); return;
        default: return;
        }

    case K_CTRL:
        switch (k.ch) {
        case 'O': cmd_open(e); return;
        case 'S': cmd_save(e); return;
        case 'Q': case 'X': cmd_quit(e); return;
        case 'G': e->help_active = 1; e->help_scroll = 0; return;
        case 'W': cmd_find(e); return;
        case 'F': search_next(e, +1); return;
        case 'R': cmd_replace(e); return;
        case 'K': edit_cut_line(e); return;
        case 'U': edit_paste(e); return;
        case 'Y': edit_copy_line(e); return;
        case 'Z': edit_undo(e); return;
        case 'A': move_home(e, 0); return;
        case 'E': move_end(e, 0); return;
        case 'D': edit_delete_char(e); return;
        case 'T':
            e->syntax_on = !e->syntax_on;
            screen_message(e, e->syntax_on ? "Syntax highlighting on" : "Syntax highlighting off", 0);
            return;
        case 'L': screen_message(e, "", 0); return;
        case 'C': {
            int sly, slx, ely, elx;
            if (sel_range(e, &sly, &slx, &ely, &elx)) { edit_copy_line(e); return; }
            char msg[128];
            snprintf(msg, sizeof msg, "Line %d, column %d", e->cy + 1, e->cx + 1);
            screen_message(e, msg, 0);
            return;
        }
        case 'N': move_down(e, 0); return;
        case 'P': move_up(e, 0); return;
        case 'V': move_page_down(e); return;
        case '_': cmd_goto(e); return;           /* Ctrl+_ */
        case '@': edit_select_all(e); return;    /* Ctrl+Space */
        default: return;
        }

    case K_ALT:
        switch (k.ch) {
        case 'c': cmd_line(e); return;
        case 'w': search_next(e, +1); return;
        case 'W': search_next(e, -1); return;
        case 'z': edit_redo(e); return;
        case 'l': e->show_numbers = !e->show_numbers; return;
        case 'u': e->show_ws = !e->show_ws; return;
        case 'm': edit_toggle_comment(e); return;
        case 'b': move_word_left(e, 0); return;
        case 'f': move_word_right(e, 0); return;
        case '<': move_home(e, 0); return;
        case '>': move_end(e, 0); return;
        case 'v': move_page_up(e); return;
        default: return;
        }

    case K_CHAR:
        edit_insert_char(e, k.ch);
        return;

    default:
        return;
    }
}

/* ------------------------------------------------------------------ main */

static void usage(void)
{
    printf("rudy %s — syntax highlighting editor\n\n", RUDY_VERSION);
    printf("Usage: rudy [options] [file]\n\n");
    printf("  -l, --language LANG   syntax language (c, cpp, csharp, rust, nasm, java)\n");
    printf("      --no-syntax       disable syntax highlighting\n");
    printf("      --tabsize=N       tab width (default 4)\n");
    printf("  -h, --help            show this help\n");
    printf("  -v, --version         show version\n");
}

int main(int argc, char **argv)
{
    Editor e;
    memset(&e, 0, sizeof e);
    edit_init(&e);
    syntax_init();

    const char *lang_opt = NULL;
    const char *file = NULL;
    int no_syntax = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("rudy %s\n", RUDY_VERSION); return 0; }
        if (!strcmp(a, "-l") || !strcmp(a, "--language")) {
            if (i + 1 < argc) lang_opt = argv[++i];
            continue;
        }
        if (!strncmp(a, "--language=", 11)) { lang_opt = a + 11; continue; }
        if (!strcmp(a, "--no-syntax")) { no_syntax = 1; continue; }
        if (!strncmp(a, "--tabsize=", 10)) { e.tabsize = atoi(a + 10); continue; }
        if (a[0] == '-' && a[1]) { fprintf(stderr, "rudy: unknown option: %s\n", a); return 2; }
        file = a;
    }
    if (e.tabsize < 1) e.tabsize = 1;
    if (e.tabsize > 16) e.tabsize = 16;

    e.lang = lang_by_path(file ? file : "");
    if (lang_opt) {
        const Language *L = lang_by_name(lang_opt);
        if (L) e.lang = L;
        else fprintf(stderr, "rudy: unknown language: %s\n", lang_opt);
    }
    if (no_syntax) e.syntax_on = 0;
    snprintf(e.lang_name, sizeof e.lang_name, "%s", e.lang ? e.lang->name : "Text");

    if (file) {
        if (!file_load(&e, file)) {
            fprintf(stderr, "rudy: cannot open %s\n", file);
            return 1;
        }
        e.lang = lang_by_path(e.path);
        snprintf(e.lang_name, sizeof e.lang_name, "%s", e.lang ? e.lang->name : "Text");
    }

    signal(SIGWINCH, on_winch);
    term_init();
    term_size(&e.rows, &e.cols);

    time_t last_autosave = time(NULL);
    while (!e.quit_request) {
        if (g_resized) {
            g_resized = 0;
            term_size(&e.rows, &e.cols);
        }
        screen_draw(&e);
        Key k = read_key();
        if (k.type == K_NONE) break;
        if (e.prompt_active) prompt_key(&e, k);
        else handle_key(&e, k);

        time_t now = time(NULL);
        if (now - last_autosave >= 60) { autosave(&e); last_autosave = now; }
    }

    if (e.modified) autosave(&e);
    term_restore();
    edit_free(&e);
    return 0;
}
