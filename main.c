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
#include <limits.h>
#include <stdalign.h>
#include <stdio.h>

#define MIN(a_, b_) ((a_) < (b_) ? (a_) : (b_))
#define MAX(a_, b_) ((a_) > (b_) ? (a_) : (b_))

// @TODO (all): (smoke)tests

// @TODO: handle allocation failures somehow

static inline void mem_set(u8 *mem, u64 sz)
{
    for (u8 *end = mem + sz; mem != end; ++mem)
        *mem = 0;
}

static inline void mem_cpy(u8 *to, u8 *from, u64 sz)
{
    for (u8 *end = to + sz; to != end; ++to, ++from)
        *to = *from;
}

static inline void mem_cpy_bw(u8 *to, u8 *from, u64 sz)
{
    for (u8 *from_end = from + sz - 1, *to_end = to + sz - 1;
        to_end != to - 1; --from_end, --to_end)
    {
        *to_end = *from_end;
    }
}

#define CLEAR(addr_) mem_set((u8 *)(addr_), sizeof(*(addr_)))

typedef struct arena_tag {
    buffer_t buf;
    u64 allocated;
} arena_t;

static u8 *arena_allocate(arena_t *arena, u64 bytes)
{
    ASSERT(buffer_is_valid(&arena->buf));
    if (arena->allocated + bytes > arena->buf.sz)
        return NULL;
    u8 *ptr = (u8 *)arena->buf.p + arena->allocated;
    arena->allocated += bytes;
    return ptr;
}

static u8 *arena_allocate_aligned(arena_t *arena, u64 bytes, u64 alignment)
{ 
    // malloc alignment must be enough (since arena itself is mallocd)
    ASSERT(alignment <= 16 && 16 % alignment == 0);
    ASSERT(buffer_is_valid(&arena->buf));
    u64 const required_start =
        (arena->allocated + alignment - 1) & ~(alignment - 1);

    if (required_start + bytes > arena->buf.sz)
        return NULL;
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
    return c == '\n' || c == EOF;
}

static inline b32 is_whitespace(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static inline b32 is_separator_char(int c)
{
    return
        c == '|' || c == '&' || c == '>' || c == '<' ||
        c == ';' || c == ')' || c == '(';
}

static inline b32 is_ws_or_sep(int c)
{
    return is_whitespace(c) || is_separator_char(c);
}

typedef struct terminal_input_tag {
    struct termios backup_ts;
    struct winsize wsz; // @NOTE: does not support resize while editing one line
    char input_buf[64];
    u32 buffered_chars_cnt;
} terminal_input_t;

static void init_term(terminal_input_t *term)
{
    tcgetattr(STDIN_FILENO, &term->backup_ts);
}

static void shutdown_term(terminal_input_t *term, bool drain)
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

static void start_terminal_editing(terminal_input_t *term)
{
    struct termios ts;
    mem_cpy((u8 *)&ts, (u8 *)&term->backup_ts, sizeof(struct termios));    

    ts.c_lflag &= ~(ICANON | IEXTEN | ISIG | ECHO);
    ts.c_cc[VMIN] = 1;
    ts.c_cc[VTIME] = 0;
    ts.c_cc[VEOF] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &ts);

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term->wsz);
}

static void finish_terminal_editing(terminal_input_t *term)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &term->backup_ts);
}

static void move_cursor_to_pos(int from, int to, terminal_input_t const *term)
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

// @TODO (line):
// Autocomplete: search fs
// Autocomplete: for first word w/out slashes look in PATH instead

static int read_line_from_terminal(
    buffer_t *buf, string_t *out_string, terminal_input_t *term)
{
    start_terminal_editing(term);

    printf("> ");
    fflush(stdout);

    int res = c_rl_ok;

    ASSERT(buffer_is_valid(buf));
    ASSERT(buf->sz > 0);

    string_t s = {buf->p, 0};
    int epos = 0;

    bool done = false;

    enum {
        e_st_dfl,
        e_st_parsed_esc,
        e_st_ready_for_arrow
    } state = e_st_dfl;

    while (!done) {
        char *p;
        int chars_consumed;
        int prev_len = s.len;
        int prev_epos = epos;

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
                    if (epos < s.len)
                        ++epos;
                    state = e_st_dfl;
                    continue;
                case 68:
                    if (epos > 0)
                        --epos;
                    state = e_st_dfl;
                    continue;
                default:
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
                    bool skipping_ws = true;
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
                if (epos < s.len) {
                    mem_cpy(
                        (u8 *)(s.p + epos - chars_to_delete),
                        (u8 *)(s.p + epos),
                        s.len - epos);
                }
                epos -= chars_to_delete;
                s.len -= chars_to_delete;
            } break;
            case '\t':
                // @TODO: if in token, look up
                if (s.len < buf->sz - 1) {
                    int chars_to_put = MIN(4, buf->sz - 1 - s.len);
                    if (epos < s.len) {
                        mem_cpy_bw(
                            (u8 *)(s.p + epos + chars_to_put),
                            (u8 *)(s.p + epos),
                            s.len - epos);
                    }
                    s.len += chars_to_put;
                    for (; chars_to_put; --chars_to_put)
                        s.p[epos++] = ' ';
                }
                break;

            default:
                if (*p < 32) // no control characters
                    break;
                if (s.len < buf->sz - 1) {
                    if (epos < s.len) {
                        mem_cpy_bw(
                            (u8 *)(s.p + epos + 1), (u8 *)(s.p + epos),
                            s.len - epos);
                    }
                    s.p[epos++] = *p;
                    ++s.len;
                }
            }

            state = e_st_dfl;
        }

    loop_end:
        chars_consumed = p - term->input_buf;
        term->buffered_chars_cnt -= chars_consumed;
        if (chars_consumed < term->buffered_chars_cnt)
            mem_cpy((u8 *)term->input_buf, (u8 *)p, term->buffered_chars_cnt);

        move_cursor_to_pos(prev_epos + 2, 0, term);
        int chars_printed = printf("> ");
        for (int i = 0; i < MAX(s.len, prev_len); ++i) {
            putchar(i < s.len ? s.p[i] : ' ');
            if ((++chars_printed) % term->wsz.ws_col == 0)
                putchar('\n');
        }
        if (epos < MAX(s.len, prev_len))
            move_cursor_to_pos(MAX(s.len, prev_len) + 2, epos + 2, term);
        if (done)
            putchar('\n');
        fflush(stdout);
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

    // @TODO: error types
    e_tt_error = -1
} token_type_t;

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

static token_t get_next_token(lexer_t *lexer, arena_t *symbols_arena)
{
    token_t tok = {};

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

            if (c == '\\' && !screen_next) {
                screen_next = true;
                continue;
            }

            if (c == '"' && !screen_next) {
                in_quotes = !in_quotes;
                continue;
            }

            if (!string_is_valid(&tok.id))
                tok.id.p = (char *)arena_allocate(symbols_arena, 0);

            ASSERT(tok.id.p + tok.id.len - symbols_arena->buf.p <
                   symbols_arena->buf.sz);

            ++symbols_arena->allocated;
            
            ASSERT(c >= SCHAR_MIN && c <= SCHAR_MAX);
            tok.id.p[tok.id.len++] = (char)c;

            screen_next = false;
        }
    }

    if (in_quotes || screen_next) {
        tok.type = e_tt_error;
        return tok;
    }

    if (state == e_lst_prefix_separator)
        tok.type = e_tt_eol;
    else {
        // To make linux syscalls happy
        ++symbols_arena->allocated;
        tok.id.p[tok.id.len] = '\0';

        tok.type = e_tt_ident;
    }

    return tok;
}

static inline b32 tok_is_valid(token_t tok)
{
    return tok.type != e_tt_uninit && tok.type != e_tt_error;
}

static inline b32 tok_is_end_of_shell(token_t tok)
{
    return tok.type == e_tt_eol || tok.type == e_tt_rparen ||
           !tok_is_valid(tok);
}

static inline b32 tok_is_cmd_elem_or_lparen(token_t tok)
{
    return
        tok.type == e_tt_ident || tok.type == e_tt_in ||
        tok.type == e_tt_out || tok.type == e_tt_append ||
        tok.type == e_tt_lparen;
}

static inline b32 tok_is_cond_sep(token_t tok)
{
    return tok.type == e_tt_and || tok.type == e_tt_or;
}

// @TODO (parser):
// proper error reporting
// tighter validation

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
    // @TODO: compress/alias?

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

// @TODO: ASSERT called functions return correct type subset token

static b32 command_is_cd(command_node_t const *cmd)
{
    string_t const cdstr = LITSTR("cd");
    return str_eq(cmd->cmd, cdstr);
}

enum {
    c_not_cd,
    c_is_cd,
    c_invalid_cd,
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
    token_t tok = {};

    enum {
        e_st_init,
        e_st_parsing_args,
        e_st_parsed_subshell,
    } state = e_st_init;

    CLEAR(out_runnable);    
    arg_node_t *last_arg = NULL;

    while (tok_is_cmd_elem_or_lparen(tok = get_next_token(lexer, arena))) {
        // @TODO: ASSERT stuff
        if (tok.type == e_tt_in) {
            if (state == e_st_init) {
                tok.type = e_tt_error; // @TODO: elaborate
                break; 
            }
            if (string_is_valid(out_stdin_redir)) {
                tok.type = e_tt_error; // @TODO: elaborate
                break; 
            }

            token_t next = get_next_token(lexer, arena);
            if (next.type != e_tt_ident) { // @TODO: elaborate
                tok.type = e_tt_error;
                break;
            } 
            *out_stdin_redir = next.id;
        } else if (tok.type == e_tt_out || tok.type == e_tt_append) {
            if (state == e_st_init) {
                tok.type = e_tt_error; // @TODO: elaborate
                break; 
            }
            if (string_is_valid(out_stdout_redir) ||
                string_is_valid(out_stdout_append_redir))
            { // @TODO: elaborate
                tok.type = e_tt_error;
                break; 
            }

            token_t next = get_next_token(lexer, arena);
            if (next.type != e_tt_ident) { // @TODO: elaborate
                tok.type = e_tt_error;
                break;
            } 

            if (tok.type == e_tt_out)
                *out_stdout_redir = next.id;
            else
                *out_stdout_append_redir = next.id;
        } else if (tok.type == e_tt_ident) {
            if (state == e_st_parsed_subshell) {
                tok.type = e_tt_error; // @TODO: elaborate
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
            }
        } else {
            assert(tok.type == e_tt_lparen);
            if (state != e_st_init) {
                tok.type = e_tt_error; // @TODO: elaborate
                break; 
            }

            out_runnable->subshell = ARENA_ALLOC(arena, uncond_chain_node_t);
            tok = parse_uncond_chain(lexer, out_runnable->subshell, arena);
            if (tok.type != e_tt_rparen) {
                tok.type = e_tt_error; // @TODO: elaborate
                break; 
            }

            out_runnable->type = e_rnt_subshell;
            state = e_st_parsed_subshell;
        }
    }

    if (state == e_st_init) // @TODO: elaborate
        tok.type = e_tt_error;
    else if (tok.type == e_tt_rparen) // @TODO: elaborate
        tok.type = e_tt_error;

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

    runnable_node_t runnable = {};
    pipe_node_t *last_elem = NULL;

    token_t sep = {};

    do {
        sep = parse_runnable(
            lexer, &runnable,
            &out_pipe_chain->stdin_redir,
            &out_pipe_chain->stdout_redir,
            &out_pipe_chain->stdout_append_redir,
            arena);

        if (sep.type == e_tt_error)
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
        sep.type = e_tt_error;

    if (sep.type != e_tt_error) {
        int pipe_cd_res = check_if_pipe_is_cd(out_pipe_chain);
        if (pipe_cd_res == c_is_cd) 
            out_pipe_chain->is_cd = true;
        if (pipe_cd_res == c_invalid_cd) // @TODO: elaborate
            sep.type = e_tt_error;
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

    pipe_chain_node_t pp = {};
    cond_node_t *last_cond = NULL;

    token_t sep = {};

    do {
        cond_link_t const link =
            sep.type == e_tt_and ? e_cl_if_success : e_cl_if_failed;

        sep = parse_pipe_chain(lexer, &pp, arena);
        if (sep.type == e_tt_error)
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
        sep.type = e_tt_error;

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

    cond_chain_node_t cond = {};
    uncond_node_t *last_uncond = NULL;

    token_t sep = {};

    do {
        uncond_link_t const link =
            sep.type == e_tt_background ? e_ul_bg : e_ul_wait;

        sep = parse_cond_chain(lexer, &cond, arena);
        if (sep.type == e_tt_error)
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

    if (sep.type == e_tt_error) {
        fprintf(
            stderr,
            "Parser or lexer error: [unspecified error] (at char %lu)\n",
            lexer.pos);
        return NULL;
    }

    ASSERT(sep.type == e_tt_eol);

    return node;
}

// @TODO (Interpreter):
// proper error reporting, ASSERTs of cmd format
// test suite

typedef int fd_pair_t[2]; 

void sigchld_handler(int)
{
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

static int execute_uncond_chain(uncond_chain_node_t const *, arena_t *);

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
            _exit(execute_uncond_chain(runnable->subshell, arena));
        }
    }

    close_fd_pair(io_fd_pairs[proc_id]);
    return pid;
}

static int execute_pipe_chain(pipe_chain_node_t const *pp, arena_t *arena)
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
        fd_pair_t fds = {};
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

static int execute_cond_chain(cond_chain_node_t const *chain, arena_t *arena)
{
    pipe_chain_node_t const *pp = &chain->first;
    for (cond_node_t *cond = chain->chain; cond; cond = cond->next) {
        int res = execute_pipe_chain(pp, arena);

        if (res == 0 && cond->link == e_cl_if_failed)
            return res;
        else if (res != 0 && cond->link == e_cl_if_success)
            return res;

        pp = &cond->pp;
    }
    return execute_pipe_chain(pp, arena);
}

static int execute_uncond_chain(uncond_chain_node_t const *chain,
                                arena_t *arena)
{
    cond_chain_node_t const *cond = &chain->first;
    for (uncond_node_t *uncond = chain->chain; uncond; uncond = uncond->next) {
        if (uncond->link == e_ul_bg) {
            pid_t pid = fork();
            if (pid == -1)
                return -2;
            if (pid == 0) {
                detach_group();

                _exit(execute_cond_chain(cond, arena));
            }
        } else
            execute_cond_chain(cond, arena);
        cond = &uncond->cond;
    }
    return execute_cond_chain(cond, arena);
}

static int execute_line(root_node_t const *ast, bool is_term, arena_t *arena)
{
    pid_t pid = fork();
    if (pid == 0) {
        detach_group();
        if (is_term)
            set_pgroup_as_term_fg();

        _exit(execute_uncond_chain(ast, arena));
    }

    signal(SIGCHLD, SIG_DFL);

    int res = -1;
    if (pid > 0) {
        int status;
        int wr = waitpid(-1, &status, 0);
        ASSERT(wr == pid);
        if (WIFEXITED(status))
            res = WEXITSTATUS(status);
    }

    if (is_term)
        set_pgroup_as_term_fg();

    sigchld_handler(0);

    return res;
}

int main()
{
    enum {
        c_program_mem_size = 1024 * 1024,
        c_line_buf_size = 1024,
        c_parser_mem_size = (c_program_mem_size / 2) - c_line_buf_size,
        c_interpreter_mem_size = c_program_mem_size / 2
    };

    int res = 0;

    buffer_t memory = allocate_buffer(c_program_mem_size);
    buffer_t line_storage = {memory.p, c_line_buf_size};
    arena_t parser_arena = {{memory.p + c_line_buf_size, c_parser_mem_size}};
    arena_t inerpreter_arena = {{
        memory.p + c_line_buf_size + c_parser_mem_size,
        c_interpreter_mem_size
    }};

    int const is_term = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    terminal_input_t term = {};

    int read_res;

    if (is_term)
        init_term(&term);

    signal(SIGCHLD, sigchld_handler);


    for (;;) {
        string_t line = {};

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

        root_node_t *ast_root = parse_line(line, &parser_arena);
        if (!ast_root)
            goto loop_end;

        print_uncond_chain(ast_root, 0);

        int retcode = execute_line(ast_root, is_term, &inerpreter_arena);
        if (retcode != 0)
            fprintf(stderr, "Interpreter error: failed w/ code %d\n", retcode);

    loop_end:
        arena_drop(&parser_arena);
        arena_drop(&inerpreter_arena);
    }

    pid_t awaited;
    while ((awaited = waitpid(-1, NULL, 0)) > 0)
        ;

    free_buffer(&memory);

    if (is_term)
        shutdown_term(&term, read_res != c_rl_eof);

    return res;
}
