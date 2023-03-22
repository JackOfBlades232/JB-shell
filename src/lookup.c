/* Toy-Shell/src/lookup.c */
#include "lookup.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <stdio.h>

extern char **environ;

enum { max_query_size = 64 };

int name_matches_prefix(const char *name, const char *prefix)
{
    for (; *name; name++) {
        if (*prefix == '\0' || *prefix != *name)
            break;
        else 
            prefix++;
    }
    return *prefix == '\0';
}

char *replace_dots_with_zero_char(char *str)
{
    while (*str && *str != ':')
        str++;
    if (*str) {
        *str = '\0';
        return str;
    } else
        return NULL;
}

int match_prefix_with_names_in_dir(
        char *dirname, const char *prefix,
        char ***result_next_ptr, char **result
        )
{
    DIR *dir;
    struct dirent *dent;

    int match_cnt;

    char *dots_ptr;

    /* trick for looking up from path with no allocations */
    dots_ptr = replace_dots_with_zero_char(dirname);

    dir = opendir(dirname);
    if (dots_ptr) /* restore dots */
        *dots_ptr = ':';
    if (!dir)
        return -1;

    match_cnt = 0;
    while ((dent = readdir(dir)) != NULL) {
        if (
                dent->d_type == DT_REG &&
                name_matches_prefix(dent->d_name, prefix)
           ) {
            int mem_len;
            if (*result_next_ptr-result >= max_query_size)
                return max_query_size+1;

            mem_len = strlen(dent->d_name);
            **result_next_ptr = malloc(mem_len+1);
            strncpy(**result_next_ptr, dent->d_name, mem_len);
            (*result_next_ptr)++;
            **result_next_ptr = NULL;
            match_cnt++;
        }
    }

    return match_cnt;
}

void advance_path_pointer(char **path_p)
{
    while (**path_p != ':') {
        if (**path_p == '\0')
            return;

        (*path_p)++;
    }

    (*path_p)++;
} 

char *perform_path_lookup(const char *prefix)
{
    char *path;
    char **result = malloc((max_query_size+1) * sizeof(char *)),
        **result_next_p = result;

    int match_cnt;
    
    *result = NULL;
    for (path = getenv("PATH"); *path; advance_path_pointer(&path)) {
        match_cnt = match_prefix_with_names_in_dir(
                path, prefix, &result_next_p, result
                );
        if (match_cnt > max_query_size)
            break;
    }

    if (match_cnt <= max_query_size) {
        for (result_next_p = result; *result_next_p; result_next_p++) {
            puts(*result_next_p);
            free(*result_next_p);
        }
    } else
        printf("Too many matches\n");

    free(result);

    return NULL;
}

char *perform_fs_lookup(const char *prefix)
{
    return NULL;
}
