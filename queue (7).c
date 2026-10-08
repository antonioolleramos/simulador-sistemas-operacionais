#include "queue.h"

#include <stdlib.h>

bool queue_init(ProcessQueue *queue, size_t capacity) {
    if (capacity == 0) {
        capacity = 1;
    }
    queue->items = calloc(capacity, sizeof(*queue->items));
    if (queue->items == NULL) {
        return false;
    }
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    return true;
}

bool queue_push(ProcessQueue *queue, Process *process) {
    if (queue->count == queue->capacity) {
        return false;
    }
    queue->items[queue->tail] = process;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    return true;
}

Process *queue_pop(ProcessQueue *queue) {
    Process *process;
    if (queue->count == 0) {
        return NULL;
    }
    process = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return process;
}

bool queue_empty(const ProcessQueue *queue) {
    return queue->count == 0;
}

void queue_destroy(ProcessQueue *queue) {
    free(queue->items);
    queue->items = NULL;
    queue->capacity = 0;
    queue->count = 0;
}
