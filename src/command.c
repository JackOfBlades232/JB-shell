/* Toy-Shell/src/command.h */
#include "command.h"

#include <stdio.h>
#include <stdlib.h>

enum { base_argv_cap = 10, argv_cap_mult = 2 };

struct command_chain_node {
    struct command *cmd;
    struct command_chain_node *next;
};

struct command_chain {
    struct command_chain_node *first, *last;
};

int command_is_empty(struct command *cp)
{
    return cp->cmd_name == NULL;
}

void free_command(struct command *cp) 
{
    char **argvp;
    for (argvp = cp->argv; *argvp; argvp++)
        free(*argvp);
    free(cp->argv);
    free(cp);
}

static void init_command(struct command *cp)
{
    cp->cmd_name = NULL;
    cp->argc = 0;
    cp->argv = malloc(base_argv_cap * sizeof(char *));
    *(cp->argv) = NULL;
    cp->argv_cap = base_argv_cap;

    cp->run_in_background = 0;
    cp->stdin_fd = -1;
    cp->stdout_fd = -1;
}

static char **resize_argv(char **argv, int *cur_cap) 
{
    *cur_cap *= argv_cap_mult;
    return realloc(argv, *cur_cap * sizeof(char *));
}

static void add_arg_to_command(struct command *cp, char *arg) 
{
    if (cp->argc >= cp->argv_cap - 1)
        cp->argv = resize_argv(cp->argv, &cp->argv_cap);

    cp->argv[cp->argc] = arg;
    cp->argc++;
    cp->argv[cp->argc] = NULL;

    if (cp->cmd_name == NULL)
        cp->cmd_name = cp->argv[0];
}

struct command_chain *create_cmd_chain()
{
    struct command_chain *cc = malloc(sizeof(struct command_chain));
    cc->first = NULL;
    cc->last = NULL;
    return cc;
}

void add_cmd_to_chain(struct command_chain *cc)
{
    struct command_chain_node *tmp;
    tmp = malloc(sizeof(struct command_chain_node));
    tmp->cmd = malloc(sizeof(struct command));
    init_command(tmp->cmd);
    tmp->next = NULL;

    if (cc->first == NULL) {
        cc->first = tmp;
        cc->last = tmp;
    } else {
        cc->last->next = tmp;
        cc->last = tmp;
    }
}

int add_arg_to_last_chain_cmd(struct command_chain *cc, char *arg)
{
    if (cc->last == NULL)
        return 0;
    add_arg_to_command(cc->last->cmd, arg);
    return 1;
}

struct command *get_first_cmd_in_chain(struct command_chain *cc)
{
    if (cc->first == NULL)
        return NULL;
    return cc->first->cmd;
}

struct command *get_last_cmd_in_chain(struct command_chain *cc)
{
    if (cc->last == NULL)
        return NULL;
    return cc->last->cmd;
}

int delete_first_cmd_from_chain(struct command_chain *cc)
{
    struct command_chain_node *tmp;

    if (cc->first == NULL)
        return 0;

    tmp = cc->first;
    if (cc->last == tmp)
        cc->last = tmp->next;
    cc->first = tmp->next;
    free(tmp);

    return 1;
}

void free_command_chain(struct command_chain *cc)
{
    struct command_chain_node *tmp;
    while (cc->first != NULL) {
        tmp = cc->first;
        cc->first = cc->first->next;
        free_command(tmp->cmd);
        free(tmp);
    }
    cc->last = NULL;
}

void print_cmd_chain(struct command_chain *cc)
{
    struct command_chain_node *tmp;
    for (tmp = cc->first; tmp; tmp = tmp->next)
        printf("%s\n", tmp->cmd->cmd_name);
}
