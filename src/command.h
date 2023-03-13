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

struct command_chain;

int command_is_empty(struct command *cp);
void free_command(struct command *cp);

struct command_chain *create_cmd_chain();
void add_cmd_to_chain(struct command_chain *cc);
int add_arg_to_last_chain_cmd(struct command_chain *cc, char *arg);
struct command *get_first_cmd_in_chain(struct command_chain *cc);
struct command *get_last_cmd_in_chain(struct command_chain *cc);
int delete_first_cmd_from_chain(struct command_chain *cc);
void free_command_chain(struct command_chain *cc);

/* debug */
void print_cmd_chain(struct command_chain *cc);

#endif
