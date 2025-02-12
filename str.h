/* Toy-Shell/src/str.h */
#ifndef STR_SENTRY
#define STR_SENTRY

#include "def.h"
#include "debug.h"

#include <stddef.h>

static_assert(sizeof(char) == sizeof(u8));

typedef struct string_tag {
    char *p;
    u64 len;
} string_t;

static inline b32 string_is_empty(const string_t *s)
{
    return (s->p && s->len > 0) ? false : true;
}

static inline void clear_string(string_t *s)
{
    s->p = NULL;
    s->len = 0;
}

#define STR_PRINTF_ARGS(str_) (int)((str_).len), ((str_).p)

#endif
