#ifndef PROCESS_H
#define PROCESS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
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
    uint64_t vruntime;
    size_t input_order;
    size_t slot;
    ProcessState state;
} Process;

Process *process_create(int creation_time, int pid, int burst_time,
                        int attribute, size_t input_order);
void process_destroy(void *process);

#endif
