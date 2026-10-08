#include "process.h"

#include <stdlib.h>

Process *process_create(int creation_time, int pid, int burst_time,
                        int attribute, int io_chance, size_t input_order) {
    Process *process = calloc(1, sizeof(*process));
    if (process == NULL) return NULL;

    process->creation_time = creation_time;
    process->pid = pid;
    process->burst_time = burst_time;
    process->remaining_time = burst_time;
    process->attribute = attribute;
    process->completion_time = -1;
    process->ready_since = -1;
    process->input_order = input_order;
    process->slot = input_order;
    process->io_chance = io_chance;
    process->io_device = -1;
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

void process_destroy(Process *process) {
    if (process == NULL) return;
    free(process->page_sequence);
    free(process);
}
