#ifndef RBTREE_H
#define RBTREE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { RB_RED, RB_BLACK } RBColor;

typedef struct RBNode {
    uint64_t key;
    uint64_t tie;
    void *data;
    RBColor color;
    struct RBNode *left;
    struct RBNode *right;
    struct RBNode *parent;
} RBNode;

typedef struct {
    RBNode *root;
    RBNode *nil;
    unsigned long size;
} RBTree;

bool rb_tree_init(RBTree *tree);
RBNode *rb_node_create(uint64_t key, uint64_t tie, void *data);
bool rb_tree_insert(RBTree *tree, RBNode *node);
RBNode *rb_tree_find(const RBTree *tree, uint64_t key, uint64_t tie);
RBNode *rb_tree_minimum(const RBTree *tree);
void rb_tree_remove(RBTree *tree, RBNode *node);
bool rb_tree_empty(const RBTree *tree);
void rb_tree_destroy(RBTree *tree, void (*destroy_data)(void *));

#endif
