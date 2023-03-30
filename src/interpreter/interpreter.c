/* Toy-Shell/src/interpreter/interpreter.c */
#include "interpreter.h"
#include "../execution/parse_command.h"
#include "../execution/execute_command.h"
#include "../execution/cmd_res.h"

#include <stdio.h>

void interpret_and_run_cmd(struct word_list *words)
{
    struct command_res cmd_res;
    struct command_chain *cmd_chain;
    enum chain_sequence_rule seq_rule;

    if (word_list_is_empty(words))
        return;

    cmd_chain = parse_tokens_to_cmd_chain(words, &seq_rule);
    if (!cmd_chain) {
        printf("Invalid command\n");
        return;
    }

    if (execute_cmd(cmd_chain, &cmd_res) == 0)
        put_cmd_res(stdout, &cmd_res);
}
