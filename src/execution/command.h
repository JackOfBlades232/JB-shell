/* Toy-Shell/src/execution/command.h */
#ifndef COMMAND_SENTRY
#define COMMAND_SENTRY

struct command {
    char *cmd_name;
    int argc;
    char **argv;
    int argv_cap;
    int stdin_fd, stdout_fd;
};

struct command_pipe;
typedef void (*command_modifier)(struct command *);

/* command */
int cmd_is_empty(struct command *cp);
void free_cmd(struct command *cp);

/* command pipe */
struct command_pipe *create_cmd_pipe();
void free_cmd_pipe(struct command_pipe *cc);

int cmd_pipe_is_empty(struct command_pipe *cc);
int cmd_pipe_len(struct command_pipe *cc);

struct command *add_cmd_to_pipe(struct command_pipe *cc);
int delete_first_cmd_from_pipe(struct command_pipe *cc);
struct command *get_first_cmd_in_pipe(struct command_pipe *cc);
struct command *get_last_cmd_in_pipe(struct command_pipe *cc);
int add_arg_to_last_pipe_cmd(struct command_pipe *cc, char *arg);

int cmd_pipe_is_background(struct command_pipe *cc);
void set_cmd_pipe_to_background(struct command_pipe *cc);

void map_to_all_cmds_in_pipe(struct command_pipe *cc, command_modifier func);
int pipe_contains_cmd(struct command_pipe *cc, const char *cmd_name);

#endif
