/* Toy-Shell/src/tokeniz/word.h */
#ifndef WORD_SENTRY
#define WORD_SENTRY

#include <stdio.h>

enum word_type { regular_wrd = 0, separator = 1 };

struct word {
    enum word_type wtype;
    char *content;
    int cap;
};

struct word *word_create(enum word_type wtype);
struct word *word_add_char(struct word *w, char c);
int word_put(FILE *f, struct word *w);
void word_free(struct word *w);

#endif
