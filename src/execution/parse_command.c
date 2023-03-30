/* Toy-Shell/src/execution/parse_command.c */
#include "parse_command.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int is_chain_end(struct word *w)
{
    if (!w) /* end of tokens is a chain separator, in a way */
        return 1;
    if (w->wtype == regular_wrd)
        return 0;
    return 
        strcmp(w->content, ";") == 0 ||
        // strcmp(w->content, "&") == 0 ||
        strcmp(w->content, "&&") == 0 ||
        strcmp(w->content, "||") == 0;
}

static int word_is_file_io_separator(struct word *w)
{
    if (w->wtype == regular_wrd)
        return 0;
    return 
        strcmp(w->content, "&") == 0 || // remove
        strcmp(w->content, ">") == 0 ||
        strcmp(w->content, "<") == 0 ||
        strcmp(w->content, ">>") == 0;
}

static int word_is_inter_cmd_separator(struct word *w)
{
    return w->wtype == separator && 
        !is_chain_end(w) &&
        !word_is_file_io_separator(w);
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
    if (w->wtype == regular_wrd) {
        cmd->stdin_fd = open(w->content, O_RDONLY);
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
    if (w->wtype == regular_wrd) {
        cmd->stdout_fd = open(w->content, mode, 0666);
        if (cmd->stdout_fd != -1)
            res = 1;
    }

    word_free(w);
    return res;
}

static int process_end_separator(
        struct word *w, 
        struct command_chain *cmd_chain,
        struct word_list *remaining_tokens)
{
    int res = 0;

    if (strcmp(w->content, "&") == 0) {
        set_cmd_chain_to_background(cmd_chain);
        res = word_list_is_empty(remaining_tokens);
    } else if (strcmp(w->content, "<") == 0) {
        res = prepare_stdin_redirection(cmd_chain, remaining_tokens);
    } else if (strcmp(w->content, ">") == 0) {
        res = prepare_stdout_redirection(cmd_chain, remaining_tokens, 0);
    } else if (strcmp(w->content, ">>") == 0) {
        res = prepare_stdout_redirection(cmd_chain, remaining_tokens, 1);
    }

    word_free(w);
    return res;
}

static int prepare_pipe_for_two_commands(
        struct command *sender,
        struct command *reciever)
{
    int fd[2];

    if (sender->stdout_fd != -1 || reciever->stdin_fd != -1)
        return 0;
    
    pipe(fd);
    sender->stdout_fd = fd[1]; 
    reciever->stdin_fd = fd[0];
    return 1;
}

static int try_add_cmd_to_pipe(struct command_chain *cmd_chain)
{
    struct command 
        *last_cmd = get_last_cmd_in_chain(cmd_chain),
        *new_cmd;

    if (last_cmd == NULL || cmd_is_empty(last_cmd))
        return 0;

    new_cmd = add_cmd_to_chain(cmd_chain);
    return prepare_pipe_for_two_commands(last_cmd, new_cmd);
}

static int process_inter_cmd_separator(
        struct word *w, 
        struct command_chain *cmd_chain,
        struct word_list *remaining_tokens)
{
    int res = 0;

    if (strcmp(w->content, "|") == 0)
        res = try_add_cmd_to_pipe(cmd_chain);

    word_free(w);
    return res;
}

static void process_regular_word(struct word *w,
        struct command_chain *cmd_chain)
{
    add_arg_to_last_chain_cmd(cmd_chain, w->content);
    free(w); /* still need the content in cmd */
}

static int parse_main_command_part(
        struct command_chain *cmd_chain,
        struct word_list *tokens, 
        struct command_res *res,
        struct word **next_w)
{
    struct word *w;

    while (!is_chain_end(w = word_list_pop_first(tokens))) {
        if (word_is_file_io_separator(w))
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

    while (!is_chain_end(w)) {
        int sep_res;

        if (!word_is_file_io_separator(w)) {
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

struct command_chain *parse_tokens_to_cmd_chain(
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
        return cmd_chain;
    } else {
        free_cmd_chain(cmd_chain);
        return NULL;
    }
}
