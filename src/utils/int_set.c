/* Toy-Shell/src/utils/int_set.c */
#include "int_set.h"

#include <stdlib.h>

struct int_set_node {
    int val;
    struct int_set_node *left, *right;
};

struct int_set {
    struct int_set_node *root;
};

struct int_set *create_int_set() 
{
    struct int_set *set = malloc(sizeof(struct int_set));
    set->root = NULL;
    return set;
}

int int_set_is_empty(struct int_set *set) 
{ 
    return set->root == NULL; 
}

static struct int_set_node **subtree_find(
        struct int_set_node **rootp, int val) 
{
    if (*rootp == NULL || (*rootp)->val == val)
        return rootp;
    else if ((*rootp)->val > val)
        return subtree_find(&((*rootp)->left), val);
    else
        return subtree_find(&((*rootp)->right), val);
}

int val_is_in_int_set(struct int_set *set, int val) 
{
    return *subtree_find(&set->root, val) != NULL;
}

int int_set_add(struct int_set *set, int val) 
{
    struct int_set_node **place = subtree_find(&set->root, val);
    if (*place != NULL)
        return 0;

    *place = malloc(sizeof(struct int_set_node));
    (*place)->val = val;
    (*place)->left = NULL;
    (*place)->right = NULL;
    return 1;
}

static struct int_set_node **subtree_max_place(struct int_set_node **rootp) 
{
    if (*rootp == NULL)
        return NULL;
    else if ((*rootp)->right == NULL)
        return rootp;
    else
        return subtree_max_place(&((*rootp)->right));
}

int int_set_remove(struct int_set *set, int val) 
{
    struct int_set_node **place = subtree_find(&set->root, val);
    struct int_set_node *tmp;
    if (*place == NULL)
        return 0;

    if ((*place)->left != NULL && ((*place)->right != NULL)) {
        struct int_set_node **alt_place = subtree_max_place(&((*place)->left));
        (*place)->val = (*alt_place)->val;
        place = alt_place;
    }

    tmp = *place;
    *place = tmp->left != NULL ? tmp->left : tmp->right;

    free(tmp);
    return 1;
}

static void free_subtree(struct int_set_node *root) 
{
    if (root == NULL)
        return;

    free_subtree(root->left);
    free_subtree(root->right);
    free(root);
}

void free_int_set(struct int_set *set) 
{ 
    free_subtree(set->root);
}
