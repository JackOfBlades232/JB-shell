/* Toy-Shell/src/execute_command.h */
#ifndef EXECUTE_COMMAND_SENTRY
#define EXECUTE_COMMAND_SENTRY

#include "word_list.h"

#include <stdio.h>

enum command_res_type { exited, killed, failed, noproc, not_implemented };
struct command_res {
    enum command_res_type type;
    int code;
};

int execute_cmd(struct word_list *tokens, struct command_res *res);
void put_cmd_res(FILE *f, struct command_res *res);

#endif
