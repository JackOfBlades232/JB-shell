/* Toy-Shell/src/cmd_data/pipe_seq.h */
#ifndef PIPE_SEQ_SENTRY
#define PIPE_SEQ_SENTRY

#include "cmd_pipe.h"
#include "command.h"

enum pipe_sequence_rule { none, always, if_success, if_failed, to_bg };

struct pipe_sequence_node {
    struct command_pipe *pipe;
    struct pipe_sequence_node *next;
    enum pipe_sequence_rule rule;
};

struct pipe_sequence {
    struct pipe_sequence_node *first, *last;
};

struct pipe_sequence *create_pipe_seq();
void free_pipe_seq(struct pipe_sequence *seq);
void add_pipe_to_seq(struct pipe_sequence *seq,
        struct command_pipe *pipe, enum pipe_sequence_rule rule);
struct pipe_sequence_node *pop_first_node_from_seq(
        struct pipe_sequence *seq);

#endif
