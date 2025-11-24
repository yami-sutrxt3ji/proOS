#include "proc.h"

#include <stddef.h>

static size_t ipc_min(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

static void ipc_copy(char *dst, const char *src, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        dst[i] = src[i];
}

int ipc_send(int target_pid, int from_pid, const char *data, size_t len)
{
    if (!data || len == 0 || len >= MSG_DATA_MAX)
        return -1;

    struct process *proc = process_lookup(target_pid);
    if (!proc || proc->state == PROC_UNUSED)
        return -1;

    struct message_queue *queue = &proc->queue;
    if (queue->count >= MSG_QUEUE_LEN)
        return -1;

    size_t copy_len = ipc_min(len, MSG_DATA_MAX - 1);
    struct message *msg = &queue->items[queue->tail];
    msg->from_pid = from_pid;
    msg->length = copy_len;
    ipc_copy(msg->data, data, copy_len);
    msg->data[copy_len] = '\0';

    queue->tail = (uint8_t)((queue->tail + 1) % MSG_QUEUE_LEN);
    ++queue->count;

    if (proc->state == PROC_WAITING)
        process_wake(proc);

    return (int)copy_len;
}

int ipc_receive(struct process *proc, struct message *out)
{
    if (!proc || !out)
        return 0;

    struct message_queue *queue = &proc->queue;
    if (queue->count == 0)
        return 0;

    struct message *msg = &queue->items[queue->head];
    out->from_pid = msg->from_pid;
    out->length = msg->length;
    ipc_copy(out->data, msg->data, msg->length);
    out->data[msg->length] = '\0';

    queue->head = (uint8_t)((queue->head + 1) % MSG_QUEUE_LEN);
    --queue->count;
    return 1;
}

int ipc_has_message(struct process *proc)
{
    if (!proc)
        return 0;
    return (proc->queue.count > 0) ? 1 : 0;
}
