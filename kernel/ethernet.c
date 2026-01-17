#include "ethernet.h"

#include "arp.h"
#include "ipv4.h"
#include "klog.h"
#include "string.h"

struct ethernet_header
{
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} __attribute__((packed));

#define ETHERNET_MAX_PAYLOAD 1500u

static uint8_t g_tx_buffer[sizeof(struct ethernet_header) + ETHERNET_MAX_PAYLOAD];

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)((data[0] << 8) | data[1]);
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)((value >> 8) & 0xFFu);
    data[1] = (uint8_t)(value & 0xFFu);
}

static void log_drop(const char *reason)
{
    if (reason)
        klog_warn(reason);
}

int ethernet_process_frame(struct net_device *dev, uint8_t *frame, size_t length)
{
    if (!dev || !frame)
        return -1;
    if (length < sizeof(struct ethernet_header))
    {
        log_drop("ethernet: frame too short");
        return -1;
    }

    struct ethernet_header *hdr = (struct ethernet_header *)frame;
    uint16_t ethertype = read_be16((const uint8_t *)&hdr->ethertype);
    uint8_t *payload = frame + sizeof(struct ethernet_header);
    size_t payload_len = length - sizeof(struct ethernet_header);

    switch (ethertype)
    {
        case ETHERTYPE_ARP:
            return arp_receive(dev, payload, payload_len, hdr->src, hdr->dst);
        case ETHERTYPE_IPV4:
            return ipv4_receive(dev, payload, payload_len, hdr->src, hdr->dst);
        default:
            log_drop("ethernet: unsupported ethertype");
            return -1;
    }
}

int ethernet_send_frame(struct net_device *dev, const uint8_t dest_mac[6], uint16_t ethertype, const uint8_t *payload, size_t length)
{
    if (!dev || !dest_mac || !payload || length == 0)
        return -1;
    if (!dev->ops || !dev->ops->transmit)
        return -1;
    if (length > ETHERNET_MAX_PAYLOAD)
    {
        klog_warn("ethernet: payload too large");
        return -1;
    }
    struct ethernet_header *hdr = (struct ethernet_header *)g_tx_buffer;
    memcpy(hdr->dst, dest_mac, sizeof(hdr->dst));
    memcpy(hdr->src, dev->mac, sizeof(hdr->src));
    write_be16((uint8_t *)&hdr->ethertype, ethertype);
    memcpy(g_tx_buffer + sizeof(struct ethernet_header), payload, length);

    return dev->ops->transmit(dev, g_tx_buffer, sizeof(struct ethernet_header) + length);
}
