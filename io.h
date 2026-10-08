#ifndef IO_H
#define IO_H

#include <stdbool.h>
#include <stddef.h>

#include "process.h"
#include "queue.h"

typedef struct {
    Process *process;
    int remaining;
} IOActiveSlot;

typedef struct {
    char id[64];
    size_t capacity;
    int operation_time;
    IOActiveSlot *active;
    size_t active_count;
    ProcessQueue waiting;
} IODevice;

bool io_device_init(IODevice *device, const char *id, size_t capacity,
                    int operation_time, size_t process_count);
void io_device_destroy(IODevice *device);
bool io_request(IODevice *device, size_t device_index, Process *process);
void io_tick(IODevice *devices, size_t device_count, int new_time,
             bool *ok);
void io_print_devices(const IODevice *devices, size_t device_count);
int io_remaining_for_process(const IODevice *device, const Process *process);

#endif
