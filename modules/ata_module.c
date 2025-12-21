#include <stddef.h>
#include <stdint.h>

#include "module_api.h"

#include "blockdev.h"
#include "bios_fallback.h"
#include "io.h"
#include "klog.h"
#include "memory.h"
#include "partition.h"
#include "string.h"

MODULE_METADATA("ata", "0.2.0", MODULE_FLAG_AUTOSTART);

#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_CTRL   0x3F6

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_READ       0x20
#define ATA_CMD_WRITE      0x30

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF  0x20
#define ATA_SR_BSY 0x80

struct ata_device
{
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t slave;
    uint8_t present;
    uint64_t sectors;
    struct block_device *block;
};

static struct ata_device primary_master;
static uint32_t disk_index = 0;

static void make_disk_name(char *buffer, size_t cap, uint32_t index)
{
    if (!buffer || cap == 0)
        return;
    size_t pos = 0;
    const char *prefix = "disk";
    while (prefix[pos] && pos + 1 < cap)
    {
        buffer[pos] = prefix[pos];
        ++pos;
    }
    buffer[pos] = '\0';

    char tmp[12];
    size_t len = 0;
    if (index == 0)
        tmp[len++] = '0';
    else
    {
        while (index > 0 && len < sizeof(tmp))
        {
            tmp[len++] = (char)('0' + (index % 10u));
            index /= 10u;
        }
    }

    while (len > 0 && pos + 1 < cap)
        buffer[pos++] = tmp[--len];
    buffer[pos] = '\0';
}

static int ata_wait(const struct ata_device *dev, int need_drq)
{
    if (!dev)
        return -1;
    for (uint32_t i = 0; i < 100000; ++i)
    {
        uint8_t status = inb(dev->io_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR)
            return -1;
        if (status & ATA_SR_DF)
            return -1;
        if (status & ATA_SR_BSY)
            continue;
        if (need_drq && (status & ATA_SR_DRQ) == 0)
            continue;
        return 0;
    }
    return -1;
}

static void ata_select(const struct ata_device *dev, uint64_t lba)
{
    outb(dev->io_base + ATA_REG_HDDEVSEL, (uint8_t)(0xE0u | (dev->slave << 4) | ((lba >> 24) & 0x0Fu)));
    io_wait();
}

static int ata_identify(struct ata_device *dev)
{
    ata_select(dev, 0);
    outb(dev->io_base + ATA_REG_SECCOUNT0, 0);
    outb(dev->io_base + ATA_REG_LBA0, 0);
    outb(dev->io_base + ATA_REG_LBA1, 0);
    outb(dev->io_base + ATA_REG_LBA2, 0);
    outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    io_wait();
    uint8_t status = inb(dev->io_base + ATA_REG_STATUS);
    if (status == 0)
        return -1;

    if (ata_wait(dev, 1) < 0)
        return -1;

    uint16_t id_buf[256];
    insw(dev->io_base + ATA_REG_DATA, id_buf, 256u);

    uint32_t sectors = ((uint32_t)id_buf[61] << 16) | (uint32_t)id_buf[60];
    if (sectors == 0)
    {
        uint32_t hi = ((uint32_t)id_buf[103] << 16) | (uint32_t)id_buf[102];
        if (hi != 0)
            sectors = hi;
    }

    if (sectors == 0)
        sectors = 0xFFFFFFFFu;

    dev->sectors = sectors;
    dev->present = 1;
    return 0;
}

static int ata_pio_read(struct ata_device *dev, uint64_t lba, uint32_t count, uint8_t *dst)
{
    uint32_t remaining = count;
    while (remaining > 0)
    {
        uint16_t chunk = (remaining > 128) ? 128 : (uint16_t)remaining;

        ata_select(dev, lba);
        outb(dev->io_base + ATA_REG_SECCOUNT0, (uint8_t)chunk);
        outb(dev->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
        outb(dev->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
        outb(dev->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
        outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_READ);

        if (ata_wait(dev, 1) < 0)
            return -1;

        size_t words = (size_t)chunk * 256u;
        insw(dev->io_base + ATA_REG_DATA, dst, words);

        remaining -= chunk;
        lba += chunk;
        dst += (size_t)chunk * 512u;
    }
    return 0;
}

static int ata_pio_write(struct ata_device *dev, uint64_t lba, uint32_t count, const uint8_t *src)
{
    uint32_t remaining = count;
    while (remaining > 0)
    {
        uint16_t chunk = (remaining > 128) ? 128 : (uint16_t)remaining;

        ata_select(dev, lba);
        outb(dev->io_base + ATA_REG_SECCOUNT0, (uint8_t)chunk);
        outb(dev->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
        outb(dev->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
        outb(dev->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
        outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE);

        if (ata_wait(dev, 1) < 0)
            return -1;

        size_t words = (size_t)chunk * 256u;
        outsw(dev->io_base + ATA_REG_DATA, src, words);

        if (ata_wait(dev, 0) < 0)
            return -1;

        remaining -= chunk;
        lba += chunk;
        src += (size_t)chunk * 512u;
    }
    return 0;
}

static int ata_block_read(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer)
{
    struct ata_device *dev = (struct ata_device *)bdev->driver_data;
    if (!dev || !buffer || count == 0)
        return -1;

    uint8_t *dst = (uint8_t *)buffer;
    if (dev->present)
    {
        if (ata_pio_read(dev, lba, count, dst) == 0)
            return 0;
    }

    if (bios_fallback_available())
    {
        uint8_t drive = bios_fallback_boot_drive();
        if (bios_fallback_read(drive, lba, count, buffer) == 0)
            return 0;
    }

    return -1;
}

static int ata_block_write(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer)
{
    struct ata_device *dev = (struct ata_device *)bdev->driver_data;
    if (!dev || !buffer || count == 0)
        return -1;

    const uint8_t *src = (const uint8_t *)buffer;
    if (dev->present)
    {
        if (ata_pio_write(dev, lba, count, src) == 0)
            return 0;
    }

    if (bios_fallback_available())
    {
        uint8_t drive = bios_fallback_boot_drive();
        if (bios_fallback_write(drive, lba, count, buffer) == 0)
            return 0;
    }

    return -1;
}

static const struct blockdev_ops ata_ops = {
    ata_block_read,
    ata_block_write
};

static int ata_register_device(struct ata_device *dev)
{
    char name[BLOCKDEV_NAME_MAX];
    memset(name, 0, sizeof(name));
    make_disk_name(name, sizeof(name), disk_index++);

    struct blockdev_descriptor desc;
    desc.name = name;
    desc.block_size = 512;
    desc.block_count = dev->sectors;
    desc.ops = &ata_ops;
    desc.driver_data = dev;
    desc.flags = 0;

    if (blockdev_register(&desc, &dev->block) < 0)
        return -1;

    partition_scan_device(dev->block);
    return 0;
}

int module_init(void)
{
    memset(&primary_master, 0, sizeof(primary_master));
    primary_master.io_base = ATA_PRIMARY_IO;
    primary_master.ctrl_base = ATA_PRIMARY_CTRL;
    primary_master.slave = 0;

    if (ata_identify(&primary_master) < 0)
        primary_master.present = 0;

    if (ata_register_device(&primary_master) < 0)
        return -1;

    klog_info("ata.driver: initialized");
    return 0;
}

void module_exit(void)
{
    (void)primary_master;
}
