/* Toy-Shell/src/utils/int_set.h */
#ifndef INT_SET_SENTRY
#define INT_SET_SENTRY

struct int_set;

struct int_set *create_int_set();
int int_set_is_empty(struct int_set *set);
int val_is_in_int_set(struct int_set *set, int val);
int int_set_add(struct int_set *set, int val);
int int_set_remove(struct int_set *set, int val);
void free_int_set(struct int_set *set);

#endif 
