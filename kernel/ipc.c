#include "ipc.h"

#include "proc.h"
#include "spinlock.h"
#include "klog.h"

#include <stddef.h>
#include <stdint.h>

#define IPC_CHANNEL_FLAG_KERNEL 0x1u

struct ipc_message_slot
{
    uint32_t header;
    uint32_t type;
    uint32_t size;
    uint32_t flags;
    int32_t sender_pid;
    uint8_t data[CONFIG_MSG_DATA_MAX];
};

struct ipc_channel
{
    int used;
    int id;
    uint32_t flags;
    char name[CONFIG_IPC_CHANNEL_NAME_MAX];
    struct ipc_message_slot queue[CONFIG_IPC_CHANNEL_QUEUE_LEN];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    struct process *waiters[CONFIG_IPC_CHANNEL_WAITERS];
    uint8_t waiter_count;
    struct process *subscribers[CONFIG_IPC_CHANNEL_SUBSCRIBERS];
    uint8_t subscriber_count;
    spinlock_t lock;
};

static struct ipc_channel channel_table[CONFIG_IPC_MAX_CHANNELS];
static int next_channel_id = 1;
static int service_channel_ids[IPC_SERVICE_COUNT];
static int ipc_initialized = 0;

static size_t local_min(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static void channel_name_copy(char *dst, size_t dst_cap, const char *src, size_t src_len)
{
    if (!dst || dst_cap == 0)
        return;

    size_t limit = local_min(dst_cap - 1, src_len);
    size_t i = 0;
    for (; i < limit; ++i)
        dst[i] = src ? src[i] : '\0';
    dst[i] = '\0';
}

static struct ipc_channel *find_channel(int channel_id)
{
    if (channel_id <= 0)
        return NULL;

    for (size_t i = 0; i < CONFIG_IPC_MAX_CHANNELS; ++i)
    {
        if (channel_table[i].used && channel_table[i].id == channel_id)
            return &channel_table[i];
    }
    return NULL;
}

static int process_has_channel(const struct process *proc, int channel_id)
{
    if (!proc)
        return 0;

    for (uint8_t i = 0; i < proc->channel_count; ++i)
    {
        if (proc->channel_slots[i] == channel_id)
            return 1;
    }
    return 0;
}

static int process_add_channel(struct process *proc, int channel_id)
{
    if (!proc)
        return -1;

    if (process_has_channel(proc, channel_id))
        return 0;

    if (proc->channel_count >= CONFIG_PROCESS_CHANNEL_SLOTS)
        return -1;

    proc->channel_slots[proc->channel_count++] = channel_id;
    return 0;
}

static void process_remove_channel(struct process *proc, int channel_id)
{
    if (!proc)
        return;

    for (uint8_t i = 0; i < proc->channel_count; ++i)
    {
        if (proc->channel_slots[i] == channel_id)
        {
            for (uint8_t j = i + 1; j < proc->channel_count; ++j)
                proc->channel_slots[j - 1] = proc->channel_slots[j];
            proc->channel_slots[proc->channel_count - 1] = -1;
            --proc->channel_count;
            break;
        }
    }
}

static void channel_remove_waiter(struct ipc_channel *channel, struct process *proc)
{
    if (!channel || !proc)
        return;

    for (uint8_t i = 0; i < channel->waiter_count; ++i)
    {
        if (channel->waiters[i] == proc)
        {
            for (uint8_t j = i + 1; j < channel->waiter_count; ++j)
                channel->waiters[j - 1] = channel->waiters[j];
            channel->waiters[channel->waiter_count - 1] = NULL;
            --channel->waiter_count;
            break;
        }
    }
}

void ipc_system_init(void)
{
    for (size_t i = 0; i < CONFIG_IPC_MAX_CHANNELS; ++i)
    {
        channel_table[i].used = 0;
        channel_table[i].id = 0;
        channel_table[i].flags = 0;
        channel_table[i].name[0] = '\0';
        channel_table[i].head = 0;
        channel_table[i].tail = 0;
        channel_table[i].count = 0;
        channel_table[i].waiter_count = 0;
        channel_table[i].subscriber_count = 0;
        spinlock_init(&channel_table[i].lock);
        for (size_t j = 0; j < CONFIG_IPC_CHANNEL_QUEUE_LEN; ++j)
        {
            channel_table[i].queue[j].header = 0;
            channel_table[i].queue[j].type = 0;
            channel_table[i].queue[j].size = 0;
            channel_table[i].queue[j].flags = 0;
            channel_table[i].queue[j].sender_pid = -1;
        }
        for (size_t w = 0; w < CONFIG_IPC_CHANNEL_WAITERS; ++w)
            channel_table[i].waiters[w] = NULL;
        for (size_t s = 0; s < CONFIG_IPC_CHANNEL_SUBSCRIBERS; ++s)
            channel_table[i].subscribers[s] = NULL;
    }

    next_channel_id = 1;

    const char *service_names[IPC_SERVICE_COUNT] = {
        "svc.devmgr",
        "svc.module",
        "svc.logger",
        "svc.scheduler"
    };

    for (int svc = 0; svc < IPC_SERVICE_COUNT; ++svc)
    {
        int id = ipc_channel_create(service_names[svc], 0, IPC_CHANNEL_FLAG_KERNEL);
        if (id < 0)
        {
            klog_error("ipc: failed to create service channel");
            service_channel_ids[svc] = -1;
        }
        else
        {
            service_channel_ids[svc] = id;
        }
    }

    ipc_initialized = 1;
}

int ipc_channel_create(const char *name, size_t name_len, uint32_t flags)
{
    for (size_t i = 0; i < CONFIG_IPC_MAX_CHANNELS; ++i)
    {
        if (channel_table[i].used)
            continue;

        channel_table[i].used = 1;
        channel_table[i].id = next_channel_id++;
        channel_table[i].flags = flags;
        channel_table[i].head = 0;
        channel_table[i].tail = 0;
        channel_table[i].count = 0;
        channel_table[i].waiter_count = 0;
        channel_table[i].subscriber_count = 0;

        size_t effective_len = name_len;
        if (name && effective_len == 0)
        {
            while (name[effective_len] && effective_len < (CONFIG_IPC_CHANNEL_NAME_MAX - 1))
                ++effective_len;
        }

        channel_name_copy(channel_table[i].name, CONFIG_IPC_CHANNEL_NAME_MAX, name, effective_len);
        return channel_table[i].id;
    }

    return -1;
}

int ipc_channel_join(struct process *proc, int channel_id)
{
    if (!proc)
        return -1;

    struct ipc_channel *channel = find_channel(channel_id);
    if (!channel)
        return -1;

    if (process_add_channel(proc, channel_id) < 0)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&channel->lock, &flags);

    for (uint8_t i = 0; i < channel->subscriber_count; ++i)
    {
        if (channel->subscribers[i] == proc)
        {
            spinlock_unlock_irqrestore(&channel->lock, flags);
            return 0;
        }
    }

    if (channel->subscriber_count >= CONFIG_IPC_CHANNEL_SUBSCRIBERS)
    {
        spinlock_unlock_irqrestore(&channel->lock, flags);
        process_remove_channel(proc, channel_id);
        return -1;
    }

    channel->subscribers[channel->subscriber_count++] = proc;
    spinlock_unlock_irqrestore(&channel->lock, flags);
    return 0;
}

int ipc_channel_leave(struct process *proc, int channel_id)
{
    if (!proc)
        return -1;

    struct ipc_channel *channel = find_channel(channel_id);
    if (!channel)
        return -1;

    process_remove_channel(proc, channel_id);

    uint32_t flags;
    spinlock_lock_irqsave(&channel->lock, &flags);

    for (uint8_t i = 0; i < channel->subscriber_count; ++i)
    {
        if (channel->subscribers[i] == proc)
        {
            for (uint8_t j = i + 1; j < channel->subscriber_count; ++j)
                channel->subscribers[j - 1] = channel->subscribers[j];
            channel->subscribers[channel->subscriber_count - 1] = NULL;
            --channel->subscriber_count;
            break;
        }
    }

    channel_remove_waiter(channel, proc);

    spinlock_unlock_irqrestore(&channel->lock, flags);
    return 0;
}

int ipc_channel_send(int channel_id, int sender_pid, uint32_t header, uint32_t type, const void *data, size_t size, uint32_t flags)
{
    (void)flags;

    if (size > CONFIG_MSG_DATA_MAX)
        return -1;

    struct ipc_channel *channel = find_channel(channel_id);
    if (!channel)
        return -1;

    struct process *sender_proc = NULL;
    if (sender_pid > 0)
    {
        sender_proc = process_lookup(sender_pid);
        if (!sender_proc)
            return -1;

        if (!process_has_channel(sender_proc, channel_id) && !(channel->flags & IPC_CHANNEL_FLAG_KERNEL))
            return -1;
    }

    uint32_t irq_flags;
    spinlock_lock_irqsave(&channel->lock, &irq_flags);

    if (channel->count >= CONFIG_IPC_CHANNEL_QUEUE_LEN)
    {
        spinlock_unlock_irqrestore(&channel->lock, irq_flags);
        return -1;
    }

    struct ipc_message_slot *slot = &channel->queue[channel->tail];
    slot->header = header;
    slot->type = type;
    slot->size = (uint32_t)size;
    slot->flags = flags;
    slot->sender_pid = sender_pid;
    if (size > 0 && data)
    {
        const uint8_t *src = (const uint8_t *)data;
        for (size_t i = 0; i < size; ++i)
            slot->data[i] = src[i];
    }

    channel->tail = (uint8_t)((channel->tail + 1) % CONFIG_IPC_CHANNEL_QUEUE_LEN);
    ++channel->count;

    struct process *wakeup_proc = NULL;
    if (channel->waiter_count > 0)
    {
        wakeup_proc = channel->waiters[0];
        for (uint8_t i = 1; i < channel->waiter_count; ++i)
            channel->waiters[i - 1] = channel->waiters[i];
        channel->waiters[channel->waiter_count - 1] = NULL;
        --channel->waiter_count;
        if (wakeup_proc)
            wakeup_proc->wait_channel = -1;
    }

    spinlock_unlock_irqrestore(&channel->lock, irq_flags);

    if (wakeup_proc)
        process_wake(wakeup_proc);

    return (int)size;
}

int ipc_channel_receive(struct process *proc, int channel_id, struct ipc_message *out, void *buffer, size_t buffer_len, uint32_t flags)
{
    if (!proc)
        return -1;

    struct ipc_channel *channel = find_channel(channel_id);
    if (!channel)
        return -1;

    if (!process_has_channel(proc, channel_id) && !(channel->flags & IPC_CHANNEL_FLAG_KERNEL))
        return -1;

    for (;;)
    {
        uint32_t irq_flags;
        spinlock_lock_irqsave(&channel->lock, &irq_flags);

        if (channel->count > 0)
        {
            struct ipc_message_slot *slot = &channel->queue[channel->head];
            channel->head = (uint8_t)((channel->head + 1) % CONFIG_IPC_CHANNEL_QUEUE_LEN);
            --channel->count;

            if (out)
            {
                out->header = slot->header;
                out->type = slot->type;
                out->sender_pid = slot->sender_pid;
                out->size = slot->size;
                out->data = buffer;
            }

            size_t copy_len = local_min(slot->size, buffer_len);
            if (slot->size > buffer_len && out)
                out->header |= IPC_MESSAGE_TRUNCATED;

            if (buffer && copy_len > 0)
            {
                uint8_t *dst = (uint8_t *)buffer;
                for (size_t i = 0; i < copy_len; ++i)
                    dst[i] = slot->data[i];
            }

            spinlock_unlock_irqrestore(&channel->lock, irq_flags);
            proc->wait_channel = -1;
            return 1;
        }

        if (flags & IPC_RECV_NONBLOCK)
        {
            spinlock_unlock_irqrestore(&channel->lock, irq_flags);
            return 0;
        }

        int already_waiting = 0;
        for (uint8_t i = 0; i < channel->waiter_count; ++i)
        {
            if (channel->waiters[i] == proc)
            {
                already_waiting = 1;
                break;
            }
        }

        if (!already_waiting)
        {
            if (channel->waiter_count >= CONFIG_IPC_CHANNEL_WAITERS)
            {
                spinlock_unlock_irqrestore(&channel->lock, irq_flags);
                return -1;
            }
            channel->waiters[channel->waiter_count++] = proc;
            proc->wait_channel = channel_id;
        }

        spinlock_unlock_irqrestore(&channel->lock, irq_flags);
        process_block_current();
    }
}

int ipc_channel_peek(int channel_id)
{
    struct ipc_channel *channel = find_channel(channel_id);
    if (!channel)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&channel->lock, &flags);
    int has_message = (channel->count > 0) ? 1 : 0;
    spinlock_unlock_irqrestore(&channel->lock, flags);
    return has_message;
}

int ipc_get_service_channel(enum ipc_service_channel service)
{
    if (service < 0 || service >= IPC_SERVICE_COUNT)
        return -1;
    return service_channel_ids[service];
}

int ipc_is_initialized(void)
{
    return ipc_initialized;
}

void ipc_process_cleanup(struct process *proc)
{
    if (!proc)
        return;

    for (uint8_t slot = 0; slot < proc->channel_count; ++slot)
    {
        int channel_id = proc->channel_slots[slot];
        struct ipc_channel *channel = find_channel(channel_id);
        if (!channel)
            continue;

        uint32_t flags;
        spinlock_lock_irqsave(&channel->lock, &flags);

        for (uint8_t i = 0; i < channel->subscriber_count; ++i)
        {
            if (channel->subscribers[i] == proc)
            {
                for (uint8_t j = i + 1; j < channel->subscriber_count; ++j)
                    channel->subscribers[j - 1] = channel->subscribers[j];
                channel->subscribers[channel->subscriber_count - 1] = NULL;
                --channel->subscriber_count;
                break;
            }
        }

        channel_remove_waiter(channel, proc);

        spinlock_unlock_irqrestore(&channel->lock, flags);
    }

    for (uint8_t i = 0; i < CONFIG_PROCESS_CHANNEL_SLOTS; ++i)
        proc->channel_slots[i] = -1;
    proc->channel_count = 0;
    proc->wait_channel = -1;
}
