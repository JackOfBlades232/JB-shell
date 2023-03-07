/* Toy-Shell/src/command.h */
#ifndef COMMAND_SENTRY
#define COMMAND_SENTRY

struct command {
    char *cmd_name;
    int argc;
    char **argv;
    int argv_cap;
    int run_in_background;
    int stdin_fd, stdout_fd;
};

void init_command(struct command *cp);
void add_arg_to_command(struct command *cp, char *arg);
void deinit_command(struct command *cp);

#endif
