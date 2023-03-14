/* Toy-Shell/src/execute_command.h */
#ifndef EXECUTE_COMMAND_SENTRY
#define EXECUTE_COMMAND_SENTRY

#include "word_list.h"
#include "command.h"

#include <stdio.h>

void set_up_process_control();
int execute_cmd(struct word_list *tokens, struct command_res *res);
void put_cmd_res(FILE *f, struct command_res *res);

#endif
