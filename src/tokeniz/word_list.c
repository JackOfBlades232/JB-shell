/* Toy-Shell/src/tokeniz/word_list.c */
#include "word_list.h"

#include <stdlib.h>

void free_word_item(struct word_item *wi)
{
    word_free(wi->wrd);
    free(wi);
}

struct word_list *word_list_create()
{
    struct word_list *lst = malloc(sizeof(struct word_list));
    lst->first = NULL;
    lst->last = NULL;
    return lst;
}

void word_list_add_item(struct word_list *lst, enum word_type wtype)
{
    struct word_item *tmp = malloc(sizeof(struct word_item));
    tmp->wrd = word_create(wtype);
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

struct word *word_list_pop_first(struct word_list *lst)
{
    struct word_item *tmp;
    struct word *ret;

    if (word_list_is_empty(lst))
        return NULL;

    tmp = lst->first;
    ret = lst->first->wrd;
    lst->first = lst->first->next;
    free(tmp); /* just free item and leave word in heap */
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

int word_list_len(struct word_list *lst)
{
    struct word_item *tmp;
    int len = 0;

    for (tmp = lst->first; tmp; tmp = tmp->next)
        len++;
    return len;
}

int word_list_is_empty(struct word_list *lst)
{
    return word_list_len(lst) == 0;
}
