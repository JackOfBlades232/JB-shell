/* Toy-Shell/src/word.c */
#include "word.h"

#include <stdlib.h>

enum {
    base_word_cap = 32,
    word_cap_modifier = 2
};

struct word {
    enum word_type wtype;
    char *content;
    int cap;
};

struct word *word_create(enum word_type wtype)
{
    struct word *w;

    w = malloc(sizeof(struct word));
    w->cap = base_word_cap;
    w->content = malloc(sizeof(char) * w->cap);
    *w->content = '\0';

    w->wtype = wtype;

    return w;
}

static int str_len(const char *word)
{
    const char *wp;
    for (wp = word; *wp; wp++)
        {}
    return wp - word;
}

static void resize_word(struct word *w)
{
    w->cap *= 2;
    w->content = realloc(w->content, sizeof(char) * w->cap);
}

struct word *word_add_char(struct word *w, char c)
{
    int w_len = str_len(w->content);
    if (w_len >= w->cap - 1)
        resize_word(w);

    w->content[w_len] = c;
    w->content[w_len+1] = '\0';
    return w;
}

int word_put(FILE *f, struct word *w)
{
    return fprintf(f, "[%s]\n", w->content);
}

void word_free(struct word *w)
{
    free(w->content);
    free(w);
}

char *word_content(struct word *w)
{
    return w->content;
}

int word_is_separator(struct word *w)
{
    return w->wtype == separator;
}
