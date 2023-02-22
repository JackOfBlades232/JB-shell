/* Toy-Shell/src/word.h */
#ifndef WORD_SENTRY
#define WORD_SENTRY

#include <stdio.h>

struct word;

struct word *word_create();
struct word *word_add_char(struct word *w, char c);
int word_put(FILE *f, struct word *w);
void word_free(struct word *w);

#endif
