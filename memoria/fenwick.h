#ifndef FENWICK_H
#define FENWICK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t *tree;
    size_t size;
} FenwickTree;

bool fenwick_init(FenwickTree *fenwick, size_t size);
void fenwick_add(FenwickTree *fenwick, size_t index, int64_t delta);
uint64_t fenwick_total(const FenwickTree *fenwick);
size_t fenwick_find_by_order(const FenwickTree *fenwick, uint64_t order);
void fenwick_destroy(FenwickTree *fenwick);

#endif
