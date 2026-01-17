#include "e1000.h"

#include <stdint.h>

#include "devmgr.h"
#include "klog.h"
#include "memory.h"
#include "net.h"
#include "pci.h"
#include "string.h"

#define E1000_VENDOR_ID 0x8086u

static const uint16_t e1000_device_ids[] = {
    0x1000u, 0x1001u, 0x1004u, 0x1008u, 0x1009u,
    0x100Cu, 0x100Du, 0x100Eu, 0x100Fu, 0x1010u,
    0x1011u, 0x1012u, 0x1013u, 0x1014u, 0x1015u,
    0x1016u, 0x1017u, 0x1018u, 0x1019u, 0x101Au,
    0x101Du, 0x101Eu, 0x1026u, 0x1027u, 0x1028u,
    0x10D3u, 0x10F5u, 0
};

#define E1000_NUM_RX_DESC 32u
#define E1000_NUM_TX_DESC 32u
#define E1000_RX_BUF_SIZE 2048u
#define E1000_TX_BUF_SIZE 2048u

#define E1000_REG_CTRL      0x0000u
#define E1000_REG_STATUS    0x0008u
#define E1000_REG_CTRL_EXT  0x0018u
#define E1000_REG_IMS       0x00D0u
#define E1000_REG_IMC       0x00D8u
#define E1000_REG_RCTL      0x0100u
#define E1000_REG_TCTL      0x0400u
#define E1000_REG_TIPG      0x0410u
#define E1000_REG_RDBAL     0x2800u
#define E1000_REG_RDBAH     0x2804u
#define E1000_REG_RDLEN     0x2808u
#define E1000_REG_RDH       0x2810u
#define E1000_REG_RDT       0x2818u
#define E1000_REG_TDBAL     0x3800u
#define E1000_REG_TDBAH     0x3804u
#define E1000_REG_TDLEN     0x3808u
#define E1000_REG_TDH       0x3810u
#define E1000_REG_TDT       0x3818u
#define E1000_REG_RAL(n)    (0x5400u + ((n) * 8u))
#define E1000_REG_RAH(n)    (0x5404u + ((n) * 8u))

#define E1000_CTRL_RST      (1u << 26)
#define E1000_CTRL_SLU      (1u << 6)
#define E1000_CTRL_ASDE     (1u << 5)

#define E1000_STATUS_LU     (1u << 1)

#define E1000_RCTL_EN       (1u << 1)
#define E1000_RCTL_SBP      (1u << 2)
#define E1000_RCTL_UPE      (1u << 3)
#define E1000_RCTL_MPE      (1u << 4)
#define E1000_RCTL_BAM      (1u << 15)
#define E1000_RCTL_SECRC    (1u << 26)

#define E1000_TCTL_EN       (1u << 1)
#define E1000_TCTL_PSP      (1u << 3)
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD_SHIFT 12

#define E1000_TXD_CMD_EOP   0x01u
#define E1000_TXD_CMD_IFCS  0x02u
#define E1000_TXD_CMD_RS    0x08u
#define E1000_TXD_STAT_DD   0x01u

#define E1000_RXD_STAT_DD   0x01u
#define E1000_RXD_STAT_EOP  0x02u

struct e1000_rx_desc
{
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc
{
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct e1000_device
{
    int present;
    int initialized;
    struct pci_device_info pci;
    volatile uint8_t *mmio_base;
    struct device_node *node;

    struct e1000_rx_desc *rx_descs;
    struct e1000_tx_desc *tx_descs;
    uint8_t *rx_buffers;
    uint8_t *tx_buffers;
    uint32_t rx_tail;
    uint32_t rx_head;
    uint32_t tx_tail;
    uint32_t tx_head;
    uint8_t mac[6];
};

static struct e1000_device g_e1000;
static struct net_device g_e1000_netdev;

static inline uint32_t e1000_reg_read(struct e1000_device *dev, uint32_t offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(dev->mmio_base + offset);
    return *reg;
}

static inline void e1000_reg_write(struct e1000_device *dev, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(dev->mmio_base + offset);
    *reg = value;
    (void)*reg;
}

static int e1000_net_transmit(struct net_device *netdev, const uint8_t *data, size_t length)
{
    if (!netdev || !data || length == 0)
        return -1;

    struct e1000_device *dev = (struct e1000_device *)netdev->driver_data;
    if (!dev || !dev->initialized)
        return -1;

    if (length > E1000_TX_BUF_SIZE)
    {
        klog_warn("e1000: tx frame too large");
        return -1;
    }

    uint32_t tail = dev->tx_tail;
    struct e1000_tx_desc *desc = &dev->tx_descs[tail];
    if ((desc->status & E1000_TXD_STAT_DD) == 0)
    {
        klog_warn("e1000: tx ring full");
        return -1;
    }

    uint8_t *buffer = dev->tx_buffers + tail * E1000_TX_BUF_SIZE;
    memcpy(buffer, data, length);

    desc->length = (uint16_t)length;
    desc->cmd = (uint8_t)(E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS);
    desc->status &= (uint8_t)~E1000_TXD_STAT_DD;

    tail = (tail + 1u) % E1000_NUM_TX_DESC;
    dev->tx_tail = tail;

    e1000_reg_write(dev, E1000_REG_TDT, tail);
    return 0;
}

static uint32_t e1000_poll_tx(struct e1000_device *dev)
{
    uint32_t reclaimed = 0;
    while (dev->tx_head != dev->tx_tail)
    {
        struct e1000_tx_desc *desc = &dev->tx_descs[dev->tx_head];
        if ((desc->status & E1000_TXD_STAT_DD) == 0)
            break;

        dev->tx_head = (dev->tx_head + 1u) % E1000_NUM_TX_DESC;
        ++reclaimed;
    }
    return reclaimed;
}

static uint32_t e1000_poll_rx(struct e1000_device *dev, struct net_device *netdev)
{
    uint32_t processed = 0;

    while (1)
    {
        struct e1000_rx_desc *desc = &dev->rx_descs[dev->rx_head];
        if ((desc->status & E1000_RXD_STAT_DD) == 0)
            break;

        uint16_t length = desc->length;
        uint8_t status = desc->status;
        uint8_t errors = desc->errors;
        uint8_t *buffer = dev->rx_buffers + dev->rx_head * E1000_RX_BUF_SIZE;

        if ((status & E1000_RXD_STAT_EOP) == 0 || errors != 0)
        {
            klog_warn("e1000: dropping rx frame");
        }
        else if (length > 0 && length <= E1000_RX_BUF_SIZE)
        {
            if (net_receive_frame(netdev, buffer, length) < 0)
                klog_warn("e1000: frame rejected by stack");
        }

        desc->status = 0;
        desc->errors = 0;
        desc->checksum = 0;

        dev->rx_tail = dev->rx_head;
        e1000_reg_write(dev, E1000_REG_RDT, dev->rx_tail);

        dev->rx_head = (dev->rx_head + 1u) % E1000_NUM_RX_DESC;
        ++processed;
    }

    return processed;
}

static int e1000_net_poll(struct net_device *netdev)
{
    if (!netdev)
        return 0;

    struct e1000_device *dev = (struct e1000_device *)netdev->driver_data;
    if (!dev || !dev->initialized)
        return 0;

    uint32_t total = 0;
    total += e1000_poll_tx(dev);
    total += e1000_poll_rx(dev, netdev);

    return (int)total;
}

static const struct net_device_ops g_e1000_netops = {
    e1000_net_transmit,
    e1000_net_poll
};

static void e1000_wait_cycles(uint32_t cycles)
{
    while (cycles--)
    asm volatile("pause");
}

static int e1000_select_mmio_bar(const struct pci_device_info *info, uint32_t *out_base)
{
    if (!info || !out_base)
        return -1;

    for (size_t i = 0; i < 6; ++i)
    {
        uint32_t bar = info->bar[i];
        if (bar == 0)
            continue;
        if (bar & 0x1u)
            continue;

        *out_base = bar & 0xFFFFFFF0u;
        return 0;
    }

    return -1;
}

static void e1000_read_mac(struct e1000_device *dev)
{
    uint32_t ral = e1000_reg_read(dev, E1000_REG_RAL(0));
    uint32_t rah = e1000_reg_read(dev, E1000_REG_RAH(0));

    dev->mac[0] = (uint8_t)(ral & 0xFFu);
    dev->mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
    dev->mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    dev->mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    dev->mac[4] = (uint8_t)(rah & 0xFFu);
    dev->mac[5] = (uint8_t)((rah >> 8) & 0xFFu);
}

static void e1000_log_mac(const struct e1000_device *dev)
{
    static const char hex[] = "0123456789ABCDEF";
    char buffer[32];
    size_t pos = 0;

    buffer[pos++] = 'e'; buffer[pos++] = '1'; buffer[pos++] = '0'; buffer[pos++] = '0'; buffer[pos++] = '0'; buffer[pos++] = ':'; buffer[pos++] = ' ';
    for (size_t i = 0; i < 6; ++i)
    {
        uint8_t byte = dev->mac[i];
        buffer[pos++] = hex[(byte >> 4) & 0xFu];
        buffer[pos++] = hex[byte & 0xFu];
        if (i < 5)
            buffer[pos++] = ':';
    }
    buffer[pos] = '\0';
    klog_info(buffer);
}

static int e1000_reset(struct e1000_device *dev)
{
    e1000_reg_write(dev, E1000_REG_IMC, 0xFFFFFFFFu);
    e1000_reg_write(dev, E1000_REG_CTRL, e1000_reg_read(dev, E1000_REG_CTRL) | E1000_CTRL_RST);
    e1000_wait_cycles(100000);

    for (uint32_t i = 0; i < 100000; ++i)
    {
        if ((e1000_reg_read(dev, E1000_REG_CTRL) & E1000_CTRL_RST) == 0)
            return 0;
    }

    klog_warn("e1000: reset timeout");
    return -1;
}

static int e1000_setup_rx(struct e1000_device *dev)
{
    e1000_reg_write(dev, E1000_REG_RCTL, 0);

    dev->rx_descs = (struct e1000_rx_desc *)kalloc_zero(sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC);
    dev->rx_buffers = (uint8_t *)kalloc_zero(E1000_NUM_RX_DESC * E1000_RX_BUF_SIZE);
    if (!dev->rx_descs || !dev->rx_buffers)
        return -1;

    for (uint32_t i = 0; i < E1000_NUM_RX_DESC; ++i)
    {
        uint8_t *buffer = dev->rx_buffers + (i * E1000_RX_BUF_SIZE);
        dev->rx_descs[i].buffer_addr = (uint64_t)(uintptr_t)buffer;
        dev->rx_descs[i].status = 0;
    }

    dev->rx_tail = E1000_NUM_RX_DESC - 1u;
    dev->rx_head = 0;

    e1000_reg_write(dev, E1000_REG_RDBAL, (uint32_t)(uintptr_t)dev->rx_descs);
    e1000_reg_write(dev, E1000_REG_RDBAH, 0);
    e1000_reg_write(dev, E1000_REG_RDLEN, (uint32_t)(sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC));
    e1000_reg_write(dev, E1000_REG_RDH, 0);
    e1000_reg_write(dev, E1000_REG_RDT, dev->rx_tail);

    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;
    e1000_reg_write(dev, E1000_REG_RCTL, rctl);
    return 0;
}

static int e1000_setup_tx(struct e1000_device *dev)
{
    dev->tx_descs = (struct e1000_tx_desc *)kalloc_zero(sizeof(struct e1000_tx_desc) * E1000_NUM_TX_DESC);
    dev->tx_buffers = (uint8_t *)kalloc_zero(E1000_NUM_TX_DESC * E1000_TX_BUF_SIZE);
    if (!dev->tx_descs || !dev->tx_buffers)
        return -1;

    for (uint32_t i = 0; i < E1000_NUM_TX_DESC; ++i)
    {
        dev->tx_descs[i].buffer_addr = (uint64_t)(uintptr_t)(dev->tx_buffers + i * E1000_TX_BUF_SIZE);
        dev->tx_descs[i].status = E1000_TXD_STAT_DD;
    }

    dev->tx_tail = 0;
    dev->tx_head = 0;

    e1000_reg_write(dev, E1000_REG_TDBAL, (uint32_t)(uintptr_t)dev->tx_descs);
    e1000_reg_write(dev, E1000_REG_TDBAH, 0);
    e1000_reg_write(dev, E1000_REG_TDLEN, (uint32_t)(sizeof(struct e1000_tx_desc) * E1000_NUM_TX_DESC));
    e1000_reg_write(dev, E1000_REG_TDH, 0);
    e1000_reg_write(dev, E1000_REG_TDT, 0);

    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP;
    tctl |= (0x0Fu << E1000_TCTL_CT_SHIFT);
    tctl |= (0x40u << E1000_TCTL_COLD_SHIFT);
    e1000_reg_write(dev, E1000_REG_TCTL, tctl);
    e1000_reg_write(dev, E1000_REG_TIPG, 0x0060200Au);
    return 0;
}

static int e1000_hw_init(struct e1000_device *dev)
{
    if (e1000_reset(dev) < 0)
        return -1;

    uint32_t ctrl = e1000_reg_read(dev, E1000_REG_CTRL);
    ctrl |= E1000_CTRL_ASDE | E1000_CTRL_SLU;
    e1000_reg_write(dev, E1000_REG_CTRL, ctrl);

    e1000_reg_write(dev, E1000_REG_IMC, 0xFFFFFFFFu);

    if (e1000_setup_rx(dev) < 0)
        return -1;
    if (e1000_setup_tx(dev) < 0)
        return -1;

    e1000_read_mac(dev);
    e1000_log_mac(dev);

    uint32_t status = e1000_reg_read(dev, E1000_REG_STATUS);
    if (status & E1000_STATUS_LU)
        klog_info("e1000: link up");
    else
        klog_warn("e1000: link down");

    dev->initialized = 1;
    return 0;
}

static int e1000_register_net_device(struct e1000_device *dev)
{
    memset(&g_e1000_netdev, 0, sizeof(g_e1000_netdev));
    g_e1000_netdev.ops = &g_e1000_netops;
    g_e1000_netdev.driver_data = dev;
    memcpy(g_e1000_netdev.name, "eth0", 5u);
    memcpy(g_e1000_netdev.mac, dev->mac, sizeof(dev->mac));

    if (net_register_device(&g_e1000_netdev) < 0)
    {
        klog_warn("e1000: failed to register netdev");
        return -1;
    }

    return 0;
}

static int e1000_probe(struct e1000_device *dev)
{
    if (!dev)
        return -1;

    struct pci_device_info candidate;
    memset(&candidate, 0, sizeof(candidate));

    for (size_t idx = 0; e1000_device_ids[idx] != 0; ++idx)
    {
        if (pci_find_device(E1000_VENDOR_ID, e1000_device_ids[idx], &candidate) == 0)
        {
            uint32_t mmio;
            if (e1000_select_mmio_bar(&candidate, &mmio) < 0)
            {
                klog_warn("e1000: device lacks usable MMIO BAR");
                return -1;
            }

            pci_enable_device(&candidate, PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

            dev->present = 1;
            dev->pci = candidate;
            dev->mmio_base = (volatile uint8_t *)(uintptr_t)mmio;
            return 0;
        }
    }

    return -1;
}

static int e1000_start(struct device_node *node)
{
    (void)node;
    if (!g_e1000.present)
        return -1;
    if (!g_e1000.initialized)
        return -1;
    return 0;
}

static void e1000_stop(struct device_node *node)
{
    (void)node;
}

static const struct device_ops e1000_ops = {
    e1000_start,
    e1000_stop,
    NULL,
    NULL,
    NULL
};

int e1000_driver_init(void)
{
    memset(&g_e1000, 0, sizeof(g_e1000));

    if (e1000_probe(&g_e1000) < 0)
    {
        klog_info("e1000: controller not detected");
        return -1;
    }

    if (e1000_hw_init(&g_e1000) < 0)
    {
        klog_warn("e1000: hardware initialization failed");
        return -1;
    }

    if (e1000_register_net_device(&g_e1000) < 0)
        return -1;

    struct device_descriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.name = "net0";
    desc.type = "net";
    desc.ops = &e1000_ops;
    desc.flags = DEVICE_FLAG_PUBLISH;
    desc.driver_data = &g_e1000;

    if (devmgr_register_device(&desc, &g_e1000.node) < 0)
    {
        klog_warn("e1000: failed to register device");
        return -1;
    }

    klog_info("e1000: controller initialized");
    return 0;
}

int e1000_present(void)
{
    return g_e1000.present && g_e1000.initialized;
}
