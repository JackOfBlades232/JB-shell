/* Toy-Shell/src/autocomplete.c */
#include "autocomplete.h"
#include "lookup.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { max_items_in_line = 6 };

enum query_type { invalid, path_executable, file };

static int is_end_of_prog_separator(char c)
{
    return c == '|' || c == '&' || c == '(' || c == ')' || c == ';';
}

static int is_end_of_file_separator(char c)
{
    return c == '<' || c == '>';
}

static int is_end_of_any_separator(char c)
{
    return char_is_separator(c) ||
        is_end_of_prog_separator(c) ||
        is_end_of_file_separator(c);
}

static int can_complete_here(struct positional_buffer *out_pbuf)
{
    return out_pbuf->bufpos > out_pbuf->buf &&
        !is_end_of_any_separator(*(out_pbuf->bufpos-1)) &&
        (
         out_pbuf->bufpos == out_pbuf->bufend || 
         is_end_of_any_separator(*out_pbuf->bufpos)
        ); 
}

static enum query_type check_autocomplete_type(
        struct positional_buffer *out_pbuf)
{
    char *buf_ptr;
    if (!can_complete_here(out_pbuf))
        return invalid;

    buf_ptr = out_pbuf->bufpos - 1;
    while (buf_ptr - out_pbuf->buf > 0 && 
            !is_end_of_any_separator(*buf_ptr))
        buf_ptr--;

    if (buf_ptr == out_pbuf->buf || 
            is_end_of_prog_separator(*buf_ptr))
        return path_executable;
    else if (is_end_of_file_separator(*buf_ptr))
        return file;

    while (buf_ptr - out_pbuf->buf > 0 && char_is_separator(*buf_ptr))
        buf_ptr--;

    if (buf_ptr == out_pbuf->buf || 
            is_end_of_prog_separator(*buf_ptr))
        return path_executable;
    else
        return file;
}

static char *get_autocomplete_prefix_copy(struct positional_buffer *out_pbuf)
{
    size_t len = 0;
    char *prefix_copy;
    char *buf_ptr = out_pbuf->bufpos - 1;

    while (buf_ptr - out_pbuf->buf > 0 &&
            !is_end_of_any_separator(*buf_ptr)) {
        buf_ptr--;
        len++;
    }
    if (is_end_of_any_separator(*buf_ptr))
        buf_ptr++;
    else
        len++;

    prefix_copy = malloc(len+1);
    strncpy(prefix_copy, buf_ptr, len);
    prefix_copy[len] = '\0';

    return prefix_copy;
}

static size_t prefix_without_dir_len(const char *prefix)
{
    size_t len = 0;
    for (; *prefix; prefix++) {
        if (*prefix == '/')
            len = 0;
        else
            len++;
    }
    
    return len;
}

static char *item_suffix(char *item, const char *prefix)
{
    char *suf = item;
    size_t i;

    for (i = 0; i < prefix_without_dir_len(prefix); i++)
        suf++;

    return suf;
}

static void complete_single_item(struct positional_buffer *out_pbuf, 
        struct string_set *item_set, const char *prefix)
{
    char *item = string_set_pop_any(item_set);
    char *suf = item_suffix(item, prefix);

    add_word(out_pbuf, suf);

    free(item);
}

static void display_multiple_options(struct positional_buffer *out_pbuf, 
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

static void display_too_many_options_msg(struct positional_buffer *out_pbuf)
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
