#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>

struct net_device;

struct net_device_ops
{
    int (*transmit)(struct net_device *dev, const uint8_t *data, size_t length);
    int (*poll)(struct net_device *dev);
};

struct net_device
{
    char name[16];
    uint8_t mac[6];
    void *driver_data;
    const struct net_device_ops *ops;
};

int net_init(void);
int net_register_device(struct net_device *dev);
int net_receive_frame(struct net_device *dev, uint8_t *frame, size_t length);
int net_poll_devices(void);
size_t net_device_count(void);
struct net_device *net_get_device(size_t index);

#endif
