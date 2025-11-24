#ifndef USER_SYSLIB_H
#define USER_SYSLIB_H

#include "../syscall.h"
#include <stddef.h>
#include <stdint.h>

static inline int sys_write(const char *buf, size_t len)
{
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_WRITE), "b"(buf), "c"(len) : "memory");
    return ret;
}

static inline int sys_yield(void)
{
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_YIELD) : "memory");
    return ret;
}

static inline int sys_spawn(void (*entry)(void), size_t stack_size)
{
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_SPAWN), "b"(entry), "c"(stack_size) : "memory");
    return ret;
}

static inline int sys_send(int pid, const char *buf, size_t len)
{
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_SEND), "b"(pid), "c"(buf), "d"(len) : "memory");
    return ret;
}

static inline int sys_recv(char *buf, size_t max_len, int *from_pid)
{
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(SYS_RECV), "b"(buf), "c"(max_len), "d"(from_pid) : "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ __volatile__("int $0x80" : : "a"(SYS_EXIT), "b"(code) : "memory");
}

#endif
