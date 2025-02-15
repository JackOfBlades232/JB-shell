/* Toy-Shell/src/buffer.h */
#ifndef STRING_SENTRY
#define STRING_SENTRY

#include "def.h"
#include "debug.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct buffer_tag {
    char *p;
    u64 sz;
} buffer_t;

static inline b32 buffer_is_valid(const buffer_t *s)
{
    return s->p ? true : false;
}

static inline void clear_buffer(buffer_t *s)
{
    s->p = NULL;
    s->sz = 0;
}

static inline buffer_t allocate_buffer(u64 sz)
{
    buffer_t s = {};
    s.p = malloc(sz + 1);
    s.sz = s.p ? sz : 0;
    return s;
}

static inline void free_buffer(buffer_t *s)
{
    ASSERT(buffer_is_valid(s));
    free(s->p);
    clear_buffer(s);
}

#endif
