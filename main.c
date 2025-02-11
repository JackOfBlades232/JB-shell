/* Toy-Shell/main.c */
#include "def.h"
#include "buffer.h"
#include "str.h"
#include "debug.h"

#include <stdio.h>
#include <limits.h>
#include <unistd.h>

static inline bool cstr_contains(const char *str, char c)
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

static inline void arena_drop(arena_t *arena)
{
    arena->allocated = 0;
}

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
    e_tt_eof,        // real eof
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

static inline bool tok_is_eol(token_t tok)
{
    return tok.type == e_tt_eof || tok.type == e_tt_eol;
}

static inline int lexer_getc(lexer_state_t *lexer)
{
    if (lexer->pos >= lexer->line.len)
        return EOF;
    return lexer->line.p[lexer->pos++];
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

// @TODO: straighten
static token_t get_next_token(lexer_state_t *lexer, arena_t *symbols_arena)
{
    token_t tok = {};

    int c;
    bool started_tok = false;
    bool in_quotes = false;
    bool is_screened = false;

    while (!is_eol(c = lexer_getc(lexer))) {
        if (!started_tok) {
            if (is_whitespace(c))
                continue;

            started_tok = true;
        }

        if (c == '\\' && !is_screened) {
            is_screened = true;
            continue;
        }

        if (c == '"' && !is_screened) {
            in_quotes = !in_quotes;
            continue;
        }

        if (!is_screened && !in_quotes) {
            if (is_whitespace(c) ||
                (!string_is_empty(&tok.id) && cstr_contains("<>|&();", c)))
            {
                break;
            }

            switch (c) {
            case '>':
                if (lexer_peek(lexer) == '>') {
                    lexer_consume(lexer);
                    tok.type = e_tt_append;
                } else
                    tok.type = e_tt_out;
                return tok;
            case '|':
                if (lexer_peek(lexer) == '|') {
                    lexer_consume(lexer);
                    tok.type = e_tt_or;
                } else
                    tok.type = e_tt_pipe;
                return tok;
            case '&':
                if (lexer_peek(lexer) == '&') {
                    lexer_consume(lexer);
                    tok.type = e_tt_and;
                } else
                    tok.type = e_tt_background;
                return tok;

            case '<':
                tok.type = e_tt_in;
                return tok;
            case ';':
                tok.type = e_tt_semicolon;
                return tok;
            case '(':
                tok.type = e_tt_lparen;
                return tok;
            case ')':
                tok.type = e_tt_rparen;
                return tok;

            default:
            }
        }

        if (string_is_empty(&tok.id))
            tok.id.p = (char *)arena_allocate(symbols_arena, 0);

        assert(tok.id.p + tok.id.len - symbols_arena->buf.p <
               symbols_arena->buf.sz);

        ++symbols_arena->allocated;
        
        assert(c >= SCHAR_MIN && c <= SCHAR_MAX);
        tok.id.p[tok.id.len++] = (char)c;
    }

    if (in_quotes || is_screened) {
        tok.type = e_tt_error;
        return tok;
    }

    if (string_is_empty(&tok.id) && is_eol(c))
        tok.type = e_tt_eol;
    else
        tok.type = e_tt_ident;

    return tok;
}

int main()
{
    const int is_term = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    const u64 line_buf_size = 1024;

    int res = 0;

    buffer_t memory = allocate_buffer(2 * line_buf_size);
    buffer_t line_storage = {memory.p, line_buf_size};
    arena_t symbols_arena = {{memory.p + line_buf_size, line_buf_size}, 0};

    for (;;) {
        if (is_term)
            printf("> ");

        string_t line = {};

        int read_res = read_line_from_stream(stdin, &line_storage, &line);
        if (read_res == e_rl_string_overflow) {
            fprintf(stderr,
                    "The line is over the limit of %lu charactes and will not be processed\n",
                    line_buf_size);
            res = 1;
            break;
        } else if (read_res == e_rl_eof)
            break;
        else if (line.len == 0)
            continue;

        lexer_state_t lexer_state = {line, 0};

        token_t tok = {};
        while (!tok_is_eol(tok = get_next_token(&lexer_state, &symbols_arena))) {
            if (tok.type == e_tt_error)
                break;

            switch (tok.type) {
            case e_tt_ident:
                printf("[%.*s]\n", (int)tok.id.len, tok.id.p);
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
    }

    free_buffer(&memory);

    if (is_term)
        consume_input(stdin);

    return res;
}
