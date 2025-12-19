#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

enum syscall_number
{
    SYS_WRITE = 0,
    SYS_YIELD = 1,
    SYS_SPAWN = 2,
    SYS_SEND = 3,
    SYS_RECV = 4,
    SYS_EXIT = 5,
    SYS_CHAN_CREATE = 6,
    SYS_CHAN_JOIN = 7,
    SYS_CHAN_LEAVE = 8,
    SYS_CHAN_PEEK = 9,
    SYS_GET_SERVICE_CHANNEL = 10,
    SYS_SLEEP = 11,
    SYS_DYNAMIC_BASE = 32
};

#define SYSCALL_MAX_ARGS 4
#define SYSCALL_TABLE_SIZE 64

struct syscall_envelope
{
    uint32_t number;
    uint32_t argc;
    uint32_t args[SYSCALL_MAX_ARGS];
    int32_t result;
    uint32_t status;
};

typedef int32_t (*syscall_handler_t)(struct syscall_envelope *message);

void syscall_init(void);
int syscall_register_handler(uint32_t number, syscall_handler_t handler, const char *name);
int syscall_unregister_handler(uint32_t number);
int syscall_validate_user_buffer(const void *ptr, size_t length);
int syscall_validate_user_pointer(const void *ptr);

#endif
