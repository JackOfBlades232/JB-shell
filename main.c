/* Toy-Shell/main.c */
#include "def.h"
#include "string.h"
#include "debug.h"

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>

typedef enum token_type_tag {
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

    e_tt_error = -1
} token_type_t;

typedef struct token_tag {
    token_type_t type;
    string_t id;
} token_t;

typedef struct lexer_state_tag {
    string_t buf;
    u64 pos;
} lexer_state_t;

static inline bool is_eol(int c)
{
    return c == '\n' || c == EOF;
}

static inline bool is_whitespace(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static inline bool tok_is_eol(token_t tok)
{
    return tok.type == e_tt_eof || tok.type == e_tt_eol;
}

enum {
    c_rl_string_overflow = -1,
    c_rl_eof = -2,
};

static int read_string_from_stream(FILE *f, string_t *buf)
{
    assert(string_is_valid(buf));
    assert(buf->len > 0);

    int c;
    int id = 0;
    while (!is_eol(c = getchar())) {
        buf->p[id++] = (char)c;
        if (id >= buf->len)
            return c_rl_string_overflow;
    }

    if (c == EOF) {
        assert(id == 0);
        return c_rl_eof;
    }

    buf->p[id] = '\0';
    return id;
}

static void consume_input(FILE *f)
{
    int c;
    while (!is_eol(c = getchar()))
        ;
}

// @TODO: un-stdio this
static int lexer_getc(lexer_state_t *lexer)
{
    if (lexer->pos >= lexer->buf.len)
        return EOF;
    return lexer->buf.p[lexer->pos++];
}

static void lexer_ungetc(lexer_state_t *lexer)
{
    assert(lexer->pos > 0);
    --lexer->pos;
}

static token_t get_next_token(lexer_state_t *lexer)
{
    // @TODO
}

int main()
{
    const int is_term = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    const u64 line_buf_size = 1024;

    int res = 0;

    string_t line_buf = allocate_string(line_buf_size);

    for (;;) {
        if (is_term)
            printf("> ");

        int read_res = read_string_from_stream(stdin, &line_buf);
        if (read_res == c_rl_string_overflow) {
            fprintf(stderr,
                    "The line is over the limit of %lu charactes and will not be processed\n",
                    line_buf_size);
            res = 1;
            break;
        } else if (read_res == c_rl_eof)
            break;
        else if (read_res == 0)
            continue;

        lexer_state_t lexer_state = {{line_buf.p, (u64)read_res}, 0};

        token_t tok = {};
        while (!tok_is_eol(tok = get_next_token(&lexer_state))) {
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

    free_string(&line_buf);

    if (is_term)
        consume_input(stdin);

    return res;
}
