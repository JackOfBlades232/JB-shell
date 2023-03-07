/* Toy-Shell/src/word_list.h */
#ifndef WORD_LIST_SENTRY
#define WORD_LIST_SENTRY

#include "word.h"

struct word_item;
struct word_list;

struct word_list *word_list_create();
void word_list_add_item(struct word_list *lst);
int word_list_add_letter_to_last(struct word_list *lst, char c);
struct word *word_list_pop_first(struct word_list *lst);
void word_list_free(struct word_list *lst);

void word_list_print(struct word_list *lst);
int word_list_len(struct word_list *lst);
int word_list_is_empty(struct word_list *lst);
char **word_list_create_token_ptrs(struct word_list *lst);

#endif
