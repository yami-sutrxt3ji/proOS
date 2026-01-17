#include "icmp.h"

#include "ipv4.h"
#include "klog.h"
#include "string.h"
#include "spinlock.h"

struct icmp_echo_header
{
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed));

struct icmp_echo_reply
{
    uint16_t identifier;
    uint16_t sequence;
    uint8_t src_ipv4[4];
};

#define ICMP_REPLY_CAPACITY 8u

static struct icmp_echo_reply g_replies[ICMP_REPLY_CAPACITY];
static size_t g_reply_count;
static spinlock_t g_reply_lock;

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)((data[0] << 8) | data[1]);
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)((value >> 8) & 0xFFu);
    data[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t icmp_checksum(const uint8_t *data, size_t length)
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

static void record_reply(uint16_t identifier, uint16_t sequence, const uint8_t src_ipv4[4])
{
    uint32_t flags = 0;
    spinlock_lock_irqsave(&g_reply_lock, &flags);

    if (g_reply_count >= ICMP_REPLY_CAPACITY)
    {
        for (size_t i = 1; i < g_reply_count; ++i)
            g_replies[i - 1] = g_replies[i];
        --g_reply_count;
    }

    struct icmp_echo_reply *slot = &g_replies[g_reply_count++];
    slot->identifier = identifier;
    slot->sequence = sequence;
    if (src_ipv4)
        memcpy(slot->src_ipv4, src_ipv4, sizeof(slot->src_ipv4));
    else
        memset(slot->src_ipv4, 0, sizeof(slot->src_ipv4));

    spinlock_unlock_irqrestore(&g_reply_lock, flags);
}

int icmp_receive(struct net_device *dev, const uint8_t *packet, size_t length, const uint8_t src_ipv4[4], const uint8_t dst_ipv4[4])
{
    if (!dev || !packet)
        return -1;
    if (length < sizeof(struct icmp_echo_header))
    {
        klog_warn("icmp: packet too small");
        return -1;
    }

    const struct icmp_echo_header *hdr = (const struct icmp_echo_header *)packet;
    uint16_t identifier = read_be16((const uint8_t *)&hdr->identifier);
    uint16_t sequence = read_be16((const uint8_t *)&hdr->sequence);

    if (hdr->type == 0u && hdr->code == 0u)
    {
        record_reply(identifier, sequence, src_ipv4);
        return 0;
    }

    if (hdr->type == 8u && hdr->code == 0u)
    {
        size_t payload_len = length - sizeof(struct icmp_echo_header);
        uint8_t buffer[sizeof(struct icmp_echo_header) + 1500];
        if (payload_len > 1500)
            payload_len = 1500;

        struct icmp_echo_header *reply = (struct icmp_echo_header *)buffer;
        reply->type = 0u;
        reply->code = 0u;
        write_be16((uint8_t *)&reply->identifier, identifier);
        write_be16((uint8_t *)&reply->sequence, sequence);
        if (payload_len > 0)
            memcpy(buffer + sizeof(struct icmp_echo_header), packet + sizeof(struct icmp_echo_header), payload_len);
        reply->checksum = 0;
        reply->checksum = icmp_checksum(buffer, sizeof(struct icmp_echo_header) + payload_len);

        return ipv4_send(dev, src_ipv4, 1u, buffer, sizeof(struct icmp_echo_header) + payload_len);
    }

    klog_warn("icmp: unsupported type");
    (void)dst_ipv4;
    return -1;
}

int icmp_send_echo_request(struct net_device *dev, const uint8_t dst_ipv4[4], uint16_t identifier, uint16_t sequence)
{
    if (!dev || !dst_ipv4)
        return -1;

    uint8_t packet[sizeof(struct icmp_echo_header)];
    struct icmp_echo_header *hdr = (struct icmp_echo_header *)packet;
    hdr->type = 8u;
    hdr->code = 0u;
    write_be16((uint8_t *)&hdr->identifier, identifier);
    write_be16((uint8_t *)&hdr->sequence, sequence);
    hdr->checksum = 0;
    hdr->checksum = icmp_checksum(packet, sizeof(packet));

    return ipv4_send(dev, dst_ipv4, 1u, packet, sizeof(packet));
}

void icmp_clear_echo_replies(void)
{
    uint32_t flags = 0;
    spinlock_lock_irqsave(&g_reply_lock, &flags);
    g_reply_count = 0;
    spinlock_unlock_irqrestore(&g_reply_lock, flags);
}

static int icmp_take_reply_at_locked(size_t index, uint16_t *identifier, uint16_t *sequence, uint8_t src_ipv4[4])
{
    if (index >= g_reply_count)
        return 0;

    if (identifier)
        *identifier = g_replies[index].identifier;
    if (sequence)
        *sequence = g_replies[index].sequence;
    if (src_ipv4)
        memcpy(src_ipv4, g_replies[index].src_ipv4, sizeof(g_replies[index].src_ipv4));

    for (size_t i = index + 1; i < g_reply_count; ++i)
        g_replies[i - 1] = g_replies[i];
    --g_reply_count;
    return 1;
}

int icmp_take_echo_reply(uint16_t identifier, uint16_t sequence, uint8_t src_ipv4[4])
{
    int result = 0;
    uint32_t flags = 0;
    spinlock_lock_irqsave(&g_reply_lock, &flags);

    for (size_t i = 0; i < g_reply_count; ++i)
    {
        if (g_replies[i].identifier == identifier && g_replies[i].sequence == sequence)
        {
            result = icmp_take_reply_at_locked(i, NULL, NULL, src_ipv4);
            break;
        }
    }

    spinlock_unlock_irqrestore(&g_reply_lock, flags);
    return result;
}

int icmp_take_any_echo_reply(uint16_t *identifier, uint16_t *sequence, uint8_t src_ipv4[4])
{
    int result = 0;
    uint32_t flags = 0;
    spinlock_lock_irqsave(&g_reply_lock, &flags);

    if (g_reply_count > 0)
        result = icmp_take_reply_at_locked(0, identifier, sequence, src_ipv4);

    spinlock_unlock_irqrestore(&g_reply_lock, flags);
    return result;
}
