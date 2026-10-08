#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#include "process.h"
#include "rbtree.h"

typedef enum {
    SCHED_ROUND_ROBIN,
    SCHED_PRIORITY,
    SCHED_LOTTERY,
    SCHED_CFS
} SchedulerAlgorithm;

typedef struct {
    SchedulerAlgorithm algorithm;
    char algorithm_name[32];
    int quantum;
    Process **processes;
    Process **arrival_order;
    size_t process_count;
    RBTree process_registry;
} Workload;

typedef struct {
    char algorithm_name[32];
    double average_turnaround;
    double average_ready;
    int total_time;
} SimulationSummary;

bool workload_load(const char *path, Workload *workload);
void workload_destroy(Workload *workload);
bool scheduler_run(Workload *workload, uint64_t seed,
                   SimulationSummary *summary);

#endif
