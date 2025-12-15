#include "syscall.h"
#include "interrupts.h"
#include "proc.h"
#include "vga.h"
#include "spinlock.h"

#include "config.h"

#include <stddef.h>
#include <stdint.h>

struct syscall_entry
{
    syscall_handler_t handler;
    const char *name;
};

static struct syscall_entry syscall_table[SYSCALL_TABLE_SIZE];
static spinlock_t syscall_lock;

static void copy_to_user(char *dst, const char *src, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        dst[i] = src[i];
}

static void copy_from_user(char *dst, const char *src, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        dst[i] = src[i];
}

int syscall_validate_user_pointer(const void *ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    if (addr == 0)
        return 0;
    if (addr >= CONFIG_USER_SPACE_LIMIT)
        return 0;
    return 1;
}

int syscall_validate_user_buffer(const void *ptr, size_t length)
{
    if (length == 0)
        return 1;
    if (!syscall_validate_user_pointer(ptr))
        return 0;

    uintptr_t addr = (uintptr_t)ptr;
    if (addr + length < addr)
        return 0;
    if ((addr + length) >= CONFIG_USER_SPACE_LIMIT)
        return 0;
    return 1;
}

static int32_t sys_write_handler(struct syscall_envelope *msg)
{
    if (msg->argc < 2)
        return -1;

    const char *buf = (const char *)msg->args[0];
    size_t len = (size_t)msg->args[1];
    if (len == 0)
        return 0;
    if (!syscall_validate_user_buffer(buf, len))
        return -1;

    for (size_t i = 0; i < len; ++i)
    {
        char ch;
        copy_from_user(&ch, buf + i, 1);
        vga_write_char(ch);
    }

    return (int32_t)len;
}

static int32_t sys_yield_handler(struct syscall_envelope *msg)
{
    (void)msg;
    process_yield();
    return 0;
}

static int32_t sys_spawn_handler(struct syscall_envelope *msg)
{
    if (msg->argc < 2)
        return -1;

    void (*entry)(void) = (void (*)(void))msg->args[0];
    size_t stack_size = (size_t)msg->args[1];
    if (!syscall_validate_user_pointer((const void *)entry))
        return -1;
    return process_create(entry, stack_size);
}

static int32_t sys_send_handler(struct syscall_envelope *msg)
{
    if (msg->argc < 3)
        return -1;

    int pid = (int)msg->args[0];
    const char *buffer = (const char *)msg->args[1];
    size_t len = (size_t)msg->args[2];
    if (len == 0)
        return 0;
    if (!syscall_validate_user_buffer(buffer, len))
        return -1;

    struct process *sender = process_current();
    if (!sender)
        return -1;
    return ipc_send(pid, sender->pid, buffer, len);
}

static int32_t sys_recv_handler(struct syscall_envelope *msg)
{
    if (msg->argc < 3)
        return -1;

    char *buffer = (char *)msg->args[0];
    size_t maxlen = (size_t)msg->args[1];
    int *from_pid = (int *)msg->args[2];

    if (maxlen == 0)
        return -1;
    if (!syscall_validate_user_buffer(buffer, maxlen))
        return -1;
    if (from_pid && !syscall_validate_user_buffer(from_pid, sizeof(int)))
        return -1;

    struct process *proc = process_current();
    if (!proc)
        return -1;

    for (;;)
    {
        struct message msg_buf;
        if (ipc_receive(proc, &msg_buf))
        {
            size_t copy_len = (msg_buf.length < maxlen) ? msg_buf.length : (maxlen - 1);
            for (size_t i = 0; i < copy_len; ++i)
                copy_to_user(buffer + i, &msg_buf.data[i], 1);
            copy_to_user(buffer + copy_len, "\0", 1);
            if (from_pid)
                copy_to_user((char *)from_pid, (const char *)&msg_buf.from_pid, sizeof(int));
            return (int32_t)copy_len;
        }

        process_block_current();
    }
}

static int32_t sys_exit_handler(struct syscall_envelope *msg)
{
    if (msg->argc < 1)
        return -1;
    int code = (int)msg->args[0];
    process_exit(code);
    return 0;
}

static int32_t syscall_invoke(struct syscall_envelope *msg)
{
    if (msg->number >= SYSCALL_TABLE_SIZE)
        return -1;

    syscall_handler_t handler = syscall_table[msg->number].handler;
    if (!handler)
        return -1;

    return handler(msg);
}

int syscall_register_handler(uint32_t number, syscall_handler_t handler, const char *name)
{
    if (number >= SYSCALL_TABLE_SIZE || !handler)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&syscall_lock, &flags);
    struct syscall_entry *entry = &syscall_table[number];
    if (entry->handler && entry->handler != handler)
    {
        spinlock_unlock_irqrestore(&syscall_lock, flags);
        return -1;
    }
    entry->handler = handler;
    entry->name = name;
    spinlock_unlock_irqrestore(&syscall_lock, flags);
    return 0;
}

int syscall_unregister_handler(uint32_t number)
{
    if (number >= SYSCALL_TABLE_SIZE)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&syscall_lock, &flags);
    syscall_table[number].handler = NULL;
    syscall_table[number].name = NULL;
    spinlock_unlock_irqrestore(&syscall_lock, flags);
    return 0;
}

void syscall_init(void)
{
    spinlock_init(&syscall_lock);
    for (size_t i = 0; i < SYSCALL_TABLE_SIZE; ++i)
    {
        syscall_table[i].handler = NULL;
        syscall_table[i].name = NULL;
    }

    syscall_register_handler(SYS_WRITE, sys_write_handler, "sys_write");
    syscall_register_handler(SYS_YIELD, sys_yield_handler, "sys_yield");
    syscall_register_handler(SYS_SPAWN, sys_spawn_handler, "sys_spawn");
    syscall_register_handler(SYS_SEND, sys_send_handler, "sys_send");
    syscall_register_handler(SYS_RECV, sys_recv_handler, "sys_recv");
    syscall_register_handler(SYS_EXIT, sys_exit_handler, "sys_exit");
}

void syscall_handler(struct regs *frame)
{
    struct syscall_envelope *message = (struct syscall_envelope *)frame->eax;
    if (!syscall_validate_user_buffer(message, sizeof(struct syscall_envelope)))
    {
        frame->eax = (uint32_t)-1;
        return;
    }

    if (message->argc > SYSCALL_MAX_ARGS)
    {
        message->status = 1;
        message->result = -1;
        frame->eax = (uint32_t)-1;
        return;
    }

    int32_t result = syscall_invoke(message);
    message->result = result;
    message->status = (result < 0) ? 1u : 0u;
    frame->eax = (uint32_t)result;
}
