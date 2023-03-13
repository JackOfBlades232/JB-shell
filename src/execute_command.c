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

static int prepare_stdin_redirection(struct command_chain *cmd_chain,
        struct word_list *remaining_tokens)
{
    struct command *cmd = get_first_cmd_in_chain(cmd_chain);
    struct word *w;
    int res = 0;

    if (word_list_is_empty(remaining_tokens))
        return 0;
    if (cmd->stdin_fd != -1)
        return 0;

    w = word_list_pop_first(remaining_tokens);
    if (!word_is_separator(w)) {
        cmd->stdin_fd = open(word_content(w), O_RDONLY);
        if (cmd->stdin_fd != -1)
            res = 1;
    }

    word_free(w);
    return res;

}

static int prepare_stdout_redirection(struct command_chain *cmd_chain, 
        struct word_list *remaining_tokens, int is_append)
{
    struct command *cmd = get_last_cmd_in_chain(cmd_chain);
    struct word *w;
    int res = 0;
    int mode = O_WRONLY | O_CREAT | (is_append ? O_APPEND : O_TRUNC);

    if (word_list_is_empty(remaining_tokens))
        return 0;
    if (cmd->stdout_fd != -1)
        return 0;

    w = word_list_pop_first(remaining_tokens);
    if (!word_is_separator(w)) {
        cmd->stdout_fd = open(word_content(w), mode, 0666);
        if (cmd->stdout_fd != -1)
            res = 1;
    }

    word_free(w);
    return res;

}

static int word_is_end_separator(struct word *w)
{
    char *sep;
    if (!word_is_separator(w))
        return 0;
    sep = word_content(w);
    return 
        strcmp(sep, "&") == 0 ||
        strcmp(sep, ">") == 0 ||
        strcmp(sep, "<") == 0 ||
        strcmp(sep, ">>") == 0;
}

static int word_is_inter_cmd_separator(struct word *w)
{
    return word_is_separator(w) && !word_is_end_separator(w);
}

static int process_end_separator(
        struct word *w, 
        struct command_chain *cmd_chain,
        struct word_list *remaining_tokens)
{
    char *sep = word_content(w);
    int res = 0;

    if (strcmp(sep, "&") == 0) {
        struct command *last_cmd = get_last_cmd_in_chain(cmd_chain);
        last_cmd->run_in_background = 1;
        res = word_list_is_empty(remaining_tokens);
    } else if (strcmp(sep, "<") == 0) {
        res = prepare_stdin_redirection(cmd_chain, remaining_tokens);
    } else if (strcmp(sep, ">") == 0) {
        res = prepare_stdout_redirection(cmd_chain, remaining_tokens, 0);
    } else if (strcmp(sep, ">>") == 0) {
        res = prepare_stdout_redirection(cmd_chain, remaining_tokens, 1);
    }

    word_free(w);
    return res;
}

static int process_inter_cmd_separator(
        struct word *w, 
        struct command_chain *cmd_chain,
        struct word_list *remaining_tokens)
{
    struct command *last_cmd = get_last_cmd_in_chain(cmd_chain);
    char *sep = word_content(w);
    int res = 0;

    if (strcmp(sep, "|") == 0) {
        if (last_cmd == NULL || command_is_empty(last_cmd))
            res = 0;
        else {
            add_cmd_to_chain(cmd_chain);
            res = 1;
        }
    } else if (strcmp(sep, "||") == 0) { /* not implemented */
        res = -1;
    } else if (strcmp(sep, "&") == 0) { /* will become inter-cmd */
        res = -1;
    } else if (strcmp(sep, "&&") == 0) {
        res = -1;
    } else if (strcmp(sep, "(") == 0) {
        res = -1;
    } else if (strcmp(sep, ")") == 0) {
        res = -1;
    } else if (strcmp(sep, ";") == 0) {
        res = -1;
    }

    word_free(w);
    return res;
}

static void process_regular_word(struct word *w,
        struct command_chain *cmd_chain)
{
    add_arg_to_last_chain_cmd(cmd_chain, word_content(w));
    free(w); /* still need the content in cmd */
}

static int parse_main_command_part(
        struct command_chain *cmd_chain,
        struct word_list *tokens, 
        struct command_res *res,
        struct word **next_w)
{
    struct word *w;

    while ((w = word_list_pop_first(tokens)) != NULL) {
        if (word_is_end_separator(w))
            break;
        else if (word_is_inter_cmd_separator(w)) {
            int sep_res;
            sep_res = process_inter_cmd_separator(w, cmd_chain, tokens);
            if (sep_res <= 0) {
                res->type = sep_res == -1 ? not_implemented : failed;
                return 0;
            }
        } else 
            process_regular_word(w, cmd_chain);
    }

    *next_w = w;
    return 1;
}

static int parse_command_end(
        struct command_chain *cmd_chain,
        struct word_list *tokens, 
        struct command_res *res,
        struct word *last_w)
{
    struct word *w = last_w;

    while (w != NULL) {
        int sep_res;

        if (!word_is_end_separator(w)) {
            res->type = failed;
            word_free(w);
            return 0;
        }

        sep_res = process_end_separator(w, cmd_chain, tokens);
        if (sep_res <= 0) {
            res->type = sep_res == -1 ? not_implemented : failed;
            return 0;
        }

        w = word_list_pop_first(tokens);
    }

    return 1;
}

static struct command_chain *parse_tokens_to_cmd_chain(
        struct word_list *tokens, struct command_res *res)
{
    int parse_res;
    struct command_chain *cmd_chain;
    struct word *last_w;

    cmd_chain = create_cmd_chain();
    add_cmd_to_chain(cmd_chain);

    parse_res = 
        parse_main_command_part(cmd_chain, tokens, res, &last_w) &&
        parse_command_end(cmd_chain, tokens, res, last_w);

    if (parse_res) {
        /* test */
        print_cmd_chain(cmd_chain);

        return cmd_chain;
    } else {
        free_command_chain(cmd_chain);
        return NULL;
    }
}

int execute_cmd(struct word_list *tokens, struct command_res *res)
{
    int pid;
    int status, wr;

    struct command_chain *cmd_chain;

    struct command *cmd = NULL; /* test */
    
    if (word_list_is_empty(tokens))
        return 1;

    cmd_chain = parse_tokens_to_cmd_chain(tokens, res);
    if (cmd_chain == NULL)
        goto deinit;

    cmd = get_first_cmd_in_chain(cmd_chain);

    if (try_execute_cd(cmd, res))
        goto deinit;

    pid = fork();
    if (pid == 0) { /* child proc */
        signal(SIGCHLD, SIG_DFL); /* for child restore default handler */

        if (cmd->stdin_fd != STDIN_FILENO) /* set redirections of io */
            dup2(cmd->stdin_fd, STDIN_FILENO);
        if (cmd->stdout_fd != STDOUT_FILENO)
            dup2(cmd->stdout_fd, STDOUT_FILENO);

        execvp(cmd->cmd_name, cmd->argv);
        perror(cmd->cmd_name);
        _exit(1);
    } else if (pid == -1) {
        res->type = failed;
        goto deinit;
    }

    if (cmd->stdin_fd != -1)
        close(cmd->stdin_fd);
    if (cmd->stdout_fd != -1)
        close(cmd->stdout_fd);

    if (cmd->run_in_background) {
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
    if (cmd_chain != NULL)
        free_command_chain(cmd_chain);
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
