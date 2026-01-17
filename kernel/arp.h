#ifndef ARP_H
#define ARP_H

#include <stddef.h>
#include <stdint.h>

#include "net.h"

int arp_receive(struct net_device *dev, const uint8_t *packet, size_t length, const uint8_t src_mac[6], const uint8_t dst_mac[6]);
int arp_resolve(struct net_device *dev, const uint8_t ipv4[4], uint8_t mac_out[6]);
int arp_cache_lookup(struct net_device *dev, const uint8_t ipv4[4], uint8_t mac_out[6]);

#endif
