/* Toy-Shell/src/interpreter/execute_command.h */

#ifndef EXECUTE_COMMAND_SENTRY
#define EXECUTE_COMMAND_SENTRY

#include "../tokeniz/word_list.h"
#include "../cmd_data/pipe_seq.h"
#include "cmd_res.h"

#include <stdio.h>

void set_up_process_control();
int execute_seq(struct pipe_sequence *pipe_seq, struct command_res *res);
void put_cmd_res(FILE *f, struct command_res *res);

#endif
