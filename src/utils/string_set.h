/* Toy-Shell/src/utils/string_set.h */
#ifndef INT_SET_SENTRY
#define INT_SET_SENTRY

struct string_set;

struct string_set *create_string_set();
void free_string_set(struct string_set *set);
int string_set_is_empty(struct string_set *set);
int string_set_size(struct string_set *set);
int val_is_in_string_set(struct string_set *set, const char *val);
int string_set_add(struct string_set *set, const char *val);
int string_set_remove(struct string_set *set, const char *val);
char *string_set_pop_any(struct string_set *set);

#endif 
