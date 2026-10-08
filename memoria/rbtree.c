#include "rbtree.h"

#include <stdlib.h>

static int compare_pair(uint64_t key_a, uint64_t tie_a,
                        uint64_t key_b, uint64_t tie_b) {
    if (key_a < key_b) return -1;
    if (key_a > key_b) return 1;
    if (tie_a < tie_b) return -1;
    if (tie_a > tie_b) return 1;
    return 0;
}

/*
 * A folha NIL e compartilhada por todos os nos desta arvore. Isso elimina
 * testes repetidos contra NULL e garante que toda folha seja preta, como exige
 * a definicao de uma arvore rubro-negra.
 */
bool rb_tree_init(RBTree *tree) {
    tree->nil = calloc(1, sizeof(*tree->nil));
    if (tree->nil == NULL) {
        return false;
    }
    tree->nil->color = RB_BLACK;
    tree->nil->left = tree->nil;
    tree->nil->right = tree->nil;
    tree->nil->parent = tree->nil;
    tree->root = tree->nil;
    tree->size = 0;
    return true;
}

RBNode *rb_node_create(uint64_t key, uint64_t tie, void *data) {
    RBNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->key = key;
    node->tie = tie;
    node->data = data;
    node->color = RB_RED;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    return node;
}

static void rotate_left(RBTree *tree, RBNode *x) {
    RBNode *y = x->right;
    x->right = y->left;
    if (y->left != tree->nil) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == tree->nil) tree->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

static void rotate_right(RBTree *tree, RBNode *y) {
    RBNode *x = y->left;
    y->left = x->right;
    if (x->right != tree->nil) x->right->parent = y;
    x->parent = y->parent;
    if (y->parent == tree->nil) tree->root = x;
    else if (y == y->parent->right) y->parent->right = x;
    else y->parent->left = x;
    x->right = y;
    y->parent = x;
}

static void insert_fixup(RBTree *tree, RBNode *node) {
    /* Restaura as propriedades de cor usando recoloracoes e rotacoes. */
    while (node->parent->color == RB_RED) {
        if (node->parent == node->parent->parent->left) {
            RBNode *uncle = node->parent->parent->right;
            if (uncle->color == RB_RED) {
                node->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                node = node->parent->parent;
            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    rotate_left(tree, node);
                }
                node->parent->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                rotate_right(tree, node->parent->parent);
            }
        } else {
            RBNode *uncle = node->parent->parent->left;
            if (uncle->color == RB_RED) {
                node->parent->color = RB_BLACK;
                uncle->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                node = node->parent->parent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    rotate_right(tree, node);
                }
                node->parent->color = RB_BLACK;
                node->parent->parent->color = RB_RED;
                rotate_left(tree, node->parent->parent);
            }
        }
    }
    tree->root->color = RB_BLACK;
}

bool rb_tree_insert(RBTree *tree, RBNode *node) {
    RBNode *parent = tree->nil;
    RBNode *current = tree->root;

    while (current != tree->nil) {
        int cmp = compare_pair(node->key, node->tie, current->key, current->tie);
        parent = current;
        if (cmp == 0) return false;
        current = cmp < 0 ? current->left : current->right;
    }

    node->parent = parent;
    node->left = tree->nil;
    node->right = tree->nil;
    node->color = RB_RED;
    if (parent == tree->nil) tree->root = node;
    else if (compare_pair(node->key, node->tie, parent->key, parent->tie) < 0)
        parent->left = node;
    else
        parent->right = node;

    tree->size++;
    insert_fixup(tree, node);
    return true;
}

RBNode *rb_tree_find(const RBTree *tree, uint64_t key, uint64_t tie) {
    RBNode *current = tree->root;
    while (current != tree->nil) {
        int cmp = compare_pair(key, tie, current->key, current->tie);
        if (cmp == 0) return current;
        current = cmp < 0 ? current->left : current->right;
    }
    return NULL;
}

RBNode *rb_tree_minimum(const RBTree *tree) {
    RBNode *node;
    if (tree->root == tree->nil) return NULL;
    node = tree->root;
    while (node->left != tree->nil) node = node->left;
    return node;
}

static void transplant(RBTree *tree, RBNode *old_node, RBNode *new_node) {
    if (old_node->parent == tree->nil) tree->root = new_node;
    else if (old_node == old_node->parent->left) old_node->parent->left = new_node;
    else old_node->parent->right = new_node;
    new_node->parent = old_node->parent;
}

static RBNode *subtree_minimum(const RBTree *tree, RBNode *node) {
    while (node->left != tree->nil) node = node->left;
    return node;
}

static void remove_fixup(RBTree *tree, RBNode *node) {
    /* Corrige o possivel excesso de preto deixado pela remocao. */
    while (node != tree->root && node->color == RB_BLACK) {
        if (node == node->parent->left) {
            RBNode *sibling = node->parent->right;
            if (sibling->color == RB_RED) {
                sibling->color = RB_BLACK;
                node->parent->color = RB_RED;
                rotate_left(tree, node->parent);
                sibling = node->parent->right;
            }
            if (sibling->left->color == RB_BLACK &&
                sibling->right->color == RB_BLACK) {
                sibling->color = RB_RED;
                node = node->parent;
            } else {
                if (sibling->right->color == RB_BLACK) {
                    sibling->left->color = RB_BLACK;
                    sibling->color = RB_RED;
                    rotate_right(tree, sibling);
                    sibling = node->parent->right;
                }
                sibling->color = node->parent->color;
                node->parent->color = RB_BLACK;
                sibling->right->color = RB_BLACK;
                rotate_left(tree, node->parent);
                node = tree->root;
            }
        } else {
            RBNode *sibling = node->parent->left;
            if (sibling->color == RB_RED) {
                sibling->color = RB_BLACK;
                node->parent->color = RB_RED;
                rotate_right(tree, node->parent);
                sibling = node->parent->left;
            }
            if (sibling->right->color == RB_BLACK &&
                sibling->left->color == RB_BLACK) {
                sibling->color = RB_RED;
                node = node->parent;
            } else {
                if (sibling->left->color == RB_BLACK) {
                    sibling->right->color = RB_BLACK;
                    sibling->color = RB_RED;
                    rotate_left(tree, sibling);
                    sibling = node->parent->left;
                }
                sibling->color = node->parent->color;
                node->parent->color = RB_BLACK;
                sibling->left->color = RB_BLACK;
                rotate_right(tree, node->parent);
                node = tree->root;
            }
        }
    }
    node->color = RB_BLACK;
}

void rb_tree_remove(RBTree *tree, RBNode *node) {
    /*
     * Primeiro executa uma remocao de busca binaria comum. Se o no fisicamente
     * retirado era preto, remove_fixup restaura a altura negra dos caminhos.
     */
    RBNode *moved = node;
    RBNode *fixup;
    RBColor original_color = moved->color;

    if (node->left == tree->nil) {
        fixup = node->right;
        transplant(tree, node, node->right);
    } else if (node->right == tree->nil) {
        fixup = node->left;
        transplant(tree, node, node->left);
    } else {
        moved = subtree_minimum(tree, node->right);
        original_color = moved->color;
        fixup = moved->right;
        if (moved->parent == node) {
            fixup->parent = moved;
        } else {
            transplant(tree, moved, moved->right);
            moved->right = node->right;
            moved->right->parent = moved;
        }
        transplant(tree, node, moved);
        moved->left = node->left;
        moved->left->parent = moved;
        moved->color = node->color;
    }

    tree->size--;
    if (original_color == RB_BLACK) remove_fixup(tree, fixup);
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
}

bool rb_tree_empty(const RBTree *tree) {
    return tree->root == tree->nil;
}

static void destroy_subtree(RBTree *tree, RBNode *node,
                            void (*destroy_data)(void *)) {
    if (node == tree->nil) return;
    destroy_subtree(tree, node->left, destroy_data);
    destroy_subtree(tree, node->right, destroy_data);
    if (destroy_data != NULL) destroy_data(node->data);
    free(node);
}

void rb_tree_destroy(RBTree *tree, void (*destroy_data)(void *)) {
    if (tree->nil == NULL) return;
    destroy_subtree(tree, tree->root, destroy_data);
    free(tree->nil);
    tree->root = NULL;
    tree->nil = NULL;
    tree->size = 0;
}
