/* Toy-Shell/src/string_set.c */
#include "string_set.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct string_set_node {
    char *val;
    struct string_set_node *left, *right;
};

struct string_set {
    struct string_set_node *root;
};

struct string_set *create_string_set() 
{
    struct string_set *set = malloc(sizeof(struct string_set));
    set->root = NULL;
    return set;
}

static void free_subtree(struct string_set_node *root) 
{
    if (root == NULL)
        return;

    free_subtree(root->left);
    free_subtree(root->right);
    free(root->val);
    free(root);
}

void free_string_set(struct string_set *set) 
{ 
    free_subtree(set->root);
}

int string_set_is_empty(struct string_set *set) 
{ 
    return set->root == NULL; 
}

static int subtree_size(struct string_set_node *root)
{
    if (root == NULL)
        return 0;

    return 1 + subtree_size(root->left) + subtree_size(root->right);
}

int string_set_size(struct string_set *set)
{
    return subtree_size(set->root);
}

static struct string_set_node **subtree_find(
        struct string_set_node **rootp, const char *val) 
{
    int comp_res;
    if (*rootp == NULL)
        return rootp;

    comp_res = strcmp((*rootp)->val, val);
    if (comp_res == 0)
        return rootp;
    else if (comp_res > 0)
        return subtree_find(&((*rootp)->left), val);
    else
        return subtree_find(&((*rootp)->right), val);
}

int val_is_in_string_set(struct string_set *set, const char *val) 
{
    return *subtree_find(&set->root, val) != NULL;
}

int string_set_add(struct string_set *set, const char *val) 
{
    size_t val_size;
    struct string_set_node **place = subtree_find(&set->root, val);
    if (*place != NULL)
        return 0;

    *place = malloc(sizeof(struct string_set_node));

    val_size = strlen(val)+1;
    (*place)->val = malloc(val_size);
    strncpy((*place)->val, val, val_size);

    (*place)->left = NULL;
    (*place)->right = NULL;
    return 1;
}

static struct string_set_node **subtree_max_place(
        struct string_set_node **rootp) 
{
    if (*rootp == NULL)
        return NULL;
    else if ((*rootp)->right == NULL)
        return rootp;
    else
        return subtree_max_place(&((*rootp)->right));
}

int string_set_remove(struct string_set *set, const char *val) 
{
    struct string_set_node **place = subtree_find(&set->root, val);
    struct string_set_node *tmp;
    if (*place == NULL)
        return 0;

    free((*place)->val);
    if ((*place)->left != NULL && ((*place)->right != NULL)) {
        struct string_set_node **alt_place =
            subtree_max_place(&((*place)->left));
        (*place)->val = (*alt_place)->val;
        place = alt_place;
    }

    tmp = *place;
    *place = tmp->left != NULL ? tmp->left : tmp->right;

    free(tmp);
    return 1;
}

char *string_set_pop_any(struct string_set *set)
{
    struct string_set_node **place = subtree_max_place(&set->root);
    struct string_set_node *tmp;
    char *popped_str;
    tmp = *place;
    *place = tmp->left != NULL ? tmp->left : tmp->right;
    popped_str = tmp->val;
    free(tmp);
    return popped_str;
}
