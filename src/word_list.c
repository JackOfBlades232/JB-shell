/* Toy-Shell/src/word_list.c */
#include "word_list.h"

#include <stdlib.h>

struct word_item {
    struct word *wrd;
    struct word_item *next;
};

struct word_list {
    struct word_item *first, *last;
};

struct word_list *word_list_create()
{
    struct word_list *lst = malloc(sizeof(struct word_list));
    lst->first = NULL;
    lst->last = NULL;
    return lst;
}

static int word_list_is_empty(struct word_list *lst)
{
    return lst->first == NULL && lst->last == NULL;
}

void word_list_add_item(struct word_list *lst)
{
    struct word_item *tmp = malloc(sizeof(struct word_item));
    tmp->wrd = word_create();
    tmp->next = NULL;

    if (word_list_is_empty(lst))
        lst->last = lst->first = tmp;
    else {
        lst->last->next = tmp;
        lst->last = tmp;
    }
}

int word_list_add_letter_to_last(struct word_list *lst, char c)
{
    if (lst->last == NULL)
        return 0;

    lst->last->wrd = word_add_char(lst->last->wrd, c);
    return 1;
}

static void free_word_item(struct word_item *wi)
{
    word_free(wi->wrd);
    free(wi);
}

struct word *word_list_pop_first(struct word_list *lst)
{
    struct word *ret;

    if (word_list_is_empty(lst))
        return NULL;

    ret = lst->first->wrd;
    free_word_item(lst->first);
    return ret;
}

void word_list_free(struct word_list *lst)
{
    struct word_item *tmp;

    while (lst->first) {
        tmp = lst->first;
        lst->first = lst->first->next;
        free_word_item(tmp);
    }

    free(lst);
}

void word_list_print(struct word_list *lst)
{
    struct word_item *tmp;
    for (tmp = lst->first; tmp; tmp = tmp->next)
        word_put(stdout, tmp->wrd);
}
