/* Toy-Shell/main.c */
#include "def.h"
#include "buffer.h"
#include "str.h"
#include "debug.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
#include <limits.h>
#include <stdalign.h>
#include <stdio.h>

// @TODO: handle allocation failures somehow

static inline void mem_set(u8 *mem, u64 sz)
{
    for (u8 *end = mem + sz; mem != end; ++mem)
        *mem = 0;
}

#define CLEAR(addr_) mem_set((u8 *)(addr_), sizeof(*(addr_)))

static inline b32 cstr_contains(char const *str, char c)
{
    for (; *str; ++str) {
        if (*str == c)
            return true;
    }
    return false;
}

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

static int read_line_from_stream(FILE *f, buffer_t *buf, string_t *out_string)
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
        ASSERT(s.len == 0);
        return c_rl_eof;
    }

    s.p[s.len] = '\0';
    *out_string = s;
    return c_rl_ok;
}

static void consume_input(FILE *f)
{
    int c;
    while (!is_eol(c = getchar()))
        ;
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
                (is_whitespace(c) || cstr_contains("<>|&;()", c)))
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
// background
// In/out/append
// pipe
// chaining
// subshells
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
        printf("stdout -> append to %.*s\n",
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

string_t const cdstr = LITSTR("cd");
string_t const homedirstr = LITSTR("~");

static b32 command_is_cd(command_node_t const *cmd)
{
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

    int pipe_cd_res = check_if_pipe_is_cd(out_pipe_chain);
    if (pipe_cd_res == c_is_cd) 
        out_pipe_chain->is_cd = true;
    if (pipe_cd_res == c_invalid_cd) // @TODO: elaborate
        sep.type = e_tt_error;

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
        fprintf(stderr,
                "Parser or lexer error: [unspecified error] (at char %lu)\n",
                lexer.pos);
        return NULL;
    }

    ASSERT(sep.type == e_tt_eol);

    return node;
}

// @TODO (Interpreter):
// In/out/append
// pipe
// chaining
// subshells
// proper error reporting, ASSERTs of cmd format

void sigchld_handler(int)
{
    signal(SIGCHLD, sigchld_handler);
    while (wait4(-1, NULL, WNOHANG, NULL) > 0)
        ;
}

static int execute_uncond_chain(uncond_chain_node_t const *, arena_t *);

typedef int fd_pair_t[2]; 

static int await_processes(int *pids, int count)
{
    for (;;) {
        int status;
        int wr = wait(&status);
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

static int execute_runnable(
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

        if (cmd->arg_cnt == 0 || str_eq(cmd->args->name, homedirstr)) {
            if ((dir = getenv("HOME")) == NULL)
                dir = getpwuid(getuid())->pw_dir;
        } else
            dir = cmd->args->name.p;

        return chdir(dir) == 0 ? 0 : 1;
    }

    int elem_cnt = pp->cmd_cnt + 1;
    int *pids = ARENA_ALLOC_N(arena, int, elem_cnt);
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
        int pid = execute_runnable(
            runnable, io_fd_pairs, elem_cnt, this_proc_index, arena);
        if (pid == -1) {
            close_fd_pairs(io_fd_pairs, elem_cnt);
            return -2;
        }
        
        pids[this_proc_index] = pid;
        runnable = &elem->runnable;
    }
    int this_proc_index = launched_proc_cnt++;
    int pid = execute_runnable(
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
            if (pid == 0)
                _exit(execute_cond_chain(cond, arena));
        } else
            execute_cond_chain(cond, arena);
        cond = &uncond->cond;
    }
    return execute_cond_chain(cond, arena);
}

int main()
{
    enum {
        c_program_mem_size = 1024 * 1024,
        c_line_buf_size = 1024,
        c_parser_mem_size = (c_program_mem_size / 2) - c_line_buf_size,
        c_interpreter_mem_size = c_program_mem_size / 2
    };

    signal(SIGCHLD, sigchld_handler);

    int const is_term = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    int res = 0;

    buffer_t memory = allocate_buffer(c_program_mem_size);
    buffer_t line_storage = {memory.p, c_line_buf_size};
    arena_t parser_arena = {{memory.p + c_line_buf_size, c_parser_mem_size}};
    arena_t inerpreter_arena = {{
        memory.p + c_line_buf_size + c_parser_mem_size,
        c_interpreter_mem_size
    }};

    for (;;) {
        if (is_term)
            printf("> ");

        string_t line = {};

        int read_res = read_line_from_stream(stdin, &line_storage, &line);
        if (read_res == c_rl_string_overflow) {
            fprintf(stderr,
                    "The line is over the limit of %d charactes "
                    "and will not be processed\n",
                    c_line_buf_size);
            res = 1;
            break;
        } else if (read_res == c_rl_eof)
            break;
        else if (line.len == 0)
            goto loop_end;

        root_node_t *ast_root = parse_line(line, &parser_arena);
        if (!ast_root)
            goto loop_end;

        print_uncond_chain(ast_root, 0);

        int retcode = execute_uncond_chain(ast_root, &inerpreter_arena);
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
        consume_input(stdin);

    return res;
}
