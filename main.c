/* Toy-Shell/main.c */
#include "def.h"
#include "buffer.h"
#include "str.h"
#include "debug.h"

#include <stdio.h>
#include <limits.h>
#include <stdalign.h>
#include <unistd.h>

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

#define ARENA_ALLOC_ONE(arena_, type_) \
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

typedef struct lexer_state_tag {
    string_t line;
    u64 pos;
} lexer_state_t;

static inline bool tok_is_terminating(token_t tok)
{
    return tok.type == e_tt_eol || tok.type == e_tt_error;
}

static inline int lexer_peek(lexer_state_t *lexer)
{
    if (lexer->pos >= lexer->line.len)
        return EOF;
    return lexer->line.p[lexer->pos];
}
static inline void lexer_consume(lexer_state_t *lexer)
{
    assert(lexer->pos < lexer->line.len);
    ++lexer->pos;
}

static token_t get_next_token(lexer_state_t *lexer, arena_t *symbols_arena)
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
    else
        tok.type = e_tt_ident;

    return tok;
}

int main()
{
    enum {
        c_program_mem_size = 1024 * 1024,
        c_line_buf_size = 1024,
    };

    int const is_term = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    int res = 0;

    buffer_t memory = allocate_buffer(c_program_mem_size);
    buffer_t line_storage = {memory.p, c_line_buf_size};
    arena_t symbols_arena = {{memory.p + c_line_buf_size, c_line_buf_size}};
    arena_t interpreter_arena = {{
        memory.p + 2 * c_line_buf_size,
        c_program_mem_size - 2 * c_line_buf_size
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

        lexer_state_t lexer_state = {line, 0};

        token_t *tokp = ARENA_ALLOC_ONE(&interpreter_arena, token_t);

        token_t *tokens = tokp;
        u64 token_cnt = 0;

        while (!tok_is_terminating(
                   *tokp = get_next_token(&lexer_state, &symbols_arena)))
        {
            if (tokp->type == e_tt_error)
                break;

            ++token_cnt;
            tokp = ARENA_ALLOC_ONE(&interpreter_arena, token_t);
        }

        if (tokp->type == e_tt_error) {
            fprintf(stderr, "Error: [unspecified error] at char %lu\n",
                    lexer_state.pos);
            goto loop_end;
        }

        for (token_t *t = tokens; t != tokens + token_cnt; ++t) {
            switch (t->type) {
            case e_tt_ident:
                printf("[%.*s]\n", (int)t->id.len, t->id.p);
                break;
            case e_tt_in:
                printf("<\n");
                break;
            case e_tt_out:
                printf(">\n");
                break;
            case e_tt_append:
                printf(">>\n");
                break;
            case e_tt_pipe:
                printf("|\n");
                break;
            case e_tt_and:
                printf("&&\n");
                break;
            case e_tt_or:
                printf("||\n");
                break;
            case e_tt_semicolon:
                printf(";\n");
                break;
            case e_tt_background:
                printf("&\n");
                break;
            case e_tt_lparen:
                printf("(\n");
                break;
            case e_tt_rparen:
                printf(")\n");
                break;
            default:
            }
        }

    loop_end:
        arena_drop(&symbols_arena);
        arena_drop(&interpreter_arena);
    }

    free_buffer(&memory);

    if (is_term)
        consume_input(stdin);

    return res;
}
