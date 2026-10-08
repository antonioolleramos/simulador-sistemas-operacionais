#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_FINISHED
} ProcessState;

typedef struct Process {
    int creation_time;
    int pid;
    int burst_time;
    int remaining_time;
    int attribute;
    int completion_time;

    long long ready_time;
    int ready_since;
    long long blocked_time;

    size_t input_order;
    size_t slot;

    size_t memory_bytes;
    size_t virtual_pages;
    size_t frame_limit;
    int *page_sequence;
    size_t page_count;

    int io_chance;
    int io_device;
    bool io_waiting;
    unsigned long long io_requests;

    ProcessState state;
} Process;

Process *process_create(int creation_time, int pid, int burst_time,
                        int attribute, int io_chance, size_t input_order);
bool process_set_memory(Process *process, size_t memory_bytes,
                        size_t page_bytes, int allocation_percent,
                        int *page_sequence, size_t page_count);
void process_destroy(Process *process);

#endif
