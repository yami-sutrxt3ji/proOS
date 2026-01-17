#include "net_socket.h"

#include "net.h"
#include "spinlock.h"
#include "string.h"

#define NET_SOCKET_CAPACITY 4
#define NET_SOCKET_QUEUE 8
#define NET_SOCKET_FRAME_MAX 1600

struct net_raw_packet
{
    size_t length;
    uint8_t data[NET_SOCKET_FRAME_MAX];
};

struct net_raw_socket
{
    int used;
    struct net_device *device;
    size_t head;
    size_t tail;
    size_t count;
    struct net_raw_packet packets[NET_SOCKET_QUEUE];
    spinlock_t lock;
};

static struct net_raw_socket g_sockets[NET_SOCKET_CAPACITY];

static struct net_raw_socket *resolve_socket(int handle)
{
    if (handle <= 0)
        return NULL;
    size_t index = (size_t)(handle - 1);
    if (index >= NET_SOCKET_CAPACITY)
        return NULL;
    struct net_raw_socket *sock = &g_sockets[index];
    if (!sock->used)
        return NULL;
    return sock;
}

void net_socket_system_init(void)
{
    for (size_t i = 0; i < NET_SOCKET_CAPACITY; ++i)
    {
        g_sockets[i].used = 0;
        g_sockets[i].device = NULL;
        g_sockets[i].head = 0;
        g_sockets[i].tail = 0;
        g_sockets[i].count = 0;
        spinlock_init(&g_sockets[i].lock);
    }
}

int net_open(void)
{
    struct net_device *dev = net_get_device(0);
    if (!dev)
        return -1;

    for (size_t i = 0; i < NET_SOCKET_CAPACITY; ++i)
    {
        struct net_raw_socket *sock = &g_sockets[i];
        if (sock->used)
            continue;

        sock->used = 1;
        sock->device = dev;
        sock->head = 0;
        sock->tail = 0;
        sock->count = 0;
        return (int)(i + 1);
    }

    return -1;
}

int net_send(int sock_handle, const void *data, size_t size)
{
    struct net_raw_socket *sock = resolve_socket(sock_handle);
    if (!sock || !data || size == 0)
        return -1;
    if (size > NET_SOCKET_FRAME_MAX)
        return -1;
    if (!sock->device || !sock->device->ops || !sock->device->ops->transmit)
        return -1;

    return sock->device->ops->transmit(sock->device, (const uint8_t *)data, size);
}

int net_recv(int sock_handle, void *buf, size_t max)
{
    struct net_raw_socket *sock = resolve_socket(sock_handle);
    if (!sock || !buf || max == 0)
        return -1;

    uint32_t flags = 0;
    spinlock_lock_irqsave(&sock->lock, &flags);
    if (sock->count == 0)
    {
        spinlock_unlock_irqrestore(&sock->lock, flags);
        return 0;
    }

    struct net_raw_packet *packet = &sock->packets[sock->head];
    if (packet->length > max)
    {
        sock->head = (sock->head + 1u) % NET_SOCKET_QUEUE;
        --sock->count;
        spinlock_unlock_irqrestore(&sock->lock, flags);
        return -1;
    }

    memcpy(buf, packet->data, packet->length);
    size_t copied = packet->length;
    sock->head = (sock->head + 1u) % NET_SOCKET_QUEUE;
    --sock->count;
    spinlock_unlock_irqrestore(&sock->lock, flags);

    return (int)copied;
}

int net_close(int sock_handle)
{
    struct net_raw_socket *sock = resolve_socket(sock_handle);
    if (!sock)
        return -1;

    uint32_t flags = 0;
    spinlock_lock_irqsave(&sock->lock, &flags);
    sock->used = 0;
    sock->device = NULL;
    sock->head = 0;
    sock->tail = 0;
    sock->count = 0;
    spinlock_unlock_irqrestore(&sock->lock, flags);
    return 0;
}

void net_socket_notify_frame(struct net_device *dev, const uint8_t *frame, size_t length)
{
    if (!dev || !frame || length == 0)
        return;

    if (length > NET_SOCKET_FRAME_MAX)
        length = NET_SOCKET_FRAME_MAX;

    for (size_t i = 0; i < NET_SOCKET_CAPACITY; ++i)
    {
        struct net_raw_socket *sock = &g_sockets[i];
        if (!sock->used || sock->device != dev)
            continue;

        uint32_t flags = 0;
        spinlock_lock_irqsave(&sock->lock, &flags);

        if (sock->count == NET_SOCKET_QUEUE)
        {
            sock->head = (sock->head + 1u) % NET_SOCKET_QUEUE;
            --sock->count;
        }

        struct net_raw_packet *packet = &sock->packets[sock->tail];
        packet->length = length;
        memcpy(packet->data, frame, length);
        sock->tail = (sock->tail + 1u) % NET_SOCKET_QUEUE;
        ++sock->count;
        spinlock_unlock_irqrestore(&sock->lock, flags);
    }
}
