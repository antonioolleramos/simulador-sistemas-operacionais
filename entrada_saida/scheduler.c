#include "scheduler.h"

#include "queue.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_SIZE 65536

typedef struct {
    uint64_t state;
} RandomGenerator;

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) text++;
    if (*text == '\0') return text;
    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return text;
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

static int compare_ints(const void *left, const void *right) {
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

static bool parse_page_sequence(char *text, size_t expected_count,
                                size_t virtual_pages, int **sequence_out) {
    int *sequence = malloc(expected_count * sizeof(*sequence));
    int *sorted;
    size_t count = 0;
    size_t i, distinct;
    char *cursor = text;

    if (sequence == NULL) return false;
    while (*cursor != '\0') {
        char *end;
        long value;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '\0' || *cursor == '|') break;
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

static int compare_arrivals(const void *left, const void *right) {
    const Process *a = *(const Process *const *)left;
    const Process *b = *(const Process *const *)right;
    if (a->creation_time != b->creation_time)
        return a->creation_time < b->creation_time ? -1 : 1;
    if (a->input_order != b->input_order)
        return a->input_order < b->input_order ? -1 : 1;
    return 0;
}

static int compare_pid(const void *left, const void *right) {
    const Process *a = *(const Process *const *)left;
    const Process *b = *(const Process *const *)right;
    return (a->pid > b->pid) - (a->pid < b->pid);
}

static bool append_process(Workload *workload, Process *process,
                           size_t *capacity) {
    Process **resized;
    size_t i;
    for (i = 0; i < workload->process_count; i++) {
        if (workload->processes[i]->pid == process->pid) {
            fprintf(stderr, "Erro: PID %d aparece mais de uma vez.\n",
                    process->pid);
            return false;
        }
    }

    if (workload->process_count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        resized = realloc(workload->processes,
                          new_capacity * sizeof(*workload->processes));
        if (resized == NULL) return false;
        workload->processes = resized;
        *capacity = new_capacity;
    }
    process->slot = workload->process_count;
    workload->processes[workload->process_count++] = process;
    return true;
}

static bool parse_process_line(Workload *workload, char *line,
                               size_t line_number, size_t *capacity) {
    int creation_time, pid, burst_time, attribute, io_chance;
    unsigned long long memory_bytes;
    char *fields[7];
    char *cursor = line;
    size_t field_count = 0;
    size_t virtual_pages;
    int *sequence = NULL;
    Process *process = NULL;

    while (field_count < 7) {
        char *separator = strchr(cursor, '|');
        fields[field_count++] = cursor;
        if (separator == NULL) break;
        *separator = '\0';
        cursor = separator + 1;
    }
    if (field_count != 7 || strchr(fields[6], '|') != NULL) {
        fprintf(stderr, "Erro na linha %zu: processo invalido.\n", line_number);
        return false;
    }

    {
        char extra;
        if (sscanf(trim(fields[0]), "%d %c", &creation_time, &extra) != 1 ||
            sscanf(trim(fields[1]), "%d %c", &pid, &extra) != 1 ||
            sscanf(trim(fields[2]), "%d %c", &burst_time, &extra) != 1 ||
            sscanf(trim(fields[3]), "%d %c", &attribute, &extra) != 1 ||
            sscanf(trim(fields[4]), "%llu %c", &memory_bytes, &extra) != 1 ||
            sscanf(trim(fields[6]), "%d %c", &io_chance, &extra) != 1) {
            fprintf(stderr, "Erro na linha %zu: processo invalido.\n", line_number);
            return false;
        }
    }

    if (creation_time < 0 || pid < 0 || burst_time <= 0 || attribute < 0 ||
        memory_bytes == 0 || memory_bytes > SIZE_MAX ||
        io_chance < 0 || io_chance > 100) {
        fprintf(stderr, "Erro na linha %zu: valores de processo invalidos.\n",
                line_number);
        return false;
    }

    virtual_pages = (size_t)memory_bytes / workload->memory.page_bytes +
                    ((size_t)memory_bytes % workload->memory.page_bytes != 0);
    if (!parse_page_sequence(trim(fields[5]), (size_t)burst_time,
                             virtual_pages, &sequence)) {
        fprintf(stderr,
                "Erro na linha %zu: sequencia de paginas invalida.\n",
                line_number);
        return false;
    }

    process = process_create(creation_time, pid, burst_time, attribute,
                             io_chance, workload->process_count);
    if (process == NULL ||
        !process_set_memory(process, (size_t)memory_bytes,
                            workload->memory.page_bytes,
                            workload->memory.allocation_percent,
                            sequence, (size_t)burst_time)) {
        free(sequence);
        process_destroy(process);
        fprintf(stderr, "Erro ao criar processo da linha %zu.\n", line_number);
        return false;
    }

    if (!append_process(workload, process, capacity)) {
        process_destroy(process);
        fprintf(stderr, "Erro ao armazenar processo da linha %zu.\n", line_number);
        return false;
    }
    return true;
}

bool workload_load(const char *path, Workload *workload) {
    FILE *file;
    char buffer[LINE_SIZE];
    char algorithm[32];
    char scope[16];
    unsigned long long memory_bytes, page_bytes, device_count;
    int allocation_percent;
    size_t line_number = 0;
    size_t process_capacity = 0;
    size_t d;

    memset(workload, 0, sizeof(*workload));
    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'.\n", path);
        return false;
    }

    if (fgets(buffer, sizeof(buffer), file) == NULL) {
        fprintf(stderr, "Erro: arquivo '%s' vazio.\n", path);
        fclose(file);
        return false;
    }
    line_number++;
    {
        char extra;
        if (sscanf(trim(buffer), "%31[^|]|%d|%15[^|]|%llu|%llu|%d|%llu %c",
                   algorithm, &workload->quantum, scope, &memory_bytes,
                   &page_bytes, &allocation_percent, &device_count, &extra) != 7 ||
            strcmp(trim(algorithm), "alternancia") != 0 ||
            workload->quantum <= 0 || page_bytes == 0 || memory_bytes < page_bytes ||
            memory_bytes > SIZE_MAX || page_bytes > SIZE_MAX ||
            allocation_percent <= 0 || allocation_percent > 100 ||
            device_count == 0 || device_count > SIZE_MAX ||
            !parse_memory_scope(trim(scope), &workload->memory.scope)) {
            fprintf(stderr,
                    "Erro na linha 1: esta etapa exige alternancia e um cabecalho de E/S valido.\n");
            fclose(file);
            return false;
        }
    }

    snprintf(workload->algorithm_name, sizeof(workload->algorithm_name),
             "%s", trim(algorithm));
    workload->memory.memory_bytes = (size_t)memory_bytes;
    workload->memory.page_bytes = (size_t)page_bytes;
    workload->memory.frame_count = (size_t)(memory_bytes / page_bytes);
    workload->memory.allocation_percent = allocation_percent;
    workload->device_count = (size_t)device_count;
    workload->devices = calloc(workload->device_count, sizeof(*workload->devices));
    if (workload->devices == NULL) {
        fclose(file);
        return false;
    }

    for (d = 0; d < workload->device_count; d++) {
        char id[64];
        unsigned long long simultaneous;
        int operation_time;
        char extra;
        if (fgets(buffer, sizeof(buffer), file) == NULL) {
            fprintf(stderr, "Erro: faltam descricoes de dispositivos de E/S.\n");
            fclose(file);
            workload_destroy(workload);
            return false;
        }
        line_number++;
        if (sscanf(trim(buffer), "%63[^|]|%llu|%d %c", id, &simultaneous,
                   &operation_time, &extra) != 3 || simultaneous == 0 ||
            simultaneous > SIZE_MAX || operation_time <= 0 ||
            !io_device_init(&workload->devices[d], trim(id),
                            (size_t)simultaneous, operation_time, 1)) {
            fprintf(stderr, "Erro na linha %zu: dispositivo de E/S invalido.\n",
                    line_number);
            fclose(file);
            workload_destroy(workload);
            return false;
        }
    }

    /* As filas de espera precisam conhecer o numero de processos. Como esse
       numero so e conhecido apos a leitura, os dispositivos serao reiniciados
       ao final com a capacidade correta. */
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        char *line = trim(buffer);
        line_number++;
        if (*line == '\0' || *line == '#') continue;
        if (!parse_process_line(workload, line, line_number, &process_capacity)) {
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

    for (d = 0; d < workload->device_count; d++) {
        char id[64];
        size_t capacity = workload->devices[d].capacity;
        int operation_time = workload->devices[d].operation_time;
        snprintf(id, sizeof(id), "%s", workload->devices[d].id);
        io_device_destroy(&workload->devices[d]);
        if (!io_device_init(&workload->devices[d], id, capacity,
                            operation_time, workload->process_count)) {
            fprintf(stderr, "Erro ao preparar fila de E/S.\n");
            workload_destroy(workload);
            return false;
        }
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
    for (i = 0; i < workload->device_count; i++)
        io_device_destroy(&workload->devices[i]);
    free(workload->devices);
    for (i = 0; i < workload->process_count; i++)
        process_destroy(workload->processes[i]);
    free(workload->processes);
    free(workload->arrival_order);
    memset(workload, 0, sizeof(*workload));
}

static uint64_t random_next(RandomGenerator *random) {
    uint64_t x = random->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    random->state = x;
    return x * UINT64_C(2685821657736338717);
}

static size_t random_bounded(RandomGenerator *random, size_t limit) {
    return limit == 0 ? 0 : (size_t)(random_next(random) % limit);
}

static bool admit_processes(Workload *workload, ProcessQueue *ready,
                            int time, size_t *next_arrival) {
    while (*next_arrival < workload->process_count &&
           workload->arrival_order[*next_arrival]->creation_time <= time) {
        Process *process = workload->arrival_order[*next_arrival];
        process->state = PROCESS_READY;
        process->ready_since = process->creation_time;
        if (!queue_push(ready, process)) return false;
        (*next_arrival)++;
    }
    return true;
}

static bool enqueue_newly_ready(Workload *workload, ProcessQueue *ready,
                                bool *already_enqueued) {
    size_t i;
    for (i = 0; i < workload->process_count; i++) {
        Process *process = workload->processes[i];
        if (process->state == PROCESS_READY && !already_enqueued[i]) {
            if (!queue_push(ready, process)) return false;
            already_enqueued[i] = true;
        }
    }
    return true;
}

static void mark_queue_members(const ProcessQueue *ready, bool *flags,
                               size_t process_count) {
    size_t i;
    memset(flags, 0, process_count * sizeof(*flags));
    for (i = 0; i < ready->count; i++) {
        size_t index = (ready->head + i) % ready->capacity;
        flags[ready->items[index]->slot] = true;
    }
}

static void count_blocked_interval(Workload *workload) {
    size_t i;
    for (i = 0; i < workload->process_count; i++) {
        if (workload->processes[i]->state == PROCESS_BLOCKED)
            workload->processes[i]->blocked_time++;
    }
}

static bool advance_one_time_unit(Workload *workload, ProcessQueue *ready,
                                  size_t *next_arrival, int *time,
                                  bool *queue_flags) {
    bool io_ok = true;
    count_blocked_interval(workload);
    io_tick(workload->devices, workload->device_count, *time + 1, &io_ok);
    if (!io_ok) return false;
    (*time)++;
    if (!admit_processes(workload, ready, *time, next_arrival)) return false;
    mark_queue_members(ready, queue_flags, workload->process_count);
    if (!enqueue_newly_ready(workload, ready, queue_flags)) return false;
    return true;
}

static const char *device_name_for_process(const Workload *workload,
                                           const Process *process) {
    if (process->io_device < 0 ||
        (size_t)process->io_device >= workload->device_count)
        return "-";
    return workload->devices[process->io_device].id;
}

static void print_system_state(const Workload *workload,
                               const Process *running, int time) {
    Process **ordered = malloc(workload->process_count * sizeof(*ordered));
    size_t i;

    printf("\n================ ESTADO DO SISTEMA EM t=%d ================\n", time);
    if (running != NULL)
        printf("CPU: PID %d | CPU restante: %d\n",
               running->pid, running->remaining_time);
    else
        printf("CPU: OCIOSA\n");

    if (ordered != NULL) {
        memcpy(ordered, workload->processes,
               workload->process_count * sizeof(*ordered));
        qsort(ordered, workload->process_count, sizeof(*ordered), compare_pid);
    }

    printf("PRONTOS:\n");
    {
        bool any = false;
        for (i = 0; i < workload->process_count; i++) {
            Process *p = ordered != NULL ? ordered[i] : workload->processes[i];
            if (p->state == PROCESS_READY) {
                printf("  PID %d | CPU restante: %d\n", p->pid, p->remaining_time);
                any = true;
            }
        }
        if (!any) printf("  nenhum\n");
    }

    printf("BLOQUEADOS:\n");
    {
        bool any = false;
        for (i = 0; i < workload->process_count; i++) {
            Process *p = ordered != NULL ? ordered[i] : workload->processes[i];
            if (p->state == PROCESS_BLOCKED) {
                const IODevice *device = &workload->devices[p->io_device];
                int remaining = io_remaining_for_process(device, p);
                printf("  PID %d | CPU restante: %d | %s | %s",
                       p->pid, p->remaining_time,
                       device_name_for_process(workload, p),
                       p->io_waiting ? "AGUARDANDO" : "EM USO");
                if (!p->io_waiting && remaining >= 0)
                    printf(" (E/S restante: %d)", remaining);
                printf("\n");
                any = true;
            }
        }
        if (!any) printf("  nenhum\n");
    }

    io_print_devices(workload->devices, workload->device_count);
    printf("============================================================\n");
    free(ordered);
}

static void print_final_report(Workload *workload, int total_time,
                               const MemoryManager *memory,
                               SimulationSummary *summary) {
    Process **ordered = malloc(workload->process_count * sizeof(*ordered));
    long long total_turnaround = 0;
    long long total_ready = 0;
    long long total_blocked = 0;
    size_t i;

    if (ordered != NULL) {
        memcpy(ordered, workload->processes,
               workload->process_count * sizeof(*ordered));
        qsort(ordered, workload->process_count, sizeof(*ordered), compare_pid);
    }

    printf("\nRESULTADO FINAL - Round Robin + LRU + E/S\n");
    printf("+------+---------+----------+----------+------------+--------+-----------+----------+\n");
    printf("| PID  | Criacao | Execucao | Conclusao| Turnaround | Pronto | Bloqueado | Req. E/S |\n");
    printf("+------+---------+----------+----------+------------+--------+-----------+----------+\n");
    for (i = 0; i < workload->process_count; i++) {
        Process *p = ordered != NULL ? ordered[i] : workload->processes[i];
        int turnaround = p->completion_time - p->creation_time;
        printf("| %-4d | %-7d | %-8d | %-8d | %-10d | %-6lld | %-9lld | %-8llu |\n",
               p->pid, p->creation_time, p->burst_time, p->completion_time,
               turnaround, p->ready_time, p->blocked_time, p->io_requests);
        total_turnaround += turnaround;
        total_ready += p->ready_time;
        total_blocked += p->blocked_time;
    }
    printf("+------+---------+----------+----------+------------+--------+-----------+----------+\n");
    printf("Tempo total: %d\n", total_time);
    printf("Media de turnaround: %.2f\n",
           (double)total_turnaround / (double)workload->process_count);
    printf("Media em pronto: %.2f\n",
           (double)total_ready / (double)workload->process_count);
    printf("Media em bloqueado: %.2f\n",
           (double)total_blocked / (double)workload->process_count);

    memory_print_summary(memory);

    summary->average_turnaround =
        (double)total_turnaround / (double)workload->process_count;
    summary->average_ready =
        (double)total_ready / (double)workload->process_count;
    summary->average_blocked =
        (double)total_blocked / (double)workload->process_count;
    summary->total_time = total_time;
    summary->memory_replacements = memory->replacements;
    free(ordered);
}

bool scheduler_run(Workload *workload, uint64_t seed,
                   SimulationSummary *summary) {
    ProcessQueue ready;
    MemoryManager memory;
    RandomGenerator random;
    bool *queue_flags = NULL;
    size_t next_arrival = 0;
    size_t completed = 0;
    int time = 0;
    bool ready_initialized = false;
    bool memory_initialized = false;

    memset(summary, 0, sizeof(*summary));
    random.state = seed == 0 ? UINT64_C(0x9e3779b97f4a7c15) : seed;

    if (!queue_init(&ready, workload->process_count)) {
        fprintf(stderr, "Erro ao criar fila de prontos.\n");
        return false;
    }
    ready_initialized = true;
    queue_flags = calloc(workload->process_count, sizeof(*queue_flags));
    if (queue_flags == NULL) goto fail;

    if (!memory_init(&memory, &workload->memory, workload->processes,
                     workload->process_count)) {
        fprintf(stderr, "Erro ao iniciar o gerenciamento de memoria.\n");
        goto fail;
    }
    memory_initialized = true;

    printf("\n============================================================\n");
    printf("PARTE 3 - ESCALONAMENTO, MEMORIA E ENTRADA/SAIDA\n");
    printf("Escalonamento: Round Robin | quantum: %d\n", workload->quantum);
    printf("Memoria: LRU | politica: %s\n",
           workload->memory.scope == MEMORY_LOCAL ? "local" : "global");
    printf("Dispositivos de E/S: %zu | semente: %" PRIu64 "\n",
           workload->device_count, seed);
    printf("============================================================\n");

    while (completed < workload->process_count) {
        Process *current;
        size_t max_slice;
        bool will_request_io;
        size_t io_device_index = 0;
        size_t io_moment = 0;
        size_t used = 0;
        bool blocked = false;

        if (!admit_processes(workload, &ready, time, &next_arrival)) goto fail;
        mark_queue_members(&ready, queue_flags, workload->process_count);
        if (!enqueue_newly_ready(workload, &ready, queue_flags)) goto fail;

        if (queue_empty(&ready)) {
            print_system_state(workload, NULL, time);
            printf("[t=%d -> %d] CPU OCIOSA\n", time, time + 1);
            if (!advance_one_time_unit(workload, &ready, &next_arrival,
                                       &time, queue_flags))
                goto fail;
            continue;
        }

        current = queue_pop(&ready);
        if (current == NULL) goto fail;
        current->ready_time += (long long)(time - current->ready_since);
        current->state = PROCESS_RUNNING;

        max_slice = (size_t)workload->quantum;
        if ((size_t)current->remaining_time < max_slice)
            max_slice = (size_t)current->remaining_time;

        will_request_io = random_bounded(&random, 100) <
                          (size_t)current->io_chance;
        if (will_request_io) {
            size_t io_window = max_slice;

            /* Se esta fatia conseguiria concluir o processo, a E/S precisa
               ocorrer antes do ultimo ciclo de CPU. Assim nao anunciamos uma
               requisicao para o mesmo instante em que o processo termina. */
            if (max_slice == (size_t)current->remaining_time) {
                if (io_window <= 1) {
                    will_request_io = false;
                } else {
                    io_window--;
                }
            }
            if (will_request_io) {
                io_device_index = random_bounded(&random, workload->device_count);
                io_moment = random_bounded(&random, io_window) + 1;
            }
        }

        print_system_state(workload, current, time);
        if (will_request_io) {
            printf("Decisao de E/S: PID %d solicitara %s apos %zu unidade(s) de CPU desta fatia.\n",
                   current->pid, workload->devices[io_device_index].id,
                   io_moment);
        } else {
            printf("Decisao de E/S: PID %d nao solicitara E/S nesta fatia.\n",
                   current->pid);
        }

        while (used < max_slice && current->remaining_time > 0) {
            int before = current->remaining_time;
            size_t access_index =
                (size_t)(current->burst_time - current->remaining_time);
            int page = current->page_sequence[access_index];

            if (!memory_access(&memory, current, page)) {
                fprintf(stderr, "Erro durante acesso a memoria do PID %d.\n",
                        current->pid);
                goto fail;
            }
            current->remaining_time--;
            used++;
            printf("[t=%d -> %d] PID %-4d | pagina %-3d | CPU restante: %d -> %d\n",
                   time, time + 1, current->pid, page, before,
                   current->remaining_time);

            if (!advance_one_time_unit(workload, &ready, &next_arrival,
                                       &time, queue_flags))
                goto fail;

            if (current->remaining_time == 0) break;

            if (will_request_io && used == io_moment) {
                IODevice *device = &workload->devices[io_device_index];
                if (!io_request(device, io_device_index, current)) goto fail;
                printf("           PID %d solicitou E/S em %s em t=%d: %s.\n",
                       current->pid, device->id, time,
                       current->io_waiting ? "entrou na fila de espera" :
                                             "iniciou a operacao");
                blocked = true;
                break;
            }
        }

        if (current->remaining_time == 0) {
            current->state = PROCESS_FINISHED;
            current->completion_time = time;
            memory_release_process(&memory, current->slot);
            completed++;
            printf("           PID %d concluido em t=%d.\n", current->pid, time);
        } else if (!blocked) {
            current->state = PROCESS_READY;
            current->ready_since = time;
            if (!queue_push(&ready, current)) goto fail;
        }
    }

    print_final_report(workload, time, &memory, summary);
    memory_destroy(&memory);
    free(queue_flags);
    queue_destroy(&ready);
    return true;

fail:
    fprintf(stderr, "Erro durante a simulacao da Parte 3.\n");
    if (memory_initialized) memory_destroy(&memory);
    free(queue_flags);
    if (ready_initialized) queue_destroy(&ready);
    return false;
}
