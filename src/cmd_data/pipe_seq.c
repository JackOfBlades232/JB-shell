/* Toy-Shell/src/cmd_data/cmd_pipe.h */
#include "cmd_data.h"

#include <stdio.h>
#include <stdlib.h>

struct pipe_sequence *create_pipe_seq()
{
    struct pipe_sequence *seq = malloc(sizeof(struct pipe_sequence));
    seq->first = seq->last = NULL;
    return seq;
}

void free_pipe_seq(struct pipe_sequence *seq)
{
    struct pipe_sequence_node *tmp;
    while (seq->first) {
        tmp = seq->first;
        seq->first = seq->first->next;
        free_cmd_pipe(tmp->pipe);
        free(tmp);
    }
    seq->first = seq->last = NULL;
}

void add_pipe_to_seq(struct pipe_sequence *seq,
        struct command_pipe *pipe, enum pipe_sequence_rule rule)
{
    struct pipe_sequence_node *new = malloc(sizeof(struct pipe_sequence_node));
    new->pipe = pipe;
    new->rule = rule;
    new->next = NULL;
    if (seq->first) {
        seq->last->next = new;
        seq->last = new;
    } else
        seq->first = seq->last = new;
}

struct pipe_sequence_node *pop_first_node_from_seq(
        struct pipe_sequence *seq)
{
    struct pipe_sequence_node *ret;
    if (!seq->first)
        return NULL;
    ret = seq->first;
    seq->first = seq->first->next;
    if (!seq->first)
        seq->last = NULL;
    return ret;
}
