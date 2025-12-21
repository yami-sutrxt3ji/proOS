#include "partition.h"

#include "klog.h"
#include "memory.h"
#include "string.h"

#define MBR_SIGNATURE_OFFSET 510
#define MBR_PARTITION_TABLE  446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_PARTITION_COUNT 4

struct mbr_partition
{
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_start;
    uint32_t lba_length;
} __attribute__((packed));

struct partition_data
{
    struct block_device *parent;
    uint64_t lba_start;
    uint64_t lba_length;
};

static void append_number(char *dst, size_t cap, uint32_t value)
{
    if (!dst || cap == 0)
        return;
    char tmp[12];
    size_t len = 0;
    if (value == 0)
        tmp[len++] = '0';
    else
    {
        while (value > 0 && len < sizeof(tmp))
        {
            tmp[len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    size_t pos = strlen(dst);
    while (len > 0 && pos + 1 < cap)
        dst[pos++] = tmp[--len];
    dst[pos] = '\0';
}

static void make_partition_name(char *buffer, size_t cap, const char *base, uint32_t index)
{
    if (!buffer || cap == 0)
        return;
    size_t pos = 0;
    while (base && base[pos] && pos + 1 < cap)
    {
        buffer[pos] = base[pos];
        ++pos;
    }
    if (pos + 1 < cap)
        buffer[pos++] = 'p';
    if (pos < cap)
        buffer[pos] = '\0';
    append_number(buffer, cap, index);
}

static int partition_read(struct block_device *device, uint64_t lba, uint32_t count, void *buffer)
{
    if (!device || !buffer)
        return -1;
    struct partition_data *data = (struct partition_data *)device->driver_data;
    if (!data || !data->parent)
        return -1;
    if (data->lba_length != 0 && lba + count > data->lba_length)
        return -1;
    return blockdev_read(data->parent, data->lba_start + lba, count, buffer);
}

static int partition_write(struct block_device *device, uint64_t lba, uint32_t count, const void *buffer)
{
    if (!device || !buffer)
        return -1;
    struct partition_data *data = (struct partition_data *)device->driver_data;
    if (!data || !data->parent)
        return -1;
    if (data->lba_length != 0 && lba + count > data->lba_length)
        return -1;
    return blockdev_write(data->parent, data->lba_start + lba, count, buffer);
}

static const struct blockdev_ops partition_ops = {
    partition_read,
    partition_write
};

void partition_init(void)
{
    /* no-op for now */
}

void partition_scan_device(struct block_device *device)
{
    if (!device || device->block_size != 512)
        return;
    if (device->flags & BLOCKDEV_FLAG_PARTITION)
        return;
    if (device->scanned_partitions)
        return;

    uint8_t sector[512];
    if (blockdev_read(device, 0, 1, sector) < 0)
        return;

    if (sector[MBR_SIGNATURE_OFFSET] != 0x55 || sector[MBR_SIGNATURE_OFFSET + 1] != 0xAA)
    {
        device->scanned_partitions = 1;
        return;
    }

    const struct mbr_partition *entries = (const struct mbr_partition *)(sector + MBR_PARTITION_TABLE);
    uint32_t part_index = 1;
    for (int i = 0; i < MBR_PARTITION_COUNT; ++i)
    {
        const struct mbr_partition *entry = &entries[i];
        if (entry->type == 0 || entry->lba_length == 0)
            continue;

        struct partition_data *pdata = (struct partition_data *)kalloc(sizeof(struct partition_data));
        if (!pdata)
            continue;
        pdata->parent = device;
        pdata->lba_start = entry->lba_start;
        pdata->lba_length = entry->lba_length;

        char name[BLOCKDEV_NAME_MAX];
        memset(name, 0, sizeof(name));
        make_partition_name(name, sizeof(name), device->name, part_index++);

        struct blockdev_descriptor desc;
        desc.name = name;
        desc.block_size = device->block_size;
        desc.block_count = pdata->lba_length;
        desc.ops = &partition_ops;
        desc.driver_data = pdata;
        desc.flags = BLOCKDEV_FLAG_PARTITION;

        if (blockdev_register(&desc, NULL) < 0)
            continue;
    }

    device->scanned_partitions = 1;
}

void partition_autoscan(void)
{
    const struct block_device *devices[BLOCKDEV_MAX_DEVICES];
    size_t count = blockdev_enumerate(devices, BLOCKDEV_MAX_DEVICES);
    for (size_t i = 0; i < count; ++i)
        partition_scan_device((struct block_device *)devices[i]);
}
