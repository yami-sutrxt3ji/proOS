#include "net.h"

#include "ethernet.h"
#include "klog.h"
#include "string.h"
#include "net_socket.h"

#define MAX_NET_DEVICES 4

static struct net_device *g_devices[MAX_NET_DEVICES];
static size_t g_device_count;

int net_init(void)
{
    g_device_count = 0;
    for (size_t i = 0; i < MAX_NET_DEVICES; ++i)
        g_devices[i] = NULL;
    net_socket_system_init();
    klog_info("net: stack initialized");
    return 0;
}

int net_register_device(struct net_device *dev)
{
    if (!dev)
        return -1;
    if (!dev->ops || !dev->ops->transmit)
    {
        klog_warn("net: device missing ops");
        return -1;
    }
    if (g_device_count >= MAX_NET_DEVICES)
    {
        klog_warn("net: device limit reached");
        return -1;
    }

    g_devices[g_device_count++] = dev;
    klog_info("net: device registered");
    return 0;
}

int net_receive_frame(struct net_device *dev, uint8_t *frame, size_t length)
{
    if (!dev || !frame || length == 0)
        return -1;
    net_socket_notify_frame(dev, frame, length);
    return ethernet_process_frame(dev, frame, length);
}

int net_poll_devices(void)
{
    int polled = 0;
    for (size_t i = 0; i < g_device_count; ++i)
    {
        struct net_device *dev = g_devices[i];
        if (!dev || !dev->ops || !dev->ops->poll)
            continue;
        int rc = dev->ops->poll(dev);
        if (rc > 0)
            polled += rc;
    }
    return polled;
}

size_t net_device_count(void)
{
    return g_device_count;
}

struct net_device *net_get_device(size_t index)
{
    if (index >= g_device_count)
        return NULL;
    return g_devices[index];
}
