/* Toy-Shell/main.c */
#include "def.h"
#include "buffer.h"
#include "str.h"
#include "debug.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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

static inline bool cstr_contains(char const *str, char c)
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
    assert(buffer_is_valid(&arena->buf));
    if (arena->allocated + bytes > arena->buf.sz)
        return NULL;
    u8 *ptr = (u8 *)arena->buf.p + arena->allocated;
    arena->allocated += bytes;
    return ptr;
}

static u8 *arena_allocate_aligned(arena_t *arena, u64 bytes, u64 alignment)
{ 
    // malloc alignment must be enough (since arena itself is mallocd)
    assert(alignment <= 16 && 16 % alignment == 0);
    assert(buffer_is_valid(&arena->buf));
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

enum readline_res_t {
    e_rl_ok = 0,
    e_rl_string_overflow = -1,
    e_rl_eof = -2,
};

static inline bool is_eol(int c)
{
    return c == '\n' || c == EOF;
}

static inline bool is_whitespace(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int read_line_from_stream(FILE *f, buffer_t *buf, string_t *out_string)
{
    assert(buffer_is_valid(buf));
    assert(buf->sz > 0);

    string_t s = {buf->p, 0};

    int c;
    while (!is_eol(c = getchar())) {
        s.p[s.len++] = (char)c;
        if (s.len >= buf->sz)
            return e_rl_string_overflow;
    }

    if (c == EOF) {
        assert(s.len == 0);
        return e_rl_eof;
    }

    s.p[s.len] = '\0';
    *out_string = s;
    return e_rl_ok;
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
    assert(lexer->pos < lexer->line.len);
    ++lexer->pos;
}

static token_t get_next_token(lexer_t *lexer, arena_t *symbols_arena)
{
    token_t tok = {};

    enum {
        e_lst_prefix_separator,
        e_lst_parsing_identifier
    } state = e_lst_prefix_separator;

    bool in_quotes = false;
    bool screen_next = false;
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

            if (string_is_empty(&tok.id))
                tok.id.p = (char *)arena_allocate(symbols_arena, 0);

            assert(tok.id.p + tok.id.len - symbols_arena->buf.p <
                   symbols_arena->buf.sz);

            ++symbols_arena->allocated;
            
            assert(c >= SCHAR_MIN && c <= SCHAR_MAX);
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

static inline bool tok_is_valid(token_t tok)
{
    return tok.type != e_tt_uninit && tok.type != e_tt_error;
}

static inline bool tok_is_terminating(token_t tok)
{
    return tok.type == e_tt_eol || !tok_is_valid(tok);
}

static inline bool tok_is_command_sep(token_t tok)
{
    return tok_is_terminating(tok) || tok.type == e_tt_pipe ||
           tok.type == e_tt_and || tok.type == e_tt_or ||
           tok.type == e_tt_semicolon || tok.type == e_tt_background ||
           tok.type == e_tt_lparen || tok.type == e_tt_rparen;
}

// @TODO (parser):
// background
// In/out/append
// pipe
// chaining
// subshells
// proper error reporting

typedef enum node_type_tag {
    e_nd_notinit = 0,

    e_nd_command,
} node_type_t;

typedef struct arg_node_tag {
    string_t name;
    struct arg_node_tag *next;
} arg_node_t;

typedef struct command_node_tag {
    string_t cmd;
    arg_node_t *args;    
    u64 arg_cnt;
} command_node_t;

typedef struct node_tag {
    node_type_t type;
    union {
        command_node_t cmd;
    };
} node_t;

token_t parse_command(lexer_t *lexer, command_node_t *out_cmd, arena_t *arena)
{
    token_t tok = {};

    enum {
        e_cst_init,
        e_cst_parsing_args,
    } state = e_cst_init;

    CLEAR(out_cmd);    
    arg_node_t *last_arg = NULL;

    while (!tok_is_command_sep(tok = get_next_token(lexer, arena))) {
        if (tok.type != e_tt_ident) {
            fprintf(stderr,
                    "Parser error: in/out/append not implemented yet "
                    "(at char %lu)\n",
                    lexer->pos);
            tok.type = e_tt_error;
            break;
        }

        // @TODO: assert stuff

        switch (state) {
        case e_cst_init:
            out_cmd->cmd = tok.id;
            state = e_cst_parsing_args;
            break;
        case e_cst_parsing_args: {
            arg_node_t *arg = ARENA_ALLOC(arena, arg_node_t);
            arg->name = tok.id;
            arg->next = NULL;
            if (out_cmd->arg_cnt == 0)
                out_cmd->args = arg;
            else
                last_arg->next = arg; 
            last_arg = arg;
            ++out_cmd->arg_cnt;
        } break;
        default: 
        }
    }

    if (state == e_cst_init) // @TODO: elaborate
        tok.type = e_tt_error;

    return tok;
}

node_t *parse_line(string_t line, arena_t *arena)
{
    lexer_t lexer = {line, 0};

    node_t *node = ARENA_ALLOC(arena, node_t);

    node->type = e_nd_command;
    token_t endtok = parse_command(&lexer, &node->cmd, arena); 

    if (endtok.type == e_tt_error) {
        fprintf(stderr,
                "Parser or lexer error: [unspecified error] (at char %lu)\n",
                lexer.pos);
        return NULL;
    } else if (endtok.type != e_tt_eol) {
        fprintf(stderr,
                "Parser error: encountered uniplemented token #%d "
                "(at char %lu)\n", endtok.type, lexer.pos);
        return NULL;
    }

    return node;
}

#if 0
static void print_indentation(int indentation)
{
    for (int i = 0; i < indentation; ++i)
        printf("    ");
}

static void print_command(node_t *node, int indentation)
{
    assert(node->type == e_nd_command);
    command_node_t *cmd = &node->cmd;
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
#endif

// @TODO (Interpreter):
// background
// In/out/append
// pipe
// chaining
// subshells
// proper error reporting

static int execute_command(node_t *node, arena_t *arena)
{
    assert(node->type == e_nd_command);
    command_node_t *cmd = &node->cmd;

    const string_t cdstr = LITSTR("cd");
    const string_t homedirstr = LITSTR("~");

    if (str_eq(cmd->cmd, cdstr)) {
        if (cmd->arg_cnt > 1)
            return -1;
    
        char const *dir = NULL;

        if (cmd->arg_cnt == 0 || str_eq(cmd->args->name, homedirstr)) {
            if ((dir = getenv("HOME")) == NULL)
                dir = getpwuid(getuid())->pw_dir;
        } else
            dir = cmd->args->name.p;

        if (chdir(dir) != 0)
            return 1;
    } else {
        char **argv = ARENA_ALLOC_N(arena, char *, cmd->arg_cnt + 2);
        argv[0] = cmd->cmd.p;
        u64 i = 1;
        for (arg_node_t *arg = cmd->args; arg; arg = arg->next)
            argv[i++] = arg->name.p;
        argv[i] = NULL;

        pid_t pid = fork();
        if (pid == 0) {
            int ret = execvp(argv[0], argv);
            exit(ret);
        } else if (pid == -1)
            return -2;

        int status = 0;
        pid_t awaited = waitpid(-1, &status, 0);
        if (awaited != pid || !WIFEXITED(status))
            return -2;

        return WEXITSTATUS(status);
    }

    return 0;
}

int main()
{
    enum {
        c_program_mem_size = 1024 * 1024,
        c_line_buf_size = 1024,
        c_parser_mem_size = (c_program_mem_size / 2) - c_line_buf_size,
        c_interpreter_mem_size = c_program_mem_size / 2
    };

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
        if (read_res == e_rl_string_overflow) {
            fprintf(stderr,
                    "The line is over the limit of %d charactes "
                    "and will not be processed\n",
                    c_line_buf_size);
            res = 1;
            break;
        } else if (read_res == e_rl_eof)
            break;
        else if (line.len == 0)
            goto loop_end;

        node_t *ast_root = parse_line(line, &parser_arena);
        if (!ast_root)
            goto loop_end;

        assert(ast_root->type == e_nd_command);

        int retcode = execute_command(ast_root, &inerpreter_arena);
        if (retcode != 0) {
            fprintf(stderr,
                    "Interpreter error: '%.*s' failed with code %d\n",
                    STR_PRINTF_ARGS(ast_root->cmd.cmd), retcode);
        }

    loop_end:
        arena_drop(&parser_arena);
        arena_drop(&inerpreter_arena);
    }

    free_buffer(&memory);

    if (is_term)
        consume_input(stdin);

    return res;
}
