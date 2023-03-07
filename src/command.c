/* Toy-Shell/src/command.h */
#include "command.h"

#include <stdlib.h>

enum { base_argv_cap = 10, argv_cap_mult = 2 };

void init_command(struct command *cp) {
    cp->cmd_name = NULL;
    cp->argc = 0;
    cp->argv = malloc(base_argv_cap * sizeof(char *));
    *(cp->argv) = NULL;
    cp->argv_cap = base_argv_cap;

    cp->run_in_background = 0;
}

static char **resize_argv(char **argv, int *cur_cap) {
    *cur_cap *= argv_cap_mult;
    return realloc(argv, *cur_cap * sizeof(char *));
}

void add_arg_to_command(struct command *cp, char *arg) {
    if (cp->argc >= cp->argv_cap - 1)
        cp->argv = resize_argv(cp->argv, &cp->argv_cap);

    cp->argv[cp->argc] = arg;
    cp->argc++;
    cp->argv[cp->argc] = NULL;

    if (cp->cmd_name == NULL)
        cp->cmd_name = cp->argv[0];
}

void deinit_command(struct command *cp) {
    char **argvp;
    for (argvp = cp->argv; *argvp; argvp++)
        free(*argvp);
    free(cp->argv);
}
