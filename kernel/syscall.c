#include "syscall.h"
#include "interrupts.h"
#include "proc.h"
#include "vga.h"

#include <stddef.h>
#include <stdint.h>

static void copy_to_user(char *dst, const char *src, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        dst[i] = src[i];
}

static int sys_write_impl(const char *buf, size_t len)
{
    if (!buf || len == 0)
        return 0;

    for (size_t i = 0; i < len; ++i)
        vga_write_char(buf[i]);

    return (int)len;
}

static int sys_yield_impl(void)
{
    process_yield();
    return 0;
}

static int sys_spawn_impl(void (*entry)(void), size_t stack_size)
{
    return process_create(entry, stack_size);
}

static int sys_send_impl(int pid, const char *data, size_t len)
{
    struct process *sender = process_current();
    if (!sender)
        return -1;

    return ipc_send(pid, sender->pid, data, len);
}

static int sys_recv_impl(char *buffer, size_t maxlen, int *from_pid)
{
    struct process *proc = process_current();
    if (!proc || !buffer || maxlen == 0)
        return -1;

    for (;;)
    {
        struct message msg;
        if (ipc_receive(proc, &msg))
        {
            size_t copy_len = (msg.length < maxlen) ? msg.length : (maxlen - 1);
            copy_to_user(buffer, msg.data, copy_len);
            buffer[copy_len] = '\0';
            if (from_pid)
                *from_pid = msg.from_pid;
            return (int)copy_len;
        }

        process_block_current();
    }
}

static int sys_exit_impl(int code)
{
    process_exit(code);
    return 0;
}

void syscall_init(void)
{
}

void syscall_handler(struct regs *frame)
{
    int32_t result = -1;

    switch (frame->eax)
    {
    case SYS_WRITE:
        result = sys_write_impl((const char *)frame->ebx, (size_t)frame->ecx);
        break;
    case SYS_YIELD:
        result = sys_yield_impl();
        break;
    case SYS_SPAWN:
        result = sys_spawn_impl((void (*)(void))frame->ebx, (size_t)frame->ecx);
        break;
    case SYS_SEND:
        result = sys_send_impl((int)frame->ebx, (const char *)frame->ecx, (size_t)frame->edx);
        break;
    case SYS_RECV:
        result = sys_recv_impl((char *)frame->ebx, (size_t)frame->ecx, (int *)frame->edx);
        break;
    case SYS_EXIT:
        result = sys_exit_impl((int)frame->ebx);
        break;
    default:
        result = -1;
        break;
    }

    frame->eax = (uint32_t)result;
}
