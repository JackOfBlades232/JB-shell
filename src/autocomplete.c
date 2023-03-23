/* Toy-Shell/src/autocomplete.c */
#include "autocomplete.h"
#include "lookup.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { max_items_in_line = 6 };

enum query_type { invalid, path_executable, file };

int char_is_single_prog_sep(char c)
{
    return c == '|' || c == '&' || c == '(' || c == ')' || c == ';';
}

int is_end_of_prog_separator(const char *bufpos, const char *buf)
{
    if (bufpos-buf <= 0)
        return 0;
    else
        return char_is_single_prog_sep(*bufpos);
}

int char_is_single_file_sep(char c)
{
    return c == '<' || c == '>';
}

int is_end_of_file_separator(const char *bufpos, const char *buf)
{
    if (bufpos-buf <= 0)
        return 0;
    else 
        return char_is_single_file_sep(*bufpos);
}

int is_end_of_any_separator(const char *bufpos, const char *buf)
{
    return char_is_separator(*bufpos) ||
        is_end_of_prog_separator(bufpos, buf) ||
        is_end_of_file_separator(bufpos, buf);
}

int can_complete_here(struct positional_buffer *out_pbuf)
{
    return out_pbuf->bufpos > out_pbuf->buf &&
        !is_end_of_any_separator(out_pbuf->bufpos-1, out_pbuf->buf) &&
        (
         out_pbuf->bufpos == out_pbuf->bufend || 
         is_end_of_any_separator(out_pbuf->bufpos, out_pbuf->buf)
        ); 
}

enum query_type check_autocomplete_type(struct positional_buffer *out_pbuf)
{
    if (!can_complete_here(out_pbuf))
        return invalid;

    /* TODO: remake to last separator cheking */
    return path_executable;
}

char *get_autocomplete_prefix_copy(struct positional_buffer *out_pbuf)
{
    size_t len = 0;
    char *prefix_copy;
    char *buf_ptr = out_pbuf->bufpos - 1;

    while (buf_ptr - out_pbuf->buf > 0 &&
            !is_end_of_any_separator(buf_ptr, out_pbuf->buf)) {
        buf_ptr--;
        len++;
    }
    if (is_end_of_any_separator(buf_ptr, out_pbuf->buf))
        buf_ptr++;
    else
        len++;

    prefix_copy = malloc(len+1);
    strncpy(prefix_copy, buf_ptr, len);
    prefix_copy[len] = '\0';

    return prefix_copy;
}

char *item_suffix(char *item, const char *prefix)
{
    char *suf = item;
    size_t i;

    for (i = 0; i < strlen(prefix); i++)
        suf++;

    return suf;
}

void complete_single_item(struct positional_buffer *out_pbuf, 
        struct string_set *item_set, const char *prefix)
{
    char *item = string_set_pop_any(item_set);
    char *suf = item_suffix(item, prefix);

    add_word(out_pbuf, suf);

    free(item);
}

void display_multiple_options(struct positional_buffer *out_pbuf, 
        struct string_set *item_set)
{
    const char *buf_ptr = out_pbuf->bufpos;
    char *item;
    int item_counter;

    while (*buf_ptr) {
        putchar(*buf_ptr);
        buf_ptr++;
    }
     
    putchar('\n');

    item_counter = 0;
    while (!string_set_is_empty(item_set)) {
        if (item_counter >= max_items_in_line) {
            putchar('\n');
            item_counter = 0;
        }

        item = string_set_pop_any(item_set);
        printf("%s ", item);
        free(item);
        
        item_counter++;
    }

    putchar('\n');
    redraw_full_buf(out_pbuf);
    fflush(stdout);
}

void display_too_many_options_msg(struct positional_buffer *out_pbuf)
{
    const char *buf_ptr = out_pbuf->bufpos;
    while (*buf_ptr) {
        putchar(*buf_ptr);
        buf_ptr++;
    }

    printf("\nToo many options\n");
    redraw_full_buf(out_pbuf);
    fflush(stdout);
}

void try_autocomplete(struct positional_buffer *out_pbuf)
{
    enum query_type q_type;
    struct query_result q_res;
    char *prefix_copy;


    q_type = check_autocomplete_type(out_pbuf);
    if (q_type == invalid)
        return;

    prefix_copy = get_autocomplete_prefix_copy(out_pbuf);
    if (q_type == path_executable)
        q_res = perform_path_lookup(prefix_copy);
    else
        q_res = perform_fs_lookup(prefix_copy);

    switch (q_res.type) {
        case not_found:
            break;
        case single:
            complete_single_item(out_pbuf, q_res.set, prefix_copy);
            break;
        case multiple:
            display_multiple_options(out_pbuf, q_res.set);
            break;
        case too_many:
            display_too_many_options_msg(out_pbuf);
            break;
    }

    free_query_result_mem(&q_res);
}
