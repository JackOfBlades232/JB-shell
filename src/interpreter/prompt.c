/* Toy-Shell/src/interpreter/prompt.c */
#include "prompt.h"
#include "../edit/input.h"
#include "../tokeniz/line_tokenization.h"
#include "../tokeniz/word_list.h"
#include "../execution/execute_command.h"
#include "../execution/cmd_res.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int run_command_prompt()
{
    char *line;
    struct word_list *words;
    int token_res;

#ifndef TOKENIZER_DEBUG
    struct command_res cmd_res;
    set_up_process_control();
#endif

    if (!isatty(0)) {
        fprintf(stderr, "Not a terminal\n");
        return -1;
    }

    for (;;) {
        line = read_input_line();
        if (line == NULL) /* EOF */
            break; 

        token_res = tokenize_input_line_to_word_list(line, &words);
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
