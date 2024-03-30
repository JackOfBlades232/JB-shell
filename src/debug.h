/* Toy-Shell/src/debug.h */
#ifndef DEBUG_SENTRY
#define DEBUG_SENTRY

#if USE_ASSERTIONS
  #include <assert.h>
#else
  #define assert(_e)
  #define static_assert(_e)
#endif

#endif
