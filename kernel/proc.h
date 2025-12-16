#ifndef PROC_H
#define PROC_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

#define MAX_PROCS       CONFIG_MAX_PROCS
#define PROC_STACK_SIZE CONFIG_PROC_STACK_SIZE

typedef enum
{
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_WAITING,
    PROC_ZOMBIE
} proc_state_t;

struct context
{
    uint32_t esp;
};

struct process
{
    int pid;
    proc_state_t state;
    struct context ctx;
    uint8_t stack[PROC_STACK_SIZE];
    size_t stack_size;
    int channel_slots[CONFIG_PROCESS_CHANNEL_SLOTS];
    uint8_t channel_count;
    int wait_channel;
    int exit_code;
};

void process_system_init(void);
int process_create(void (*entry)(void), size_t stack_size);
void process_yield(void);
void process_exit(int code);
void process_block_current(void);
void process_wake(struct process *proc);
void process_schedule(void);
struct process *process_current(void);
struct process *process_lookup(int pid);
void process_debug_list(void);
int process_count(void);

/* IPC helpers exposed to syscall layer */
/* Legacy IPC entry points removed in favour of channel system */

#endif
