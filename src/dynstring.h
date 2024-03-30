/* Toy-Shell/src/dynstring.h */
#ifndef DYNSTRING_SENTRY
#define DYNSTRING_SENTRY
#include "debug.h"

#include <stdlib.h>
#include <string.h>


typedef struct string_tag {
    char *p;
    size_t len, cap;
} string_t;

static inline string_t string_make(const char *p) 
{ 
    string_t s; 
    s.len = strlen(p);
    s.cap = s.len + 1;
    size_t byte_size = s.cap * sizeof(*s.p);
    s.p = (char *)malloc(byte_size);
    memcpy(s.p, p, byte_size);
    return s;
}

static inline void string_release(string_t *s)
{
    assert(s->p);
    s->p = NULL;
    s->cap = s->len = 0;
}

static inline void string_clear(string_t *s)
{
    assert(s->p);
    free(s->p);
    string_release(s);
}

static inline void string_resize(string_t *s, size_t desired_cap)
{
    assert(s->p);
    if (s->cap > desired_cap)
        return;
    while (s->cap <= desired_cap)
        s->cap *= 2;
    s->p = (char *)realloc(s->p, s->cap);
}

static inline void string_push_char(string_t *s, char c)
{
    assert(s->p);
    if (s->len + 1 >= s->cap)
        string_resize(s, s->len + 1);
    s->p[s->len++] = c;
    s->p[s->len] = '\0';
}

#endif
