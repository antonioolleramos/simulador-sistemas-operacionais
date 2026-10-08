#include "fenwick.h"

#include <stdlib.h>

bool fenwick_init(FenwickTree *fenwick, size_t size) {
    fenwick->tree = calloc(size + 1, sizeof(*fenwick->tree));
    if (fenwick->tree == NULL) {
        return false;
    }
    fenwick->size = size;
    return true;
}

void fenwick_add(FenwickTree *fenwick, size_t index, int64_t delta) {
    size_t i = index + 1;
    while (i <= fenwick->size) {
        if (delta >= 0) {
            fenwick->tree[i] += (uint64_t)delta;
        } else {
            fenwick->tree[i] -= (uint64_t)(-delta);
        }
        i += i & (~i + 1);
    }
}

uint64_t fenwick_total(const FenwickTree *fenwick) {
    uint64_t sum = 0;
    size_t i = fenwick->size;
    while (i > 0) {
        sum += fenwick->tree[i];
        i -= i & (~i + 1);
    }
    return sum;
}

size_t fenwick_find_by_order(const FenwickTree *fenwick, uint64_t order) {
    size_t index = 0;
    size_t step = 1;

    while ((step << 1) <= fenwick->size) {
        step <<= 1;
    }
    while (step != 0) {
        size_t next = index + step;
        if (next <= fenwick->size && fenwick->tree[next] < order) {
            index = next;
            order -= fenwick->tree[next];
        }
        step >>= 1;
    }
    return index;
}

void fenwick_destroy(FenwickTree *fenwick) {
    free(fenwick->tree);
    fenwick->tree = NULL;
    fenwick->size = 0;
}
