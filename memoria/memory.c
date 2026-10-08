#include "memory.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    REPLACE_FIFO,
    REPLACE_LRU,
    REPLACE_NFU,
    REPLACE_OPTIMAL
} ReplacementAlgorithm;

typedef struct {
    bool occupied;
    size_t process_slot;
    int pid;
    int page;
    uint64_t loaded_at;
    uint64_t last_used;
    uint64_t frequency;
} Frame;

typedef struct {
    ReplacementAlgorithm algorithm;
    MemoryScope scope;
    Frame *frames;
    size_t frame_count;
    size_t *local_start;
    size_t *local_count;
    unsigned long long replacements;
} PolicySimulation;

static bool build_local_partitions(const MemoryConfig *config,
                                   Process **processes, size_t process_count,
                                   size_t **starts_out, size_t **counts_out) {
    size_t *starts = calloc(process_count, sizeof(*starts));
    size_t *counts = calloc(process_count, sizeof(*counts));
    size_t desired_total = 0;
    size_t i;

    if (starts == NULL || counts == NULL) {
        free(starts);
        free(counts);
        return false;
    }
    for (i = 0; i < process_count; i++) {
        size_t desired = processes[i]->frame_limit;
        if (desired > config->frame_count) desired = config->frame_count;
        desired_total += desired;
    }

    if (desired_total <= config->frame_count) {
        size_t next = 0;
        for (i = 0; i < process_count; i++) {
            counts[i] = processes[i]->frame_limit;
            if (counts[i] > config->frame_count) counts[i] = config->frame_count;
            starts[i] = next;
            next += counts[i];
        }
    } else {
        size_t remaining;
        bool progress = true;
        if (config->frame_count < process_count) {
            fprintf(stderr,
                    "Erro: politica local exige ao menos uma moldura por processo.\n");
            free(starts);
            free(counts);
            return false;
        }

        /* Uma moldura por processo; as restantes sao distribuidas em rodadas. */
        for (i = 0; i < process_count; i++) counts[i] = 1;
        remaining = config->frame_count - process_count;
        while (remaining > 0 && progress) {
            progress = false;
            for (i = 0; i < process_count && remaining > 0; i++) {
                if (counts[i] < processes[i]->frame_limit) {
                    counts[i]++;
                    remaining--;
                    progress = true;
                }
            }
        }
        starts[0] = 0;
        for (i = 1; i < process_count; i++)
            starts[i] = starts[i - 1] + counts[i - 1];
    }

    *starts_out = starts;
    *counts_out = counts;
    return true;
}

static bool policy_init(PolicySimulation *simulation,
                        ReplacementAlgorithm algorithm,
                        const MemoryConfig *config,
                        const size_t *local_start,
                        const size_t *local_count,
                        size_t process_count) {
    memset(simulation, 0, sizeof(*simulation));
    simulation->algorithm = algorithm;
    simulation->scope = config->scope;
    simulation->frame_count = config->frame_count;
    simulation->frames = calloc(config->frame_count,
                                sizeof(*simulation->frames));
    if (simulation->frames == NULL) return false;

    if (config->scope == MEMORY_LOCAL) {
        simulation->local_start = malloc(process_count *
                                         sizeof(*simulation->local_start));
        simulation->local_count = malloc(process_count *
                                         sizeof(*simulation->local_count));
        if (simulation->local_start == NULL || simulation->local_count == NULL) {
            free(simulation->frames);
            free(simulation->local_start);
            free(simulation->local_count);
            memset(simulation, 0, sizeof(*simulation));
            return false;
        }
        memcpy(simulation->local_start, local_start,
               process_count * sizeof(*local_start));
        memcpy(simulation->local_count, local_count,
               process_count * sizeof(*local_count));
    }
    return true;
}

static void policy_destroy(PolicySimulation *simulation) {
    free(simulation->frames);
    free(simulation->local_start);
    free(simulation->local_count);
    memset(simulation, 0, sizeof(*simulation));
}

static size_t resident_count(const PolicySimulation *simulation,
                             size_t process_slot) {
    size_t count = 0;
    size_t i;
    for (i = 0; i < simulation->frame_count; i++) {
        if (simulation->frames[i].occupied &&
            simulation->frames[i].process_slot == process_slot)
            count++;
    }
    return count;
}

static size_t find_hit(const PolicySimulation *simulation,
                       const MemoryAccess *access) {
    size_t i;
    for (i = 0; i < simulation->frame_count; i++) {
        const Frame *frame = &simulation->frames[i];
        if (frame->occupied && frame->process_slot == access->process_slot &&
            frame->page == access->page)
            return i;
    }
    return SIZE_MAX;
}

static size_t find_free(const PolicySimulation *simulation,
                        const Process *process) {
    size_t begin = 0;
    size_t end = simulation->frame_count;
    size_t i;

    if (simulation->scope == MEMORY_LOCAL) {
        begin = simulation->local_start[process->slot];
        end = begin + simulation->local_count[process->slot];
    } else {
        size_t limit = process->frame_limit;
        if (limit > simulation->frame_count) limit = simulation->frame_count;
        if (resident_count(simulation, process->slot) >= limit) return SIZE_MAX;
    }

    for (i = begin; i < end; i++) {
        if (!simulation->frames[i].occupied) return i;
    }
    return SIZE_MAX;
}

static size_t next_use(const MemoryAccess *trace, size_t trace_count,
                       size_t current, const Frame *frame) {
    size_t i;
    for (i = current + 1; i < trace_count; i++) {
        if (trace[i].process_slot == frame->process_slot &&
            trace[i].page == frame->page)
            return i;
    }
    return SIZE_MAX;
}

static bool frame_is_candidate(const PolicySimulation *simulation,
                               size_t frame_index, const Process *process,
                               bool force_same_process) {
    const Frame *frame = &simulation->frames[frame_index];
    if (!frame->occupied) return false;
    if (simulation->scope == MEMORY_LOCAL) {
        size_t begin = simulation->local_start[process->slot];
        size_t end = begin + simulation->local_count[process->slot];
        return frame_index >= begin && frame_index < end;
    }
    return !force_same_process || frame->process_slot == process->slot;
}

static bool better_victim(const PolicySimulation *simulation,
                          const Frame *candidate, size_t candidate_next,
                          const Frame *best, size_t best_next) {
    switch (simulation->algorithm) {
        case REPLACE_FIFO:
            return candidate->loaded_at < best->loaded_at;
        case REPLACE_LRU:
            return candidate->last_used < best->last_used;
        case REPLACE_NFU:
            if (candidate->frequency != best->frequency)
                return candidate->frequency < best->frequency;
            if (candidate->page != best->page)
                return candidate->page < best->page;
            return candidate->pid < best->pid;
        case REPLACE_OPTIMAL:
            if (candidate_next != best_next)
                return candidate_next == SIZE_MAX ||
                       (best_next != SIZE_MAX && candidate_next > best_next);
            if (candidate->page != best->page)
                return candidate->page < best->page;
            return candidate->pid < best->pid;
    }
    return false;
}

static size_t choose_victim(const PolicySimulation *simulation,
                            const Process *process,
                            const MemoryAccess *trace, size_t trace_count,
                            size_t current_access) {
    size_t limit = process->frame_limit;
    bool force_same_process;
    size_t best = SIZE_MAX;
    size_t best_next = 0;
    size_t i;

    if (limit > simulation->frame_count) limit = simulation->frame_count;
    force_same_process = simulation->scope == MEMORY_GLOBAL &&
                         resident_count(simulation, process->slot) >= limit;

    for (i = 0; i < simulation->frame_count; i++) {
        size_t candidate_next = 0;
        if (!frame_is_candidate(simulation, i, process, force_same_process))
            continue;
        if (simulation->algorithm == REPLACE_OPTIMAL)
            candidate_next = next_use(trace, trace_count, current_access,
                                      &simulation->frames[i]);
        if (best == SIZE_MAX ||
            better_victim(simulation, &simulation->frames[i], candidate_next,
                          &simulation->frames[best], best_next)) {
            best = i;
            best_next = candidate_next;
        }
    }
    return best;
}

static bool policy_access(PolicySimulation *simulation, Process *process,
                          const MemoryAccess *trace, size_t trace_count,
                          size_t access_index) {
    const MemoryAccess *access = &trace[access_index];
    size_t frame_index = find_hit(simulation, access);
    Frame *frame;

    if (frame_index != SIZE_MAX) {
        frame = &simulation->frames[frame_index];
        frame->last_used = access_index;
        frame->frequency++;
        return true;
    }

    frame_index = find_free(simulation, process);
    if (frame_index == SIZE_MAX) {
        frame_index = choose_victim(simulation, process, trace, trace_count,
                                    access_index);
        if (frame_index == SIZE_MAX) return false;
        simulation->replacements++;
    }

    frame = &simulation->frames[frame_index];
    frame->occupied = true;
    frame->process_slot = process->slot;
    frame->pid = process->pid;
    frame->page = access->page;
    frame->loaded_at = access_index;
    frame->last_used = access_index;
    frame->frequency = 1;
    return true;
}

static void release_process(PolicySimulation *simulation, size_t process_slot) {
    size_t i;
    for (i = 0; i < simulation->frame_count; i++) {
        if (simulation->frames[i].occupied &&
            simulation->frames[i].process_slot == process_slot)
            memset(&simulation->frames[i], 0, sizeof(simulation->frames[i]));
    }
}

static unsigned long long distance_from_optimal(unsigned long long value,
                                                unsigned long long optimal) {
    return value >= optimal ? value - optimal : optimal - value;
}

static void determine_closest(MemoryResult *result) {
    unsigned long long distances[3];
    unsigned long long minimum;
    int winners = 0;
    int winner = -1;
    int i;

    distances[0] = distance_from_optimal(result->fifo_replacements,
                                         result->optimal_replacements);
    distances[1] = distance_from_optimal(result->lru_replacements,
                                         result->optimal_replacements);
    distances[2] = distance_from_optimal(result->nfu_replacements,
                                         result->optimal_replacements);
    minimum = distances[0];
    if (distances[1] < minimum) minimum = distances[1];
    if (distances[2] < minimum) minimum = distances[2];
    for (i = 0; i < 3; i++) {
        if (distances[i] == minimum) {
            winners++;
            winner = i;
        }
    }
    if (winners != 1) snprintf(result->closest, sizeof(result->closest), "empate");
    else if (winner == 0) snprintf(result->closest, sizeof(result->closest), "FIFO");
    else if (winner == 1) snprintf(result->closest, sizeof(result->closest), "LRU");
    else snprintf(result->closest, sizeof(result->closest), "NUF");
}

bool memory_simulate(const MemoryConfig *config, Process **processes,
                     size_t process_count, const MemoryAccess *trace,
                     size_t trace_count, MemoryResult *result) {
    PolicySimulation simulations[4];
    ReplacementAlgorithm algorithms[4] = {
        REPLACE_FIFO, REPLACE_LRU, REPLACE_NFU, REPLACE_OPTIMAL
    };
    size_t *local_start = NULL;
    size_t *local_count = NULL;
    size_t initialized = 0;
    size_t i, j;

    memset(result, 0, sizeof(*result));
    memset(simulations, 0, sizeof(simulations));
    if (config->scope == MEMORY_LOCAL &&
        !build_local_partitions(config, processes, process_count,
                                &local_start, &local_count))
        return false;

    for (i = 0; i < 4; i++) {
        if (!policy_init(&simulations[i], algorithms[i], config,
                         local_start, local_count, process_count))
            goto fail;
        initialized++;
    }

    for (i = 0; i < trace_count; i++) {
        Process *process = processes[trace[i].process_slot];
        for (j = 0; j < 4; j++) {
            if (!policy_access(&simulations[j], process, trace,
                               trace_count, i))
                goto fail;
        }
        if (trace[i].process_finished) {
            for (j = 0; j < 4; j++)
                release_process(&simulations[j], trace[i].process_slot);
        }
    }

    result->fifo_replacements = simulations[0].replacements;
    result->lru_replacements = simulations[1].replacements;
    result->nfu_replacements = simulations[2].replacements;
    result->optimal_replacements = simulations[3].replacements;
    determine_closest(result);

    printf("\nGERENCIAMENTO DE MEMORIA - politica %s\n",
           config->scope == MEMORY_LOCAL ? "local" : "global");
    printf("Memoria: %zu bytes | Pagina/moldura: %zu bytes | Molduras: %zu\n",
           config->memory_bytes, config->page_bytes, config->frame_count);
    if (config->scope == MEMORY_LOCAL) {
        printf("Particoes locais (PID:molduras): ");
        for (i = 0; i < process_count; i++)
            printf("%s%d:%zu", i == 0 ? "" : " | ",
                   processes[i]->pid, local_count[i]);
        printf("\n");
    }
    printf("+----------+---------------------+\n");
    printf("| Algoritmo| Trocas de pagina    |\n");
    printf("+----------+---------------------+\n");
    printf("| FIFO     | %-19llu |\n", result->fifo_replacements);
    printf("| LRU      | %-19llu |\n", result->lru_replacements);
    printf("| NUF      | %-19llu |\n", result->nfu_replacements);
    printf("| Otimo    | %-19llu |\n", result->optimal_replacements);
    printf("+----------+---------------------+\n");
    printf("Mais proximo do otimo: %s\n", result->closest);
    printf("%llu|%llu|%llu|%llu|%s\n",
           result->fifo_replacements, result->lru_replacements,
           result->nfu_replacements, result->optimal_replacements,
           result->closest);

    for (i = 0; i < initialized; i++) policy_destroy(&simulations[i]);
    free(local_start);
    free(local_count);
    return true;

fail:
    fprintf(stderr, "Erro durante a simulacao de memoria.\n");
    for (i = 0; i < initialized; i++) policy_destroy(&simulations[i]);
    free(local_start);
    free(local_count);
    return false;
}
