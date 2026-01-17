#ifndef ICMP_H
#define ICMP_H

#include <stddef.h>
#include <stdint.h>

#include "net.h"

int icmp_receive(struct net_device *dev, const uint8_t *packet, size_t length, const uint8_t src_ipv4[4], const uint8_t dst_ipv4[4]);
int icmp_send_echo_request(struct net_device *dev, const uint8_t dst_ipv4[4], uint16_t identifier, uint16_t sequence);
void icmp_clear_echo_replies(void);
int icmp_take_echo_reply(uint16_t identifier, uint16_t sequence, uint8_t src_ipv4[4]);
int icmp_take_any_echo_reply(uint16_t *identifier, uint16_t *sequence, uint8_t src_ipv4[4]);

#endif
