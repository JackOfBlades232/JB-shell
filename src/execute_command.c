/* Toy-Shell/src/execute_command.c */
#include "execute_command.h"
#include "command.h"
#include "word.h"
#include "word_list.h"

#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>

extern char **environ;

void chld_handler(int s)
{
    int save_errno = errno;
    signal(SIGCHLD, chld_handler);
    while (wait4(-1, NULL, WNOHANG, NULL) > 0)
        {}
    errno = save_errno;
}

void set_up_process_control()
{
    signal(SIGCHLD, chld_handler);
}

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

static int prepare_stdin_redirection(struct command *cmd, 
        struct word_list *remaining_tokens)
{
    struct word *w;
    int res = 0;

    if (word_list_is_empty(remaining_tokens))
        return 0;
    if (cmd->stdin_fd != -1)
        return 0;

    w = word_list_pop_first(remaining_tokens);
    if (!word_is_split_ptn(w)) {
        cmd->stdin_fd = open(word_content(w), O_RDONLY);
        if (cmd->stdin_fd != -1)
            res = 1;
    }

    word_free(w);
    return res;

}

static int prepare_stdout_redirection(struct command *cmd, 
        struct word_list *remaining_tokens, int is_append)
{
    struct word *w;
    int res = 0;
    int mode = O_WRONLY | O_CREAT | (is_append ? O_APPEND : O_TRUNC);

    if (word_list_is_empty(remaining_tokens))
        return 0;
    if (cmd->stdout_fd != -1)
        return 0;

    w = word_list_pop_first(remaining_tokens);
    if (!word_is_split_ptn(w)) {
        cmd->stdout_fd = open(word_content(w), mode, 0666);
        if (cmd->stdout_fd != -1)
            res = 1;
    }

    word_free(w);
    return res;

}

static int process_split_ptn(struct word *w, struct command *cmd,
        struct word_list *remaining_tokens)
{
    char *ptn = word_content(w);
    int res = -1; /* not implemented */

    if (strcmp(ptn, "&") == 0) {
        cmd->run_in_background = 1;
        res = word_list_is_empty(remaining_tokens);
    } else if (strcmp(ptn, "<") == 0) {
        res = prepare_stdin_redirection(cmd, remaining_tokens);
    } else if (strcmp(ptn, ">") == 0) {
        res = prepare_stdout_redirection(cmd, remaining_tokens, 0);
    } else if (strcmp(ptn, ">>") == 0) {
        res = prepare_stdout_redirection(cmd, remaining_tokens, 1);
    }

    word_free(w);
    return res;
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
        if (word_is_split_ptn(w))
            break;

        add_arg_to_command(&cmd, word_content(w));
        free(w); /* still need the content in cmd */
    }

    while (w != NULL) {
        int split_res;

        if (!word_is_split_ptn(w)) {
            res->type = failed;
            word_free(w);
            goto deinit;
        }

        split_res = process_split_ptn(w, &cmd, tokens);
        if (split_res <= 0) {
            res->type = split_res == -1 ? not_implemented : failed;
            goto deinit;
        }

        w = word_list_pop_first(tokens);
    }

    if (try_execute_cd(&cmd, res))
        goto deinit;

    pid = fork();
    if (pid == 0) { /* child proc */
        signal(SIGCHLD, SIG_DFL); /* for child restore default handler */

        if (cmd.stdin_fd != STDIN_FILENO) /* set redirections of io */
            dup2(cmd.stdin_fd, STDIN_FILENO);
        if (cmd.stdout_fd != STDOUT_FILENO)
            dup2(cmd.stdout_fd, STDOUT_FILENO);

        execvp(cmd.cmd_name, cmd.argv);
        perror(cmd.cmd_name);
        _exit(1);
    } else if (pid == -1) {
        res->type = failed;
        goto deinit;
    }

    if (cmd.stdin_fd != -1)
        close(cmd.stdin_fd);
    if (cmd.stdout_fd != -1)
        close(cmd.stdout_fd);

    if (cmd.run_in_background) {
        res->type = noproc;
        goto deinit;
    }

    signal(SIGCHLD, SIG_DFL); /* remove additional wait cycle */
    while ((wr = wait(&status)) != pid) {
        if (wr == -1) {
            res->type = failed;
            goto deinit;
        }
    }
    signal(SIGCHLD, chld_handler); /* restore handler */

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
