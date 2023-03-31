/* Toy-Shell/src/cmd_data/command.h */
#include "command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { base_argv_cap = 10, argv_cap_mult = 2 };

void init_cmd(struct command *cp)
{
    cp->cmd_name = NULL;
    cp->argc = 0;
    cp->argv = malloc(base_argv_cap * sizeof(char *));
    *(cp->argv) = NULL;
    cp->argv_cap = base_argv_cap;

    cp->stdin_fd = -1;
    cp->stdout_fd = -1;
}

void free_cmd(struct command *cp) 
{
    char **argvp;
    for (argvp = cp->argv; *argvp; argvp++)
        free(*argvp);
    free(cp->argv);
    free(cp);
}

int cmd_is_empty(struct command *cp)
{
    return cp->cmd_name == NULL;
}

static char **resize_argv(char **argv, int *cur_cap) 
{
    *cur_cap *= argv_cap_mult;
    return realloc(argv, *cur_cap * sizeof(char *));
}

void add_arg_to_cmd(struct command *cp, char *arg) 
{
    if (cp->argc >= cp->argv_cap - 1)
        cp->argv = resize_argv(cp->argv, &cp->argv_cap);

    cp->argv[cp->argc] = arg;
    cp->argc++;
    cp->argv[cp->argc] = NULL;

    if (cp->cmd_name == NULL)
        cp->cmd_name = cp->argv[0];
}
