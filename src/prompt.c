/* Toy-Shell/src/prompt.c */
#include "prompt.h"
#include "line_tokenization.h"
#include "word_list.h"
#include "execute_command.h"

#include <stdio.h>

int run_command_prompt()
{
    struct word_list *words;
    struct command_res cmd_res;
    int eol_ch = 0;
    int token_res;

    while (eol_ch != EOF) {
        printf("> ");

        token_res = tokenize_input_line_to_word_list(stdin, &words, &eol_ch);
        if (token_res != 0) {
            fprintf(stderr, "Invalid command\n");
            continue;
        }

        if (execute_cmd(words, &cmd_res) == 0)
            put_cmd_res(stdout, &cmd_res);

        word_list_free(words);
    }

    putchar('\n');

    return 0;
}
