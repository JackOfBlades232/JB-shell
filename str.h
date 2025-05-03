/* Toy-Shell/src/str.h */
#ifndef STR_SENTRY
#define STR_SENTRY

#include "def.h"
#include "debug.h"

#include <stddef.h>

STATIC_ASSERT(sizeof(char) == sizeof(u8));

typedef struct string_tag {
    char *p;
    u64 len;
} string_t;

static inline b32 string_is_valid(const string_t *s)
{
    return s->p ? true : false;
}

static inline b32 string_is_empty(const string_t *s)
{
    return (s->p && s->len > 0) ? false : true;
}

static inline void clear_string(string_t *s)
{
    s->p = NULL;
    s->len = 0;
}

#define LITSTR(litcstr_) {(litcstr_), sizeof(litcstr_) - 1}

#define STR_PRINTF_ARGS(str_) (int)((str_).len), ((str_).p)

static inline bool str_eq(string_t s1, string_t s2)
{
    if (s1.len != s2.len)
        return false;
    for (char *p1 = s1.p, *p2 = s2.p; p1 != s1.p + s1.len; ++p1, ++p2) {
        if (*p1 != *p2)
            return false;
    }
    return true;
}

static inline bool str_is_prefix_of(string_t prefix, string_t of)
{
    if (prefix.len > of.len)
        return false;
    for (char *p = prefix.p, *o = of.p; p != prefix.p + prefix.len; ++p, ++o) {
        if (*p != *o)
            return false;
    }
    return true;
}

static inline bool str_has_chr(string_t s, char c)
{
    for (char *p = s.p; p != s.p + s.len; ++p) {
        if (*p == c)
            return true;
    }
    return false;
}

static inline string_t str_from_cstr(char *cstr)
{
    string_t res = {cstr, 0};
    if (cstr) {
        while (*cstr++)
            ++res.len;
    }
    return res;
}

#endif
