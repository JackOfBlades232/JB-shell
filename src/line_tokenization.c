/* Toy-Shell/src/line_tokenization.c */
#include "line_tokenization.h"

enum line_traverse_mode { regular, in_quotes };

static int char_is_eol(int c)
{
    return c == '\n' || c == '\r' || c == EOF;
}

static int char_is_space(int c)
{
    return c == ' ' || c == '\t';
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
    int in_word;
    enum line_traverse_mode cur_mode;

    *out_words = word_list_create();
    cur_mode = regular;
    in_word = 0;

    while (!char_is_eol((c = getc(f)))) {
         if (!in_word && !char_is_space(c))
             word_list_add_item(*out_words);

        in_word = !char_is_space(c);

        if (in_word)
            word_list_add_letter_to_last(*out_words, c);
    }

    if (status == 0)
        *eol_char = c;
    else
        word_list_free(*out_words);

    return status;
}
