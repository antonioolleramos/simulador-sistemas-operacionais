#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    bool occupied;
    size_t process_slot;
    int pid;
    int page;
    uint64_t last_used;
} MemoryFrame;

typedef struct {
    MemoryConfig config;
    MemoryFrame *frames;
    size_t *local_start;
    size_t *local_count;
    size_t process_count;
    uint64_t access_clock;
    unsigned long long replacements;
} MemoryManager;

bool memory_init(MemoryManager *manager, const MemoryConfig *config,
                 Process **processes, size_t process_count);
bool memory_access(MemoryManager *manager, Process *process, int page);
void memory_release_process(MemoryManager *manager, size_t process_slot);
void memory_print_summary(const MemoryManager *manager);
void memory_destroy(MemoryManager *manager);

#endif
