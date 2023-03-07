/* Toy-Shell/src/execute_command.c */
#include "execute_command.h"
#include "command.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

extern char **environ;

static int cmd_is_cd(struct command *cmd)
{
    return strcmp(cmd->cmd_name, "cd") == 0 && cmd->argc <= 2;
}

static int try_execute_cd(struct command *cmd, struct command_res *res)
{
    const char *dir;

    if (!cmd_is_cd(cmd))
        return 0;

    if (cmd->argc == 1) {
        if ((dir = getenv("HOME")) == NULL)
            dir = getpwuid(getuid())->pw_dir;
    } else
        dir = cmd->argv[1];

    res->type = chdir(dir) == -1 ? failed : noproc;
    return 1;
}

static int process_split_ptn(char *ptn, struct command *cmd,
        struct word_list *remaining_tokens)
{
    if (strcmp(ptn, "&") == 0) {
        cmd->run_in_background = 1;
        return word_list_is_empty(remaining_tokens);
    } else if (strcmp(ptn, "&&") == 0) {
        return -1;
    } else if (strcmp(ptn, "|") == 0) {
        return -1;
    } else if (strcmp(ptn, "||") == 0) {
        return -1;
    } else if (strcmp(ptn, "<") == 0) {
        return -1;
    } else if (strcmp(ptn, ">") == 0) {
        return -1;
    } else if (strcmp(ptn, ">>") == 0) {
        return -1;
    } else if (strcmp(ptn, ";") == 0) {
        return -1;
    } else if (strcmp(ptn, "(") == 0) {
        return -1;
    } else if (strcmp(ptn, ")") == 0) {
        return -1;
    }

    return -1;
}

int execute_cmd(struct word_list *tokens, struct command_res *res)
{
    int pid;
    int status, wr;

    struct command cmd;
    struct word *w;

    if (word_list_is_empty(tokens))
        return 1;

    init_command(&cmd);
    while ((w = word_list_pop_first(tokens)) != NULL) {
        char *wc = word_content(w);
        int split_res;

        if (word_is_split_ptn(w)) {
            split_res = process_split_ptn(wc, &cmd, tokens);
            if (split_res <= 0) {
                res->type = split_res == -1 ? not_implemented : failed;
                goto deinit;
            }
        } else
            add_arg_to_command(&cmd, word_content(w));
    }

    if (try_execute_cd(&cmd, res))
        goto deinit;

    if (cmd.run_in_background) /* kill all zombies */
        wait4(-1, NULL, WNOHANG, NULL);

    pid = fork();
    if (pid == 0) { /* child proc */
        execvp(cmd.cmd_name, cmd.argv);
        perror(cmd.cmd_name);
        _exit(1);
    } else if (pid == -1) {
        res->type = failed;
        goto deinit;
    }

    if (cmd.run_in_background) {
        res->type = noproc;
        goto deinit;
    }

    while ((wr = wait(&status)) != pid) {
        if (wr == -1) {
            res->type = failed;
            goto deinit;
        }
    }

    if (WIFEXITED(status)) {
        res->type = exited;
        res->code = WEXITSTATUS(status);
    } else {
        res->type = killed;
        res->code = WSTOPSIG(status);
    }

deinit:
    deinit_command(&cmd);
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
        case not_implemented:
            fprintf(f, "feature not implemented\n");
            break;
        default:
            break;
    }
}
