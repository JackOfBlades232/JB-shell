/* Toy-Shell/main.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if USE_ASSERTIONS
  #include <assert.h>
#else
  #define assert(_e)
  #define static_assert(_e)
#endif
#include <unistd.h>

/* @TODO
 * Write a parser (output -- tree)
 * Pull out and finish string (write dyn array if need be)
 * Pull out tokenizer
 */

typedef struct string_tag {
    char *p;
    size_t len, cap;
} string_t;

static string_t string_make(const char *p) 
{ 
    string_t s; 
    s.len = strlen(p);
    s.cap = s.len + 1;
    size_t byte_size = s.cap * sizeof(*s.p);
    s.p = malloc(byte_size);
    memcpy(s.p, p, byte_size);
    return s;
}

static void string_release(string_t *s)
{
    assert(s->p);
    s->p = NULL;
    s->cap = s->len = 0;
}

static void string_clear(string_t *s)
{
    assert(s->p);
    free(s->p);
    string_release(s);
}

static void string_resize(string_t *s, size_t desired_cap)
{
    assert(s->p);
    if (s->cap > desired_cap)
        return;
    while (s->cap <= desired_cap)
        s->cap *= 2;
    s->p = realloc(s->p, s->cap);
}

static void string_push_char(string_t *s, char c)
{
    assert(s->p);
    if (s->len + 1 >= s->cap)
        string_resize(s, s->len + 1);
    s->p[s->len++] = c;
    s->p[s->len] = '\0';
}

typedef enum token_type_tag {
    tt_eol,        // last token
    tt_eof,        // real eof
    tt_ident,      // anything
    tt_in,         // <
    tt_out,        // >
    tt_pipe,       // |
    tt_and,        // &&
    tt_or,         // ||
    tt_semicolon,  // ;
    tt_background, // &
    tt_lparen,     // (
    tt_rparen,     // )

    tt_error = -1
} token_type_t;

typedef struct token_tag {
    token_type_t type;
    string_t id;
} token_t;

static inline int is_eol(int c)
{
    return c == '\n' || c == EOF;
}

static inline int is_whitespace(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static token_t get_next_token(FILE *f)
{
    // @TODO: last error code as static var?

    token_t tok = { tt_error, string_make("") };
    int is_in_string = 0;
    int token_had_string = 0; // for "" tokens
    int c;
    for (;;) {
        c = fgetc(f);
    
        int is_screened = c == '\\';
        if (is_screened)
            c = fgetc(f);

        if ((is_screened || is_in_string) && is_eol(c))
            goto yield; // tok type is error

        if (!is_screened && c == '"') {
            is_in_string = !is_in_string;
            token_had_string = 1; // @NOTE: only first assignment is needed
            continue;
        }

        if (!is_in_string && !is_screened) {
            // Separator found
            if (is_eol(c) || is_whitespace(c) || strchr("<>|;&()", c)) {
                // If there was an identifier, return sep to stream and yield
                if (tok.id.len || token_had_string) {
                    ungetc(c, f);
                    tok.type = tt_ident;
                    goto yield;
                }

                // Else, parse separator

                // Try token-type separators
                switch (c) {
                case '<':
                    tok.type = tt_in;
                    goto yield;
                case '>':
                    tok.type = tt_out;
                    goto yield;
                case '|': // | or ||
                    if ((c = fgetc(f)) == '|')
                        tok.type = tt_or;
                    else {
                        tok.type = tt_pipe;
                        ungetc(c, f);
                    }
                    goto yield;
                case ';':
                    tok.type = tt_semicolon;
                    goto yield;
                case '&': // & or &&
                    if ((c = fgetc(f)) == '&')
                        tok.type = tt_and;
                    else {
                        tok.type = tt_background;
                        ungetc(c, f);
                    }
                    goto yield;
                case '(':
                    tok.type = tt_lparen;
                    goto yield;
                case ')':
                    tok.type = tt_rparen;
                    goto yield;
                default:
                }

                // Otherwise, eol/eof
                if (is_eol(c)) {
                    tok.type = c == EOF ? tt_eof : tt_eol;
                    goto yield;
                }

                // If none of the above, just whitespace
                continue;
            }
        }

        // If no separator found or screened/in string, add char to id
        assert(c >= SCHAR_MIN && c <= SCHAR_MAX);
        string_push_char(&tok.id, c);
    }

yield:
    if (tok.type != tt_ident)
        string_clear(&tok.id);
    return tok;
}

int main()
{
    int is_term = isatty(STDOUT_FILENO);

    if (is_term)
        printf("> ");

    for (;;) {
        token_t next_token = get_next_token(stdin);
        assert((next_token.type == tt_ident && next_token.id.p) ||
               (next_token.type != tt_ident && !next_token.id.p));
        if (is_term) {
            // @TODO: check in/out flushing 
            // (if there comes a time when an error can be at non-eof place)
            switch (next_token.type) {
            case tt_eol:
                printf("$\n");
                printf("> ");
                break;
            case tt_eof:
                goto eof;
            case tt_ident:
                printf("[%s]\n", next_token.id.p);
                string_clear(&next_token.id);
                break;
            case tt_in:
                printf("<\n");
                break;
            case tt_out:
                printf(">\n");
                break;
            case tt_pipe:
                printf("|\n");
                break;
            case tt_and:
                printf("&&\n");
                break;
            case tt_or:
                printf("||\n");
                break;
            case tt_semicolon:
                printf(";\n");
                break;
            case tt_background:
                printf("&\n");
                break;
            case tt_lparen:
                printf("(\n");
                break;
            case tt_rparen:
                printf(")\n");
                break;
            case tt_error:
                printf("Tokenizer error!\n");
                printf("> ");
                break;

            default: assert(0);
            }
        }
    }

eof:
    return 0;
}
