/* Toy-Shell/src/cmd_data/cmd_pipe.h */
#include "cmd_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct command_pipe_node {
    struct command *cmd;
    struct command_pipe_node *next;
};

struct command_pipe {
    struct command_pipe_node *first, *last;
    int run_in_background;
};

struct command_pipe *create_cmd_pipe()
{
    struct command_pipe *cc = malloc(sizeof(struct command_pipe));
    cc->first = NULL;
    cc->last = NULL;
    cc->run_in_background = 0;
    return cc;
}

void free_cmd_pipe(struct command_pipe *cc)
{
    struct command_pipe_node *tmp;
    while (cc->first != NULL) {
        tmp = cc->first;
        cc->first = cc->first->next;
        free_cmd(tmp->cmd);
        free(tmp);
    }
    cc->last = NULL;
}

int cmd_pipe_is_empty(struct command_pipe *cc)
{
    return cc->first == NULL;
}

int cmd_pipe_len(struct command_pipe *cc)
{
    struct command_pipe_node *tmp;
    int len = 0;
    for (tmp = cc->first; tmp; tmp = tmp->next)
        len++;
    return len;
}

struct command *add_cmd_to_pipe(struct command_pipe *cc, int is_exec)
{
    struct command_pipe_node *tmp;
    tmp = malloc(sizeof(struct command_pipe_node));
    tmp->cmd = malloc(sizeof(struct command));
    tmp->next = NULL;

    if (is_exec)
        init_exec_cmd(tmp->cmd);
    else
        init_rec_cmd(tmp->cmd);

    if (cc->first == NULL) {
        cc->first = tmp;
        cc->last = tmp;
    } else {
        cc->last->next = tmp;
        cc->last = tmp;
    }

    return cc->last->cmd;
}

int delete_first_cmd_from_pipe(struct command_pipe *cc)
{
    struct command_pipe_node *tmp;

    if (cc->first == NULL)
        return 0;

    tmp = cc->first;
    if (cc->last == tmp)
        cc->last = tmp->next;
    cc->first = tmp->next;
    free_cmd(tmp->cmd);
    free(tmp);

    return 1;
}

struct command *get_first_cmd_in_pipe(struct command_pipe *cc)
{
    if (cc->first == NULL)
        return NULL;
    return cc->first->cmd;
}

struct command *get_last_cmd_in_pipe(struct command_pipe *cc)
{
    if (cc->last == NULL)
        return NULL;
    return cc->last->cmd;
}

int add_arg_to_last_pipe_cmd(struct command_pipe *cc, char *arg)
{
    if (cc->last == NULL)
        return 0;
    add_arg_to_cmd(cc->last->cmd, arg);
    return 1;
}

int cmd_pipe_is_background(struct command_pipe *cc)
{
    return cc->run_in_background;
}

void set_cmd_pipe_to_background(struct command_pipe *cc)
{
    cc->run_in_background = 1;
}

void map_to_all_cmds_in_pipe(struct command_pipe *cc, command_modifier func)
{
    struct command_pipe_node *tmp;
    for (tmp = cc->first; tmp; tmp = tmp->next) {
        (*func)(tmp->cmd);
    }
}

int pipe_contains_cmd(struct command_pipe *cc, const char *cmd_name)
{
    struct command_pipe_node *tmp;
    for (tmp = cc->first; tmp; tmp = tmp->next) {
        if (strcmp(cmd_name, tmp->cmd->cmd_name) == 0)
            return 1;
    }
    return 0;
}
