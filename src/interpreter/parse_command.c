/* Toy-Shell/src/execution/parse_command.c */
#include "parse_command.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int is_pipe_end(struct word *w)
{
    if (!w) /* end of tokens is a pipe separator, in a way */
        return 1;
    if (w->wtype == regular_wrd)
        return 0;
    return 
        strcmp(w->content, ";") == 0 ||
        strcmp(w->content, "&") == 0 ||
        strcmp(w->content, "&&") == 0 ||
        strcmp(w->content, "||") == 0;
}

static int word_is_pipe_end_separator(struct word *w)
{
    if (w->wtype == regular_wrd)
        return 0;
    return 
        strcmp(w->content, ">") == 0 ||
        strcmp(w->content, "<") == 0 ||
        strcmp(w->content, ">>") == 0;
}

static int word_is_open_paren(struct word *w)
{
    return w->wtype == separator && strcmp(w->content, "(") == 0;
}

static int word_is_close_paren(struct word *w)
{
    return w->wtype == separator && strcmp(w->content, ")") == 0;
}

static int word_is_inter_cmd_separator(struct word *w)
{
    return w->wtype == separator && strcmp(w->content, "|") == 0;
}

static int prepare_stdin_redirection(struct command_pipe *cmd_pipe,
        struct word_list *remaining_tokens)
{
    struct command *cmd = get_first_cmd_in_pipe(cmd_pipe);
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

static int prepare_stdout_redirection(struct command_pipe *cmd_pipe, 
        struct word_list *remaining_tokens, int is_append)
{
    struct command *cmd = get_last_cmd_in_pipe(cmd_pipe);
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
        struct command_pipe *cmd_pipe,
        struct word_list *remaining_tokens)
{
    int res = 0;

    if (cmd_pipe_is_empty(cmd_pipe)) {
        word_free(w);
        return 0;
    }

    if (strcmp(w->content, "<") == 0) {
        res = prepare_stdin_redirection(cmd_pipe, remaining_tokens);
    } else if (strcmp(w->content, ">") == 0) {
        res = prepare_stdout_redirection(cmd_pipe, remaining_tokens, 0);
    } else if (strcmp(w->content, ">>") == 0) {
        res = prepare_stdout_redirection(cmd_pipe, remaining_tokens, 1);
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

static int try_add_cmd_to_pipe(struct command_pipe *cmd_pipe)
{
    struct command 
        *last_cmd = get_last_cmd_in_pipe(cmd_pipe),
        *new_cmd;

    if (last_cmd == NULL || cmd_is_empty(last_cmd))
        return 0;

    new_cmd = add_cmd_to_pipe(cmd_pipe);
    return prepare_pipe_for_two_commands(last_cmd, new_cmd);
}

static int process_inter_cmd_separator(
        struct word *w, 
        struct command_pipe *cmd_pipe,
        struct word_list *remaining_tokens)
{
    int res = 0;

    if (strcmp(w->content, "|") == 0)
        res = try_add_cmd_to_pipe(cmd_pipe);

    word_free(w);
    return res;
}

static int process_regular_word(struct word *w,
        struct command_pipe *cmd_pipe)
{
    struct command *last_cmd = get_last_cmd_in_pipe(cmd_pipe);

    if (cmd_is_uninitialized(last_cmd))
        init_exec_cmd(last_cmd);
    else if (cmd_is_rec(last_cmd)) {
        word_free(w);
        return 0;
    }

    add_arg_to_last_pipe_cmd(cmd_pipe, w->content);
    free(w); /* still need the content in cmd */
    return 1;
}

static int parse_recursive_call(
        struct command_pipe *cmd_pipe,
        struct word_list *tokens
        )
{
    struct command *last_cmd = get_last_cmd_in_pipe(cmd_pipe);

    struct word_list *rec_tokens;
    struct word_item *pre_prev, *prev;
    int paren_balance = 1; // 1 for one open (

    if (!cmd_is_uninitialized(last_cmd))
        return 0;
    init_rec_cmd(last_cmd);

    rec_tokens = word_list_create();
    prev = pre_prev = NULL;
    rec_tokens->first = tokens->first;

    // cut out to end of list or till all ( matched
    while (tokens->first && paren_balance > 0) {
        if (word_is_open_paren(tokens->first->wrd))
            paren_balance++;
        else if (word_is_close_paren(tokens->first->wrd))
            paren_balance--;

        pre_prev = prev;
        prev = tokens->first;
        tokens->first = tokens->first->next;
    }

    if (paren_balance != 0) {
        word_list_free(rec_tokens);
        tokens->last = NULL;
        return 0;
    }

    rec_tokens->last = pre_prev;
    pre_prev->next = NULL;
    free_word_item(prev); // free last )
    
    free_pipe_seq(last_cmd->rec_seq);
    last_cmd->rec_seq = parse_tokens_to_pipe_seq(rec_tokens);
    if (last_cmd->rec_seq == NULL) {
        word_list_free(rec_tokens);
        return 0;
    }

    return 1;
}

static int parse_main_command_part(
        struct command_pipe *cmd_pipe,
        struct word_list *tokens, 
        struct word **next_w)
{
    struct word *w;
    int sep_res;

    add_cmd_to_pipe(cmd_pipe);

    while (!is_pipe_end(w = word_list_pop_first(tokens))) {
        if (word_is_pipe_end_separator(w))
            break;
        else if (word_is_close_paren(w))
            return 0;
        else if (word_is_open_paren(w)) {
            sep_res = parse_recursive_call(cmd_pipe, tokens);
            if (!sep_res)
                return 0;
        } else if (word_is_inter_cmd_separator(w)) {
            sep_res = process_inter_cmd_separator(w, cmd_pipe, tokens);
            if (sep_res <= 0)
                return 0;
        } else 
            process_regular_word(w, cmd_pipe);
    }

    *next_w = w;
    return 1;
}

static int parse_command_end(
        struct command_pipe *cmd_pipe,
        struct word_list *tokens, 
        struct word **next_w)
{
    while (!is_pipe_end(*next_w)) {
        int sep_res;

        if (!word_is_pipe_end_separator(*next_w)) {
            word_free(*next_w);
            return 0;
        }

        sep_res = process_end_separator(*next_w, cmd_pipe, tokens);
        if (sep_res <= 0)
            return 0;

        *next_w = word_list_pop_first(tokens);
    }

    return 1;
}

static enum pipe_sequence_rule get_sequence_rule_of_word(struct word *w)
{
    if (!w || w->wtype != separator)
        return none;

    if (strcmp(w->content, ";") == 0)
        return always;
    else if (strcmp(w->content, "&") == 0)
        return to_bg;
    else if (strcmp(w->content, "&&") == 0)
        return if_success;
    else if (strcmp(w->content, "||") == 0)
        return if_failed;
    else
        return none;
}

static struct command_pipe *parse_tokens_to_cmd_pipe(
        struct word_list *tokens,
        enum pipe_sequence_rule *rule_out
        )
{
    int parse_res;
    struct command_pipe *cmd_pipe;
    struct word *last_w;

    cmd_pipe = create_cmd_pipe();

    parse_res = 
        parse_main_command_part(cmd_pipe, tokens, &last_w) &&
        parse_command_end(cmd_pipe, tokens, &last_w);

    if (parse_res) {
        *rule_out = get_sequence_rule_of_word(last_w);
        if (*rule_out == to_bg)
            set_cmd_pipe_to_background(cmd_pipe);
        if (last_w)
            word_free(last_w);
        return cmd_pipe;
    } else {
        free_cmd_pipe(cmd_pipe);
        return NULL;
    }
}

int seq_node_is_valid(struct pipe_sequence_node *node)
{
    return !cmd_pipe_is_empty(node->pipe) ||
        (node->rule == none || node->rule == always);
}

struct pipe_sequence *parse_tokens_to_pipe_seq(struct word_list *tokens)
{
    struct pipe_sequence *pipe_seq;
    struct command_pipe *cmd_pipe;
    enum pipe_sequence_rule rule;

    pipe_seq = create_pipe_seq();

    while (!word_list_is_empty(tokens)) {
        cmd_pipe = parse_tokens_to_cmd_pipe(tokens, &rule);
        if (!cmd_pipe) {
            free_pipe_seq(pipe_seq);
            return NULL;
        }

        add_pipe_to_seq(pipe_seq, cmd_pipe, rule);
        if (!seq_node_is_valid(pipe_seq->last)) {
            free_pipe_seq(pipe_seq);
            return NULL;
        }
    }

    return pipe_seq;
}
