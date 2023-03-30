/* Toy-Shell/src/interpreter/interpreter.c */
#include "interpreter.h"
#include "../execution/execute_command.h"
#include "../execution/cmd_res.h"

#include <stdio.h>

void interpret_and_run_cmd(struct word_list *words)
{
    struct command_res cmd_res;
    if (execute_cmd(words, &cmd_res) == 0)
        put_cmd_res(stdout, &cmd_res);
}
