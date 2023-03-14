/* Toy-Shell/src/prompt.c */
#include "prompt.h"
#include "line_tokenization.h"
#include "word_list.h"
#include "execute_command.h"
#include "cmd_res.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

int run_command_prompt()
{
    struct word_list *words;
    int eol_ch = 0;
    int token_res;

#ifndef TOKENIZER_DEBUG
    struct command_res cmd_res;
    set_up_process_control();
#endif

    while (eol_ch != EOF) {
        printf("> ");

        token_res = tokenize_input_line_to_word_list(stdin, &words, &eol_ch);
        if (token_res != 0) {
            fprintf(stderr, "Invalid command\n");
            continue;
        }

#ifndef TOKENIZER_DEBUG
        if (execute_cmd(words, &cmd_res) == 0)
            put_cmd_res(stdout, &cmd_res);
#else
        word_list_print(words);
#endif

        word_list_free(words);
    }

    putchar('\n');

    return 0;
}
