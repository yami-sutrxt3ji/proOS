#ifndef IPV4_H
#define IPV4_H

#include <stddef.h>
#include <stdint.h>

#include "net.h"

int ipv4_receive(struct net_device *dev, const uint8_t *packet, size_t length, const uint8_t src_mac[6], const uint8_t dst_mac[6]);
int ipv4_send(struct net_device *dev, const uint8_t dst_ipv4[4], uint8_t protocol, const uint8_t *payload, size_t length);
void ipv4_set_address(const uint8_t addr[4]);
void ipv4_get_address(uint8_t out[4]);

#endif
