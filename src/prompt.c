/* Toy-Shell/src/prompt.c */
#include "prompt.h"
#include "line_tokenization.h"
#include "word_list.h"

#include <stdio.h>

int run_command_prompt()
{
    struct word_list *words;
    int eol_ch = 0;
    int token_res;

    while (eol_ch != EOF) {
        printf("> ");
        token_res = tokenize_input_line_to_word_list(stdin, &words, &eol_ch);
        if (token_res == 0) {
            word_list_print(words);
            word_list_free(words);
        } else
            fprintf(stderr, "Invalid command\n");
    }

    putchar('\n');

    return 0;
}
