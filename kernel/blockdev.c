#include "blockdev.h"

#include "klog.h"
#include "string.h"

#define BLOCKDEV_INLINE_CAP 8

static struct block_device device_table[BLOCKDEV_MAX_DEVICES];
static size_t device_count = 0;

static int str_equals(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    size_t ia = 0;
    while (a[ia] && b[ia])
    {
        if (a[ia] != b[ia])
            return 0;
        ++ia;
    }
    return a[ia] == '\0' && b[ia] == '\0';
}

static int name_in_use(const char *name)
{
    if (!name)
        return 0;
    for (size_t i = 0; i < device_count; ++i)
    {
        if (str_equals(device_table[i].name, name))
            return 1;
    }
    return 0;
}

static void copy_name(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    size_t len = strlen(src ? src : "");
    if (len >= cap)
        len = cap - 1;
    for (size_t i = 0; i < len; ++i)
        dst[i] = src[i];
    dst[len] = '\0';
}

static void log_device(const struct block_device *dev)
{
    if (!dev)
        return;
    char buf[BLOCKDEV_NAME_MAX + 16];
    size_t pos = 0;
    const char *prefix = "blockdev: ";
    while (prefix[pos] && pos + 1 < sizeof(buf))
    {
        buf[pos] = prefix[pos];
        ++pos;
    }
    size_t i = 0;
    while (dev->name[i] && pos + 1 < sizeof(buf))
    {
        buf[pos++] = dev->name[i++];
    }
    buf[pos] = '\0';
    klog_info(buf);
}

void blockdev_init(void)
{
    device_count = 0;
    memset(device_table, 0, sizeof(device_table));
}

int blockdev_register(const struct blockdev_descriptor *desc, struct block_device **out_dev)
{
    if (!desc || !desc->name || !desc->ops)
        return -1;
    if (device_count >= BLOCKDEV_MAX_DEVICES)
        return -1;
    if (name_in_use(desc->name))
        return -1;

    struct block_device *slot = &device_table[device_count++];
    memset(slot, 0, sizeof(*slot));
    copy_name(slot->name, sizeof(slot->name), desc->name);
    slot->block_size = desc->block_size ? desc->block_size : 512;
    slot->block_count = desc->block_count;
    slot->ops = desc->ops;
    slot->driver_data = desc->driver_data;
    slot->flags = desc->flags;
    slot->scanned_partitions = 0;

    if (out_dev)
        *out_dev = slot;

    log_device(slot);
    return 0;
}

int blockdev_unregister(const char *name)
{
    if (!name)
        return -1;
    for (size_t i = 0; i < device_count; ++i)
    {
        if (!str_equals(device_table[i].name, name))
            continue;
        if (i + 1 < device_count)
            memmove(&device_table[i], &device_table[i + 1], (device_count - i - 1) * sizeof(struct block_device));
        --device_count;
        memset(&device_table[device_count], 0, sizeof(struct block_device));
        return 0;
    }
    return -1;
}

struct block_device *blockdev_find(const char *name)
{
    if (!name)
        return NULL;
    for (size_t i = 0; i < device_count; ++i)
    {
        if (str_equals(device_table[i].name, name))
            return &device_table[i];
    }
    return NULL;
}

size_t blockdev_enumerate(const struct block_device **out_array, size_t max_count)
{
    if (!out_array || max_count == 0)
        return 0;
    size_t to_copy = (device_count < max_count) ? device_count : max_count;
    for (size_t i = 0; i < to_copy; ++i)
        out_array[i] = &device_table[i];
    return to_copy;
}

static int op_guard(const struct block_device *dev, uint64_t lba, uint32_t count)
{
    if (!dev)
        return -1;
    if (count == 0)
        return 0;
    if (dev->block_count != 0 && lba + count > dev->block_count)
        return -1;
    return 0;
}

int blockdev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer)
{
    if (!dev || !buffer)
        return -1;
    if (!dev->ops || !dev->ops->read)
        return -1;
    if (op_guard(dev, lba, count) < 0)
        return -1;
    return dev->ops->read(dev, lba, count, buffer);
}

int blockdev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer)
{
    if (!dev || !buffer)
        return -1;
    if (!dev->ops || !dev->ops->write)
        return -1;
    if (op_guard(dev, lba, count) < 0)
        return -1;
    return dev->ops->write(dev, lba, count, buffer);
}

uint32_t blockdev_device_count(void)
{
    return (uint32_t)device_count;
}

void blockdev_log_devices(void)
{
    for (size_t i = 0; i < device_count; ++i)
        log_device(&device_table[i]);
}
