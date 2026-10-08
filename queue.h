#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#include "process.h"

typedef struct {
    Process **items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} ProcessQueue;

bool queue_init(ProcessQueue *queue, size_t capacity);
bool queue_push(ProcessQueue *queue, Process *process);
Process *queue_pop(ProcessQueue *queue);
bool queue_empty(const ProcessQueue *queue);
void queue_destroy(ProcessQueue *queue);

#endif
