/* Toy-Shell/tests.c */
#include "src/edit/lookup.h"
#include "src/utils/string_set.h"

#include <stdio.h>
#include <stdlib.h>

void test_lookup_func(const char *query,
        struct query_result (*lookup_func)(const char *) )
{
    struct query_result q_res;
    char *elem;

    q_res = (*lookup_func)(query);
    switch (q_res.type) {
        case not_found: 
            printf("Not found\n");
            break;
        case single:
            elem = string_set_pop_any(q_res.set);
            printf("Single: %s\n", elem);
            free(elem);
            break;
        case multiple:
            while (!string_set_is_empty(q_res.set)) {
                elem = string_set_pop_any(q_res.set);
                puts(elem);
                free(elem);
            }
            break;
        case too_many:
            printf("Too many results\n");
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 1;

    test_lookup_func(argv[1], perform_path_lookup);
    putchar('\n');
    test_lookup_func(argv[1], perform_fs_lookup);

    return 0;
}
