#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#include "process.h"

typedef enum {
    MEMORY_LOCAL,
    MEMORY_GLOBAL
} MemoryScope;

typedef struct {
    MemoryScope scope;
    size_t memory_bytes;
    size_t page_bytes;
    size_t frame_count;
    int allocation_percent;
} MemoryConfig;

typedef struct {
    size_t process_slot;
    int pid;
    int page;
    bool process_finished;
} MemoryAccess;

typedef struct {
    unsigned long long fifo_replacements;
    unsigned long long lru_replacements;
    unsigned long long nfu_replacements;
    unsigned long long optimal_replacements;
    char closest[16];
} MemoryResult;

bool memory_simulate(const MemoryConfig *config, Process **processes,
                     size_t process_count, const MemoryAccess *trace,
                     size_t trace_count, MemoryResult *result);

#endif
