#ifndef USER_SYSLIB_H
#define USER_SYSLIB_H

#include "../syscall.h"
#include "../ipc_types.h"
#include <stddef.h>
#include <stdint.h>

static inline int32_t sys_call(uint32_t number, uint32_t argc, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    struct syscall_envelope request;
    request.number = number;
    request.argc = argc;
    request.args[0] = arg0;
    request.args[1] = arg1;
    request.args[2] = arg2;
    request.args[3] = arg3;
    request.result = 0;
    request.status = 0;

    int32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(&request) : "memory");
    return request.result;
}

static inline int sys_write(const char *buf, size_t len)
{
    return (int)sys_call(SYS_WRITE, 2, (uint32_t)(uintptr_t)buf, (uint32_t)len, 0, 0);
}

static inline int sys_yield(void)
{
    return (int)sys_call(SYS_YIELD, 0, 0, 0, 0, 0);
}

static inline int sys_spawn(void (*entry)(void), size_t stack_size)
{
    return (int)sys_call(SYS_SPAWN, 2, (uint32_t)(uintptr_t)entry, (uint32_t)stack_size, 0, 0);
}

static inline int sys_chan_create(const char *name, size_t name_len, uint32_t flags)
{
    return (int)sys_call(SYS_CHAN_CREATE, 3, (uint32_t)(uintptr_t)name, (uint32_t)name_len, flags, 0);
}

static inline int sys_chan_join(int channel_id)
{
    return (int)sys_call(SYS_CHAN_JOIN, 1, (uint32_t)channel_id, 0, 0, 0);
}

static inline int sys_chan_leave(int channel_id)
{
    return (int)sys_call(SYS_CHAN_LEAVE, 1, (uint32_t)channel_id, 0, 0, 0);
}

static inline int sys_chan_peek(int channel_id)
{
    return (int)sys_call(SYS_CHAN_PEEK, 1, (uint32_t)channel_id, 0, 0, 0);
}

static inline int sys_get_service_channel(enum ipc_service_channel service)
{
    return (int)sys_call(SYS_GET_SERVICE_CHANNEL, 1, (uint32_t)service, 0, 0, 0);
}

static inline int sys_chan_send(int channel_id, struct ipc_message *message, uint32_t flags)
{
    return (int)sys_call(SYS_SEND, 3, (uint32_t)channel_id, (uint32_t)(uintptr_t)message, flags, 0);
}

static inline int sys_chan_recv(int channel_id, struct ipc_message *message, uint32_t flags)
{
    return (int)sys_call(SYS_RECV, 3, (uint32_t)channel_id, (uint32_t)(uintptr_t)message, flags, 0);
}

static inline void sys_exit(int code)
{
    (void)sys_call(SYS_EXIT, 1, (uint32_t)code, 0, 0, 0);
}

#endif
