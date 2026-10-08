#include "process.h"

#include <stdbool.h>
#include <stdlib.h>

Process *process_create(int creation_time, int pid, int burst_time,
                        int attribute, size_t input_order) {
    Process *process = malloc(sizeof(*process));
    if (process == NULL) {
        return NULL;
    }

    process->creation_time = creation_time;
    process->pid = pid;
    process->burst_time = burst_time;
    process->remaining_time = burst_time;
    process->attribute = attribute;
    process->completion_time = -1;
    process->ready_time = 0;
    process->ready_since = -1;
    process->vruntime = 0;
    process->input_order = input_order;
    process->slot = input_order;
    process->memory_bytes = 0;
    process->virtual_pages = 0;
    process->frame_limit = 0;
    process->page_sequence = NULL;
    process->page_count = 0;
    process->state = PROCESS_NEW;
    return process;
}

bool process_set_memory(Process *process, size_t memory_bytes,
                        size_t page_bytes, int allocation_percent,
                        int *page_sequence, size_t page_count) {
    size_t pages;
    size_t limit;
    if (page_bytes == 0 || memory_bytes == 0 || page_count == 0)
        return false;

    pages = memory_bytes / page_bytes + (memory_bytes % page_bytes != 0);
    /* Percentual maximo: arredondamento para baixo, com minimo operacional 1. */
    limit = (pages / 100) * (size_t)allocation_percent +
            ((pages % 100) * (size_t)allocation_percent) / 100;
    if (limit == 0) limit = 1;
    if (limit > pages) limit = pages;

    process->memory_bytes = memory_bytes;
    process->virtual_pages = pages;
    process->frame_limit = limit;
    process->page_sequence = page_sequence;
    process->page_count = page_count;
    return true;
}

void process_destroy(void *process) {
    Process *typed = process;
    if (typed != NULL) free(typed->page_sequence);
    free(typed);
}
