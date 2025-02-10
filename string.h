/* Toy-Shell/src/string.h */
#ifndef STRING_SENTRY
#define STRING_SENTRY

#include "def.h"
#include "debug.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct string_tag {
    char *p;
    u64 len;
} string_t;

static inline b32 string_is_valid(const string_t *s)
{
    return s->p ? true : false;
}

static inline void clear_string(string_t *s)
{
    s->p = NULL;
    s->len = 0;
}

static inline string_t allocate_string(u64 len)
{
    string_t s = {};
    s.p = malloc(len + 1);
    s.len = s.p ? len : 0;
    return s;
}

static inline void free_string(string_t *s)
{
    assert(string_is_valid(s));
    free(s->p);
    clear_string(s);
}

#endif
