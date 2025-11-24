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
    SYS_EXIT = 5
};

void syscall_init(void);

#endif
