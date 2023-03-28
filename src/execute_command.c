/* Toy-Shell/src/execute_command.c */
#include "execute_command.h"
#include "parse_command.h"
#include "int_set.h"

#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
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

static void try_execute_cd(struct command *cmd, struct command_res *res)
{
    const char *dir;

    if (cmd->argc > 2) {
        res->type = failed;
        return;
    }

    if (cmd->argc == 1) {
        if ((dir = getenv("HOME")) == NULL)
            dir = getpwuid(getuid())->pw_dir;
    } else
        dir = cmd->argv[1];

    res->type = chdir(dir) == -1 ? failed : noproc;
}

static void close_additional_descriptors(struct command *cmd)
{
    if (cmd->stdin_fd != -1 && cmd->stdin_fd != STDIN_FILENO)
        close(cmd->stdin_fd);
    if (cmd->stdout_fd != -1 && cmd->stdout_fd != STDOUT_FILENO)
        close(cmd->stdout_fd);
}

static void close_all_additional_descriptors(struct command_chain *cmd_chain)
{
    map_to_all_cmds_in_chain(cmd_chain, close_additional_descriptors);
}

static int execute_next_command(struct command_chain *cmd_chain)
{
    struct command *cmd;
    int pid;

    cmd = get_first_cmd_in_chain(cmd_chain);
    if (cmd == NULL)
        return -1;

    pid = fork();
    if (pid == 0) { /* child proc */
        signal(SIGCHLD, SIG_DFL); /* for child restore default handler */

        if (cmd->stdin_fd != -1) /* set redirections of io */
            dup2(cmd->stdin_fd, STDIN_FILENO);
        if (cmd->stdout_fd != -1)
            dup2(cmd->stdout_fd, STDOUT_FILENO);
        close_all_additional_descriptors(cmd_chain);

        execvp(cmd->cmd_name, cmd->argv);
        perror(cmd->cmd_name);
        _exit(1);
    } 

    close_additional_descriptors(cmd);
    delete_first_cmd_from_chain(cmd_chain);

    return pid;
}

static int spawn_processes_for_all_commands(
        struct command_chain *cmd_chain,
        struct int_set *pids,
        int *last_proc_pid,
        int is_bg
        )
{
    int pid;

    while (!cmd_chain_is_empty(cmd_chain)) {
        pid = execute_next_command(cmd_chain);
        if (pid == -1)
            return 0;

        if (!is_bg) {
            int_set_add(pids, pid);
            *last_proc_pid = pid;
        }
    }

    return 1;
}

static void set_group_to_fg(int pgid)
{
    signal(SIGTTOU, SIG_IGN); /* otherwise tcsetpgrp will freeze it */
    tcsetpgrp(0, pgid);
    signal(SIGTTOU, SIG_DFL);
}

static int run_proc_group(struct command_chain *cmd_chain)
{
    int pid;
    int status, wr;
    int last_proc_pid = -1,
        exit_code = 0;

    int is_bg = cmd_chain_is_background(cmd_chain);
    struct int_set *pids = NULL;

    pid = fork();
    if (pid != 0)
        return pid;

    /* spawn new group */
    pid = getpid();
    setpgid(pid, pid);

    /* change cur pgroup if not a background proc */
    if (!is_bg) {
        set_group_to_fg(pid);
        pids = create_int_set();
    }

    /* if not cd, spin up all procs in chain, and save their pids if non-bg */
    spawn_processes_for_all_commands(cmd_chain, pids, &last_proc_pid, is_bg);

    /* if running in background, skip the wait cycle */
    if (is_bg) {
        exit_code = 0;
        goto gleader_deinit;
    }

    /* else, wait until all processes from the saved pids set finish */
    signal(SIGCHLD, SIG_DFL); /* remove possible interrupting wait cycle */
    while (!int_set_is_empty(pids)) {
        wr = wait(&status);
        if (wr == -1) {
            exit_code = 1;
            goto gleader_deinit;
        } else if (wr == last_proc_pid) { /* save last proc in pipe res */
            if (WIFEXITED(status))
                exit_code = WEXITSTATUS(status);
            else
                exit_code = -1;
        }

        int_set_remove(pids, wr);
    }
    signal(SIGCHLD, chld_handler); /* restore handler */
    
gleader_deinit:
    if (pids != NULL)
        free_int_set(pids);
    free_cmd_chain(cmd_chain);
    _exit(exit_code);
}
            
int execute_cmd(struct word_list *tokens, struct command_res *res)
{
    int pgid;
    int status, wr;
    struct command_chain *cmd_chain;
    
    /* return value != 0 only if empty cmd given */
    if (word_list_is_empty(tokens))
        return 1;

    /* parse out all commands and prepare additional io streams */
    cmd_chain = parse_tokens_to_cmd_chain(tokens, res);
    if (cmd_chain == NULL)
        goto deinit;

    /* deal with cd command, as it can not be spawned as a separate proc 
     * ( that would not change the current dir of the interpretor ) */
    if (chain_contains_cmd(cmd_chain, "cd")) {
        /* cd should not be used in a pipe */
        if (cmd_chain_len(cmd_chain) == 1)
            try_execute_cd(get_first_cmd_in_chain(cmd_chain), res);
        else
            res->type = failed;

        goto deinit;
    }

    /* create new group with intermediary g-leader proc and run command */
    pgid = run_proc_group(cmd_chain);
    close_all_additional_descriptors(cmd_chain);
    if (pgid == -1) {
        res->type = failed;
        goto deinit;
    }

    /* if running in background, skip the wait */
    if (cmd_chain_is_background(cmd_chain)) {
        res->type = noproc;
        goto deinit;
    }

    /* else, wait for group leader (who waits for everybody else) */
    signal(SIGCHLD, SIG_DFL); /* remove possible interrupting wait cycle */
    for (;;) {
        wr = wait(&status);
        if (wr == -1 || wr == pgid)
            break;
    }
    signal(SIGCHLD, chld_handler); /* restore handler */
    set_group_to_fg(getpgid(getpid()));

    /* parse result to res struct */
    if (wr == -1)
        res->type = noproc;
    else if (WIFEXITED(status)) {
        res->type = exited;
        res->code = WEXITSTATUS(status);
    } else {
        res->type = killed;
        res->code = WSTOPSIG(status);
    }

deinit:
    if (cmd_chain != NULL)
        free_cmd_chain(cmd_chain);
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
