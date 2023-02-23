/* Toy-Shell/src/line_tokenization.c */
#include "line_tokenization.h"

enum line_traverse_mode { regular, in_quotes };

static int char_is_eol(int c)
{
    return c == '\n' || c == '\r' || c == EOF;
}

static int char_is_in_word(int c, enum line_traverse_mode cur_mode)
{
    return cur_mode == in_quotes || (c != ' ' && c != '\t');
}

static void switch_traverse_mode(enum line_traverse_mode *mode)
{
    *mode = *mode == regular ? in_quotes : regular;
}

int tokenize_input_line_to_word_list(FILE *f, 
        struct word_list **out_words, int *eol_char)
{
    int status = 0;
    int c;
    int in_word, was_spec_char;
    enum line_traverse_mode cur_mode;

    *out_words = word_list_create();

    in_word = 0;
    cur_mode = regular;
    was_spec_char = 0;

    while (!char_is_eol((c = getc(f)))) {
        if (c == '"') {
            switch_traverse_mode(&cur_mode);
            was_spec_char = 1;
        } else
            was_spec_char = 0;

        if (!in_word && char_is_in_word(c, cur_mode))
             word_list_add_item(*out_words);

        in_word = char_is_in_word(c, cur_mode);

        if (in_word && !was_spec_char)
            word_list_add_letter_to_last(*out_words, c);
    }

    if (cur_mode != regular)
        status = 1;

    if (status == 0)
        *eol_char = c;
    else
        word_list_free(*out_words);

    return status;
}
