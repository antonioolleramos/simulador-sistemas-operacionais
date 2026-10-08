#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t resident_count(const MemoryManager *manager, size_t process_slot) {
    size_t count = 0;
    size_t i;
    for (i = 0; i < manager->config.frame_count; i++) {
        if (manager->frames[i].occupied &&
            manager->frames[i].process_slot == process_slot)
            count++;
    }
    return count;
}

static bool build_local_partitions(MemoryManager *manager,
                                   Process **processes) {
    size_t desired_total = 0;
    size_t i;

    manager->local_start = calloc(manager->process_count,
                                  sizeof(*manager->local_start));
    manager->local_count = calloc(manager->process_count,
                                  sizeof(*manager->local_count));
    if (manager->local_start == NULL || manager->local_count == NULL)
        return false;

    for (i = 0; i < manager->process_count; i++) {
        size_t desired = processes[i]->frame_limit;
        if (desired > manager->config.frame_count)
            desired = manager->config.frame_count;
        desired_total += desired;
    }

    if (desired_total <= manager->config.frame_count) {
        size_t next = 0;
        for (i = 0; i < manager->process_count; i++) {
            size_t count = processes[i]->frame_limit;
            if (count > manager->config.frame_count)
                count = manager->config.frame_count;
            manager->local_start[i] = next;
            manager->local_count[i] = count;
            next += count;
        }
        return true;
    }

    if (manager->config.frame_count < manager->process_count) {
        fprintf(stderr,
                "Erro: politica local exige ao menos uma moldura por processo.\n");
        return false;
    }

    /* Garante uma moldura por processo e distribui as demais em rodadas. */
    {
        size_t remaining = manager->config.frame_count - manager->process_count;
        bool progress = true;
        for (i = 0; i < manager->process_count; i++)
            manager->local_count[i] = 1;

        while (remaining > 0 && progress) {
            progress = false;
            for (i = 0; i < manager->process_count && remaining > 0; i++) {
                if (manager->local_count[i] < processes[i]->frame_limit) {
                    manager->local_count[i]++;
                    remaining--;
                    progress = true;
                }
            }
        }
    }

    manager->local_start[0] = 0;
    for (i = 1; i < manager->process_count; i++)
        manager->local_start[i] = manager->local_start[i - 1] +
                                  manager->local_count[i - 1];
    return true;
}

bool memory_init(MemoryManager *manager, const MemoryConfig *config,
                 Process **processes, size_t process_count) {
    memset(manager, 0, sizeof(*manager));
    manager->config = *config;
    manager->process_count = process_count;
    manager->frames = calloc(config->frame_count, sizeof(*manager->frames));
    if (manager->frames == NULL) return false;

    if (config->scope == MEMORY_LOCAL &&
        !build_local_partitions(manager, processes)) {
        memory_destroy(manager);
        return false;
    }
    return true;
}

static size_t find_hit(const MemoryManager *manager, const Process *process,
                       int page) {
    size_t i;
    for (i = 0; i < manager->config.frame_count; i++) {
        const MemoryFrame *frame = &manager->frames[i];
        if (frame->occupied && frame->process_slot == process->slot &&
            frame->page == page)
            return i;
    }
    return SIZE_MAX;
}

static size_t find_free(const MemoryManager *manager, const Process *process) {
    size_t begin = 0;
    size_t end = manager->config.frame_count;
    size_t i;

    if (manager->config.scope == MEMORY_LOCAL) {
        begin = manager->local_start[process->slot];
        end = begin + manager->local_count[process->slot];
    } else {
        size_t limit = process->frame_limit;
        if (limit > manager->config.frame_count)
            limit = manager->config.frame_count;
        if (resident_count(manager, process->slot) >= limit)
            return SIZE_MAX;
    }

    for (i = begin; i < end; i++) {
        if (!manager->frames[i].occupied) return i;
    }
    return SIZE_MAX;
}

static size_t choose_lru_victim(const MemoryManager *manager,
                                const Process *process) {
    bool same_process_only = false;
    size_t best = SIZE_MAX;
    size_t i;

    if (manager->config.scope == MEMORY_GLOBAL) {
        size_t limit = process->frame_limit;
        if (limit > manager->config.frame_count)
            limit = manager->config.frame_count;
        same_process_only = resident_count(manager, process->slot) >= limit;
    }

    for (i = 0; i < manager->config.frame_count; i++) {
        const MemoryFrame *frame = &manager->frames[i];
        bool candidate;
        if (!frame->occupied) continue;

        if (manager->config.scope == MEMORY_LOCAL) {
            size_t begin = manager->local_start[process->slot];
            size_t end = begin + manager->local_count[process->slot];
            candidate = i >= begin && i < end;
        } else {
            candidate = !same_process_only ||
                        frame->process_slot == process->slot;
        }
        if (!candidate) continue;

        if (best == SIZE_MAX ||
            frame->last_used < manager->frames[best].last_used ||
            (frame->last_used == manager->frames[best].last_used &&
             (frame->page < manager->frames[best].page ||
              (frame->page == manager->frames[best].page &&
               frame->pid < manager->frames[best].pid))))
            best = i;
    }
    return best;
}

bool memory_access(MemoryManager *manager, Process *process, int page) {
    size_t frame_index = find_hit(manager, process, page);
    MemoryFrame *frame;

    manager->access_clock++;
    if (frame_index != SIZE_MAX) {
        manager->frames[frame_index].last_used = manager->access_clock;
        return true;
    }

    frame_index = find_free(manager, process);
    if (frame_index == SIZE_MAX) {
        frame_index = choose_lru_victim(manager, process);
        if (frame_index == SIZE_MAX) return false;
        manager->replacements++;
    }

    frame = &manager->frames[frame_index];
    frame->occupied = true;
    frame->process_slot = process->slot;
    frame->pid = process->pid;
    frame->page = page;
    frame->last_used = manager->access_clock;
    return true;
}

void memory_release_process(MemoryManager *manager, size_t process_slot) {
    size_t i;
    for (i = 0; i < manager->config.frame_count; i++) {
        if (manager->frames[i].occupied &&
            manager->frames[i].process_slot == process_slot)
            memset(&manager->frames[i], 0, sizeof(manager->frames[i]));
    }
}

void memory_print_summary(const MemoryManager *manager) {
    printf("\nGERENCIAMENTO DE MEMORIA - LRU / politica %s\n",
           manager->config.scope == MEMORY_LOCAL ? "local" : "global");
    printf("Memoria: %zu bytes | Pagina/moldura: %zu bytes | Molduras: %zu\n",
           manager->config.memory_bytes, manager->config.page_bytes,
           manager->config.frame_count);
    printf("Trocas de pagina (LRU): %llu\n", manager->replacements);
}

void memory_destroy(MemoryManager *manager) {
    free(manager->frames);
    free(manager->local_start);
    free(manager->local_count);
    memset(manager, 0, sizeof(*manager));
}
