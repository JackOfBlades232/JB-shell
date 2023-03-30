/* Toy-Shell/main.c */
#include "src/edit/input.h"
#include "src/tokeniz/line_tokenization.h"
#include "src/tokeniz/word_list.h"
#include "src/execution/execute_command.h"
#include "src/execution/cmd_res.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main()
{
    char *line;
    struct word_list *words;
    int token_res;

    struct command_res cmd_res;
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
            fprintf(stderr, "Invalid command\n");
            continue;
        }

        /* step 3: execution of the tokens by the interpreter */
        // @TODO: remake to interpreter call (for recursiveness)
        if (execute_cmd(words, &cmd_res) == 0)
            put_cmd_res(stdout, &cmd_res);

        word_list_free(words);
    }

    putchar('\n');

    return 0;
}
