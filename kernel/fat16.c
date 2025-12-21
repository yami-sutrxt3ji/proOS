#include "fat16.h"

#include "fatfs.h"
#include "klog.h"

static struct fatfs_volume g_fat_volume;
static int g_fat_ready = 0;
static int g_fat_type = FATFS_TYPE_NONE;

int fat16_init(const void *base, size_t size)
{
    g_fat_ready = 0;
    g_fat_type = fatfs_init(&g_fat_volume, (void *)base, size);
    if (g_fat_type == FATFS_TYPE_NONE)
    {
        klog_warn("fat: unsupported FAT volume");
        return 0;
    }

    g_fat_ready = 1;
    if (g_fat_type == FATFS_TYPE_FAT32)
        klog_info("fat: detected FAT32 volume");
    else
        klog_info("fat: detected FAT16 volume");
    return 1;
}

int fat16_ready(void)
{
    return g_fat_ready && fatfs_ready(&g_fat_volume);
}

int fat16_type(void)
{
    return g_fat_type;
}

int fat16_mount_volume(const char *name)
{
    if (!fat16_ready())
        return -1;
    return fatfs_mount(&g_fat_volume, name);
}

int fat16_ls(char *out, size_t max_len)
{
    if (!fat16_ready())
        return -1;
    return fatfs_list(&g_fat_volume, "", out, max_len);
}

int fat16_read(const char *path, char *out, size_t max_len)
{
    if (!fat16_ready())
        return -1;

    size_t copied = 0;
    if (fatfs_read(&g_fat_volume, path, out, max_len, &copied) < 0)
        return -1;
    if (copied < max_len)
        out[copied] = '\0';
    return (int)copied;
}

int fat16_read_file(const char *path, void *out, size_t max_len, size_t *out_size)
{
    if (!fat16_ready())
        return -1;
    return fatfs_read(&g_fat_volume, path, out, max_len, out_size);
}

int fat16_file_size(const char *path, uint32_t *out_size)
{
    if (!fat16_ready())
        return -1;
    return fatfs_file_size(&g_fat_volume, path, out_size);
}

int fat16_write_file(const char *path, const void *data, size_t length)
{
    if (!fat16_ready())
        return -1;
    return fatfs_write(&g_fat_volume, path, data, length, VFS_WRITE_REPLACE);
}

int fat16_append_file(const char *path, const void *data, size_t length)
{
    if (!fat16_ready())
        return -1;
    return fatfs_write(&g_fat_volume, path, data, length, VFS_WRITE_APPEND);
}

int fat16_remove(const char *path)
{
    if (!fat16_ready())
        return -1;
    return fatfs_remove(&g_fat_volume, path);
}

int fat16_mkdir(const char *path)
{
    if (!fat16_ready())
        return -1;
    return fatfs_mkdir(&g_fat_volume, path);
}

struct fatfs_volume *fat16_volume(void)
{
    return fat16_ready() ? &g_fat_volume : NULL;
}

void fat16_configure_backing(uint32_t lba_start, uint32_t sector_count)
{
    fatfs_bind_backing(&g_fat_volume, lba_start, sector_count);
}

