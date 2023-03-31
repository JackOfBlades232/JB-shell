/* Toy-Shell/src/cmd_data/command.h */
#ifndef COMMAND_SENTRY
#define COMMAND_SENTRY

struct command {
    char *cmd_name;
    int argc;
    char **argv;
    int argv_cap;
    int stdin_fd, stdout_fd;
};

void init_cmd(struct command *cp);
void free_cmd(struct command *cp);
int cmd_is_empty(struct command *cp);
void add_arg_to_cmd(struct command *cp, char *arg);

#endif
