/* Toy-Shell/src/execution/execute_command.h */

#ifndef EXECUTE_COMMAND_SENTRY
#define EXECUTE_COMMAND_SENTRY

#include "../tokeniz/word_list.h"
#include "command.h"
#include "cmd_res.h"

#include <stdio.h>

void set_up_process_control();
int execute_cmd(struct command_pipe *cmd_pipe, struct command_res *res);
void put_cmd_res(FILE *f, struct command_res *res);

#endif
