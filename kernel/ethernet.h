#ifndef ETHERNET_H
#define ETHERNET_H

#include <stddef.h>
#include <stdint.h>

#include "net.h"

#define ETHERTYPE_ARP 0x0806u
#define ETHERTYPE_IPV4 0x0800u
#define ETHERNET_MAX_PAYLOAD 1500u

int ethernet_process_frame(struct net_device *dev, uint8_t *frame, size_t length);
int ethernet_send_frame(struct net_device *dev, const uint8_t dest_mac[6], uint16_t ethertype, const uint8_t *payload, size_t length);

#endif
