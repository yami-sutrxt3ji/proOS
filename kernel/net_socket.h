#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include <stddef.h>
#include <stdint.h>

struct net_device;

int net_open(void);
int net_send(int sock, const void *data, size_t size);
int net_recv(int sock, void *buf, size_t max);
int net_close(int sock);

void net_socket_system_init(void);
void net_socket_notify_frame(struct net_device *dev, const uint8_t *frame, size_t length);

#endif
