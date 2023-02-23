/* Toy-Shell/main.c */
#include "src/line_tokenization.h"
#include "src/word_list.h"

#include <stdio.h>

int main()
{
    int status;

    struct word_list *words;
    int eol_ch;

    status = tokenize_input_line_to_word_list(stdin, &words, &eol_ch);
    word_list_print(words);
    word_list_free(words);

    return status;
}
