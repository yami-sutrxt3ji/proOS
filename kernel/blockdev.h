#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include <stddef.h>
#include <stdint.h>

#define BLOCKDEV_MAX_DEVICES 16
#define BLOCKDEV_NAME_MAX    32

enum blockdev_flags
{
    BLOCKDEV_FLAG_NONE       = 0,
    BLOCKDEV_FLAG_READ_ONLY  = 1u << 0,
    BLOCKDEV_FLAG_PARTITION  = 1u << 1,
    BLOCKDEV_FLAG_REMOVABLE  = 1u << 2
};

struct block_device;

struct blockdev_ops
{
    int (*read)(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
    int (*write)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
};

struct block_device
{
    char name[BLOCKDEV_NAME_MAX];
    uint32_t block_size;
    uint64_t block_count;
    const struct blockdev_ops *ops;
    void *driver_data;
    uint32_t flags;
    uint8_t scanned_partitions;
};

struct blockdev_descriptor
{
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    const struct blockdev_ops *ops;
    void *driver_data;
    uint32_t flags;
};

void blockdev_init(void);
int blockdev_register(const struct blockdev_descriptor *desc, struct block_device **out_dev);
int blockdev_unregister(const char *name);
struct block_device *blockdev_find(const char *name);
size_t blockdev_enumerate(const struct block_device **out_array, size_t max_count);
int blockdev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
int blockdev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
uint32_t blockdev_device_count(void);
void blockdev_log_devices(void);

#endif
