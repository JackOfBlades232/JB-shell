/* Toy-Shell/src/line_tokenization.c */
#include "line_tokenization.h"

enum line_traverse_mode { regular, in_word };

static int char_is_eol(int c)
{
    return c == '\n' || c == '\r' || c == EOF;
}

static void switch_traverse_mode(enum line_traverse_mode *mode)
{
    *mode = *mode == regular ? in_word : regular;
}

int tokenize_input_line_to_word_list(FILE *f, 
        struct word_list *out_words, int *eol_char)
{
    int c;
    struct word_list *words;
    enum line_traverse_mode cur_mode;

    words = word_list_create();
    cur_mode = regular;

    while (!char_is_eol((c = getc(f)))) {
         
    }

    word_list_free(words);
    return 0;
}
