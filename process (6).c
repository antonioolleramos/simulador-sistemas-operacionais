#include "process.h"

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
    process->state = PROCESS_NEW;
    return process;
}

void process_destroy(void *process) {
    free(process);
}
