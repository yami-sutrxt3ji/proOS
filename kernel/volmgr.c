#include "volmgr.h"

#include "blockdev.h"
#include "klog.h"
#include "partition.h"
#include "string.h"
#include "vfs.h"

struct volume_record
{
    int used;
    char name[VOLMGR_NAME_MAX];
    char mount_path[VFS_MAX_PATH];
    struct block_device *device;
    unsigned int index;
};

static struct volume_record volume_table[VOLMGR_MAX_VOLUMES];
static unsigned int next_index = 0;

static void reset_table(void)
{
    for (size_t i = 0; i < VOLMGR_MAX_VOLUMES; ++i)
    {
        volume_table[i].used = 0;
        volume_table[i].name[0] = '\0';
        volume_table[i].mount_path[0] = '\0';
        volume_table[i].device = NULL;
        volume_table[i].index = 0;
    }
    next_index = 0;
}

static void format_volume_name(char *buffer, size_t cap, unsigned int index)
{
    if (!buffer || cap == 0)
        return;

    const char *prefix = "Disk";
    size_t pos = 0;
    while (prefix[pos] && pos + 1 < cap)
    {
        buffer[pos] = prefix[pos];
        ++pos;
    }

    char tmp[12];
    size_t len = 0;
    if (index == 0)
    {
        tmp[len++] = '0';
    }
    else
    {
        unsigned int value = index;
        while (value > 0 && len < sizeof(tmp))
        {
            tmp[len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }

    while (len > 0 && pos + 1 < cap)
        buffer[pos++] = tmp[--len];
    buffer[pos] = '\0';
}

static void format_mount_path(char *buffer, size_t cap, const char *name)
{
    if (!buffer || cap == 0 || !name)
        return;

    const char *prefix = "/Volumes/";
    size_t pos = 0;
    while (prefix[pos] && pos + 1 < cap)
    {
        buffer[pos] = prefix[pos];
        ++pos;
    }
    size_t i = 0;
    while (name[i] && pos + 1 < cap)
        buffer[pos++] = name[i++];
    buffer[pos] = '\0';
}

static int str_equals(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    size_t i = 0;
    while (a[i] && b[i])
    {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void str_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    size_t i = 0;
    if (src)
    {
        while (src[i] && i + 1 < cap)
        {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static struct volume_record *find_by_device(struct block_device *device)
{
    if (!device)
        return NULL;
    for (size_t i = 0; i < VOLMGR_MAX_VOLUMES; ++i)
    {
        if (volume_table[i].used && volume_table[i].device == device)
            return &volume_table[i];
    }
    return NULL;
}

static struct volume_record *allocate_slot(void)
{
    for (size_t i = 0; i < VOLMGR_MAX_VOLUMES; ++i)
    {
        if (!volume_table[i].used)
            return &volume_table[i];
    }
    return NULL;
}

static void attach_device(struct block_device *device)
{
    if (!device)
        return;
    if (!(device->flags & BLOCKDEV_FLAG_PARTITION))
        return;
    if (find_by_device(device))
        return;

    struct volume_record *slot = allocate_slot();
    if (!slot)
        return;

    char name[VOLMGR_NAME_MAX];
    memset(name, 0, sizeof(name));
    format_volume_name(name, sizeof(name), next_index++);

    slot->used = 1;
    slot->device = device;
    slot->index = next_index - 1;
    str_copy(slot->name, sizeof(slot->name), name);
    format_mount_path(slot->mount_path, sizeof(slot->mount_path), slot->name);

    klog_info("volmgr: volume attached");
}

void volmgr_init(void)
{
    reset_table();
    partition_autoscan();
    volmgr_rescan();
}

void volmgr_rescan(void)
{
    const struct block_device *devices[BLOCKDEV_MAX_DEVICES];
    size_t count = blockdev_enumerate(devices, BLOCKDEV_MAX_DEVICES);
    for (size_t i = 0; i < count; ++i)
        attach_device((struct block_device *)devices[i]);
}

size_t volmgr_volume_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < VOLMGR_MAX_VOLUMES; ++i)
    {
        if (volume_table[i].used)
            ++count;
    }
    return count;
}

const struct volume_info *volmgr_volume_at(size_t index)
{
    size_t seen = 0;
    for (size_t i = 0; i < VOLMGR_MAX_VOLUMES; ++i)
    {
        if (!volume_table[i].used)
            continue;
        if (seen == index)
        {
            static struct volume_info info;
            info.name = volume_table[i].name;
            info.mount_path = volume_table[i].mount_path;
            info.device = volume_table[i].device;
            info.index = volume_table[i].index;
            return &info;
        }
        ++seen;
    }
    return NULL;
}

struct block_device *volmgr_find_device(const char *name)
{
    if (!name)
        return NULL;
    for (size_t i = 0; i < VOLMGR_MAX_VOLUMES; ++i)
    {
        if (!volume_table[i].used)
            continue;
        if (str_equals(volume_table[i].name, name))
            return volume_table[i].device;
    }
    return NULL;
}
