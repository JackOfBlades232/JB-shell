/* Toy-Shell/src/execute_command.c */
#include "execute_command.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

extern char **environ;

static int cmd_is_cd(char *prog_name, int argc)
{
    return strcmp(prog_name, "cd") == 0 && argc <= 2;
}

static int try_execute_cd(char *prog_name, int argc, char **argv,
        struct command_res *res)
{
    const char *dir;

    if (!cmd_is_cd(prog_name, argc))
        return 0;

    if (argc == 1) {
        if ((dir = getenv("HOME")) == NULL)
            dir = getpwuid(getuid())->pw_dir;
    } else
        dir = argv[1];

    res->type = chdir(dir) == -1 ? failed : noproc;
    return 1;
}

int execute_cmd(struct word_list *tokens, struct command_res *res)
{
    int pid;
    int status, wr;

    char *prog_name;
    int argc;
    char **argv;

    if (word_list_is_empty(tokens))
        return 1;

    argc = word_list_len(tokens);
    argv = word_list_create_token_ptrs(tokens);
    prog_name = argv[0];

    if (try_execute_cd(prog_name, argc, argv, res))
        goto deinit;

    pid = fork();
    if (pid == 0) { /* child proc */
        execvp(prog_name, argv);
        perror(prog_name);
        _exit(1);
    } else if (pid == -1) {
        res->type = failed;
        goto deinit;
    }

    wr = wait(&status);
    if (wr == -1)
        res->type = failed;
    else if (WIFEXITED(status)) {
        res->type = exited;
        res->code = WEXITSTATUS(status);
    } else {
        res->type = killed;
        res->code = WSTOPSIG(status);
    }

deinit:
    free(argv);
    return 0;
}

void put_cmd_res(FILE *f, struct command_res *res)
{
    switch (res->type) {
        case exited:
            fprintf(f, "exit code %d\n", res->code);
            break;
        case killed:
            fprintf(f, "killed by signal %d\n", res->code);
            break;
        case failed:
            fprintf(f, "failed to execute command\n");
            break;
        default:
            break;
    }
}
