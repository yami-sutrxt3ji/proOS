#include "ipv4.h"

#include "arp.h"
#include "ethernet.h"
#include "icmp.h"
#include "klog.h"
#include "string.h"

struct ipv4_header
{
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t src[4];
    uint8_t dst[4];
} __attribute__((packed));

static uint8_t g_ipv4_address[4];
static uint16_t g_ipv4_ident;
static uint8_t g_ipv4_tx_buffer[sizeof(struct ipv4_header) + ETHERNET_MAX_PAYLOAD];

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)((data[0] << 8) | data[1]);
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)((value >> 8) & 0xFFu);
    data[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t ipv4_checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    while (length > 1)
    {
        sum += (uint32_t)((data[0] << 8) | data[1]);
        data += 2;
        length -= 2;
    }

    if (length)
        sum += (uint32_t)(data[0] << 8);

    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);

    return (uint16_t)~sum;
}

int ipv4_receive(struct net_device *dev, const uint8_t *packet, size_t length, const uint8_t src_mac[6], const uint8_t dst_mac[6])
{
    (void)src_mac;
    (void)dst_mac;

    if (!dev || !packet)
        return -1;
    if (length < sizeof(struct ipv4_header))
    {
        klog_warn("ipv4: packet too small");
        return -1;
    }

    const struct ipv4_header *hdr = (const struct ipv4_header *)packet;
    uint8_t version = hdr->version_ihl >> 4;
    uint8_t ihl = hdr->version_ihl & 0x0Fu;
    if (version != 4 || ihl < 5)
    {
        klog_warn("ipv4: invalid header");
        return -1;
    }

    size_t header_len = (size_t)ihl * 4u;
    if (length < header_len)
    {
        klog_warn("ipv4: truncated header");
        return -1;
    }

    uint16_t total_length = read_be16((const uint8_t *)&hdr->total_length);
    if (total_length > length)
    {
        klog_warn("ipv4: total length mismatch");
        return -1;
    }

    const uint8_t *payload = packet + header_len;
    size_t payload_len = total_length - header_len;

    switch (hdr->protocol)
    {
        case 1u:
            return icmp_receive(dev, payload, payload_len, hdr->src, hdr->dst);
        default:
            klog_warn("ipv4: unsupported protocol");
            return -1;
    }
}

int ipv4_send(struct net_device *dev, const uint8_t dst_ipv4[4], uint8_t protocol, const uint8_t *payload, size_t length)
{
    if (!dev || !dst_ipv4)
        return -1;
    if (length > 0 && !payload)
        return -1;

    if (sizeof(struct ipv4_header) + length > ETHERNET_MAX_PAYLOAD)
    {
        klog_warn("ipv4: payload too large");
        return -1;
    }

    uint8_t mac[6];
    int resolve = arp_resolve(dev, dst_ipv4, mac);
    if (resolve < 0)
        return -1;
    if (resolve > 0)
        return 1;

    struct ipv4_header *hdr = (struct ipv4_header *)g_ipv4_tx_buffer;
    hdr->version_ihl = 0x45u;
    hdr->dscp_ecn = 0;
    size_t total_length = sizeof(struct ipv4_header) + length;
    write_be16((uint8_t *)&hdr->total_length, (uint16_t)total_length);
    write_be16((uint8_t *)&hdr->identification, ++g_ipv4_ident);
    write_be16((uint8_t *)&hdr->flags_fragment, 0);
    hdr->ttl = 64u;
    hdr->protocol = protocol;
    hdr->checksum = 0;
    memcpy(hdr->src, g_ipv4_address, sizeof(hdr->src));
    memcpy(hdr->dst, dst_ipv4, sizeof(hdr->dst));

    if (length > 0)
        memcpy(g_ipv4_tx_buffer + sizeof(struct ipv4_header), payload, length);

    hdr->checksum = ipv4_checksum((const uint8_t *)hdr, sizeof(struct ipv4_header));

    return ethernet_send_frame(dev, mac, ETHERTYPE_IPV4, g_ipv4_tx_buffer, total_length);
}

void ipv4_set_address(const uint8_t addr[4])
{
    if (!addr)
        return;
    for (size_t i = 0; i < 4; ++i)
        g_ipv4_address[i] = addr[i];
}

void ipv4_get_address(uint8_t out[4])
{
    if (!out)
        return;
    for (size_t i = 0; i < 4; ++i)
        out[i] = g_ipv4_address[i];
}
