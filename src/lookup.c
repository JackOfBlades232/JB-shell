/* Toy-Shell/src/lookup.c */
#include "lookup.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <stdio.h>

extern char **environ;

enum { max_query_size = 32 };

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

int file_is_executable(const char *dirname, const char *filename)
{
    int res;

    struct stat st;
    int stat_res;

    size_t dirnm_len = strlen(dirname),
           filenm_len = strlen(filename);
    char *full_filename = malloc(dirnm_len+filenm_len+2);

    strncpy(full_filename, dirname, dirnm_len);
    full_filename[dirnm_len] = '/';
    strncpy(full_filename+dirnm_len+1, filename, filenm_len);
    full_filename[dirnm_len+filenm_len+1] = '\0';

    stat_res = stat(full_filename, &st);
    if (stat_res == -1) {
        res = 0;
        goto deinit;
    }

    res = S_ISREG(st.st_mode) &&
        ((st.st_mode & 0111) != 0); /* at least one of x bits is 1 */

deinit:
    free(full_filename);
    return res;
}

int match_prefix_with_names_in_dir(
        char *dirname, const char *prefix,
        struct string_set *query_res_set,
        int is_path
        )
{
    int res;

    DIR *dir;
    struct dirent *dent;

    char *dots_ptr = NULL; 

    /* trick for looking up from path with no allocations */
    if (is_path)
        dots_ptr = replace_dots_with_zero_char(dirname);

    dir = opendir(dirname);
    if (!dir) {
        res = 0;
        goto deinit;
    }

    while ((dent = readdir(dir)) != NULL) {
        if (
                dent->d_type == DT_REG &&
                name_matches_prefix(dent->d_name, prefix) &&
                (!is_path || file_is_executable(dirname, dent->d_name))
           ) {
            string_set_add(query_res_set, dent->d_name);
        }
    }

    res = 1;

deinit:
    if (dots_ptr) /* restore dots */
        *dots_ptr = ':';
    return res;
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

void assign_query_result_type(struct query_result *q_res)
{
    int set_size = string_set_size(q_res->set);

    if (set_size == 0) {
        q_res->type = not_found;
        free_string_set(q_res->set);
    } else if (set_size == 1)
        q_res->type = single;
    else if (set_size <= max_query_size)
        q_res->type = multiple;
    else
        q_res->type = too_many;
}

struct query_result perform_path_lookup(const char *prefix)
{
    char *path;
    struct query_result q_res;
    
    q_res.set = create_string_set();

    for (path = getenv("PATH"); *path; advance_path_pointer(&path)) {
        match_prefix_with_names_in_dir(path, prefix, q_res.set, 1);
        if (string_set_size(q_res.set) > max_query_size)
            break;
    }

    assign_query_result_type(&q_res);
    return q_res;
}

char *try_replace_last_slash_with_zero(char *filepath)
{
    char *fpp;
    for (fpp = filepath; *fpp; fpp++)
        {}
    while (fpp-filepath > 0 && *fpp != '/')
        fpp--;
    if (*fpp != '/')
        return filepath;
    *fpp = '\0';
    return fpp + 1;
}

struct query_result perform_fs_lookup(const char *prefix)
{
    /* refac? */
    char *mut_prefix = (char *) prefix;
    char *filenm_prefix;
    filenm_prefix = try_replace_last_slash_with_zero(mut_prefix);

    struct query_result q_res;
    q_res.set = create_string_set();

    if (filenm_prefix == mut_prefix)
        match_prefix_with_names_in_dir("./", filenm_prefix, q_res.set, 0);        
    else {
        match_prefix_with_names_in_dir(mut_prefix, filenm_prefix, q_res.set, 0);        
        filenm_prefix--;
        *filenm_prefix = '/';
    }

    assign_query_result_type(&q_res);
    return q_res;
}

void free_query_result_mem(struct query_result *q_res)
{
    free_string_set(q_res->set);
    q_res->set = NULL;
}
