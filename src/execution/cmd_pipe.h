/* Toy-Shell/src/execution/cmd_pipe.h */
#ifndef CMD_PIPE_SENTRY
#define CMD_PIPE_SENTRY

#include "command.h"

struct command_pipe;
typedef void (*command_modifier)(struct command *);

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
