#ifndef FATFS_H
#define FATFS_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

#define FATFS_TYPE_NONE 0
#define FATFS_TYPE_FAT16 16
#define FATFS_TYPE_FAT32 32

struct block_device;

struct fatfs_volume
{
    uint8_t *base;
    size_t size;
    int ready;
    uint32_t fat_type;

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t data_start_sector;
    uint32_t total_clusters;

    uint32_t root_dir_sector;
    uint32_t root_dir_sectors;
    uint32_t root_entries;
    uint32_t root_cluster;

    char mount_path[VFS_MAX_PATH];

    struct block_device *device;
    uint32_t backing_lba;
    uint32_t backing_sectors;
    int backing_configured;
    int dirty;
};

int fatfs_init(struct fatfs_volume *volume, void *base, size_t size);
int fatfs_ready(const struct fatfs_volume *volume);
int fatfs_type(const struct fatfs_volume *volume);
int fatfs_mount(struct fatfs_volume *volume, const char *name);
int fatfs_list(struct fatfs_volume *volume, const char *path, char *buffer, size_t buffer_size);
int fatfs_read(struct fatfs_volume *volume, const char *path, void *out, size_t max_len, size_t *out_size);
int fatfs_write(struct fatfs_volume *volume, const char *path, const void *data, size_t length, enum vfs_write_mode mode);
int fatfs_remove(struct fatfs_volume *volume, const char *path);
int fatfs_mkdir(struct fatfs_volume *volume, const char *path);
int fatfs_file_size(struct fatfs_volume *volume, const char *path, uint32_t *out_size);
void fatfs_bind_backing(struct fatfs_volume *volume, uint32_t lba_start, uint32_t sector_count);

#endif
