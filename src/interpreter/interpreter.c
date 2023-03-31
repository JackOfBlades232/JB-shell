/* Toy-Shell/src/interpreter/interpreter.c */
#include "interpreter.h"
#include "parse_command.h"
#include "execute_command.h"
#include "cmd_res.h"

#include <stdio.h>

void interpret_and_run_cmd(struct word_list *words)
{
    struct pipe_sequence *pipe_seq;
    struct command_res cmd_res;

    if (word_list_is_empty(words))
        return;

    pipe_seq = parse_tokens_to_pipe_seq(words);
    if (!pipe_seq) {
        printf("Invalid command\n");
        return;
    }

    if (execute_seq(pipe_seq, &cmd_res) == 0)
        put_cmd_res(stdout, &cmd_res);

    free_pipe_seq(pipe_seq);
}
