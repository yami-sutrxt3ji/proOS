#include "arp.h"

#include "ethernet.h"
#include "klog.h"
#include "string.h"
#include "pit.h"
#include "ipv4.h"

struct arp_header
{
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} __attribute__((packed));

#define ARP_HTYPE_ETHERNET 0x0001u
#define ARP_PTYPE_IPV4     0x0800u
#define ARP_OPER_REQUEST   0x0001u
#define ARP_OPER_REPLY     0x0002u

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)((data[0] << 8) | data[1]);
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)((value >> 8) & 0xFFu);
    data[1] = (uint8_t)(value & 0xFFu);
}

static void write_ipv4(uint8_t *dst, const uint8_t *src)
{
    for (size_t i = 0; i < 4; ++i)
        dst[i] = src[i];
}

#define ARP_CACHE_CAPACITY 8u

struct arp_cache_entry
{
    struct net_device *dev;
    uint8_t ipv4[4];
    uint8_t mac[6];
    uint64_t updated;
};

static struct arp_cache_entry g_cache[ARP_CACHE_CAPACITY];

static int ipv4_equal(const uint8_t *a, const uint8_t *b)
{
    for (size_t i = 0; i < 4; ++i)
    {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static void arp_cache_store(struct net_device *dev, const uint8_t ipv4[4], const uint8_t mac[6])
{
    if (!dev || !ipv4 || !mac)
        return;

    size_t free_index = ARP_CACHE_CAPACITY;
    size_t oldest_index = ARP_CACHE_CAPACITY;
    uint64_t oldest_time = (uint64_t)-1;

    for (size_t i = 0; i < ARP_CACHE_CAPACITY; ++i)
    {
        struct arp_cache_entry *entry = &g_cache[i];
        if (entry->dev && entry->dev == dev && ipv4_equal(entry->ipv4, ipv4))
        {
            memcpy(entry->mac, mac, sizeof(entry->mac));
            entry->updated = get_ticks();
            return;
        }

        if (!entry->dev && free_index == ARP_CACHE_CAPACITY)
            free_index = i;

        if (entry->dev && entry->updated < oldest_time)
        {
            oldest_time = entry->updated;
            oldest_index = i;
        }
    }

    size_t target = (free_index != ARP_CACHE_CAPACITY) ? free_index : oldest_index;
    if (target == ARP_CACHE_CAPACITY)
        target = 0;

    struct arp_cache_entry *dest = &g_cache[target];
    dest->dev = dev;
    memcpy(dest->ipv4, ipv4, sizeof(dest->ipv4));
    memcpy(dest->mac, mac, sizeof(dest->mac));
    dest->updated = get_ticks();
}

int arp_cache_lookup(struct net_device *dev, const uint8_t ipv4[4], uint8_t mac_out[6])
{
    if (!dev || !ipv4 || !mac_out)
        return 0;

    for (size_t i = 0; i < ARP_CACHE_CAPACITY; ++i)
    {
        struct arp_cache_entry *entry = &g_cache[i];
        if (!entry->dev)
            continue;
        if (entry->dev == dev && ipv4_equal(entry->ipv4, ipv4))
        {
            memcpy(mac_out, entry->mac, sizeof(entry->mac));
            return 1;
        }
    }

    return 0;
}

int arp_receive(struct net_device *dev, const uint8_t *packet, size_t length, const uint8_t src_mac[6], const uint8_t dst_mac[6])
{
    (void)dst_mac;

    if (!dev || !packet)
        return -1;
    if (length < sizeof(struct arp_header))
    {
        klog_warn("arp: packet too small");
        return -1;
    }

    const struct arp_header *hdr = (const struct arp_header *)packet;
    if (read_be16((const uint8_t *)&hdr->htype) != ARP_HTYPE_ETHERNET ||
        read_be16((const uint8_t *)&hdr->ptype) != ARP_PTYPE_IPV4 ||
        hdr->hlen != 6u || hdr->plen != 4u)
    {
        klog_warn("arp: unsupported format");
        return -1;
    }

    uint16_t oper = read_be16((const uint8_t *)&hdr->oper);

    if (oper == ARP_OPER_REQUEST)
    {
        arp_cache_store(dev, hdr->spa, hdr->sha);

        struct arp_header reply;
        write_be16((uint8_t *)&reply.htype, ARP_HTYPE_ETHERNET);
        write_be16((uint8_t *)&reply.ptype, ARP_PTYPE_IPV4);
        reply.hlen = 6u;
        reply.plen = 4u;
        write_be16((uint8_t *)&reply.oper, ARP_OPER_REPLY);

        memcpy(reply.sha, dev->mac, sizeof(reply.sha));
        write_ipv4(reply.spa, hdr->tpa);
        memcpy(reply.tha, hdr->sha, sizeof(reply.tha));
        write_ipv4(reply.tpa, hdr->spa);

        if (ethernet_send_frame(dev, hdr->sha, ETHERTYPE_ARP, (const uint8_t *)&reply, sizeof(reply)) < 0)
        {
            klog_warn("arp: failed to send reply");
            return -1;
        }

        return 0;
    }

    if (oper == ARP_OPER_REPLY)
    {
        arp_cache_store(dev, hdr->spa, hdr->sha);
        (void)src_mac;
        return 0;
    }

    return -1;
}

int arp_resolve(struct net_device *dev, const uint8_t ipv4[4], uint8_t mac_out[6])
{
    if (!dev || !ipv4 || !mac_out)
        return -1;

    if (arp_cache_lookup(dev, ipv4, mac_out))
        return 0;

    struct arp_header request;
    write_be16((uint8_t *)&request.htype, ARP_HTYPE_ETHERNET);
    write_be16((uint8_t *)&request.ptype, ARP_PTYPE_IPV4);
    request.hlen = 6u;
    request.plen = 4u;
    write_be16((uint8_t *)&request.oper, ARP_OPER_REQUEST);

    memcpy(request.sha, dev->mac, sizeof(request.sha));

    uint8_t local_ip[4];
    ipv4_get_address(local_ip);
    int have_ip = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        if (local_ip[i] != 0)
        {
            have_ip = 1;
            break;
        }
    }

    if (have_ip)
        write_ipv4(request.spa, local_ip);
    else
        write_ipv4(request.spa, (const uint8_t[]){0, 0, 0, 0});

    memset(request.tha, 0, sizeof(request.tha));
    write_ipv4(request.tpa, ipv4);

    uint8_t broadcast[6];
    memset(broadcast, 0xFF, sizeof(broadcast));

    if (ethernet_send_frame(dev, broadcast, ETHERTYPE_ARP, (const uint8_t *)&request, sizeof(request)) < 0)
        return -1;

    return 1;
}
