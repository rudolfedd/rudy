/*
 * rudy — a nano-like terminal editor with syntax highlighting.
 * Pure C (C11), POSIX, no external dependencies.
 */
#ifndef RUDY_H
#define RUDY_H

#include <stdio.h>
#include <stddef.h>
#include <regex.h>

#define RUDY_VERSION "1.0.0"
#define PATH_MAX_LEN 4096
#define MSG_MAX_LEN 256

/* ------------------------------------------------------------------ Str */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Str;

void  str_init(Str *s);
void  str_free(Str *s);
void  str_clear(Str *s);
void  str_reserve(Str *s, size_t need);
void  str_append(Str *s, const char *p, size_t n);
void  str_puts(Str *s, const char *p);
void  str_putc(Str *s, char c);
void  str_printf(Str *s, const char *fmt, ...);
char *str_dupn(const char *p, size_t n);
char *str_dupz(const char *p);

/* -------------------------------------------------------------- WordSet */
typedef struct {
    char **keys;
    size_t cap;
    size_t n;
} WordSet;

void ws_init(WordSet *ws, const char *words);
void ws_free(WordSet *ws);
int  ws_has(const WordSet *ws, const char *w, size_t len);

/* ------------------------------------------------------------------ utf8 */
int    utf8_decode(const char *s, size_t len, int *cp);
int    utf8_encode(int cp, char *out);
int    utf8_width(int cp);
size_t utf8_prev(const char *s, size_t pos);
size_t utf8_next(const char *s, size_t pos, size_t len);
size_t utf8_len(const char *s, size_t len);
size_t utf8_col_to_off(const char *s, size_t len, int col, int tabsize);
int    utf8_off_to_col(const char *s, size_t len, size_t off, int tabsize);

/* ---------------------------------------------------------------- Buffer */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Line;

typedef struct {
    Line          *lines;
    size_t         n;
    size_t         cap;
    unsigned long  version;
} Buffer;

void  buf_init(Buffer *b);
void  buf_free(Buffer *b);
void  buf_clear(Buffer *b);
char *buf_line_dup(const Buffer *b, size_t idx);
void  buf_set_line(Buffer *b, size_t idx, const char *s, size_t len);
void  buf_replace(Buffer *b, size_t start, size_t count, char **repl, size_t nrepl);

/* --------------------------------------------------------------- Undo */
enum { CH_NONE = 0, CH_INSERT, CH_DELETE, CH_OTHER };

typedef struct {
    size_t  old_start, old_count;
    size_t  new_start, new_count;
    char  **old_lines;
    char  **new_lines;
    int     old_cy, old_cx;
    int     new_cy, new_cx;
    int     kind;
} Change;

typedef struct {
    Change *items;
    size_t  n;
    size_t  cap;
} ChangeStack;

void chg_init(ChangeStack *cs);
void chg_free(ChangeStack *cs);
void chg_clear(ChangeStack *cs);
void chg_push(ChangeStack *cs, Change *c);

/* -------------------------------------------------------------- Syntax */
typedef enum {
    T_PLAIN = 0, T_KEYWORD, T_TYPE, T_CONST, T_STRING, T_CHAR, T_NUMBER,
    T_COMMENT, T_PREPROC, T_FUNC, T_OPERATOR, T_IDENT, T_BUILTIN,
    T_LABEL, T_DIRECTIVE, T_ATTR, T_REG, T_MACRO
} TokType;

typedef struct { int start, len; TokType type; } Span;
typedef struct { Span *v; int n, cap; } SpanList;

#define LF_PREPROC  (1u << 0)
#define LF_RAWSTR   (1u << 1)
#define LF_TEXTBLK  (1u << 2)
#define LF_VERBATIM (1u << 3)
#define LF_ASM      (1u << 4)
#define LF_CAPTYPE  (1u << 5)
#define LF_LIFETIME (1u << 6)

typedef struct Language {
    const char *name;
    const char *exts;
    const char *keywords;
    const char *types;
    const char *consts;
    const char *builtins;
    const char *labels;
    const char *regs;
    const char *line_comment;
    const char *block_open;
    const char *block_close;
    unsigned    flags;
    WordSet     kw, ty, cn, bi, lb, rg;
} Language;

void             syntax_init(void);
const Language  *lang_by_name(const char *name);
const Language  *lang_by_path(const char *path);
int              lang_count(void);
const Language  *lang_at(int i);

typedef struct { int mode; int hashes; } HLState;

void             hl_cache_invalidate(void);
const SpanList  *hl_spans_for(Buffer *b, const Language *L, size_t line);

/* -------------------------------------------------------------- Editor */
typedef struct Editor Editor;

struct Editor {
    Buffer       buf;
    int          cy, cx;
    int          top, left, want_cx;
    int          modified;
    int          tabsize;
    int          syntax_on;
    int          show_numbers;
    int          show_ws;
    int          crlf;
    int          quit_request;
    char         path[PATH_MAX_LEN];
    int          has_path;
    ChangeStack  undo, redo;
    char       **clip;
    size_t       clip_n, clip_cap;
    int          sel_active;
    int          sel_ay, sel_ax;
    int          sel_by, sel_bx;
    char         pattern[512];
    int          has_pattern;
    regex_t      re;
    int          re_valid;
    int          re_icase;
    int          cur_match_line;
    size_t       cur_match_so, cur_match_eo;
    const Language *lang;
    char         lang_name[32];
    char         message[MSG_MAX_LEN];
    int          msg_error;
    int          rows, cols;
    int          help_active;
    int          help_scroll;
    int          prompt_active;
    int          prompt_type;
    char         prompt_label[64];
    Str          prompt_input;
    int          prompt_cursor;
    void       (*prompt_cb)(Editor *e, int ok, const char *text);
};

/* -------------------------------------------------------------- Screen */
void term_init(void);
void term_restore(void);
void term_size(int *rows, int *cols);
void screen_draw(Editor *e);
void screen_message(Editor *e, const char *msg, int is_error);
int  gutter_width(const Editor *e);

/* -------------------------------------------------------------- Edit */
void edit_init(Editor *e);
void edit_free(Editor *e);
void edit_insert_char(Editor *e, int cp);
void edit_insert_text(Editor *e, const char *s, size_t len);
void edit_backspace(Editor *e);
void edit_delete_char(Editor *e);
void edit_newline(Editor *e);
void edit_indent(Editor *e);
void edit_outdent(Editor *e);
void edit_toggle_comment(Editor *e);
void edit_copy_line(Editor *e);
void edit_cut_line(Editor *e);
void edit_paste(Editor *e);
void edit_delete_selection(Editor *e);
void edit_undo(Editor *e);
void edit_redo(Editor *e);
void edit_select_all(Editor *e);

/* ------------------------------------------------------------- Editing */
void   move_left(Editor *e, int extend);
void   move_right(Editor *e, int extend);
void   move_up(Editor *e, int extend);
void   move_down(Editor *e, int extend);
void   move_home(Editor *e, int extend);
void   move_end(Editor *e, int extend);
void   move_word_left(Editor *e, int extend);
void   move_word_right(Editor *e, int extend);
void   move_page_up(Editor *e);
void   move_page_down(Editor *e);
void   move_to_line(Editor *e, int line);
void   sel_clear(Editor *e);
int    sel_range(Editor *e, int *sly, int *slx, int *ely, int *elx);
void   view_update(Editor *e);

/* --------------------------------------------------------------- Files */
int  file_load(Editor *e, const char *path);
int  file_save(Editor *e, const char *path);
void autosave(Editor *e);
int  autosave_recover(Editor *e);

/* -------------------------------------------------------------- Search */
typedef struct { size_t so, eo; } Match;

int  search_set_pattern(Editor *e, const char *pat, int icase);
int  search_next(Editor *e, int dir);
void search_clear(Editor *e);
int  search_line_matches(Editor *e, size_t line, Match *out, int max);
int  search_replace_all(Editor *e, const char *from, const char *to);
int  search_replace_regex(Editor *e, const char *pat, const char *rep);

#endif /* RUDY_H */
