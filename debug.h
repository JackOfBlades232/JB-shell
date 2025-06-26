/* JB-shell/src/debug.h */
#ifndef DEBUG_SENTRY
#define DEBUG_SENTRY

#include <assert.h>

#define ASSERT(...) assert(__VA_ARGS__)
#define STATIC_ASSERT(...) _Static_assert(__VA_ARGS__, "")

#endif
