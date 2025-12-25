#include "devicefs.h"

#include "blockdev.h"
#include "devmgr.h"
#include "klog.h"
#include "volmgr.h"
#include "vfs.h"

#include <stddef.h>
#include <stdint.h>

#define DEVICEFS_MAX_DATA 32
#define DEVICEFS_DATA_CAP 512

enum devicefs_kind
{
    DEVICEFS_KEYBOARD = 1,
    DEVICEFS_MOUSE,
    DEVICEFS_DISK0,
    DEVICEFS_NULL
};

struct devicefs_alias
{
    const char *name;
    enum devicefs_kind kind;
};

static const struct devicefs_alias alias_table[] = {
    { "Keyboard", DEVICEFS_KEYBOARD },
    { "Mouse", DEVICEFS_MOUSE },
    { "Disk0", DEVICEFS_DISK0 },
    { "Null", DEVICEFS_NULL }
};

struct devicefs_data_entry
{
    int used;
    char name[VFS_NODE_NAME_MAX];
    size_t size;
    char data[DEVICEFS_DATA_CAP];
};

static struct devicefs_data_entry data_entries[DEVICEFS_MAX_DATA];

static size_t local_strlen(const char *s)
{
    size_t len = 0;
    if (!s)
        return 0;
    while (s[len])
        ++len;
    return len;
}

static int names_equal(const char *a, const char *b)
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

static void copy_name(char *dst, size_t cap, const char *src)
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

static const struct devicefs_alias *find_alias(const char *name)
{
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof(alias_table) / sizeof(alias_table[0]); ++i)
    {
        if (names_equal(alias_table[i].name, name))
            return &alias_table[i];
    }
    return NULL;
}

static struct devicefs_data_entry *find_data_entry(const char *name)
{
    if (!name)
        return NULL;
    for (size_t i = 0; i < DEVICEFS_MAX_DATA; ++i)
    {
        if (!data_entries[i].used)
            continue;
        if (names_equal(data_entries[i].name, name))
            return &data_entries[i];
    }
    return NULL;
}

static struct devicefs_data_entry *allocate_data_entry(const char *name)
{
    for (size_t i = 0; i < DEVICEFS_MAX_DATA; ++i)
    {
        if (data_entries[i].used)
            continue;
        data_entries[i].used = 1;
        data_entries[i].size = 0;
        data_entries[i].data[0] = '\0';
        copy_name(data_entries[i].name, sizeof(data_entries[i].name), name);
        return &data_entries[i];
    }
    return NULL;
}

static void reset_data_entries(void)
{
    for (size_t i = 0; i < DEVICEFS_MAX_DATA; ++i)
    {
        data_entries[i].used = 0;
        data_entries[i].name[0] = '\0';
        data_entries[i].size = 0;
        data_entries[i].data[0] = '\0';
    }
}

static int append_text(char *buffer, size_t buffer_size, size_t *pos, const char *text)
{
    if (!buffer || !text || !pos)
        return 0;
    size_t idx = 0;
    while (text[idx])
    {
        if (*pos + 1 >= buffer_size)
            return 0;
        buffer[(*pos)++] = text[idx++];
    }
    return 1;
}

static int append_newline(char *buffer, size_t buffer_size, size_t *pos)
{
    if (!buffer || !pos)
        return 0;
    if (*pos + 1 >= buffer_size)
        return 0;
    buffer[(*pos)++] = '\n';
    return 1;
}

static void number_to_text(uint32_t value, char *dst, size_t cap)
{
    if (!dst || cap == 0)
        return;
    char tmp[32];
    size_t len = 0;
    if (value == 0)
    {
        if (len < sizeof(tmp))
            tmp[len++] = '0';
    }
    else
    {
        while (value > 0 && len < sizeof(tmp))
        {
            uint32_t digit = value % 10u;
            tmp[len++] = (char)('0' + (uint8_t)digit);
            value /= 10u;
        }
    }

    size_t pos = 0;
    while (len > 0 && pos + 1 < cap)
        dst[pos++] = tmp[--len];
    dst[pos] = '\0';
}

static int fill_disk_info(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return -1;

    size_t pos = 0;
    struct block_device *dev = blockdev_find("disk0");
    if (!dev)
    {
        if (!append_text(buffer, buffer_size, &pos, "disk0 unavailable"))
            return -1;
        buffer[pos] = '\0';
        return (int)pos;
    }

    if (!append_text(buffer, buffer_size, &pos, "Name: disk0"))
        return -1;
    if (!append_newline(buffer, buffer_size, &pos))
        return -1;

    char num[32];
    if (!append_text(buffer, buffer_size, &pos, "Block Size: "))
        return -1;
    number_to_text((uint64_t)dev->block_size, num, sizeof(num));
    if (!append_text(buffer, buffer_size, &pos, num))
        return -1;
    if (!append_newline(buffer, buffer_size, &pos))
        return -1;

    if (!append_text(buffer, buffer_size, &pos, "Block Count: "))
        return -1;
    number_to_text(dev->block_count, num, sizeof(num));
    if (!append_text(buffer, buffer_size, &pos, num))
        return -1;
    if (!append_newline(buffer, buffer_size, &pos))
        return -1;

    size_t volume_count = volmgr_volume_count();
    if (!append_text(buffer, buffer_size, &pos, "Volumes:"))
        return -1;
    if (!append_newline(buffer, buffer_size, &pos))
        return -1;

    for (size_t i = 0; i < volume_count; ++i)
    {
        const struct volume_info *info = volmgr_volume_at(i);
        if (!info)
            continue;
        if (!append_text(buffer, buffer_size, &pos, "  "))
            return -1;
        if (!append_text(buffer, buffer_size, &pos, info->name))
            return -1;
        if (!append_text(buffer, buffer_size, &pos, " -> "))
            return -1;
        if (!append_text(buffer, buffer_size, &pos, info->mount_path ? info->mount_path : "(unmounted)"))
            return -1;
        if (!append_newline(buffer, buffer_size, &pos))
            return -1;
    }

    if (pos >= buffer_size)
        pos = buffer_size - 1;
    buffer[pos] = '\0';
    return (int)pos;
}

static int read_alias(enum devicefs_kind kind, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return -1;

    if (kind == DEVICEFS_KEYBOARD)
    {
        struct device_node *node = devmgr_find_node("ps2kbd0");
        if (!node || !node->ops || !node->ops->read)
            return 0;
        size_t out = 0;
        if (node->ops->read(node, buffer, buffer_size, &out) < 0)
            return -1;
        if (out < buffer_size)
            buffer[out] = '\0';
        return (int)out;
    }

    if (kind == DEVICEFS_MOUSE)
    {
        struct device_node *node = devmgr_find_node("ps2mouse0");
        if (node && node->ops && node->ops->read)
        {
            size_t out = 0;
            if (node->ops->read(node, buffer, buffer_size, &out) == 0)
            {
                if (out < buffer_size)
                    buffer[out] = '\0';
                return (int)out;
            }
        }

        const char *fallback = "mouse: no data\n";
        size_t len = local_strlen(fallback);
        if (len + 1 > buffer_size)
            len = buffer_size - 1;
        for (size_t i = 0; i < len; ++i)
            buffer[i] = fallback[i];
        buffer[len] = '\0';
        return (int)len;
    }

    if (kind == DEVICEFS_DISK0)
        return fill_disk_info(buffer, buffer_size);

    if (kind == DEVICEFS_NULL)
    {
        buffer[0] = '\0';
        return 0;
    }

    return -1;
}

static int write_alias(enum devicefs_kind kind, const void *data, size_t length)
{
    if (kind == DEVICEFS_NULL)
        return (int)length;
    (void)data;
    (void)length;
    return -1;
}

static int devicefs_list(void *ctx, const char *path, char *buffer, size_t buffer_size)
{
    (void)ctx;
    if (!buffer || buffer_size == 0)
        return -1;
    if (path && path[0] != '\0')
        return -1;

    size_t pos = 0;
    for (size_t i = 0; i < sizeof(alias_table) / sizeof(alias_table[0]); ++i)
    {
        size_t len = local_strlen(alias_table[i].name);
        if (len + 1 >= buffer_size - pos)
            break;
        for (size_t j = 0; j < len; ++j)
            buffer[pos++] = alias_table[i].name[j];
        buffer[pos++] = '\n';
    }

    for (size_t i = 0; i < DEVICEFS_MAX_DATA; ++i)
    {
        if (!data_entries[i].used)
            continue;
        size_t len = local_strlen(data_entries[i].name);
        if (len + 1 >= buffer_size - pos)
            break;
        for (size_t j = 0; j < len; ++j)
            buffer[pos++] = data_entries[i].name[j];
        buffer[pos++] = '\n';
    }

    if (pos > 0)
    {
        --pos;
        buffer[pos] = '\0';
        return (int)pos;
    }

    buffer[0] = '\0';
    return 0;
}

static int devicefs_read(void *ctx, const char *path, char *buffer, size_t buffer_size)
{
    (void)ctx;
    if (!path || path[0] == '\0' || !buffer || buffer_size == 0)
        return -1;

    const struct devicefs_alias *alias = find_alias(path);
    if (alias)
        return read_alias(alias->kind, buffer, buffer_size);

    struct devicefs_data_entry *entry = find_data_entry(path);
    if (!entry)
        return -1;

    size_t to_copy = entry->size;
    if (to_copy >= buffer_size)
        to_copy = buffer_size - 1;
    for (size_t i = 0; i < to_copy; ++i)
        buffer[i] = entry->data[i];
    buffer[to_copy] = '\0';
    return (int)to_copy;
}

static int devicefs_write(void *ctx, const char *path, const char *data, size_t length, enum vfs_write_mode mode)
{
    (void)ctx;
    if (!path || path[0] == '\0')
        return -1;

    const struct devicefs_alias *alias = find_alias(path);
    if (alias)
        return write_alias(alias->kind, data, length);

    struct devicefs_data_entry *entry = find_data_entry(path);
    if (!entry)
        entry = allocate_data_entry(path);
    if (!entry)
        return -1;

    if (mode == VFS_WRITE_REPLACE)
    {
        entry->size = 0;
        entry->data[0] = '\0';
    }

    if (!data || length == 0)
    {
        entry->data[entry->size] = '\0';
        return 0;
    }

    size_t capacity = (DEVICEFS_DATA_CAP > 0) ? DEVICEFS_DATA_CAP - 1 : 0;
    if (entry->size > capacity)
        entry->size = capacity;
    size_t available = capacity > entry->size ? capacity - entry->size : 0;
    size_t to_copy = (length > available) ? available : length;
    for (size_t i = 0; i < to_copy; ++i)
        entry->data[entry->size++] = data[i];
    entry->data[entry->size] = '\0';
    return (int)to_copy;
}

static int devicefs_remove(void *ctx, const char *path)
{
    (void)ctx;
    if (!path || path[0] == '\0')
        return -1;

    if (find_alias(path))
        return -1;

    struct devicefs_data_entry *entry = find_data_entry(path);
    if (!entry)
        return -1;

    entry->used = 0;
    entry->name[0] = '\0';
    entry->size = 0;
    entry->data[0] = '\0';
    return 0;
}

static int devicefs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    return -1;
}

static const struct vfs_fs_ops devicefs_ops = {
    devicefs_list,
    devicefs_read,
    devicefs_write,
    devicefs_remove,
    devicefs_mkdir
};

int devicefs_mount(void)
{
    reset_data_entries();
    if (vfs_mount("/Devices", &devicefs_ops, NULL) < 0)
    {
        klog_error("devicefs: mount failed");
        return -1;
    }
    return 0;
}
