/* screen.c — terminal setup, rendering, status line. */
#include "rudy.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#define CLR_RESET   "\x1b[0m"
#define CLR_BAR     "\x1b[48;5;24m\x1b[38;5;252m"
#define CLR_TITLE   "\x1b[48;5;25m\x1b[38;5;231m\x1b[1m"
#define CLR_HINT    "\x1b[48;5;236m\x1b[38;5;250m"
#define CLR_ERR     "\x1b[38;5;203m"
#define CLR_OK      "\x1b[38;5;114m"
#define CLR_SEL     "\x1b[48;5;60m"
#define CLR_MATCH   "\x1b[48;5;238m"
#define CLR_CURM    "\x1b[48;5;58m\x1b[38;5;231m"

static struct termios g_orig;
static int           g_raw = 0;
static int           g_alt = 0;

void term_init(void)
{
    if (tcgetattr(STDIN_FILENO, &g_orig) == 0) {
        struct termios raw = g_orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_oflag &= ~(OPOST);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) g_raw = 1;
    }
    if (isatty(STDOUT_FILENO)) {
        ssize_t r = write(STDOUT_FILENO, "\x1b[?1049h\x1b[?25l", 14);
        (void)r;
        g_alt = 1;
    }
}

void term_restore(void)
{
    if (g_raw) tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
    if (g_alt) {
        ssize_t r = write(STDOUT_FILENO, "\x1b[?25h\x1b[?1049l" CLR_RESET, 20);
        (void)r;
    }
    g_raw = g_alt = 0;
}

void term_size(int *rows, int *cols)
{
    *rows = 24;
    *cols = 80;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
}

/* ---------------------------------------------------------------- Colors */

static const char *tok_color(TokType t)
{
    switch (t) {
    case T_KEYWORD:   return "\x1b[38;5;81m";
    case T_TYPE:      return "\x1b[38;5;111m";
    case T_CONST:     return "\x1b[38;5;179m";
    case T_STRING:    return "\x1b[38;5;114m";
    case T_CHAR:      return "\x1b[38;5;150m";
    case T_NUMBER:    return "\x1b[38;5;215m";
    case T_COMMENT:   return "\x1b[38;5;245m";
    case T_PREPROC:   return "\x1b[38;5;203m";
    case T_FUNC:      return "\x1b[38;5;220m";
    case T_OPERATOR:  return "\x1b[38;5;252m";
    case T_BUILTIN:   return "\x1b[38;5;209m";
    case T_LABEL:     return "\x1b[38;5;222m";
    case T_DIRECTIVE: return "\x1b[38;5;205m";
    case T_ATTR:      return "\x1b[38;5;140m";
    case T_REG:       return "\x1b[38;5;176m";
    case T_MACRO:     return "\x1b[38;5;213m";
    default:          return "";
    }
}

int gutter_width(const Editor *e)
{
    if (!e->show_numbers) return 0;
    size_t n = e->buf.n ? e->buf.n : 1;
    int    d = 1;
    while (n >= 10) { n /= 10; d++; }
    return d + 2;
}

/* ------------------------------------------------------------------ Help */

static const char *HELP_LINES[] = {
    "  rudy " RUDY_VERSION " — editor.  Ctrl+G or Esc — back",
    "",
    "  File            ^O open           ^S save            ^Q quit",
    "                  ^X quit           ^R search/replace  ^W search",
    "                  ^F next match     Alt+W next        ^_ go to line",
    "                  Alt+C command line",
    "",
    "  Edit            Ctrl+Z undo       Alt+Z redo        Ctrl+K cut line",
    "                  Ctrl+Y copy       Ctrl+U paste       Alt+M comment",
    "                  Ctrl+A home       Ctrl+E end         Ctrl+D delete char",
    "                  Ctrl+Space select all",
    "",
    "  Movement        arrows            Ctrl+←/→ word      Alt+B/Alt+F word",
    "                  Alt+</Alt+> line start/end             PgUp/PgDn",
    "                  Shift+arrows — selection              Ctrl+↑/↓",
    "",
    "  Indent          Tab indent        Shift+Tab outdent",
    "                  Enter — auto-indent",
    "",
    "  View            Ctrl+L redraw     Ctrl+T syntax       Alt+L line numbers",
    "                  Alt+U spaces/tabs",
    "",
    "  Commands (:)    :w save   :w file   :q quit   :q! discard changes",
    "                  :wq save and quit   :o file open",
    "                  :s/pattern/replace/g   :set syntax=rust   :set tabsize=4",
    "                  :set numbers | :set nonumbers | :set syntax=off | :help",
    "",
    "  Languages: C, C++, C#, Rust, NASM, Java (by file extension)",
    ""
};

static void draw_help(Editor *e, Str *out)
{
    int rows = e->rows, cols = e->cols;
    int count = (int)(sizeof HELP_LINES / sizeof HELP_LINES[0]);
    int visible = rows - 1;
    if (visible < 1) visible = 1;

    str_puts(out, "\x1b[H");
    for (int i = 0; i < visible; i++) {
        int idx = e->help_scroll + i;
        str_puts(out, CLR_RESET);
        if (idx < count) {
            const char *l = HELP_LINES[idx];
            str_puts(out, l);
            size_t vis = utf8_len(l, strlen(l));
            for (size_t k = vis; k < (size_t)cols; k++) str_putc(out, ' ');
        } else {
            for (int k = 0; k < cols; k++) str_putc(out, ' ');
        }
        str_puts(out, "\x1b[K\r\n");
    }
    /* bottom line */
    str_puts(out, CLR_HINT);
    const char *hint = " ↑/↓ scroll   PgUp/PgDn   any other key — exit ";
    str_puts(out, hint);
    for (size_t k = strlen(hint); k < (size_t)cols; k++) str_putc(out, ' ');
    str_puts(out, CLR_RESET);
}

/* ---------------------------------------------------------- Line drawing */

static TokType span_at(const SpanList *sp, size_t off)
{
    if (!sp) return T_PLAIN;
    for (int i = 0; i < sp->n; i++)
        if ((size_t)sp->v[i].start <= off && off < (size_t)(sp->v[i].start + sp->v[i].len))
            return sp->v[i].type;
    return T_PLAIN;
}

static void render_line(Str *out, Editor *e, size_t lineno, int width)
{
    const char *s = "";
    size_t      len = 0;
    if (lineno < e->buf.n) {
        s = e->buf.lines[lineno].data ? e->buf.lines[lineno].data : "";
        len = e->buf.lines[lineno].len;
    }

    const SpanList *sp = NULL;
    if (e->syntax_on && e->lang && lineno < e->buf.n)
        sp = hl_spans_for(&e->buf, e->lang, lineno);

    Match matches[256];
    int   nm = 0;
    if (e->has_pattern && e->re_valid && lineno < e->buf.n)
        nm = search_line_matches(e, lineno, matches, 256);

    /* selection range */
    int sel_sly = 0, sel_slx = 0, sel_ely = 0, sel_elx = 0;
    int has_sel = sel_range(e, &sel_sly, &sel_slx, &sel_ely, &sel_elx);

    size_t p = 0;
    int    col = 0;
    int    emitted = 0;
    int    mi = 0;
    const char *cur_color = NULL;

    while (p < len && emitted < width) {
        char   ch = s[p];
        int    cp = 0;
        size_t n = 1;
        int    w;
        if (ch == '\t') {
            w = e->tabsize - (col % e->tabsize);
            cp = '\t';
        } else {
            n = utf8_decode(s + p, len - p, &cp);
            w = utf8_width(cp);
        }

        if (col + w <= e->left) { col += w; p += n; continue; }
        int skip = (col < e->left) ? (e->left - col) : 0;

        TokType t = span_at(sp, p);
        const char *color = tok_color(t);

        int in_match = 0, in_cur = 0;
        while (mi < nm && matches[mi].eo <= p) mi++;
        if (mi < nm && p >= matches[mi].so && p < matches[mi].eo) {
            in_match = 1;
            if ((int)lineno == e->cur_match_line && matches[mi].so == e->cur_match_so)
                in_cur = 1;
        }

        int in_sel = 0;
        if (has_sel && (int)lineno >= sel_sly && (int)lineno <= sel_ely) {
            if ((int)lineno == sel_sly && (int)lineno == sel_ely) {
                if ((int)p >= sel_slx && (int)p < sel_elx) in_sel = 1;
            } else if ((int)lineno == sel_sly) {
                if ((int)p >= sel_slx) in_sel = 1;
            } else if ((int)lineno == sel_ely) {
                if ((int)p < sel_elx) in_sel = 1;
            } else {
                in_sel = 1;
            }
        }

        if (color != cur_color) {
            if (cur_color && *cur_color) str_puts(out, CLR_RESET);
            if (*color) str_puts(out, color);
            cur_color = color;
        }
        if (in_cur) str_puts(out, CLR_CURM);
        else if (in_match) str_puts(out, CLR_MATCH);
        else if (in_sel) str_puts(out, CLR_SEL);

        if (ch == '\t') {
            int total = w - skip;
            if (e->show_ws && skip == 0 && emitted < width) {
                str_puts(out, "\xe2\x86\x92");            /* → */
                emitted++;
                for (int i = 1; i < total && emitted < width; i++) { str_putc(out, ' '); emitted++; }
            } else {
                for (int i = 0; i < total && emitted < width; i++) { str_putc(out, ' '); emitted++; }
            }
        } else if (skip > 0) {
            for (int i = 0; i < skip && emitted < width; i++) { str_putc(out, ' '); emitted++; }
        } else {
            if (e->show_ws && cp == ' ') {
                str_puts(out, "\xc2\xb7");                /* · */
                emitted += w;
            } else {
                str_append(out, s + p, n);
                emitted += w;
            }
        }

        if (in_cur || in_match || in_sel) str_puts(out, "\x1b[49m");

        col += w;
        p += n;
    }

    if (cur_color && *cur_color) str_puts(out, CLR_RESET);
}

/* ------------------------------------------------------------ Main drawing */

void screen_draw(Editor *e)
{
    int rows = e->rows, cols = e->cols;
    if (rows < 4) rows = 4;
    if (cols < 10) cols = 10;

    Str out;
    str_init(&out);
    str_puts(&out, "\x1b[H");

    if (e->help_active) {
        draw_help(e, &out);
        fwrite(out.data, 1, out.len, stdout);
        fflush(stdout);
        str_free(&out);
        return;
    }

    int gutter = gutter_width(e);
    int text_rows = rows - 3;
    if (text_rows < 1) text_rows = 1;
    int text_cols = cols - gutter;
    if (text_cols < 1) text_cols = 1;

    /* --- title bar --- */
    Str title;
    str_init(&title);
    str_printf(&title, " rudy %s", RUDY_VERSION);
    if (e->has_path) {
        const char *base = strrchr(e->path, '/');
        str_printf(&title, "  %s%s", base ? base + 1 : e->path, e->modified ? " *" : "");
    } else {
        str_printf(&title, "  [No Name]%s", e->modified ? " *" : "");
    }
    if (e->lang) str_printf(&title, "  %s", e->lang->name);
    if (!e->syntax_on) str_puts(&title, " (no syntax)");

    Str right;
    str_init(&right);
    int pct = e->buf.n ? (int)((e->cy + 1) * 100 / (int)e->buf.n) : 100;
    str_printf(&right, "Ln %d/%zu (%d%%)  Col %d  %s ",
               e->cy + 1, e->buf.n, pct, e->cx + 1, e->has_pattern ? "search" : "");

    str_puts(&out, CLR_TITLE);
    str_append(&out, title.data, title.len);
    size_t used = title.len + right.len;
    for (int i = (int)used; i < cols; i++) str_putc(&out, ' ');
    str_append(&out, right.data, right.len);
    str_puts(&out, CLR_RESET);
    str_puts(&out, "\x1b[K\r\n");
    str_free(&title);
    str_free(&right);

    /* --- text --- */
    for (int r = 0; r < text_rows; r++) {
        size_t lineno = (size_t)(e->top + r);
        if (gutter) {
            char num[32];
            size_t v = lineno + 1;
            int    nd = 0;
            for (size_t t = v; t; t /= 10) nd++;
            int pad = (gutter - 1) - nd;
            if (pad < 0) pad = 0;
            for (int i = 0; i < pad; i++) num[i] = ' ';
            snprintf(num + pad, sizeof num - (size_t)pad, "%zu", v);
            if ((int)lineno == e->cy) {
                str_puts(&out, "\x1b[38;5;231m\x1b[1m");
                str_puts(&out, num);
                str_puts(&out, "\x1b[0m");
                str_putc(&out, ' ');
            } else {
                str_puts(&out, "\x1b[38;5;240m");
                str_puts(&out, num);
                str_puts(&out, "\x1b[0m");
                str_putc(&out, ' ');
            }
        }
        if (lineno < e->buf.n) render_line(&out, e, lineno, text_cols);
        str_puts(&out, "\x1b[K\r\n");
    }

    /* --- status line / prompt --- */
    str_puts(&out, CLR_RESET);
    if (e->prompt_active) {
        str_puts(&out, "\x1b[1m");
        str_puts(&out, e->prompt_label);
        str_puts(&out, "\x1b[0m");
        str_append(&out, e->prompt_input.data ? e->prompt_input.data : "", e->prompt_input.len);
        /* cursor position inside the prompt line */
    } else if (e->message[0]) {
        str_puts(&out, e->msg_error ? CLR_ERR : CLR_OK);
        str_puts(&out, e->message);
        str_puts(&out, CLR_RESET);
    }
    str_puts(&out, "\x1b[K\r\n");

    /* --- hints --- */
    str_puts(&out, CLR_HINT);
    const char *hint = " ^O open  ^S save  ^W search  ^R replace  ^G help  ^Q quit ";
    str_puts(&out, hint);
    for (size_t k = strlen(hint); k < (size_t)cols; k++) str_putc(&out, ' ');
    str_puts(&out, CLR_RESET);
    str_puts(&out, "\x1b[K");

    /* --- cursor position --- */
    int cur_row, cur_col;
    if (e->prompt_active) {
        int col = (int)utf8_len(e->prompt_label, strlen(e->prompt_label)) +
                  utf8_off_to_col(e->prompt_input.data ? e->prompt_input.data : "",
                                  e->prompt_input.len, (size_t)e->prompt_cursor, e->tabsize);
        cur_row = rows - 2;
        cur_col = col;
    } else {
        const char *s = "";
        size_t      len = 0;
        if ((size_t)e->cy < e->buf.n) {
            s = e->buf.lines[e->cy].data ? e->buf.lines[e->cy].data : "";
            len = e->buf.lines[e->cy].len;
        }
        int col = utf8_off_to_col(s, len, (size_t)e->cx, e->tabsize) - e->left;
        cur_row = 1 + (e->cy - e->top);
        cur_col = gutter + col;
    }
    if (cur_row < 0) cur_row = 0;
    if (cur_row >= rows) cur_row = rows - 1;
    if (cur_col < 0) cur_col = 0;
    if (cur_col >= cols) cur_col = cols - 1;
    str_printf(&out, "\x1b[%d;%dH\x1b[?25h", cur_row + 1, cur_col + 1);

    fwrite(out.data, 1, out.len, stdout);
    fflush(stdout);
    str_free(&out);
}

void screen_message(Editor *e, const char *msg, int is_error)
{
    snprintf(e->message, sizeof e->message, "%s", msg ? msg : "");
    e->msg_error = is_error;
}
