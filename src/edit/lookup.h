/* Toy-Shell/src/edit/lookup.h */
#ifndef LOOKUP_SENTRY
#define LOOKUP_SENTRY

#include "../utils/string_set.h"

enum query_result_type { not_found, single, multiple, too_many };

struct query_result {
    enum query_result_type type;
    struct string_set *set;
};

struct query_result perform_path_lookup(const char *prefix);
struct query_result perform_fs_lookup(const char *prefix);

void free_query_result_mem(struct query_result *q_res);

#endif
