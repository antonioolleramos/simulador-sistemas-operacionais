#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool io_device_init(IODevice *device, const char *id, size_t capacity,
                    int operation_time, size_t process_count) {
    memset(device, 0, sizeof(*device));
    if (capacity == 0 || operation_time <= 0) return false;
    snprintf(device->id, sizeof(device->id), "%s", id);
    device->capacity = capacity;
    device->operation_time = operation_time;
    device->active = calloc(capacity, sizeof(*device->active));
    if (device->active == NULL) return false;
    if (!queue_init(&device->waiting, process_count)) {
        free(device->active);
        device->active = NULL;
        return false;
    }
    return true;
}

void io_device_destroy(IODevice *device) {
    free(device->active);
    queue_destroy(&device->waiting);
    memset(device, 0, sizeof(*device));
}

static bool start_operation(IODevice *device, Process *process) {
    size_t i;
    for (i = 0; i < device->capacity; i++) {
        if (device->active[i].process == NULL) {
            device->active[i].process = process;
            device->active[i].remaining = device->operation_time;
            device->active_count++;
            process->io_waiting = false;
            return true;
        }
    }
    return false;
}

bool io_request(IODevice *device, size_t device_index, Process *process) {
    process->state = PROCESS_BLOCKED;
    process->io_device = (int)device_index;
    process->io_requests++;

    if (device->active_count < device->capacity)
        return start_operation(device, process);

    process->io_waiting = true;
    return queue_push(&device->waiting, process);
}

static void compact_finished_and_promote(IODevice *device, int new_time,
                                         bool *ok) {
    size_t i;
    for (i = 0; i < device->capacity; i++) {
        Process *process = device->active[i].process;
        if (process != NULL && device->active[i].remaining == 0) {
            process->state = PROCESS_READY;
            process->ready_since = new_time;
            process->io_device = -1;
            process->io_waiting = false;
            device->active[i].process = NULL;
            device->active_count--;
            printf("           PID %d concluiu E/S em %s em t=%d.\n",
                   process->pid, device->id, new_time);
        }
    }

    while (device->active_count < device->capacity &&
           !queue_empty(&device->waiting)) {
        Process *process = queue_pop(&device->waiting);
        if (process == NULL || !start_operation(device, process)) {
            *ok = false;
            return;
        }
        printf("           PID %d iniciou E/S em %s em t=%d.\n",
               process->pid, device->id, new_time);
    }
}

void io_tick(IODevice *devices, size_t device_count, int new_time,
             bool *ok) {
    size_t d, i;
    for (d = 0; d < device_count; d++) {
        IODevice *device = &devices[d];
        for (i = 0; i < device->capacity; i++) {
            if (device->active[i].process != NULL &&
                device->active[i].remaining > 0)
                device->active[i].remaining--;
        }
    }
    for (d = 0; d < device_count; d++)
        compact_finished_and_promote(&devices[d], new_time, ok);
}

int io_remaining_for_process(const IODevice *device, const Process *process) {
    size_t i;
    for (i = 0; i < device->capacity; i++) {
        if (device->active[i].process == process)
            return device->active[i].remaining;
    }
    return -1;
}

static void print_waiting_queue(const ProcessQueue *queue) {
    size_t i;
    if (queue->count == 0) {
        printf("nenhum");
        return;
    }
    for (i = 0; i < queue->count; i++) {
        size_t index = (queue->head + i) % queue->capacity;
        printf("%sPID %d", i == 0 ? "" : ", ", queue->items[index]->pid);
    }
}

void io_print_devices(const IODevice *devices, size_t device_count) {
    size_t d, i;
    printf("DISPOSITIVOS DE E/S:\n");
    for (d = 0; d < device_count; d++) {
        const IODevice *device = &devices[d];
        printf("  %s | em uso %zu/%zu | tempo por operacao %d\n",
               device->id, device->active_count, device->capacity,
               device->operation_time);
        printf("    Usando: ");
        if (device->active_count == 0) {
            printf("nenhum");
        } else {
            bool first = true;
            for (i = 0; i < device->capacity; i++) {
                if (device->active[i].process != NULL) {
                    printf("%sPID %d (restam %d)", first ? "" : ", ",
                           device->active[i].process->pid,
                           device->active[i].remaining);
                    first = false;
                }
            }
        }
        printf("\n    Aguardando: ");
        print_waiting_queue(&device->waiting);
        printf("\n");
    }
}
