#include "scheduler.h"

#include "fenwick.h"
#include "queue.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_SIZE 65536
#define VRUNTIME_SCALE UINT64_C(1000000)

typedef struct {
    SchedulerAlgorithm algorithm;
    ProcessQueue round_robin;
    RBTree ordered;
    FenwickTree lottery;
    Process **by_slot;
    uint64_t sequence;
    uint64_t random_state;
    uint64_t min_vruntime;
} ReadySet;

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) text++;
    if (*text == '\0') return text;
    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return text;
}

static size_t count_character(const char *text, char character) {
    size_t count = 0;
    while (*text != '\0') {
        if (*text == character) count++;
        text++;
    }
    return count;
}

static int compare_ints(const void *left, const void *right) {
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

static bool parse_memory_scope(const char *name, MemoryScope *scope) {
    if (strcmp(name, "local") == 0) {
        *scope = MEMORY_LOCAL;
        return true;
    }
    if (strcmp(name, "global") == 0) {
        *scope = MEMORY_GLOBAL;
        return true;
    }
    return false;
}

static bool parse_page_sequence(char *text, size_t expected_count,
                                size_t virtual_pages, int **sequence_out) {
    int *sequence = malloc(expected_count * sizeof(*sequence));
    int *sorted;
    size_t count = 0;
    char *cursor = text;
    size_t i, distinct;

    if (sequence == NULL) return false;
    while (*cursor != '\0') {
        char *end;
        long value;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0') break;
        value = strtol(cursor, &end, 10);
        if (end == cursor || value < 0 || value > INT_MAX ||
            count >= expected_count) {
            free(sequence);
            return false;
        }
        sequence[count++] = (int)value;
        cursor = end;
    }
    if (count != expected_count) {
        free(sequence);
        return false;
    }

    /* IDs podem comecar em 0 ou 1; conta IDs distintos em O(n log n). */
    sorted = malloc(count * sizeof(*sorted));
    if (sorted == NULL) {
        free(sequence);
        return false;
    }
    memcpy(sorted, sequence, count * sizeof(*sorted));
    qsort(sorted, count, sizeof(*sorted), compare_ints);
    distinct = count == 0 ? 0 : 1;
    for (i = 1; i < count; i++) {
        if (sorted[i] != sorted[i - 1]) distinct++;
    }
    free(sorted);
    if (distinct > virtual_pages) {
        free(sequence);
        return false;
    }
    *sequence_out = sequence;
    return true;
}

static bool parse_algorithm(const char *name, SchedulerAlgorithm *algorithm) {
    if (strcmp(name, "alternancia") == 0) {
        *algorithm = SCHED_ROUND_ROBIN;
    } else if (strcmp(name, "prioridade") == 0) {
        *algorithm = SCHED_PRIORITY;
    } else if (strcmp(name, "loteria") == 0) {
        *algorithm = SCHED_LOTTERY;
    } else if (strcmp(name, "cfs") == 0) {
        *algorithm = SCHED_CFS;
    } else {
        return false;
    }
    return true;
}

static int compare_arrivals(const void *left, const void *right) {
    const Process *a = *(const Process *const *)left;
    const Process *b = *(const Process *const *)right;
    if (a->creation_time != b->creation_time)
        return a->creation_time < b->creation_time ? -1 : 1;
    if (a->input_order != b->input_order)
        return a->input_order < b->input_order ? -1 : 1;
    return 0;
}

static bool append_process(Workload *workload, Process *process,
                           size_t *capacity) {
    RBNode *node;
    Process **resized;

    if (rb_tree_find(&workload->process_registry,
                     (uint64_t)process->pid, 0) != NULL) {
        fprintf(stderr, "Erro: PID %d aparece mais de uma vez.\n", process->pid);
        return false;
    }
    if (workload->process_count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        resized = realloc(workload->processes,
                          new_capacity * sizeof(*workload->processes));
        if (resized == NULL) return false;
        workload->processes = resized;
        *capacity = new_capacity;
    }

    node = rb_node_create((uint64_t)process->pid, 0, process);
    if (node == NULL || !rb_tree_insert(&workload->process_registry, node)) {
        free(node);
        return false;
    }
    process->slot = workload->process_count;
    workload->processes[workload->process_count++] = process;
    return true;
}

bool workload_load(const char *path, Workload *workload) {
    FILE *file;
    char buffer[LINE_SIZE];
    char algorithm_name[32];
    size_t capacity = 0;
    size_t line_number = 0;

    memset(workload, 0, sizeof(*workload));
    algorithm_name[0] = '\0';
    if (!rb_tree_init(&workload->process_registry)) {
        fprintf(stderr, "Erro: memoria insuficiente para a arvore de processos.\n");
        return false;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'.\n", path);
        workload_destroy(workload);
        return false;
    }

    if (fgets(buffer, sizeof(buffer), file) == NULL) {
        fprintf(stderr, "Erro: arquivo '%s' vazio.\n", path);
        fclose(file);
        workload_destroy(workload);
        return false;
    }
    line_number++;
    {
        char *header = trim(buffer);
        char extra;
        size_t separator_count = count_character(header, '|');
        if (separator_count == 1) {
            if (sscanf(header, "%31[^|]|%d %c", algorithm_name,
                       &workload->quantum, &extra) != 2)
                workload->quantum = 0;
        } else if (separator_count == 5) {
            char scope_name[16];
            unsigned long long memory_bytes;
            unsigned long long page_bytes;
            int allocation_percent;
            if (sscanf(header, "%31[^|]|%d|%15[^|]|%llu|%llu|%d %c",
                       algorithm_name, &workload->quantum, scope_name,
                       &memory_bytes, &page_bytes, &allocation_percent,
                       &extra) != 6 || memory_bytes > SIZE_MAX ||
                page_bytes > SIZE_MAX || page_bytes == 0 ||
                memory_bytes < page_bytes || allocation_percent <= 0 ||
                allocation_percent > 100 ||
                !parse_memory_scope(trim(scope_name), &workload->memory.scope)) {
                workload->quantum = 0;
            } else {
                workload->memory_enabled = true;
                workload->memory.memory_bytes = (size_t)memory_bytes;
                workload->memory.page_bytes = (size_t)page_bytes;
                workload->memory.frame_count =
                    (size_t)(memory_bytes / page_bytes);
                workload->memory.allocation_percent = allocation_percent;
            }
        } else {
            workload->quantum = 0;
        }
        if (workload->quantum <= 0 ||
            !parse_algorithm(trim(algorithm_name), &workload->algorithm)) {
            fprintf(stderr, "Erro na linha 1: cabecalho invalido.\n");
            fclose(file);
            workload_destroy(workload);
            return false;
        }
    }
    snprintf(workload->algorithm_name, sizeof(workload->algorithm_name),
             "%s", trim(algorithm_name));

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        int creation_time, pid, burst_time, attribute;
        Process *process;
        char *line;
        line_number++;
        line = trim(buffer);
        if (*line == '\0' || *line == '#') continue;

        if (workload->memory_enabled) {
            unsigned long long memory_bytes;
            int offset = 0;
            int *sequence = NULL;
            size_t virtual_pages;
            if (sscanf(line, "%d|%d|%d|%d|%llu|%n", &creation_time, &pid,
                       &burst_time, &attribute, &memory_bytes, &offset) != 5 ||
                offset <= 0 || memory_bytes == 0 || memory_bytes > SIZE_MAX) {
                fprintf(stderr, "Erro na linha %zu: processo invalido.\n",
                        line_number);
                fclose(file);
                workload_destroy(workload);
                return false;
            }
            virtual_pages = (size_t)memory_bytes / workload->memory.page_bytes +
                            ((size_t)memory_bytes % workload->memory.page_bytes != 0);
            if (burst_time <= 0 ||
                !parse_page_sequence(line + offset, (size_t)burst_time,
                                     virtual_pages, &sequence)) {
                fprintf(stderr,
                        "Erro na linha %zu: a sequencia deve ter uma pagina "
                        "por ciclo e caber na memoria virtual.\n", line_number);
                fclose(file);
                workload_destroy(workload);
                return false;
            }
            process = process_create(creation_time, pid, burst_time, attribute,
                                     workload->process_count);
            if (process == NULL ||
                !process_set_memory(process, (size_t)memory_bytes,
                                    workload->memory.page_bytes,
                                    workload->memory.allocation_percent,
                                    sequence, (size_t)burst_time)) {
                free(sequence);
                process_destroy(process);
                fprintf(stderr, "Erro ao criar processo da linha %zu.\n",
                        line_number);
                fclose(file);
                workload_destroy(workload);
                return false;
            }
        } else {
            char extra;
            if (sscanf(line, "%d|%d|%d|%d %c", &creation_time, &pid,
                       &burst_time, &attribute, &extra) != 4) {
                fprintf(stderr, "Erro na linha %zu: processo invalido.\n",
                        line_number);
                fclose(file);
                workload_destroy(workload);
                return false;
            }
            process = process_create(creation_time, pid, burst_time, attribute,
                                     workload->process_count);
        }

        if (creation_time < 0 || pid < 0 || burst_time <= 0 ||
            attribute < 0 ||
            ((workload->algorithm == SCHED_LOTTERY ||
              workload->algorithm == SCHED_CFS) && attribute == 0)) {
            process_destroy(process);
            fprintf(stderr, "Erro na linha %zu: processo invalido.\n", line_number);
            fclose(file);
            workload_destroy(workload);
            return false;
        }
        if (process == NULL || !append_process(workload, process, &capacity)) {
            process_destroy(process);
            fprintf(stderr, "Erro ao armazenar processo da linha %zu.\n", line_number);
            fclose(file);
            workload_destroy(workload);
            return false;
        }
    }
    fclose(file);

    if (workload->process_count == 0) {
        fprintf(stderr, "Erro: o arquivo nao possui processos.\n");
        workload_destroy(workload);
        return false;
    }

    workload->arrival_order = malloc(workload->process_count *
                                     sizeof(*workload->arrival_order));
    if (workload->arrival_order == NULL) {
        workload_destroy(workload);
        return false;
    }
    memcpy(workload->arrival_order, workload->processes,
           workload->process_count * sizeof(*workload->arrival_order));
    qsort(workload->arrival_order, workload->process_count,
          sizeof(*workload->arrival_order), compare_arrivals);
    return true;
}

void workload_destroy(Workload *workload) {
    size_t i;
    rb_tree_destroy(&workload->process_registry, NULL);
    for (i = 0; i < workload->process_count; i++) {
        process_destroy(workload->processes[i]);
    }
    free(workload->processes);
    free(workload->arrival_order);
    memset(workload, 0, sizeof(*workload));
}

static bool ready_init(ReadySet *ready, Workload *workload, uint64_t seed) {
    memset(ready, 0, sizeof(*ready));
    ready->algorithm = workload->algorithm;
    ready->by_slot = workload->processes;
    ready->random_state = seed == 0 ? UINT64_C(0x9e3779b97f4a7c15) : seed;

    switch (ready->algorithm) {
        case SCHED_ROUND_ROBIN:
            return queue_init(&ready->round_robin, workload->process_count);
        case SCHED_PRIORITY:
        case SCHED_CFS:
            return rb_tree_init(&ready->ordered);
        case SCHED_LOTTERY:
            return fenwick_init(&ready->lottery, workload->process_count);
    }
    return false;
}

static void ready_destroy(ReadySet *ready) {
    switch (ready->algorithm) {
        case SCHED_ROUND_ROBIN:
            queue_destroy(&ready->round_robin);
            break;
        case SCHED_PRIORITY:
        case SCHED_CFS:
            rb_tree_destroy(&ready->ordered, NULL);
            break;
        case SCHED_LOTTERY:
            fenwick_destroy(&ready->lottery);
            break;
    }
}

static bool ready_add(ReadySet *ready, Process *process, int time,
                      bool first_admission) {
    RBNode *node;
    if (first_admission && ready->algorithm == SCHED_CFS)
        process->vruntime = ready->min_vruntime;

    process->state = PROCESS_READY;
    process->ready_since = time;
    switch (ready->algorithm) {
        case SCHED_ROUND_ROBIN:
            return queue_push(&ready->round_robin, process);
        case SCHED_PRIORITY:
            /* A sequencia faz processos empatados alternarem em FIFO. */
            node = rb_node_create((uint64_t)process->attribute,
                                  ready->sequence++, process);
            if (node == NULL) return false;
            if (!rb_tree_insert(&ready->ordered, node)) {
                free(node);
                return false;
            }
            return true;
        case SCHED_LOTTERY:
            /* Atualizacao e futuro sorteio custam O(log n). */
            fenwick_add(&ready->lottery, process->slot, process->attribute);
            return true;
        case SCHED_CFS:
            /* O menor par (vruntime, PID) fica na extremidade esquerda. */
            node = rb_node_create(process->vruntime,
                                  (uint64_t)process->pid, process);
            if (node == NULL) return false;
            if (!rb_tree_insert(&ready->ordered, node)) {
                free(node);
                return false;
            }
            return true;
    }
    return false;
}

static bool ready_empty(const ReadySet *ready) {
    switch (ready->algorithm) {
        case SCHED_ROUND_ROBIN:
            return queue_empty(&ready->round_robin);
        case SCHED_PRIORITY:
        case SCHED_CFS:
            return rb_tree_empty(&ready->ordered);
        case SCHED_LOTTERY:
            return fenwick_total(&ready->lottery) == 0;
    }
    return true;
}

static uint64_t random_next(ReadySet *ready) {
    uint64_t x = ready->random_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    ready->random_state = x;
    return x * UINT64_C(2685821657736338717);
}

static Process *ready_take(ReadySet *ready, int time) {
    Process *process;
    RBNode *node;
    switch (ready->algorithm) {
        case SCHED_ROUND_ROBIN:
            process = queue_pop(&ready->round_robin);
            break;
        case SCHED_PRIORITY:
        case SCHED_CFS:
            node = rb_tree_minimum(&ready->ordered);
            if (node == NULL) return NULL;
            process = node->data;
            rb_tree_remove(&ready->ordered, node);
            free(node);
            break;
        case SCHED_LOTTERY: {
            uint64_t total = fenwick_total(&ready->lottery);
            uint64_t winning_ticket;
            size_t slot;
            if (total == 0) return NULL;
            winning_ticket = random_next(ready) % total + 1;
            slot = fenwick_find_by_order(&ready->lottery, winning_ticket);
            process = ready->by_slot[slot];
            fenwick_add(&ready->lottery, slot, -process->attribute);
            break;
        }
        default:
            return NULL;
    }
    process->ready_time += (long long)(time - process->ready_since);
    return process;
}

static bool admit_processes(Workload *workload, ReadySet *ready, int time,
                            size_t *next_arrival) {
    /* arrival_order foi ordenado uma unica vez durante a leitura. */
    while (*next_arrival < workload->process_count &&
           workload->arrival_order[*next_arrival]->creation_time <= time) {
        Process *process = workload->arrival_order[*next_arrival];
        if (!ready_add(ready, process, process->creation_time, true)) return false;
        (*next_arrival)++;
    }
    return true;
}

static void update_cfs_floor(ReadySet *ready, const Process *running) {
    /* min_vruntime nunca diminui e serve de base para processos novos. */
    uint64_t candidate = UINT64_MAX;
    RBNode *minimum = rb_tree_minimum(&ready->ordered);
    if (minimum != NULL) candidate = minimum->key;
    if (running != NULL && running->vruntime < candidate)
        candidate = running->vruntime;
    if (candidate != UINT64_MAX && candidate > ready->min_vruntime)
        ready->min_vruntime = candidate;
}

static const char *attribute_label(SchedulerAlgorithm algorithm) {
    if (algorithm == SCHED_ROUND_ROBIN) return "ignorado na alternancia";
    if (algorithm == SCHED_LOTTERY) return "bilhetes";
    if (algorithm == SCHED_CFS) return "peso";
    return "prioridade";
}

static bool trace_append(MemoryAccess **trace, size_t *count, size_t *capacity,
                         Process *process, int page, bool finished) {
    MemoryAccess *resized;
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 64 : *capacity * 2;
        resized = realloc(*trace, new_capacity * sizeof(**trace));
        if (resized == NULL) return false;
        *trace = resized;
        *capacity = new_capacity;
    }
    (*trace)[*count].process_slot = process->slot;
    (*trace)[*count].pid = process->pid;
    (*trace)[*count].page = page;
    (*trace)[*count].process_finished = finished;
    (*count)++;
    return true;
}

static int compare_pid(const void *left, const void *right) {
    const Process *a = *(const Process *const *)left;
    const Process *b = *(const Process *const *)right;
    return (a->pid > b->pid) - (a->pid < b->pid);
}

static void print_report(Workload *workload, int total_time,
                         SimulationSummary *summary) {
    Process **ordered = malloc(workload->process_count * sizeof(*ordered));
    long long total_turnaround = 0;
    long long total_ready = 0;
    size_t i;

    printf("\nRESULTADO FINAL - %s\n", workload->algorithm_name);
    printf("+------+---------+----------+----------+-------------+--------+\n");
    printf("| PID  | Criacao | Execucao | Conclusao| Turnaround  | Pronto |\n");
    printf("+------+---------+----------+----------+-------------+--------+\n");
    if (ordered != NULL) {
        memcpy(ordered, workload->processes,
               workload->process_count * sizeof(*ordered));
        qsort(ordered, workload->process_count, sizeof(*ordered), compare_pid);
    }
    for (i = 0; i < workload->process_count; i++) {
        Process *p = ordered != NULL ? ordered[i] : workload->processes[i];
        int turnaround = p->completion_time - p->creation_time;
        printf("| %-4d | %-7d | %-8d | %-8d | %-11d | %-6lld |\n",
               p->pid, p->creation_time, p->burst_time, p->completion_time,
               turnaround, p->ready_time);
        total_turnaround += turnaround;
        total_ready += p->ready_time;
    }
    printf("+------+---------+----------+----------+-------------+--------+\n");
    printf("Tempo total: %d\n", total_time);
    printf("Media de turnaround: %.2f\n",
           (double)total_turnaround / (double)workload->process_count);
    printf("Media em pronto: %.2f\n",
           (double)total_ready / (double)workload->process_count);
    free(ordered);

    snprintf(summary->algorithm_name, sizeof(summary->algorithm_name),
             "%s", workload->algorithm_name);
    summary->average_turnaround =
        (double)total_turnaround / (double)workload->process_count;
    summary->average_ready =
        (double)total_ready / (double)workload->process_count;
    summary->total_time = total_time;
}

bool scheduler_run(Workload *workload, uint64_t seed,
                   SimulationSummary *summary) {
    ReadySet ready;
    size_t next_arrival = 0;
    size_t completed = 0;
    int time = 0;
    MemoryAccess *trace = NULL;
    size_t trace_count = 0;
    size_t trace_capacity = 0;

    if (!ready_init(&ready, workload, seed)) {
        fprintf(stderr, "Erro: nao foi possivel criar a fila de prontos.\n");
        return false;
    }

    printf("\n============================================================\n");
    printf("Algoritmo: %s | quantum: %d | processos: %zu\n",
           workload->algorithm_name, workload->quantum,
           workload->process_count);
    if (workload->algorithm == SCHED_LOTTERY)
        printf("Semente da loteria: %" PRIu64 "\n", seed);
    printf("Ultimo campo interpretado como: %s\n",
           attribute_label(workload->algorithm));
    printf("============================================================\n");

    while (completed < workload->process_count) {
        Process *current;
        int used = 0;

        if (!admit_processes(workload, &ready, time, &next_arrival)) goto fail;
        if (ready_empty(&ready)) {
            printf("[t=%d -> %d] CPU OCIOSA\n", time, time + 1);
            time++;
            continue;
        }

        current = ready_take(&ready, time);
        if (current == NULL) goto fail;
        current->state = PROCESS_RUNNING;

        while (used < workload->quantum && current->remaining_time > 0) {
            int before = current->remaining_time;
            int page = 0;
            if (workload->memory_enabled) {
                size_t access_index =
                    (size_t)(current->burst_time - current->remaining_time);
                page = current->page_sequence[access_index];
            }
            current->remaining_time--;
            if (workload->algorithm == SCHED_CFS) {
                uint64_t delta = VRUNTIME_SCALE / (uint64_t)current->attribute;
                if (delta == 0) delta = 1;
                current->vruntime += delta;
            }
            printf("[t=%d -> %d] PID %-4d | restante: %d -> %d\n",
                   time, time + 1, current->pid, before,
                   current->remaining_time);
            if (workload->memory_enabled &&
                !trace_append(&trace, &trace_count, &trace_capacity, current,
                              page, current->remaining_time == 0))
                goto fail;
            time++;
            used++;

            if (workload->algorithm == SCHED_CFS)
                update_cfs_floor(&ready, current);
            if (!admit_processes(workload, &ready, time, &next_arrival)) goto fail;
        }

        if (current->remaining_time == 0) {
            current->state = PROCESS_FINISHED;
            current->completion_time = time;
            completed++;
            printf("           PID %d concluido em t=%d.\n", current->pid, time);
            if (workload->algorithm == SCHED_CFS)
                update_cfs_floor(&ready, NULL);
        } else if (!ready_add(&ready, current, time, false)) {
            goto fail;
        }
    }

    ready_destroy(&ready);
    print_report(workload, time, summary);
    if (workload->memory_enabled) {
        MemoryResult memory_result;
        if (!memory_simulate(&workload->memory, workload->processes,
                             workload->process_count, trace, trace_count,
                             &memory_result)) {
            free(trace);
            return false;
        }
    }
    free(trace);
    return true;

fail:
    fprintf(stderr, "Erro durante a simulacao.\n");
    ready_destroy(&ready);
    free(trace);
    return false;
}
