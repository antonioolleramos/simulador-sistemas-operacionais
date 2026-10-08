#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "memory.h"
#include "process.h"

typedef struct {
    char algorithm_name[32];
    int quantum;
    MemoryConfig memory;

    IODevice *devices;
    size_t device_count;

    Process **processes;
    Process **arrival_order;
    size_t process_count;
} Workload;

typedef struct {
    double average_turnaround;
    double average_ready;
    double average_blocked;
    int total_time;
    unsigned long long memory_replacements;
} SimulationSummary;

bool workload_load(const char *path, Workload *workload);
void workload_destroy(Workload *workload);
bool scheduler_run(Workload *workload, uint64_t seed,
                   SimulationSummary *summary);

#endif
