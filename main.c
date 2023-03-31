/* Toy-Shell/main.c */
#include "src/edit/input.h"
#include "src/tokeniz/line_tokenization.h"
#include "src/tokeniz/word_list.h"
#include "src/interpreter/execute_command.h"
#include "src/interpreter/interpreter.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main()
{
    char *line;
    struct word_list *words;
    int token_res;

    set_up_process_control();

    if (!isatty(0)) {
        fprintf(stderr, "Not a terminal\n");
        return -1;
    }

    for (;;) {
        /* step 1: input with line editing and autocomp */
        line = read_input_line();
        if (line == NULL) /* EOF */
            break; 

        /* step 2: lex analisys resulting in token list */
        token_res = tokenize_input_line_to_word_list(line, &words);
        if (token_res != 0) {
            printf("Invalid command\n");
            free(line);
            continue;
        }

        /* step 3: execution of the tokens by the interpreter */
        interpret_and_run_cmd(words);

        /* free resources */
        word_list_free(words);
        free(line);
    }

    putchar('\n');

    return 0;
}
