/* Toy-Shell/main.c */
#include "def.h"
#include "buffer.h"
#include "str.h"
#include "debug.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <pwd.h>
#include <dirent.h>

#include <limits.h>
#include <stdalign.h>
#include <stdio.h>

#define MIN(a_, b_) ((a_) < (b_) ? (a_) : (b_))
#define MAX(a_, b_) ((a_) > (b_) ? (a_) : (b_))
#define ALIGN_UP(n_, align_) ((((n_) - 1) / (align_) + 1) * (align_))

#define LIKELY(x_) __builtin_expect(!!(x_), 1)
#define UNLIKELY(x_) __builtin_expect(!!(x_), 0)

// @TODO(total):
// Fix trailing uncond link -- it should work
// Fix crash on ^C to telnet -- I think telnet should not react at all
// Make a file with test commands
// Error reporting in the parser
// Better error reporting in interpreter
// Tighten assertions
// ^L
// Suggest local directories in first word autocomplete:w
// Command history (const depth)


static inline void mem_clear(void *mem, u64 sz)
{
    u8 *p = (u8 *)mem;
    for (u8 *end = p + sz; p != end; ++p)
        *p = 0;
}

static inline void mem_cpy(void *to, void *from, u64 sz)
{
    u8 *pt = (u8 *)to, *pf = (u8 *)from;
    for (u8 *end = pt + sz; pt != end; ++pt, ++pf)
        *pt = *pf;
}

static inline void mem_cpy_bw(void *to, void *from, u64 sz)
{
    u8 *pt = (u8 *)to, *pf = (u8 *)from;
    for (u8 *from_end = pf + sz - 1, *to_end = pt + sz - 1;
        to_end != pt - 1; --from_end, --to_end)
    {
        *to_end = *from_end;
    }
}

#define CLEAR(addr_) mem_clear((addr_), sizeof(*(addr_)))

typedef struct arena_tag {
    buffer_t buf;
    u64 allocated;
} arena_t;

static u8 *arena_allocate_aligned(arena_t *arena, u64 bytes, u64 alignment)
{ 
    // malloc alignment must be enough (since arena itself is mallocd)
    ASSERT(alignment <= 16 && 16 % alignment == 0);
    ASSERT(buffer_is_valid(&arena->buf));
    u64 const required_start = ALIGN_UP(arena->allocated, alignment);

    if (UNLIKELY(required_start + bytes > arena->buf.sz)) {
        fprintf(stderr, "OOM, check your assumptions!\n");
        exit(-1);
    }
    u8 *ptr = (u8 *)arena->buf.p + required_start;
    arena->allocated = required_start + bytes;
    return ptr;
}

static inline void arena_drop(arena_t *arena)
{
    arena->allocated = 0;
}

#define ARENA_ALLOC(arena_, type_) \
    (type_ *)arena_allocate_aligned((arena_), sizeof(type_), _Alignof(type_))
#define ARENA_ALLOC_N(arena_, type_, n_) \
    (type_ *)arena_allocate_aligned(     \
        (arena_), (n_) * sizeof(type_), _Alignof(type_))

enum {
    c_rl_ok = 0,
    c_rl_string_overflow = -1,
    c_rl_eof = -2,
};

static inline b32 is_eol(int c)
{
    return (c == '\n') | (c == EOF);
}

static inline b32 is_whitespace(int c)
{
    return (c == ' ') | (c == '\t') | (c == '\r');
}

static inline b32 is_separator_char(int c)
{
    return
        (c == '|') | (c == '&') | (c == '>') | (c == '<') |
        (c == ';') | (c == ')') | (c == '(');
}

static inline b32 is_ws_or_sep(int c)
{
    return is_whitespace(c) || is_separator_char(c);
}

static string_t get_token_postfix(string_t s, b32 *is_first)
{
    string_t pf = {s.p + s.len, 0};
    while (pf.p > s.p && !is_ws_or_sep(pf.p[-1])) {
        --pf.p;
        ++pf.len;
    }
    char const *p = pf.p - 1;
    while (p >= s.p && is_whitespace(*p))
        --p;
    *is_first = p < s.p || is_separator_char(*p);
    return pf;
}

typedef struct split_path_tag {
    string_t dir;
    string_t file;
} split_path_t;

static split_path_t split_path(string_t path)
{
    split_path_t res = {0};
    if (path.len == 0)
        return res;
    res.file.p = path.p + path.len;
    while (res.file.p > path.p && res.file.p[-1] != '/') {
        --res.file.p;
        ++res.file.len;
    }
    if (res.file.p != path.p) {
        res.dir.p = path.p;
        res.dir.len = res.file.p - path.p - 1;
    }
    return res;
}

static b32 path_has_dir(split_path_t const *path)
{
    return !string_is_empty(&path->dir);
}

typedef struct fslist_tag {
    string_t *entries;
    u32 cnt;
} fslist_t;

static b32 iterate_fslist(
    fslist_t const *list, b32 (*cb)(string_t, void *), void *user)
{
    u32 cnt = list->cnt;
    string_t const *s = list->entries;
    while (cnt--) {
        if (!cb(*s, user))
            return false;
        s = (string_t *)((u8 *)s +
            ALIGN_UP(sizeof(string_t) + s->len + 1, _Alignof(string_t)));
    }
    return true;
}

static b32 fslist_elem_is_not_eq(string_t elem, void *user)
{
    string_t *needle = (string_t *)user;
    return !str_eq(elem, *needle);
}

typedef struct search_autocomplete_in_dir_args_tag {
    string_t prefix;
    fslist_t *out;
    arena_t *arena;
} search_autocomplete_in_dir_args_t; 
static b32 search_autocomplete_in_dir(string_t dir, void *user)
{
    search_autocomplete_in_dir_args_t *args =
        (search_autocomplete_in_dir_args_t *)user;
    char *dir_cstr = ARENA_ALLOC_N(args->arena, char, dir.len + 1);
    mem_cpy(dir_cstr, dir.p, dir.len);
    dir_cstr[dir.len] = '\0';
    DIR *desc = opendir(dir_cstr);
    args->arena->allocated -= dir.len + 1;
    if (!desc)
        return true;
    struct dirent *dent;
    while ((dent = readdir(desc)) != NULL) {
        string_t dname = str_from_cstr(dent->d_name);
        if (str_is_prefix_of(args->prefix, dname)) {
            // If already contained, don't add
            if (!iterate_fslist(args->out, fslist_elem_is_not_eq, &dname))
                continue;

            string_t *s = ARENA_ALLOC(args->arena, string_t); 
            s->p = ARENA_ALLOC_N(args->arena, char, dname.len + 1);
            s->len = dname.len;
            mem_cpy(s->p, dname.p, s->len);
            s->p[s->len] = '\0';
            ++args->out->cnt;
            if (!args->out->entries)
                args->out->entries = s;
        }
    }
    return true;
}

static fslist_t search_autocomplete(
    string_t prefix, fslist_t const *path, arena_t *arena)
{
    fslist_t res = {0};
    split_path_t pref_path = split_path(prefix);
    search_autocomplete_in_dir_args_t args = {pref_path.file, &res, arena};
    if (path_has_dir(&pref_path))
        search_autocomplete_in_dir(pref_path.dir, &args);
    else if (path)
        iterate_fslist(path, search_autocomplete_in_dir, &args);
    else {
        string_t cwd = LITSTR(".");
        search_autocomplete_in_dir(cwd, &args);
    }
    return res;
}

typedef struct terminal_session_tag {
    struct termios backup_ts;
    struct winsize wsz; // @NOTE: does not support resize while editing one line

    char input_buf[64];
    u32 buffered_chars_cnt;

    fslist_t path;

    arena_t *tmpmem;
    arena_t *persmem;
} terminal_session_t;

static void init_term(terminal_session_t *term)
{
    tcgetattr(STDIN_FILENO, &term->backup_ts);
    char *path = getenv("PATH");
    if (path) {
        while (*path) {
            string_t *s = ARENA_ALLOC(term->persmem, string_t);
            if ((term->path.cnt++) == 0)
                term->path.entries = s;
            s->p = ARENA_ALLOC_N(term->persmem, char, 0);
            while (*path && *path != ':') {
                s->p[s->len++] = *path++;
                (void)ARENA_ALLOC(term->persmem, char);
            }
            s->p[s->len] = '\0';
            (void)ARENA_ALLOC(term->persmem, char);
            if (*path)
                ++path;
        }
    }
}

static void shutdown_term(terminal_session_t *term, b32 drain)
{
    if (drain) {
        int chars_read;
        while ((chars_read =
            read(STDIN_FILENO, term->input_buf, sizeof(term->input_buf))) > 0)
        {
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &term->backup_ts);
}

static void start_terminal_editing(terminal_session_t *term)
{
    struct termios ts;
    mem_cpy(&ts, &term->backup_ts, sizeof(struct termios));    

    ts.c_lflag &= ~(ICANON | IEXTEN | ISIG | ECHO);
    ts.c_cc[VMIN] = 1;
    ts.c_cc[VTIME] = 0;
    ts.c_cc[VEOF] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &ts);

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term->wsz);
}

static void finish_terminal_editing(terminal_session_t *term)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &term->backup_ts);
    arena_drop(term->tmpmem);
}

static void move_cursor_to_pos(int from, int to, terminal_session_t const *term)
{
    int from_lines = from / term->wsz.ws_col;
    int from_chars = from % term->wsz.ws_col;
    int to_lines = to / term->wsz.ws_col;
    int to_chars = to % term->wsz.ws_col;

    if (from_lines < to_lines) {
        for (int i = 0; i < to_lines - from_lines; ++i)
            printf("\033[B");
    } else if (from_lines > to_lines) {
        for (int i = 0; i < from_lines - to_lines; ++i)
            printf("\033[A");
    }
    if (from_chars < to_chars) {
        for (int i = 0; i < to_chars - from_chars; ++i)
            printf("\033[C");
    } else if (from_chars > to_chars) {
        for (int i = 0; i < from_chars - to_chars; ++i)
            printf("\033[D");
    }
}

typedef struct print_autocomplete_opt_args_tag {
    int *pos;
    terminal_session_t const *term;
    int max_rows;
    int col_alignment;
} print_autocomplete_opt_args_t;
static b32 print_autocomplete_opt(string_t opt, void *user)
{
    print_autocomplete_opt_args_t *args = (print_autocomplete_opt_args_t *)user; 

    int w = args->term->wsz.ws_col;
    int max_rows = args->max_rows;

    int start_chars = *args->pos % w;
    int start_row = *args->pos / w;

    if (start_row > max_rows || (start_row == max_rows && start_chars > 0))
        return true;

    int aligned_chars = start_chars == 0 ?
        start_chars : ALIGN_UP(start_chars, args->col_alignment);

    if (aligned_chars + MAX((int)opt.len, args->col_alignment) > w) {
        for (int i = start_chars; i < w; ++i)
            putchar(' ');
        putchar('\n');
        *args->pos += w - start_chars;

        start_chars = 0;
        aligned_chars = 0;
        ++start_row;
    }
    for (int i = start_chars; i < aligned_chars; ++i)
        putchar(' ');
    *args->pos += aligned_chars - start_chars;

    if (start_row < max_rows) {
        *args->pos += opt.len;
        while ((int)opt.len >= w) {
            string_t subs = {opt.p, w};
            printf("%.*s\n", STR_PRINTF_ARGS(subs));
            opt.p += w;
            opt.len -= (u64)w;
        }
        printf("%.*s", STR_PRINTF_ARGS(opt));
    } else
        *args->pos += printf("...");
    return true;
}

static int read_line_from_terminal(
    buffer_t *buf, string_t *out_string, terminal_session_t *term)
{
    start_terminal_editing(term);

    printf("> ");
    fflush(stdout);

    int res = c_rl_ok;

    ASSERT(buffer_is_valid(buf));
    ASSERT(buf->sz > 0);

    string_t s = {buf->p, 0};
    int epos = 0;

    enum {
        e_st_dfl,
        e_st_parsed_esc,
        e_st_ready_for_arrow
    } state = e_st_dfl;

    b32 done = false;
    int prev_len = 0;

    while (!done) {
        char *p;
        int chars_consumed;
        int prev_epos = epos;

        fslist_t autocompletes = {0};

        if (!term->buffered_chars_cnt) {
            term->buffered_chars_cnt =
                read(STDIN_FILENO, term->input_buf, sizeof(term->input_buf));
            if (!term->buffered_chars_cnt) {
                res = c_rl_eof;
                break;
            }
        }

        for (p = term->input_buf;
            p != term->input_buf + term->buffered_chars_cnt;
            ++p)
        {
            if (state == e_st_ready_for_arrow) {
                switch (*p) {
                case 67:
                    if (epos < (int)s.len)
                        ++epos;
                    state = e_st_dfl;
                    continue;
                case 68:
                    if (epos > 0)
                        --epos;
                    state = e_st_dfl;
                    continue;
                default:
                    break;
                }
            }

            switch (*p) {
            case 27:
                state = e_st_parsed_esc;
                continue;
            case 91:
                if (state == e_st_parsed_esc) {
                    state = e_st_ready_for_arrow;
                    continue;
                }
                // fall through

            case 4:
                res = c_rl_eof;
                goto func_end;
            case '\n': 
                ++p;
                done = true;
                state = e_st_dfl;
                goto loop_end;

            case 127:
            case '\b':
            case 23:
            case 21: {
                int chars_to_delete;
                if (epos == 0)
                    break;
                if (*p == 23) {
                    b32 skipping_ws = true;
                    chars_to_delete = 0;
                    while (chars_to_delete < epos &&
                        (!is_ws_or_sep(s.p[epos - chars_to_delete - 1]) ||
                        skipping_ws))
                    {
                        if (!is_ws_or_sep(s.p[epos - chars_to_delete - 1]))
                            skipping_ws = false;
                        ++chars_to_delete;
                    }
                } else if (*p == 21) {
                    chars_to_delete = epos;
                } else {
                    chars_to_delete = 1;
                }
                if (epos < (int)s.len) {
                    mem_cpy(
                        s.p + epos - chars_to_delete,
                        s.p + epos,
                        s.len - epos);
                }
                epos -= chars_to_delete;
                s.len -= chars_to_delete;
            } break;
            case '\t': {
                string_t curs = {s.p, epos};
                b32 is_first = false;
                string_t tok = get_token_postfix(curs, &is_first);
                autocompletes = search_autocomplete(
                    tok, is_first ? &term->path : NULL, term->tmpmem);
                if (autocompletes.cnt == 1 &&
                    autocompletes.entries[0].len + epos < buf->sz)
                {
                    split_path_t path = split_path(autocompletes.entries[0]);
                    split_path_t split_tok = split_path(tok);
                    string_t inserted = {
                        path.file.p + split_tok.file.len,
                        path.file.len - split_tok.file.len
                    };
                    mem_cpy_bw(
                        s.p + epos + inserted.len, s.p + epos, inserted.len);
                    mem_cpy(s.p + epos, inserted.p, inserted.len);
                    epos += inserted.len;
                    s.len += inserted.len;
                    CLEAR(&autocompletes);
                }
            } break;

            default:
                if (*p < 32) // no control characters
                    break;
                if (s.len < buf->sz - 1) {
                    if (epos < (int)s.len)
                        mem_cpy_bw(s.p + epos + 1, s.p + epos, s.len - epos);
                    s.p[epos++] = *p;
                    ++s.len;
                }
            }

            state = e_st_dfl;
        }

    loop_end:
        chars_consumed = p - term->input_buf;
        term->buffered_chars_cnt -= chars_consumed;
        if (chars_consumed < (int)term->buffered_chars_cnt)
            mem_cpy(term->input_buf, p, term->buffered_chars_cnt);

        move_cursor_to_pos(prev_epos + 2, 0, term);
        int chars_printed = printf("> ");
        for (int i = 0; i < MAX((int)s.len, prev_len); ++i) {
            putchar(i < (int)s.len ? s.p[i] : ' ');
            if ((++chars_printed) % term->wsz.ws_col == 0)
                putchar('\n');
        }

        int curspos = MAX((int)s.len, prev_len) + 2;
        if (autocompletes.cnt > 0 && !done) {
            move_cursor_to_pos(curspos, s.len + 2, term);
            curspos = s.len + 2;
            int linebreak = ALIGN_UP(curspos, term->wsz.ws_col);
            for (; curspos < linebreak; ++curspos)
                putchar(' ');
            putchar('\n');
            curspos = linebreak;

            print_autocomplete_opt_args_t args =
                {&curspos, term, 8, MAX(term->wsz.ws_col / 6, 16)};
            iterate_fslist(&autocompletes, print_autocomplete_opt, &args);
            prev_len = curspos - 2;
        } else
            prev_len = s.len;

        if (epos + 2 < curspos)
            move_cursor_to_pos(curspos, epos + 2, term);
        if (done)
            putchar('\n');

        fflush(stdout);

        arena_drop(term->tmpmem);
    }

func_end:
    finish_terminal_editing(term);
    s.p[s.len] = '\0';
    *out_string = s;
    return res;
}

static int read_line_from_regular_stdin(buffer_t *buf, string_t *out_string)
{
    ASSERT(buffer_is_valid(buf));
    ASSERT(buf->sz > 0);

    string_t s = {buf->p, 0};

    int c;
    while (!is_eol(c = getchar())) {
        s.p[s.len++] = (char)c;
        if (s.len >= buf->sz)
            return c_rl_string_overflow;
    }

    if (c == EOF) {
        s.len = 0;
        return c_rl_eof;
    }

    s.p[s.len] = '\0';
    *out_string = s;
    return c_rl_ok;
}

typedef enum token_type_tag {
    e_tt_uninit = 0,

    e_tt_eol,        // last token
    e_tt_ident,      // anything
    e_tt_in,         // <
    e_tt_out,        // >
    e_tt_append,     // >>
    e_tt_pipe,       // |
    e_tt_and,        // &&
    e_tt_or,         // ||
    e_tt_semicolon,  // ;
    e_tt_background, // &
    e_tt_lparen,     // (
    e_tt_rparen,     // )

    e_tt_lexer_error = -128,

    e_tt_parser_error = -256
} token_type_t;

static b32 tt_is_error(token_type_t tt)
{
    return tt < 0;
}

typedef struct token_tag {
    token_type_t type;
    string_t id;
} token_t;

typedef struct lexer_tag {
    string_t line;
    u64 pos;
} lexer_t;

static inline int lexer_peek(lexer_t *lexer)
{
    if (lexer->pos >= lexer->line.len)
        return EOF;
    return lexer->line.p[lexer->pos];
}
static inline void lexer_consume(lexer_t *lexer)
{
    ASSERT(lexer->pos < lexer->line.len);
    ++lexer->pos;
}

static token_t get_next_token(lexer_t *lexer, arena_t *arena)
{
    token_t tok = {0};

    enum {
        e_lst_prefix_separator,
        e_lst_parsing_identifier
    } state = e_lst_prefix_separator;

    b32 in_quotes = false;
    b32 screen_next = false;
    int c;

    while (!is_eol(c = lexer_peek(lexer))) {
        if (state == e_lst_prefix_separator) {
            if (is_whitespace(c)) {
                lexer_consume(lexer);
                continue;
            }

            switch (c) {
            case '>':
                lexer_consume(lexer);
                if (lexer_peek(lexer) == '>') {
                    lexer_consume(lexer);
                    tok.type = e_tt_append;
                } else
                    tok.type = e_tt_out;
                return tok;
            case '|':
                lexer_consume(lexer);
                if (lexer_peek(lexer) == '|') {
                    lexer_consume(lexer);
                    tok.type = e_tt_or;
                } else
                    tok.type = e_tt_pipe;
                return tok;
            case '&':
                lexer_consume(lexer);
                if (lexer_peek(lexer) == '&') {
                    lexer_consume(lexer);
                    tok.type = e_tt_and;
                } else
                    tok.type = e_tt_background;
                return tok;

            case '<':
                lexer_consume(lexer);
                tok.type = e_tt_in;
                return tok;
            case ';':
                lexer_consume(lexer);
                tok.type = e_tt_semicolon;
                return tok;
            case '(':
                lexer_consume(lexer);
                tok.type = e_tt_lparen;
                return tok;
            case ')':
                lexer_consume(lexer);
                tok.type = e_tt_rparen;
                return tok;

            default:
                state = e_lst_parsing_identifier;
            }
        }

        if (state == e_lst_parsing_identifier) {
            if (!in_quotes && !screen_next &&
                (is_whitespace(c) || is_separator_char(c)))
            {
                break;
            }

            lexer_consume(lexer);

            if (!string_is_valid(&tok.id))
                tok.id.p = ARENA_ALLOC_N(arena, char, 0);

            if (c == '\\' && !screen_next) {
                screen_next = true;
                continue;
            }

            if (c == '"' && !screen_next) {
                in_quotes = !in_quotes;
                continue;
            }

            ASSERT(c >= SCHAR_MIN && c <= SCHAR_MAX);
            (void)ARENA_ALLOC(arena, char);
            tok.id.p[tok.id.len++] = (char)c;

            screen_next = false;
        }
    }

    if (in_quotes || screen_next) {
        tok.type = e_tt_lexer_error;
        return tok;
    }

    if (state == e_lst_prefix_separator)
        tok.type = e_tt_eol;
    else {
        // To make linux syscalls happy
        (void)ARENA_ALLOC(arena, char);
        tok.id.p[tok.id.len] = '\0';

        tok.type = e_tt_ident;
    }

    return tok;
}

static inline b32 tok_is_valid(token_t tok)
{
    return tok.type != e_tt_uninit && !tt_is_error(tok.type);
}

static inline b32 tok_is_end_of_shell(token_t tok)
{
    return (tok.type == e_tt_eol) | (tok.type == e_tt_rparen) |
           !tok_is_valid(tok);
}

static inline b32 tok_is_cmd_elem_or_lparen(token_t tok)
{
    return
        (tok.type == e_tt_ident) | (tok.type == e_tt_in) |
        (tok.type == e_tt_out) | (tok.type == e_tt_append) |
        (tok.type == e_tt_lparen);
}

static inline b32 tok_is_cond_sep(token_t tok)
{
    return (tok.type == e_tt_and) | (tok.type == e_tt_or);
}

struct uncond_chain_node_tag;

typedef struct arg_node_tag {
    string_t name;
    struct arg_node_tag *next;
} arg_node_t;

typedef struct command_node_tag {
    string_t cmd;
    arg_node_t *args;    
    u64 arg_cnt;
} command_node_t;

typedef enum runnable_type_tag {
    e_rnt_cmd,
    e_rnt_subshell,
} runnable_type_t;

typedef struct runnable_node_tag {
    runnable_type_t type;
    union {
        command_node_t *cmd;
        struct uncond_chain_node_tag *subshell;
    };
} runnable_node_t;

typedef struct pipe_node_tag {
    runnable_node_t runnable;
    struct pipe_node_tag *next;
} pipe_node_t;

typedef struct pipe_chain_node_tag {
    runnable_node_t first;
    pipe_node_t *chain;
    u64 cmd_cnt;

    string_t stdin_redir;
    string_t stdout_redir;
    string_t stdout_append_redir;

    b32 is_cd;
} pipe_chain_node_t;

typedef enum cond_link_tag {
    e_cl_if_success,
    e_cl_if_failed
} cond_link_t;

typedef struct cond_node_tag {
    pipe_chain_node_t pp;
    cond_link_t link;
    struct cond_node_tag *next;
} cond_node_t;

typedef struct cond_chain_node_tag {
    pipe_chain_node_t first;
    cond_node_t *chain;
    u64 cond_cnt;
} cond_chain_node_t;

typedef enum uncond_link_tag {
    e_ul_bg,
    e_ul_wait
} uncond_link_t;

typedef struct uncond_node_tag {
    cond_chain_node_t cond;
    uncond_link_t link;
    struct uncond_node_tag *next;
} uncond_node_t;

typedef struct uncond_chain_node_tag {
    cond_chain_node_t first;
    uncond_node_t *chain;
    u64 uncond_cnt;
} uncond_chain_node_t;

typedef uncond_chain_node_t root_node_t;

static void print_indentation(int indentation)
{
    for (int i = 0; i < indentation; ++i)
        printf("    ");
}

static void print_uncond_chain(uncond_chain_node_t const *, int);

static void print_command(command_node_t const *cmd, int indentation)
{
    print_indentation(indentation);
    printf("cmd:<%.*s>", STR_PRINTF_ARGS(cmd->cmd));
    if (cmd->arg_cnt) {
        printf(", args:[<%.*s>", STR_PRINTF_ARGS(cmd->args->name));
        for (arg_node_t *arg = cmd->args->next; arg; arg = arg->next)
            printf(", <%.*s>", STR_PRINTF_ARGS(arg->name));
        printf("]");
    }
    putchar('\n');
}

static void print_runnable(runnable_node_t const *runnable, int indentation)
{
    if (runnable->type == e_rnt_cmd)
        print_command(runnable->cmd, indentation);
    else {
        print_indentation(indentation);
        printf("(\n");
        print_uncond_chain(runnable->subshell, indentation + 1);
        print_indentation(indentation);
        printf(")\n");
    }
}

static void print_pipe_chain(pipe_chain_node_t const *chain,
                             int indentation)
{
    runnable_node_t const *runnable = &chain->first;
    int const children_indentation =
        chain->cmd_cnt > 0 ? indentation + 1 : indentation;
    for (pipe_node_t *pp = chain->chain; pp; pp = pp->next) {
        print_runnable(runnable, children_indentation);
        print_indentation(indentation);
        printf("|\n");
        runnable = &pp->runnable;
    }
    print_runnable(runnable, children_indentation);
    if (string_is_valid(&chain->stdin_redir)) {
        print_indentation(indentation);
        printf("stdin -> %.*s\n", STR_PRINTF_ARGS(chain->stdin_redir));
    }
    if (string_is_valid(&chain->stdout_redir)) {
        print_indentation(indentation);
        printf("stdout -> %.*s\n", STR_PRINTF_ARGS(chain->stdout_redir));
    } else if (string_is_valid(&chain->stdout_append_redir)) {
        print_indentation(indentation);
        printf(
            "stdout -> append to %.*s\n",
            STR_PRINTF_ARGS(chain->stdout_append_redir));
    }
}

static void print_cond_chain(cond_chain_node_t const *chain,
                             int indentation)
{
    pipe_chain_node_t const *pp = &chain->first;
    int const children_indentation =
        chain->cond_cnt > 0 ? indentation + 1 : indentation;
    for (cond_node_t *cond = chain->chain; cond; cond = cond->next) {
        print_pipe_chain(pp, children_indentation);
        print_indentation(indentation);
        printf("%s\n", cond->link == e_cl_if_success ? "&&" : "||");
        pp = &cond->pp;
    }
    print_pipe_chain(pp, children_indentation);
}

static void print_uncond_chain(uncond_chain_node_t const *chain,
                               int indentation)
{
    cond_chain_node_t const *cond = &chain->first;
    int const children_indentation =
        chain->uncond_cnt > 0 ? indentation + 1 : indentation;
    for (uncond_node_t *uncond = chain->chain; uncond; uncond = uncond->next) {
        print_cond_chain(cond, children_indentation);
        print_indentation(indentation);
        printf("%s\n", uncond->link == e_ul_bg ? "&" : ";");
        cond = &uncond->cond;
    }
    print_cond_chain(cond, children_indentation);
}

static b32 command_is_cd(command_node_t const *cmd)
{
    string_t const cdstr = LITSTR("cd");
    return str_eq(cmd->cmd, cdstr);
}

enum {
    c_not_cd = 0,
    c_is_cd = 1,
    c_invalid_cd = 2,
};

// cd can not be part of a pipe, must have 0 or 1 args & cant have io redir
static int check_if_pipe_is_cd(pipe_chain_node_t const *pp)
{
    for (pipe_node_t *elem = pp->chain; elem; elem = elem->next) {
        if (elem->runnable.type == e_rnt_cmd &&
            command_is_cd(pp->first.cmd))
        {
            // Can't have complex pipe w/ cd
            return c_invalid_cd;
        }
    }

    if (pp->first.type != e_rnt_cmd || !command_is_cd(pp->first.cmd))
        return c_not_cd;

    if (pp->cmd_cnt > 0)
        return c_invalid_cd;

    if (string_is_valid(&pp->stdin_redir) ||
        string_is_valid(&pp->stdout_redir) ||
        string_is_valid(&pp->stdout_append_redir))
    {
        return c_invalid_cd;
    }

    if (pp->first.cmd->arg_cnt > 1)
        return c_invalid_cd;

    return c_is_cd;
}

static token_t parse_uncond_chain(lexer_t *, uncond_chain_node_t *, arena_t *);

static token_t parse_runnable(
    lexer_t *lexer,
    runnable_node_t *out_runnable,
    string_t *out_stdin_redir,
    string_t *out_stdout_redir,
    string_t *out_stdout_append_redir,
    arena_t *arena)
{
    token_t tok = {0};

    enum {
        e_st_init,
        e_st_parsing_args,
        e_st_parsed_subshell,
    } state = e_st_init;

    CLEAR(out_runnable);    
    arg_node_t *last_arg = NULL;

    while (tok_is_cmd_elem_or_lparen(tok = get_next_token(lexer, arena))) {
        if (tok.type == e_tt_in) {
            if (state == e_st_init) {
                tok.type = e_tt_parser_error; // @TODO: elaborate
                break; 
            }
            if (string_is_valid(out_stdin_redir)) {
                tok.type = e_tt_parser_error; // @TODO: elaborate
                break; 
            }

            token_t next = get_next_token(lexer, arena);
            if (next.type != e_tt_ident) { // @TODO: elaborate
                tok.type = e_tt_parser_error;
                break;
            } 
            *out_stdin_redir = next.id;
        } else if (tok.type == e_tt_out || tok.type == e_tt_append) {
            if (state == e_st_init) {
                tok.type = e_tt_parser_error; // @TODO: elaborate
                break; 
            }
            if (string_is_valid(out_stdout_redir) ||
                string_is_valid(out_stdout_append_redir))
            { // @TODO: elaborate
                tok.type = e_tt_parser_error;
                break; 
            }

            token_t next = get_next_token(lexer, arena);
            if (next.type != e_tt_ident) { // @TODO: elaborate
                tok.type = e_tt_parser_error;
                break;
            } 

            if (tok.type == e_tt_out)
                *out_stdout_redir = next.id;
            else
                *out_stdout_append_redir = next.id;
        } else if (tok.type == e_tt_ident) {
            if (state == e_st_parsed_subshell) {
                tok.type = e_tt_parser_error; // @TODO: elaborate
                break; 
            }

            switch (state) {
            case e_st_init:
                out_runnable->cmd = ARENA_ALLOC(arena, command_node_t);
                CLEAR(out_runnable->cmd);
                out_runnable->cmd->cmd = tok.id;
                out_runnable->type = e_rnt_cmd;
                state = e_st_parsing_args;
                break;
            case e_st_parsing_args: {
                arg_node_t *arg = ARENA_ALLOC(arena, arg_node_t);
                arg->name = tok.id;
                arg->next = NULL;
                if (out_runnable->cmd->arg_cnt == 0)
                    out_runnable->cmd->args = arg;
                else
                    last_arg->next = arg; 
                last_arg = arg;
                ++out_runnable->cmd->arg_cnt;
            } break;
            default: 
                break;
            }
        } else {
            ASSERT(tok.type == e_tt_lparen);
            if (state != e_st_init) {
                tok.type = e_tt_parser_error; // @TODO: elaborate
                break; 
            }

            out_runnable->subshell = ARENA_ALLOC(arena, uncond_chain_node_t);
            tok = parse_uncond_chain(lexer, out_runnable->subshell, arena);
            if (tok.type != e_tt_rparen) {
                tok.type = e_tt_parser_error; // @TODO: elaborate
                break; 
            }

            out_runnable->type = e_rnt_subshell;
            state = e_st_parsed_subshell;
        }
    }

    if (state == e_st_init) // @TODO: elaborate
        tok.type = e_tt_parser_error;
    else if (tok.type == e_tt_rparen) // @TODO: elaborate
        tok.type = e_tt_parser_error;

    return tok;
}

static token_t parse_pipe_chain(
    lexer_t *lexer, pipe_chain_node_t *out_pipe_chain, arena_t *arena)
{
    enum {
        e_st_init,
        e_st_parsing_pipe,
    } state = e_st_init;

    CLEAR(out_pipe_chain);    

    runnable_node_t runnable = {0};
    pipe_node_t *last_elem = NULL;

    token_t sep = {0};

    do {
        sep = parse_runnable(
            lexer, &runnable,
            &out_pipe_chain->stdin_redir,
            &out_pipe_chain->stdout_redir,
            &out_pipe_chain->stdout_append_redir,
            arena);

        if (sep.type == e_tt_parser_error)
            break;

        switch (state) {
        case e_st_init:
            out_pipe_chain->first = runnable;
            state = e_st_parsing_pipe;
            break;
        case e_st_parsing_pipe: {
            pipe_node_t *next_elem = ARENA_ALLOC(arena, pipe_node_t);
            next_elem->runnable = runnable;
            next_elem->next = NULL;
            if (out_pipe_chain->cmd_cnt == 0)
                out_pipe_chain->chain = next_elem;
            else
                last_elem->next = next_elem;
            last_elem = next_elem;
            ++out_pipe_chain->cmd_cnt;
        } break;
        }
    } while (sep.type == e_tt_pipe);

    if (state == e_st_init) // @TODO: elaborate
        sep.type = e_tt_parser_error;

    if (sep.type != e_tt_parser_error) {
        int pipe_cd_res = check_if_pipe_is_cd(out_pipe_chain);
        if (pipe_cd_res == c_is_cd) 
            out_pipe_chain->is_cd = true;
        if (pipe_cd_res == c_invalid_cd) // @TODO: elaborate
            sep.type = e_tt_parser_error;
    }

    return sep;
}

static token_t parse_cond_chain(
    lexer_t *lexer, cond_chain_node_t *out_cond_chain, arena_t *arena)
{
    CLEAR(out_cond_chain);

    enum {
        e_st_init,
        e_st_parsing_chain,
    } state = e_st_init;

    pipe_chain_node_t pp = {0};
    cond_node_t *last_cond = NULL;

    token_t sep = {0};

    do {
        cond_link_t const link =
            sep.type == e_tt_and ? e_cl_if_success : e_cl_if_failed;

        sep = parse_pipe_chain(lexer, &pp, arena);
        if (sep.type == e_tt_parser_error)
            break;

        switch (state) {
        case e_st_init:
            out_cond_chain->first = pp;
            state = e_st_parsing_chain;
            break;
        case e_st_parsing_chain: {
            cond_node_t *next_cond = ARENA_ALLOC(arena, cond_node_t);
            next_cond->pp = pp;
            next_cond->link = link;
            next_cond->next = NULL;
            if (out_cond_chain->cond_cnt == 0)
                out_cond_chain->chain = next_cond;
            else
                last_cond->next = next_cond;
            last_cond = next_cond;
            ++out_cond_chain->cond_cnt;
        } break;
        }
    } while (tok_is_cond_sep(sep));

    if (state == e_st_init) // @TODO: elaborate
        sep.type = e_tt_parser_error;

    return sep;
}

static token_t parse_uncond_chain(
    lexer_t *lexer, uncond_chain_node_t *out_uncond, arena_t *arena)
{
    CLEAR(out_uncond);

    enum {
        e_st_init,
        e_st_parsing_chain,
    } state = e_st_init;

    cond_chain_node_t cond = {0};
    uncond_node_t *last_uncond = NULL;

    token_t sep = {0};

    do {
        uncond_link_t const link =
            sep.type == e_tt_background ? e_ul_bg : e_ul_wait;

        sep = parse_cond_chain(lexer, &cond, arena);
        if (sep.type == e_tt_parser_error)
            break;

        switch (state) {
        case e_st_init:
            out_uncond->first = cond;
            state = e_st_parsing_chain;
            break;
        case e_st_parsing_chain: {
            uncond_node_t *next_uncond = ARENA_ALLOC(arena, uncond_node_t);
            next_uncond->cond = cond;
            next_uncond->link = link;
            next_uncond->next = NULL;
            if (out_uncond->uncond_cnt == 0)
                out_uncond->chain = next_uncond;
            else
                last_uncond->next = next_uncond;
            last_uncond = next_uncond;
            ++out_uncond->uncond_cnt;
        } break;
        }
    } while (!tok_is_end_of_shell(sep));

    return sep;
}

static root_node_t *parse_line(string_t line, arena_t *arena)
{
    lexer_t lexer = {line, 0};

    uncond_chain_node_t *node = ARENA_ALLOC(arena, uncond_chain_node_t);
    token_t sep = parse_uncond_chain(&lexer, node, arena);

    if (tt_is_error(sep.type)) {
        fprintf(stderr, "%s error: [unspecified error] (at char %lu)\n",
            sep.type == e_tt_lexer_error ? "Lexer" : "Parser", lexer.pos);
        return NULL;
    }

    ASSERT(sep.type == e_tt_eol);

    return node;
}

typedef int fd_pair_t[2]; 

void sigchld_handler(int sig)
{
    (void)sig;
    signal(SIGCHLD, sigchld_handler);
    while (wait4(-1, NULL, WNOHANG, NULL) > 0)
        ;
}

static void detach_group()
{
    pid_t pid = getpid();
    setpgid(pid, pid);
}

static void set_pgroup_as_term_fg()
{
    signal(SIGTTOU, SIG_IGN); // otherwise tcsetpgrp will freeze it
    tcsetpgrp(STDIN_FILENO, getpgid(getpid()));
    signal(SIGTTOU, SIG_DFL);
}

static int await_processes(pid_t *pids, int count)
{
    for (;;) {
        int status;
        int wr = waitpid(-1, &status, 0);
        ASSERT(wr > 0);
        
        if (wr == pids[count - 1]) {
            // This also collects everithing before sigchld was reinstated
            sigchld_handler(0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
        }
    }
}

static void close_fd_pair(fd_pair_t pair)
{
    if (pair[0] != STDIN_FILENO)
        close(pair[0]);
    if (pair[1] != STDOUT_FILENO)
        close(pair[1]);
    pair[0] = STDIN_FILENO;
    pair[1] = STDOUT_FILENO;
}

static void close_fd_pairs(fd_pair_t *pairs, int count)
{
    for (fd_pair_t *p = pairs; p != pairs + count; ++p)
        close_fd_pair(*p);
}

static int execute_uncond_chain(uncond_chain_node_t const *, b32, arena_t *);

static pid_t execute_runnable(
    runnable_node_t const *runnable,
    fd_pair_t *io_fd_pairs, int fd_pair_cnt,
    int proc_id, arena_t *arena)
{
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);

        if (io_fd_pairs[proc_id][0] != STDIN_FILENO)
            dup2(io_fd_pairs[proc_id][0], STDIN_FILENO);
        if (io_fd_pairs[proc_id][1] != STDOUT_FILENO)
            dup2(io_fd_pairs[proc_id][1], STDOUT_FILENO);

        close_fd_pairs(io_fd_pairs, fd_pair_cnt);

        if (runnable->type == e_rnt_cmd) {
            command_node_t const *cmd = runnable->cmd;
            char **argv = ARENA_ALLOC_N(arena, char *, cmd->arg_cnt + 2);
            argv[0] = cmd->cmd.p;
            u64 i = 1;
            for (arg_node_t *arg = cmd->args; arg; arg = arg->next)
                argv[i++] = arg->name.p;
            argv[i] = NULL;

            execvp(argv[0], argv);
            perror(argv[0]);
            _exit(1);
        } else {
            _exit(execute_uncond_chain(runnable->subshell, false, arena));
        }
    }

    close_fd_pair(io_fd_pairs[proc_id]);
    return pid;
}

static int execute_pipe_in_subprocess(
    pipe_chain_node_t const *pp, arena_t *arena)
{
    int elem_cnt = pp->cmd_cnt + 1;
    pid_t *pids = ARENA_ALLOC_N(arena, pid_t, elem_cnt);
    fd_pair_t *io_fd_pairs = ARENA_ALLOC_N(arena, fd_pair_t, elem_cnt);

    for (int i = 0; i < elem_cnt; ++i) {
        pids[i] = -1;
        io_fd_pairs[i][0] = STDIN_FILENO;
        io_fd_pairs[i][1] = STDOUT_FILENO;
    }

    if (string_is_valid(&pp->stdin_redir))
        io_fd_pairs[0][0] = open(pp->stdin_redir.p, O_RDONLY);
    if (string_is_valid(&pp->stdout_redir)) {
        io_fd_pairs[elem_cnt - 1][1] =
            open(pp->stdout_redir.p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    } else if (string_is_valid(&pp->stdout_append_redir)) {
        io_fd_pairs[elem_cnt - 1][1] =
            open(pp->stdout_append_redir.p,
                 O_WRONLY | O_CREAT | O_APPEND, 0644);
    }

    if (io_fd_pairs[0][0] < 0 || io_fd_pairs[elem_cnt - 1][1] < 0) {
        close_fd_pairs(io_fd_pairs, elem_cnt);
        return -2;
    }

    for (int i = 0; i < elem_cnt - 1; ++i) {
        fd_pair_t fds = {0};
        int res = pipe(fds);
        if (res != 0) {
            close_fd_pairs(io_fd_pairs, elem_cnt);
            return -2;
        }

        io_fd_pairs[i + 1][0] = fds[0];
        io_fd_pairs[i][1] = fds[1];
    }

    signal(SIGCHLD, SIG_DFL);

    runnable_node_t const *runnable = &pp->first;
    int launched_proc_cnt = 0;
    for (pipe_node_t *elem = pp->chain; elem; elem = elem->next) {
        int this_proc_index = launched_proc_cnt++;
        pid_t pid = execute_runnable(
            runnable, io_fd_pairs, elem_cnt, this_proc_index, arena);
        if (pid == -1) {
            close_fd_pairs(io_fd_pairs, elem_cnt);
            return -2;
        }
        
        pids[this_proc_index] = pid;
        runnable = &elem->runnable;
    }
    int this_proc_index = launched_proc_cnt++;
    pid_t pid = execute_runnable(
        runnable, io_fd_pairs, elem_cnt, this_proc_index, arena);
    if (pid == -1) {
        close_fd_pairs(io_fd_pairs, elem_cnt);
        return -2;
    }
    pids[this_proc_index] = pid;

    return await_processes(pids, launched_proc_cnt);
}

static int execute_pipe_chain(
    pipe_chain_node_t const *pp, b32 is_term, arena_t *arena)
{
    if (pp->is_cd) {
        command_node_t const *cmd = pp->first.cmd;
        char const *dir = NULL;

        string_t const homedirstr = LITSTR("~");

        if (cmd->arg_cnt == 0 || str_eq(cmd->args->name, homedirstr)) {
            if ((dir = getenv("HOME")) == NULL)
                dir = getpwuid(getuid())->pw_dir;
        } else
            dir = cmd->args->name.p;

        return chdir(dir) == 0 ? 0 : 1;
    }

    signal(SIGCHLD, SIG_DFL);

    pid_t pid = fork();
    if (pid == 0) {
        detach_group();
        if (is_term)
            set_pgroup_as_term_fg();

        _exit(execute_pipe_in_subprocess(pp, arena));
    } else if (pid == -1) {
        sigchld_handler(0);
        return -1;
    }

    int res = await_processes(&pid, 1);

    if (is_term)
        set_pgroup_as_term_fg();
    return res;
}

static int execute_cond_chain(
    cond_chain_node_t const *chain, b32 is_term, arena_t *arena)
{
    pipe_chain_node_t const *pp = &chain->first;
    for (cond_node_t *cond = chain->chain; cond; cond = cond->next) {
        int res = execute_pipe_chain(pp, is_term, arena);

        if (res == 0 && cond->link == e_cl_if_failed)
            return res;
        else if (res != 0 && cond->link == e_cl_if_success)
            return res;

        pp = &cond->pp;
    }
    return execute_pipe_chain(pp, is_term, arena);
}

static int execute_uncond_chain(
    uncond_chain_node_t const *chain, b32 is_term, arena_t *arena)
{
    cond_chain_node_t const *cond = &chain->first;
    for (uncond_node_t *uncond = chain->chain; uncond; uncond = uncond->next) {
        if (uncond->link == e_ul_bg) {
            pid_t pid = fork();
            if (pid == -1)
                return -2;
            if (pid == 0) {
                detach_group();

                _exit(execute_cond_chain(cond, false, arena));
            }
        } else
            execute_cond_chain(cond, is_term, arena);
        cond = &uncond->cond;
    }
    return execute_cond_chain(cond, is_term, arena);
}

static int execute_line(root_node_t const *ast, b32 is_term, arena_t *arena)
{
    return execute_uncond_chain(ast, is_term, arena);
}

int main(int argc, char **argv)
{
    enum {
        c_program_mem_size = 1024 * 1024,

        c_persistent_mem_size = c_program_mem_size / 4,
        c_line_mem_size = c_program_mem_size / 2,
        c_temp_mem_size = c_program_mem_size / 4,

        c_line_buf_size = 1024
    };

    b32 execute = true;
    b32 print_ast = false;
    b32 disable_term = false;

    string_t const only_parse_arg = LITSTR("--parser-only");
    string_t const print_ast_arg = LITSTR("--print-ast");
    string_t const disable_term_arg = LITSTR("--no-term-input");

    for (int i = 1; i < argc; ++i) {
        string_t arg = str_from_cstr(argv[i]);

        if (str_eq(arg, only_parse_arg)) {
            execute = false;
            print_ast = true;
        } else if (str_eq(arg, print_ast_arg)) {
            print_ast = true;
        } else if (str_eq(arg, disable_term_arg)) {
            disable_term = true;
        } else {
            fprintf(stderr, "Invalid arg: %s\n", argv[i]);
            return 1;
        }
    }

    int res = 0;

    buffer_t memory = allocate_buffer(c_program_mem_size);
    arena_t persistent_arena = {{
        memory.p,
        c_persistent_mem_size
    }, 0};
    arena_t line_arena = {{
        memory.p + c_persistent_mem_size,
        c_line_mem_size
    }, 0};
    arena_t temp_arena = {{
        memory.p + c_persistent_mem_size + c_line_mem_size,
        c_temp_mem_size
    }, 0};

    b32 const is_term =
        isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && !disable_term;
    terminal_session_t term = {0};

    if (is_term) {
        term.persmem = &persistent_arena;
        term.tmpmem = &temp_arena;
        init_term(&term);
    }

    signal(SIGCHLD, sigchld_handler);

    int read_res;

    for (;;) {
        buffer_t line_storage = {
            ARENA_ALLOC_N(&line_arena, char, c_line_buf_size),
            c_line_buf_size
        };
        string_t line = {0};

        if (is_term)
            read_res = read_line_from_terminal(&line_storage, &line, &term);
        else
            read_res = read_line_from_regular_stdin(&line_storage, &line);

        if (read_res == c_rl_string_overflow) {
            fprintf(
                stderr,
                "The line is over the limit of %d charactes "
                "and will not be processed\n",
                c_line_buf_size);
            goto loop_end;
        } else if (read_res == c_rl_eof)
            break;
        else if (line.len == 0)
            goto loop_end;

        root_node_t *ast_root = parse_line(line, &line_arena);
        if (!ast_root)
            goto loop_end;

        if (print_ast)
            print_uncond_chain(ast_root, 0);

        if (execute) {
            int retcode = execute_line(ast_root, is_term, &line_arena);
            if (retcode != 0) {
                fprintf(stderr,
                    "Interpreter error: failed w/ code %d\n", retcode);
            }
        }

    loop_end:
        arena_drop(&line_arena);
    }

    pid_t awaited;
    while ((awaited = waitpid(-1, NULL, 0)) > 0)
        ;

    free_buffer(&memory);

    if (is_term)
        shutdown_term(&term, read_res != c_rl_eof);

    return res;
}
