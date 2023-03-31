/* Toy-Shell/src/cmd_data/cmd_data.h */
#ifndef CMD_DATA_SENTRY
#define CMD_DATA_SENTRY

struct command;
struct command_pipe;
struct pipe_sequence;

typedef void (*command_modifier)(struct command *);

enum pipe_sequence_rule { none, always, if_success, if_failed, to_bg };

struct command {
    char *cmd_name; /* () for rec call */

    /* exec cmd */
    int argc;
    char **argv;
    int argv_cap;

    /* rec interpreter call */
    struct pipe_sequence *rec_seq;

    /* io redir for proc */
    int stdin_fd, stdout_fd;
};

struct pipe_sequence_node {
    struct command_pipe *pipe;
    struct pipe_sequence_node *next;
    enum pipe_sequence_rule rule;
};

struct pipe_sequence {
    struct pipe_sequence_node *first, *last;
};

/* command */
void init_exec_cmd(struct command *cp);
void init_rec_cmd(struct command *cp);
void free_cmd(struct command *cp);
int cmd_is_empty(struct command *cp);
void add_arg_to_cmd(struct command *cp, char *arg);

/* pipe */
struct command_pipe *create_cmd_pipe();
void free_cmd_pipe(struct command_pipe *cc);

int cmd_pipe_is_empty(struct command_pipe *cc);
int cmd_pipe_len(struct command_pipe *cc);

struct command *add_cmd_to_pipe(struct command_pipe *cc, int is_exec);
int delete_first_cmd_from_pipe(struct command_pipe *cc);
struct command *get_first_cmd_in_pipe(struct command_pipe *cc);
struct command *get_last_cmd_in_pipe(struct command_pipe *cc);
int add_arg_to_last_pipe_cmd(struct command_pipe *cc, char *arg);

int cmd_pipe_is_background(struct command_pipe *cc);
void set_cmd_pipe_to_background(struct command_pipe *cc);

void map_to_all_cmds_in_pipe(struct command_pipe *cc, command_modifier func);
int pipe_contains_cmd(struct command_pipe *cc, const char *cmd_name);

/* seq */
struct pipe_sequence *create_pipe_seq();
void free_pipe_seq(struct pipe_sequence *seq);
void add_pipe_to_seq(struct pipe_sequence *seq,
        struct command_pipe *pipe, enum pipe_sequence_rule rule);
struct pipe_sequence_node *pop_first_node_from_seq(
        struct pipe_sequence *seq);

#endif
