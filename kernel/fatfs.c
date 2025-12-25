#include "fatfs.h"

#include "klog.h"
#include "memory.h"
#include "blockdev.h"

#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_LFN 0x0F
#define FAT_ENTRY_FREE 0xE5
#define FAT_ENTRY_END 0x00

struct fat_dir_entry
{
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
};

struct fat_dir_scan
{
    struct fat_dir_entry *match;
    uint32_t match_cluster;
    uint32_t match_index;

    struct fat_dir_entry *free_entry;
    uint32_t free_cluster;
    uint32_t free_index;

    struct fat_dir_entry *zero_entry;
    uint32_t zero_cluster;
    uint32_t zero_index;

    uint32_t last_cluster;
};

static uint16_t read_le16(const uint8_t *ptr)
{
    return (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
}

static uint32_t read_le32(const uint8_t *ptr)
{
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

static void write_le16(uint8_t *ptr, uint16_t value)
{
    ptr[0] = (uint8_t)(value & 0xFFu);
    ptr[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_le32(uint8_t *ptr, uint32_t value)
{
    ptr[0] = (uint8_t)(value & 0xFFu);
    ptr[1] = (uint8_t)((value >> 8) & 0xFFu);
    ptr[2] = (uint8_t)((value >> 16) & 0xFFu);
    ptr[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static size_t fatfs_cluster_size_bytes(const struct fatfs_volume *volume)
{
    return (size_t)volume->bytes_per_sector * (size_t)volume->sectors_per_cluster;
}

static uint8_t *fatfs_sector_ptr(struct fatfs_volume *volume, uint32_t sector)
{
    size_t offset = (size_t)sector * (size_t)volume->bytes_per_sector;
    if (offset >= volume->size)
        return NULL;
    return volume->base + offset;
}

static uint8_t *fatfs_cluster_ptr(struct fatfs_volume *volume, uint32_t cluster)
{
    if (cluster < 2)
        return NULL;
    uint32_t relative = cluster - 2u;
    uint32_t sector = volume->data_start_sector + relative * (uint32_t)volume->sectors_per_cluster;
    return fatfs_sector_ptr(volume, sector);
}

static uint32_t fatfs_eoc_marker(const struct fatfs_volume *volume)
{
    return (volume->fat_type == FATFS_TYPE_FAT32) ? 0x0FFFFFF8u : 0xFFF8u;
}

static int fatfs_is_eoc(const struct fatfs_volume *volume, uint32_t value)
{
    if (volume->fat_type == FATFS_TYPE_FAT32)
        return (value & 0x0FFFFFFF) >= 0x0FFFFFF8u;
    return value >= 0xFFF8u;
}

static uint32_t fatfs_read_fat(struct fatfs_volume *volume, uint32_t cluster)
{
    if (cluster >= volume->total_clusters + 2u)
        return fatfs_eoc_marker(volume);

    uint32_t entry_offset;
    if (volume->fat_type == FATFS_TYPE_FAT32)
        entry_offset = cluster * 4u;
    else
        entry_offset = cluster * 2u;

    uint32_t fat_sector_offset = entry_offset / volume->bytes_per_sector;
    uint32_t fat_byte_offset = entry_offset % volume->bytes_per_sector;

    uint32_t fat_sector = volume->reserved_sectors + fat_sector_offset;
    uint8_t *sector_ptr = fatfs_sector_ptr(volume, fat_sector);
    if (!sector_ptr)
        return fatfs_eoc_marker(volume);

    if (volume->fat_type == FATFS_TYPE_FAT32)
    {
        uint32_t value = read_le32(sector_ptr + fat_byte_offset);
        return value & 0x0FFFFFFFu;
    }

    return (uint32_t)read_le16(sector_ptr + fat_byte_offset);
}

static void fatfs_write_fat(struct fatfs_volume *volume, uint32_t cluster, uint32_t value)
{
    if (cluster >= volume->total_clusters + 2u)
        return;

    uint32_t masked = value;
    if (volume->fat_type == FATFS_TYPE_FAT32)
        masked &= 0x0FFFFFFFu;

    uint32_t entry_offset = (volume->fat_type == FATFS_TYPE_FAT32) ? cluster * 4u : cluster * 2u;
    uint32_t fat_sector_offset = entry_offset / volume->bytes_per_sector;
    uint32_t fat_byte_offset = entry_offset % volume->bytes_per_sector;

    for (uint32_t copy = 0; copy < volume->fat_count; ++copy)
    {
        uint32_t fat_sector = volume->reserved_sectors + copy * volume->sectors_per_fat + fat_sector_offset;
        uint8_t *sector_ptr = fatfs_sector_ptr(volume, fat_sector);
        if (!sector_ptr)
            continue;

        if (volume->fat_type == FATFS_TYPE_FAT32)
        {
            uint32_t existing = read_le32(sector_ptr + fat_byte_offset);
            existing &= 0xF0000000u;
            existing |= masked;
            write_le32(sector_ptr + fat_byte_offset, existing);
        }
        else
        {
            write_le16(sector_ptr + fat_byte_offset, (uint16_t)(masked & 0xFFFFu));
        }
    }
}

static void fatfs_zero_cluster(struct fatfs_volume *volume, uint32_t cluster)
{
    uint8_t *ptr = fatfs_cluster_ptr(volume, cluster);
    if (!ptr)
        return;
    size_t bytes = fatfs_cluster_size_bytes(volume);
    for (size_t i = 0; i < bytes; ++i)
        ptr[i] = 0;
}

static struct block_device *fatfs_resolve_device(struct fatfs_volume *volume)
{
    if (!volume)
        return NULL;
    if (volume->device)
        return volume->device;

    const struct block_device *devices[BLOCKDEV_MAX_DEVICES];
    size_t count = blockdev_enumerate(devices, BLOCKDEV_MAX_DEVICES);
    for (size_t i = 0; i < count; ++i)
    {
        const struct block_device *candidate = devices[i];
        if (!candidate)
            continue;
        if (candidate->flags & BLOCKDEV_FLAG_PARTITION)
            continue;
        if (candidate->block_size != volume->bytes_per_sector)
            continue;
        volume->device = (struct block_device *)candidate;
        break;
    }
    return volume->device;
}

void fatfs_bind_backing(struct fatfs_volume *volume, uint32_t lba_start, uint32_t sector_count)
{
    if (!volume)
        return;
    volume->backing_lba = lba_start;
    volume->backing_sectors = sector_count;
    volume->backing_configured = (sector_count > 0);
    volume->device = NULL;
    volume->dirty = 0;
}

static void fatfs_mark_dirty(struct fatfs_volume *volume)
{
    if (!volume)
        return;
    volume->dirty = 1;
}

static int fatfs_flush(struct fatfs_volume *volume)
{
    if (!volume)
        return -1;
    if (!volume->ready)
        return -1;
    if (!volume->dirty)
        return 0;
    if (!volume->backing_configured)
        return -1;

    if (volume->bytes_per_sector == 0)
        return -1;

    uint32_t sectors = volume->backing_sectors;
    if (sectors == 0)
    {
        sectors = (uint32_t)(volume->size / volume->bytes_per_sector);
    }
    if (sectors == 0)
        return -1;

    size_t required_bytes = (size_t)sectors * (size_t)volume->bytes_per_sector;
    if (required_bytes > volume->size)
        sectors = (uint32_t)(volume->size / volume->bytes_per_sector);

    if (sectors == 0)
        return -1;

    struct block_device *device = fatfs_resolve_device(volume);
    if (!device)
        return -1;

    if (blockdev_write(device, volume->backing_lba, sectors, volume->base) < 0)
        return -1;

    volume->dirty = 0;
    return 0;
}

static void fatfs_flush_or_warn(struct fatfs_volume *volume)
{
    if (!volume)
        return;
    if (!volume->backing_configured)
        return;
    if (blockdev_device_count() == 0)
        return;
    if (fatfs_flush(volume) < 0)
        klog_warn("fat: failed to flush volume changes");
}

static uint32_t fatfs_allocate_cluster(struct fatfs_volume *volume)
{
    uint32_t total = volume->total_clusters + 1u;
    uint32_t start = 2u;
    for (uint32_t cluster = start; cluster <= total + 1u; ++cluster)
    {
        uint32_t value = fatfs_read_fat(volume, cluster);
        if (value == 0)
        {
            fatfs_write_fat(volume, cluster, fatfs_eoc_marker(volume));
            fatfs_zero_cluster(volume, cluster);
            return cluster;
        }
    }
    return 0;
}

static void fatfs_free_chain(struct fatfs_volume *volume, uint32_t start)
{
    if (start < 2u)
        return;

    uint32_t cluster = start;
    while (cluster >= 2u)
    {
        uint32_t next = fatfs_read_fat(volume, cluster);
        fatfs_write_fat(volume, cluster, 0u);
        if (fatfs_is_eoc(volume, next))
            break;
        cluster = next;
    }
}

static uint32_t fatfs_first_cluster(const struct fat_dir_entry *entry)
{
    uint32_t high = (uint32_t)entry->first_cluster_high;
    uint32_t low = (uint32_t)entry->first_cluster_low;
    return (high << 16) | low;
}

static void fatfs_set_first_cluster(struct fat_dir_entry *entry, uint32_t cluster)
{
    entry->first_cluster_high = (uint16_t)((cluster >> 16) & 0xFFFFu);
    entry->first_cluster_low = (uint16_t)(cluster & 0xFFFFu);
}

static void fatfs_clear_entry(struct fat_dir_entry *entry)
{
    uint8_t *ptr = (uint8_t *)entry;
    for (size_t i = 0; i < sizeof(*entry); ++i)
        ptr[i] = 0;
}

static size_t fatfs_entries_per_cluster(const struct fatfs_volume *volume)
{
    size_t cluster_bytes = fatfs_cluster_size_bytes(volume);
    return cluster_bytes / sizeof(struct fat_dir_entry);
}

static int fatfs_name_is_dot(const uint8_t name[11])
{
    if (name[0] == '.')
    {
        int is_dotdot = (name[1] == '.' && name[2] == ' ');
        int only_dot = (name[1] == ' ');
        return is_dotdot || only_dot;
    }
    return 0;
}

static void fatfs_format_entry_name(const struct fat_dir_entry *entry, char *out)
{
    size_t pos = 0;
    for (int i = 0; i < 8; ++i)
    {
        char c = (char)entry->name[i];
        if (c == ' ')
            break;
        out[pos++] = c;
    }

    int has_ext = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (entry->name[8 + i] != ' ')
        {
            has_ext = 1;
            break;
        }
    }

    if (has_ext)
    {
        out[pos++] = '.';
        for (int i = 0; i < 3; ++i)
        {
            char c = (char)entry->name[8 + i];
            if (c == ' ')
                break;
            out[pos++] = c;
        }
    }
    out[pos] = '\0';
}

static int fatfs_is_valid_char(char c)
{
    if (c >= 'A' && c <= 'Z')
        return 1;
    if (c >= '0' && c <= '9')
        return 1;
    if (c == '_')
        return 1;
    if (c == '-')
        return 1;
    if (c == '~')
        return 1;
    return 0;
}

static char fatfs_to_upper(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - ('a' - 'A'));
    return c;
}

static int fatfs_make_short_name(const char *segment, uint8_t out[11])
{
    for (int i = 0; i < 11; ++i)
        out[i] = ' ';

    if (!segment || !segment[0])
        return 0;

    int base_len = 0;
    int ext_len = 0;
    const char *ext = NULL;

    for (int i = 0; segment[i]; ++i)
    {
        if (segment[i] == '.')
            ext = &segment[i + 1];
    }

    if (ext)
    {
        const char *ptr = segment;
        while (*ptr && ptr != ext - 1 && base_len < 8)
        {
            char c = fatfs_to_upper(*ptr++);
            if (!fatfs_is_valid_char(c))
                return 0;
            out[base_len++] = (uint8_t)c;
        }
        if (ptr != ext - 1)
            return 0;

        const char *ext_ptr = ext;
        while (*ext_ptr && ext_len < 3)
        {
            char c = fatfs_to_upper(*ext_ptr++);
            if (!fatfs_is_valid_char(c))
                return 0;
            out[8 + ext_len++] = (uint8_t)c;
        }
        if (*ext_ptr)
            return 0;
    }
    else
    {
        const char *ptr = segment;
        while (*ptr && base_len < 8)
        {
            char c = fatfs_to_upper(*ptr++);
            if (!fatfs_is_valid_char(c))
                return 0;
            out[base_len++] = (uint8_t)c;
        }
        if (*ptr)
            return 0;
    }

    if (base_len == 0)
        return 0;
    return 1;
}

static int fatfs_dir_scan(struct fatfs_volume *volume, uint32_t dir_cluster, const uint8_t target[11], struct fat_dir_scan *scan)
{
    scan->match = NULL;
    scan->match_cluster = 0;
    scan->match_index = 0;
    scan->free_entry = NULL;
    scan->free_cluster = 0;
    scan->free_index = 0;
    scan->zero_entry = NULL;
    scan->zero_cluster = 0;
    scan->zero_index = 0;
    scan->last_cluster = (dir_cluster >= 2u) ? dir_cluster : 0u;

    size_t index = 0;

    if (dir_cluster == 0 && volume->fat_type == FATFS_TYPE_FAT16)
    {
        uint8_t *root = fatfs_sector_ptr(volume, volume->root_dir_sector);
        if (!root)
            return -1;
        size_t total_entries = (size_t)volume->root_entries;

        for (size_t i = 0; i < total_entries; ++i)
        {
            struct fat_dir_entry *entry = (struct fat_dir_entry *)(root + i * sizeof(struct fat_dir_entry));
            uint8_t first = entry->name[0];
            if (first == FAT_ENTRY_END)
            {
                if (!scan->zero_entry)
                {
                    scan->zero_entry = entry;
                    scan->zero_cluster = 0;
                    scan->zero_index = (uint32_t)index;
                }
                break;
            }
            if (first == FAT_ENTRY_FREE)
            {
                if (!scan->free_entry)
                {
                    scan->free_entry = entry;
                    scan->free_cluster = 0;
                    scan->free_index = (uint32_t)index;
                }
                ++index;
                continue;
            }
            if (entry->attr == FAT_ATTR_LFN || (entry->attr & FAT_ATTR_VOLUME_ID))
            {
                ++index;
                continue;
            }
            if (target && !scan->match)
            {
                int match = 1;
                for (int j = 0; j < 11; ++j)
                {
                    if (entry->name[j] != target[j])
                    {
                        match = 0;
                        break;
                    }
                }
                if (match)
                {
                    scan->match = entry;
                    scan->match_cluster = 0;
                    scan->match_index = (uint32_t)index;
                    return 0;
                }
            }
            ++index;
        }
        return 0;
    }

    uint32_t cluster = dir_cluster;
    uint32_t prev_cluster = 0;
    size_t entries_per_cluster = fatfs_entries_per_cluster(volume);

    while (cluster >= 2u)
    {
        uint8_t *cluster_ptr = fatfs_cluster_ptr(volume, cluster);
        if (!cluster_ptr)
            return -1;

        for (size_t i = 0; i < entries_per_cluster; ++i)
        {
            struct fat_dir_entry *entry = (struct fat_dir_entry *)(cluster_ptr + i * sizeof(struct fat_dir_entry));
            uint8_t first = entry->name[0];
            if (first == FAT_ENTRY_END)
            {
                if (!scan->zero_entry)
                {
                    scan->zero_entry = entry;
                    scan->zero_cluster = cluster;
                    scan->zero_index = (uint32_t)index;
                }
                scan->last_cluster = cluster;
                return 0;
            }
            if (first == FAT_ENTRY_FREE)
            {
                if (!scan->free_entry)
                {
                    scan->free_entry = entry;
                    scan->free_cluster = cluster;
                    scan->free_index = (uint32_t)index;
                }
                ++index;
                continue;
            }
            if (entry->attr == FAT_ATTR_LFN || (entry->attr & FAT_ATTR_VOLUME_ID))
            {
                ++index;
                continue;
            }
            if (target && !scan->match)
            {
                int match = 1;
                for (int j = 0; j < 11; ++j)
                {
                    if (entry->name[j] != target[j])
                    {
                        match = 0;
                        break;
                    }
                }
                if (match)
                {
                    scan->match = entry;
                    scan->match_cluster = cluster;
                    scan->match_index = (uint32_t)index;
                    scan->last_cluster = cluster;
                    return 0;
                }
            }
            ++index;
        }
        prev_cluster = cluster;
        uint32_t next = fatfs_read_fat(volume, cluster);
        if (fatfs_is_eoc(volume, next))
        {
            scan->last_cluster = cluster;
            break;
        }
        cluster = next;
    }

    if (!scan->last_cluster)
        scan->last_cluster = (prev_cluster ? prev_cluster : dir_cluster);
    return 0;
}

static struct fat_dir_entry *fatfs_dir_take_slot(struct fatfs_volume *volume, uint32_t dir_cluster, struct fat_dir_scan *scan)
{
    if (scan->match)
        return scan->match;

    if (scan->free_entry)
        return scan->free_entry;
    if (scan->zero_entry)
    {
        scan->zero_entry->name[0] = FAT_ENTRY_FREE;
        return scan->zero_entry;
    }

    if (dir_cluster == 0 && volume->fat_type == FATFS_TYPE_FAT16)
        return NULL;

    uint32_t target_cluster = dir_cluster;
    if (target_cluster < 2u)
        return NULL;

    uint32_t new_cluster = fatfs_allocate_cluster(volume);
    if (new_cluster == 0)
        return NULL;

    uint32_t last = scan->last_cluster ? scan->last_cluster : dir_cluster;
    if (last >= 2u)
        fatfs_write_fat(volume, last, new_cluster);
    fatfs_write_fat(volume, new_cluster, fatfs_eoc_marker(volume));
    fatfs_zero_cluster(volume, new_cluster);

    scan->last_cluster = new_cluster;

    uint8_t *cluster_ptr = fatfs_cluster_ptr(volume, new_cluster);
    if (!cluster_ptr)
        return NULL;

    struct fat_dir_entry *entry = (struct fat_dir_entry *)cluster_ptr;
    return entry;
}

static int fatfs_next_path_segment(const char **cursor, char *segment, size_t seg_capacity)
{
    const char *ptr = *cursor;
    while (*ptr == '/')
        ++ptr;
    if (*ptr == '\0')
    {
        *cursor = ptr;
        return 0;
    }

    size_t len = 0;
    while (*ptr && *ptr != '/')
    {
        if (len + 1 >= seg_capacity)
            return -1;
        segment[len++] = *ptr++;
    }
    segment[len] = '\0';
    *cursor = ptr;
    return 1;
}

static int fatfs_resolve_directory(struct fatfs_volume *volume, const char *path, uint32_t *out_cluster)
{
    if (!path || path[0] == '\0')
    {
        if (volume->fat_type == FATFS_TYPE_FAT16)
        {
            *out_cluster = 0;
            return 0;
        }
        *out_cluster = volume->root_cluster;
        return 0;
    }

    const char *cursor = path;
    uint32_t current = (volume->fat_type == FATFS_TYPE_FAT16) ? 0 : volume->root_cluster;
    char segment[16];
    uint8_t short_name[11];

    while (1)
    {
        int rc = fatfs_next_path_segment(&cursor, segment, sizeof(segment));
        if (rc <= 0)
            break;
        if (*cursor == '/')
            ++cursor;

        if (segment[0] == '\0')
            continue;

        if (!fatfs_make_short_name(segment, short_name))
            return -1;

        struct fat_dir_scan scan;
        if (fatfs_dir_scan(volume, current, short_name, &scan) < 0)
            return -1;
        if (!scan.match)
            return -1;
        if (!(scan.match->attr & FAT_ATTR_DIRECTORY))
            return -1;
        current = fatfs_first_cluster(scan.match);
        if (current == 0)
        {
            if (volume->fat_type == FATFS_TYPE_FAT16)
                current = 0;
            else
                return -1;
        }
    }

    *out_cluster = current;
    return 0;
}

static int fatfs_resolve_parent(struct fatfs_volume *volume, const char *path, uint32_t *out_parent_cluster, char *leaf, size_t leaf_capacity)
{
    const char *last_sep = NULL;
    for (const char *p = path; *p; ++p)
    {
        if (*p == '/')
            last_sep = p;
    }

    if (!last_sep)
    {
        if (volume->fat_type == FATFS_TYPE_FAT16)
            *out_parent_cluster = 0;
        else
            *out_parent_cluster = volume->root_cluster;
        size_t len = 0;
        while (path[len] && len + 1 < leaf_capacity)
        {
            leaf[len] = path[len];
            ++len;
        }
        leaf[len] = '\0';
        if (path[len])
            return -1;
        return 0;
    }

    size_t parent_len = (size_t)(last_sep - path);
    char parent[128];
    if (parent_len >= sizeof(parent))
        return -1;
    for (size_t i = 0; i < parent_len; ++i)
        parent[i] = path[i];
    parent[parent_len] = '\0';

    if (fatfs_resolve_directory(volume, parent, out_parent_cluster) < 0)
        return -1;

    const char *leaf_ptr = last_sep + 1;
    size_t len = 0;
    while (leaf_ptr[len] && len + 1 < leaf_capacity)
    {
        leaf[len] = leaf_ptr[len];
        ++len;
    }
    leaf[len] = '\0';
    if (leaf_ptr[len])
        return -1;

    return 0;
}

static int fatfs_validate_path_segment(const char *segment)
{
    if (!segment || segment[0] == '\0')
        return 0;
    for (const char *p = segment; *p; ++p)
    {
        if (*p == '.' && (p == segment))
            continue;
        char c = *p;
        if (c == '.')
            continue;
        char upper = fatfs_to_upper(c);
        if (!fatfs_is_valid_char(upper))
            return 0;
    }
    return 1;
}

static int fatfs_prepare_short_name(const char *segment, uint8_t out[11])
{
    if (!segment || segment[0] == '\0')
        return 0;
    if (!fatfs_validate_path_segment(segment))
        return 0;
    return fatfs_make_short_name(segment, out);
}

int fatfs_init(struct fatfs_volume *volume, void *base, size_t size)
{
    if (!volume || !base || size < 512u)
        return FATFS_TYPE_NONE;

    volume->base = (uint8_t *)base;
    volume->size = size;
    volume->ready = 0;
    volume->fat_type = FATFS_TYPE_NONE;
    volume->device = NULL;
    volume->backing_lba = 0;
    volume->backing_sectors = 0;
    volume->backing_configured = 0;
    volume->dirty = 0;

    const uint8_t *boot = volume->base;
    volume->bytes_per_sector = read_le16(boot + 11);
    volume->sectors_per_cluster = boot[13];
    volume->reserved_sectors = read_le16(boot + 14);
    volume->fat_count = boot[16];
    volume->root_entries = read_le16(boot + 17);

    uint16_t total16 = read_le16(boot + 19);
    uint32_t total32 = read_le32(boot + 32);
    volume->total_sectors = total16 ? total16 : total32;

    uint16_t spf16 = read_le16(boot + 22);
    uint32_t spf32 = read_le32(boot + 36);
    volume->sectors_per_fat = spf16 ? spf16 : spf32;

    if (volume->bytes_per_sector == 0 || volume->sectors_per_cluster == 0 || volume->fat_count == 0)
        return FATFS_TYPE_NONE;

    uint32_t root_dir_sectors = ((uint32_t)volume->root_entries * 32u + (uint32_t)volume->bytes_per_sector - 1u) / (uint32_t)volume->bytes_per_sector;
    volume->root_dir_sectors = root_dir_sectors;

    uint32_t fat_area = volume->fat_count * volume->sectors_per_fat;
    uint32_t data_sectors = volume->total_sectors - (volume->reserved_sectors + fat_area + root_dir_sectors);
    if ((int32_t)data_sectors < 0)
        return FATFS_TYPE_NONE;

    uint32_t cluster_count = data_sectors / volume->sectors_per_cluster;
    volume->total_clusters = cluster_count;

    int type = FATFS_TYPE_NONE;
    if (cluster_count == 0u)
        type = FATFS_TYPE_NONE;
    else if (cluster_count < 65525u)
        type = FATFS_TYPE_FAT16;
    else
        type = FATFS_TYPE_FAT32;

    if (type == FATFS_TYPE_NONE)
        return FATFS_TYPE_NONE;

    volume->fat_type = (uint32_t)type;
    if (volume->fat_type == FATFS_TYPE_FAT16)
    {
        volume->root_dir_sector = volume->reserved_sectors + fat_area;
        volume->root_cluster = 0u;
        volume->data_start_sector = volume->root_dir_sector + root_dir_sectors;
    }
    else
    {
        volume->root_cluster = read_le32(boot + 44);
        if (volume->root_cluster < 2u)
            volume->root_cluster = 2u;
        volume->root_dir_sector = 0u;
        volume->data_start_sector = volume->reserved_sectors + fat_area;
        volume->root_entries = 0;
        volume->root_dir_sectors = 0;
    }

    volume->ready = 1;
    volume->mount_path[0] = '\0';
    return type;
}

int fatfs_ready(const struct fatfs_volume *volume)
{
    return volume && volume->ready;
}

int fatfs_type(const struct fatfs_volume *volume)
{
    if (!volume || !volume->ready)
        return FATFS_TYPE_NONE;
    return (int)volume->fat_type;
}

static int fatfs_vfs_list(void *ctx, const char *path, char *buffer, size_t buffer_size)
{
    return fatfs_list((struct fatfs_volume *)ctx, path, buffer, buffer_size);
}

static int fatfs_vfs_read(void *ctx, const char *path, char *buffer, size_t buffer_size)
{
    size_t read = 0;
    int rc = fatfs_read((struct fatfs_volume *)ctx, path, buffer, buffer_size, &read);
    if (rc < 0)
        return rc;
    return (int)read;
}

static int fatfs_vfs_write(void *ctx, const char *path, const char *data, size_t length, enum vfs_write_mode mode)
{
    return fatfs_write((struct fatfs_volume *)ctx, path, data, length, mode);
}

static int fatfs_vfs_remove(void *ctx, const char *path)
{
    return fatfs_remove((struct fatfs_volume *)ctx, path);
}

static int fatfs_vfs_mkdir(void *ctx, const char *path)
{
    return fatfs_mkdir((struct fatfs_volume *)ctx, path);
}

static const struct vfs_fs_ops fatfs_ops = {
    .list = fatfs_vfs_list,
    .read = fatfs_vfs_read,
    .write = fatfs_vfs_write,
    .remove = fatfs_vfs_remove,
    .mkdir = fatfs_vfs_mkdir
};

int fatfs_mount(struct fatfs_volume *volume, const char *name)
{
    if (!fatfs_ready(volume))
        return -1;

    const char *label = (name && name[0]) ? name : "Disk0";
    const char *prefix = "/Volumes/";
    size_t prefix_len = 9u;

    size_t label_len = 0;
    while (label[label_len])
        ++label_len;

    if (prefix_len + label_len >= sizeof(volume->mount_path))
        return -1;

    for (size_t i = 0; i < prefix_len; ++i)
        volume->mount_path[i] = prefix[i];
    for (size_t i = 0; i < label_len; ++i)
        volume->mount_path[prefix_len + i] = label[i];
    volume->mount_path[prefix_len + label_len] = '\0';

    if (vfs_mkdir(volume->mount_path) < 0)
        vfs_write_file(volume->mount_path, NULL, 0);

    if (vfs_mount(volume->mount_path, &fatfs_ops, volume) < 0)
        return -1;

    return 0;
}

static int fatfs_list_directory(struct fatfs_volume *volume, uint32_t dir_cluster, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return -1;

    size_t written = 0;

    struct fat_dir_scan scan;
    if (fatfs_dir_scan(volume, dir_cluster, NULL, &scan) < 0)
        return -1;

    if (dir_cluster == 0 && volume->fat_type == FATFS_TYPE_FAT16)
    {
        uint8_t *root = fatfs_sector_ptr(volume, volume->root_dir_sector);
        if (!root)
            return -1;
        size_t total_entries = (size_t)volume->root_entries;
        for (size_t i = 0; i < total_entries; ++i)
        {
            struct fat_dir_entry *entry = (struct fat_dir_entry *)(root + i * sizeof(struct fat_dir_entry));
            if (entry->name[0] == FAT_ENTRY_END)
                break;
            if (entry->name[0] == FAT_ENTRY_FREE)
                continue;
            if (entry->attr == FAT_ATTR_LFN || (entry->attr & FAT_ATTR_VOLUME_ID))
                continue;
            if (fatfs_name_is_dot(entry->name))
                continue;

            char name[20];
            fatfs_format_entry_name(entry, name);
            size_t len = 0;
            while (name[len])
                ++len;
            if (entry->attr & FAT_ATTR_DIRECTORY)
            {
                if (len + 2 >= buffer_size - written)
                    return (int)written;
                for (size_t j = 0; j < len; ++j)
                    buffer[written++] = name[j];
                buffer[written++] = '/';
                buffer[written++] = '\n';
            }
            else
            {
                if (len + 1 >= buffer_size - written)
                    return (int)written;
                for (size_t j = 0; j < len; ++j)
                    buffer[written++] = name[j];
                buffer[written++] = '\n';
            }
        }
    }
    else
    {
        uint32_t cluster = dir_cluster;
        size_t entries_per_cluster = fatfs_entries_per_cluster(volume);
        while (cluster >= 2u)
        {
            uint8_t *cluster_ptr = fatfs_cluster_ptr(volume, cluster);
            if (!cluster_ptr)
                break;
            for (size_t i = 0; i < entries_per_cluster; ++i)
            {
                struct fat_dir_entry *entry = (struct fat_dir_entry *)(cluster_ptr + i * sizeof(struct fat_dir_entry));
                if (entry->name[0] == FAT_ENTRY_END)
                    goto done;
                if (entry->name[0] == FAT_ENTRY_FREE)
                    continue;
                if (entry->attr == FAT_ATTR_LFN || (entry->attr & FAT_ATTR_VOLUME_ID))
                    continue;
                if (fatfs_name_is_dot(entry->name))
                    continue;

                char name[20];
                fatfs_format_entry_name(entry, name);
                size_t len = 0;
                while (name[len])
                    ++len;
                if (entry->attr & FAT_ATTR_DIRECTORY)
                {
                    if (len + 2 >= buffer_size - written)
                        goto done;
                    for (size_t j = 0; j < len; ++j)
                        buffer[written++] = name[j];
                    buffer[written++] = '/';
                    buffer[written++] = '\n';
                }
                else
                {
                    if (len + 1 >= buffer_size - written)
                        goto done;
                    for (size_t j = 0; j < len; ++j)
                        buffer[written++] = name[j];
                    buffer[written++] = '\n';
                }
            }
            uint32_t next = fatfs_read_fat(volume, cluster);
            if (fatfs_is_eoc(volume, next))
                break;
            cluster = next;
        }
    }

done:
    if (written == 0)
    {
        buffer[0] = '\0';
        return 0;
    }
    if (written >= buffer_size)
        written = buffer_size - 1;
    buffer[written - 1] = '\0';
    return (int)(written - 1);
}

int fatfs_list(struct fatfs_volume *volume, const char *path, char *buffer, size_t buffer_size)
{
    if (!fatfs_ready(volume))
        return -1;

    uint32_t dir_cluster;
    if (fatfs_resolve_directory(volume, path, &dir_cluster) < 0)
        return -1;
    return fatfs_list_directory(volume, dir_cluster, buffer, buffer_size);
}

static int fatfs_load_cluster_chain(struct fatfs_volume *volume, uint32_t start, uint8_t *out, size_t max_len, size_t *copied)
{
    if (!out || max_len == 0)
        return -1;
    size_t total = 0;
    size_t cluster_size = fatfs_cluster_size_bytes(volume);
    uint32_t cluster = start;
    while (cluster >= 2u && total < max_len)
    {
        uint8_t *src = fatfs_cluster_ptr(volume, cluster);
        if (!src)
            break;
        size_t to_copy = cluster_size;
        if (total + to_copy > max_len)
            to_copy = max_len - total;
        for (size_t i = 0; i < to_copy; ++i)
            out[total + i] = src[i];
        total += to_copy;
        uint32_t next = fatfs_read_fat(volume, cluster);
        if (fatfs_is_eoc(volume, next))
            break;
        cluster = next;
    }
    if (copied)
        *copied = total;
    return 0;
}

int fatfs_read(struct fatfs_volume *volume, const char *path, void *out, size_t max_len, size_t *out_size)
{
    if (!fatfs_ready(volume) || !out || max_len == 0)
        return -1;

    uint32_t parent_cluster;
    char leaf[64];
    if (fatfs_resolve_parent(volume, path, &parent_cluster, leaf, sizeof(leaf)) < 0)
        return -1;

    uint8_t short_name[11];
    if (!fatfs_prepare_short_name(leaf, short_name))
        return -1;

    struct fat_dir_scan scan;
    if (fatfs_dir_scan(volume, parent_cluster, short_name, &scan) < 0)
        return -1;
    if (!scan.match)
        return -1;
    if (scan.match->attr & FAT_ATTR_DIRECTORY)
        return -1;

    uint32_t first_cluster = fatfs_first_cluster(scan.match);
    size_t bytes_to_copy = scan.match->file_size;
    if (bytes_to_copy > max_len)
        bytes_to_copy = max_len;

    if (bytes_to_copy == 0 || first_cluster < 2u)
    {
        if (out_size)
            *out_size = 0;
        return 0;
    }

    if (fatfs_load_cluster_chain(volume, first_cluster, (uint8_t *)out, bytes_to_copy, out_size) < 0)
        return -1;

    if (out_size && *out_size < max_len)
    {
        uint8_t *buf = (uint8_t *)out;
        buf[*out_size] = 0;
    }

    return 0;
}

static int fatfs_write_replace(struct fatfs_volume *volume, struct fat_dir_entry *entry, const uint8_t *data, size_t length)
{
    fatfs_free_chain(volume, fatfs_first_cluster(entry));
    fatfs_set_first_cluster(entry, 0);
    entry->file_size = 0;

    if (length == 0)
        return 0;

    size_t cluster_size = fatfs_cluster_size_bytes(volume);
    size_t remaining = length;
    const uint8_t *cursor = data;
    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;

    while (remaining > 0)
    {
        uint32_t cluster = fatfs_allocate_cluster(volume);
        if (!cluster)
        {
            fatfs_free_chain(volume, first_cluster);
            fatfs_set_first_cluster(entry, 0);
            entry->file_size = 0;
            return -1;
        }
        if (!first_cluster)
            first_cluster = cluster;
        if (prev_cluster)
            fatfs_write_fat(volume, prev_cluster, cluster);

        uint8_t *dest = fatfs_cluster_ptr(volume, cluster);
        if (!dest)
        {
            fatfs_free_chain(volume, first_cluster);
            fatfs_set_first_cluster(entry, 0);
            entry->file_size = 0;
            return -1;
        }

        size_t to_copy = (remaining > cluster_size) ? cluster_size : remaining;
        for (size_t i = 0; i < to_copy; ++i)
            dest[i] = cursor[i];
        if (to_copy < cluster_size)
        {
            for (size_t i = to_copy; i < cluster_size; ++i)
                dest[i] = 0;
        }

        cursor += to_copy;
        remaining -= to_copy;
        prev_cluster = cluster;
    }

    fatfs_write_fat(volume, prev_cluster, fatfs_eoc_marker(volume));
    fatfs_set_first_cluster(entry, first_cluster);
    entry->file_size = (uint32_t)length;
    return 0;
}

static int fatfs_write_append(struct fatfs_volume *volume, struct fat_dir_entry *entry, const uint8_t *data, size_t length)
{
    if (length == 0)
        return 0;

    size_t cluster_size = fatfs_cluster_size_bytes(volume);
    uint32_t first_cluster = fatfs_first_cluster(entry);
    uint32_t last_cluster = 0;
    size_t offset = entry->file_size % cluster_size;

    if (first_cluster < 2u)
    {
        first_cluster = fatfs_allocate_cluster(volume);
        if (!first_cluster)
            return -1;
        fatfs_set_first_cluster(entry, first_cluster);
        entry->file_size = 0;
        offset = 0;
        fatfs_write_fat(volume, first_cluster, fatfs_eoc_marker(volume));
    }

    last_cluster = first_cluster;
    while (1)
    {
        uint32_t next = fatfs_read_fat(volume, last_cluster);
        if (fatfs_is_eoc(volume, next))
            break;
        last_cluster = next;
    }

    uint8_t *dest = fatfs_cluster_ptr(volume, last_cluster);
    if (!dest)
        return -1;

    size_t remaining = length;
    const uint8_t *cursor = data;

    if (offset < cluster_size)
    {
        size_t space = cluster_size - offset;
        size_t to_copy = (remaining < space) ? remaining : space;
        for (size_t i = 0; i < to_copy; ++i)
            dest[offset + i] = cursor[i];
        cursor += to_copy;
        remaining -= to_copy;
        offset += to_copy;
        if (offset < cluster_size)
        {
            entry->file_size += (uint32_t)to_copy;
            return 0;
        }
    }

    while (remaining > 0)
    {
        uint32_t new_cluster = fatfs_allocate_cluster(volume);
        if (!new_cluster)
            return -1;
        fatfs_write_fat(volume, last_cluster, new_cluster);
        fatfs_write_fat(volume, new_cluster, fatfs_eoc_marker(volume));
        last_cluster = new_cluster;

        uint8_t *cluster_ptr = fatfs_cluster_ptr(volume, new_cluster);
        if (!cluster_ptr)
            return -1;

        size_t to_copy = (remaining > cluster_size) ? cluster_size : remaining;
        for (size_t i = 0; i < to_copy; ++i)
            cluster_ptr[i] = cursor[i];
        if (to_copy < cluster_size)
        {
            for (size_t i = to_copy; i < cluster_size; ++i)
                cluster_ptr[i] = 0;
        }

        cursor += to_copy;
        remaining -= to_copy;
    }

    entry->file_size += (uint32_t)length;
    return 0;
}

static int fatfs_directory_is_empty(struct fatfs_volume *volume, uint32_t cluster)
{
    if (cluster < 2u)
        return 1;

    size_t entries_per_cluster = fatfs_entries_per_cluster(volume);
    uint32_t current = cluster;

    while (current >= 2u)
    {
        uint8_t *ptr = fatfs_cluster_ptr(volume, current);
        if (!ptr)
            return -1;
        for (size_t i = 0; i < entries_per_cluster; ++i)
        {
            struct fat_dir_entry *entry = (struct fat_dir_entry *)(ptr + i * sizeof(struct fat_dir_entry));
            if (entry->name[0] == FAT_ENTRY_END)
                return 1;
            if (entry->name[0] == FAT_ENTRY_FREE)
                continue;
            if (entry->attr == FAT_ATTR_LFN || (entry->attr & FAT_ATTR_VOLUME_ID))
                continue;
            if (fatfs_name_is_dot(entry->name))
                continue;
            return 0;
        }
        uint32_t next = fatfs_read_fat(volume, current);
        if (fatfs_is_eoc(volume, next))
            break;
        current = next;
    }
    return 1;
}

int fatfs_write(struct fatfs_volume *volume, const char *path, const void *data, size_t length, enum vfs_write_mode mode)
{
    if (!fatfs_ready(volume))
        return -1;

    uint32_t parent_cluster;
    char leaf[64];
    if (fatfs_resolve_parent(volume, path, &parent_cluster, leaf, sizeof(leaf)) < 0)
        return -1;

    uint8_t short_name[11];
    if (!fatfs_prepare_short_name(leaf, short_name))
        return -1;

    struct fat_dir_scan scan;
    if (fatfs_dir_scan(volume, parent_cluster, short_name, &scan) < 0)
        return -1;

    struct fat_dir_entry *entry = scan.match;
    if (!entry)
    {
        entry = fatfs_dir_take_slot(volume, parent_cluster, &scan);
        if (!entry)
            return -1;
        fatfs_clear_entry(entry);
        for (int i = 0; i < 11; ++i)
            entry->name[i] = short_name[i];
        entry->attr = 0x20;
        fatfs_set_first_cluster(entry, 0);
        entry->file_size = 0;
    }

    if (entry->attr & FAT_ATTR_DIRECTORY)
        return -1;

    const uint8_t *bytes = (const uint8_t *)data;
    if (!bytes && length > 0)
        return -1;

    int result;
    if (mode == VFS_WRITE_REPLACE)
        result = fatfs_write_replace(volume, entry, bytes, length);
    else
        result = fatfs_write_append(volume, entry, bytes, length);

    if (result == 0)
    {
        fatfs_mark_dirty(volume);
        fatfs_flush_or_warn(volume);
    }

    return result;
}

int fatfs_remove(struct fatfs_volume *volume, const char *path)
{
    if (!fatfs_ready(volume))
        return -1;

    uint32_t parent_cluster;
    char leaf[64];
    if (fatfs_resolve_parent(volume, path, &parent_cluster, leaf, sizeof(leaf)) < 0)
        return -1;

    uint8_t short_name[11];
    if (!fatfs_prepare_short_name(leaf, short_name))
        return -1;

    struct fat_dir_scan scan;
    if (fatfs_dir_scan(volume, parent_cluster, short_name, &scan) < 0)
        return -1;
    if (!scan.match)
        return -1;

    uint32_t first_cluster = fatfs_first_cluster(scan.match);
    if (scan.match->attr & FAT_ATTR_DIRECTORY)
    {
        if (leaf[0] == '\0')
            return -1;
        if (first_cluster < 2u && volume->fat_type == FATFS_TYPE_FAT16)
            return -1;
        int empty = fatfs_directory_is_empty(volume, first_cluster);
        if (empty < 0)
            return -1;
        if (!empty)
            return -1;
    }

    if (first_cluster >= 2u)
        fatfs_free_chain(volume, first_cluster);

    scan.match->name[0] = FAT_ENTRY_FREE;
    fatfs_mark_dirty(volume);
    fatfs_flush_or_warn(volume);
    return 0;
}

static void fatfs_write_dot_entries(struct fatfs_volume *volume, uint32_t cluster, uint32_t parent_cluster)
{
    uint8_t *ptr = fatfs_cluster_ptr(volume, cluster);
    if (!ptr)
        return;
    struct fat_dir_entry *dot = (struct fat_dir_entry *)ptr;
    struct fat_dir_entry *dotdot = (struct fat_dir_entry *)(ptr + sizeof(struct fat_dir_entry));

    fatfs_clear_entry(dot);
    dot->name[0] = '.';
    for (int i = 1; i < 11; ++i)
        dot->name[i] = ' ';
    dot->attr = FAT_ATTR_DIRECTORY;
    fatfs_set_first_cluster(dot, cluster);

    fatfs_clear_entry(dotdot);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    for (int i = 2; i < 11; ++i)
        dotdot->name[i] = ' ';
    dotdot->attr = FAT_ATTR_DIRECTORY;
    if (parent_cluster < 2u && volume->fat_type == FATFS_TYPE_FAT16)
        fatfs_set_first_cluster(dotdot, 0);
    else
        fatfs_set_first_cluster(dotdot, parent_cluster);
}

int fatfs_mkdir(struct fatfs_volume *volume, const char *path)
{
    if (!fatfs_ready(volume))
        return -1;

    uint32_t parent_cluster;
    char leaf[64];
    if (fatfs_resolve_parent(volume, path, &parent_cluster, leaf, sizeof(leaf)) < 0)
        return -1;

    if (leaf[0] == '\0')
        return -1;

    uint8_t short_name[11];
    if (!fatfs_prepare_short_name(leaf, short_name))
        return -1;

    struct fat_dir_scan scan;
    if (fatfs_dir_scan(volume, parent_cluster, short_name, &scan) < 0)
        return -1;
    if (scan.match)
        return -1;

    struct fat_dir_entry *entry = fatfs_dir_take_slot(volume, parent_cluster, &scan);
    if (!entry)
        return -1;

    fatfs_clear_entry(entry);
    for (int i = 0; i < 11; ++i)
        entry->name[i] = short_name[i];
    entry->attr = FAT_ATTR_DIRECTORY;

    uint32_t new_cluster = fatfs_allocate_cluster(volume);
    if (!new_cluster)
        return -1;
    fatfs_write_fat(volume, new_cluster, fatfs_eoc_marker(volume));
    fatfs_zero_cluster(volume, new_cluster);
    fatfs_write_dot_entries(volume, new_cluster, (parent_cluster < 2u && volume->fat_type == FATFS_TYPE_FAT16) ? 0 : parent_cluster);

    fatfs_set_first_cluster(entry, new_cluster);
    entry->file_size = 0;
    fatfs_mark_dirty(volume);
    fatfs_flush_or_warn(volume);
    return 0;
}

int fatfs_file_size(struct fatfs_volume *volume, const char *path, uint32_t *out_size)
{
    if (!fatfs_ready(volume))
        return -1;

    uint32_t parent_cluster;
    char leaf[64];
    if (fatfs_resolve_parent(volume, path, &parent_cluster, leaf, sizeof(leaf)) < 0)
        return -1;

    uint8_t short_name[11];
    if (!fatfs_prepare_short_name(leaf, short_name))
        return -1;

    struct fat_dir_scan scan;
    if (fatfs_dir_scan(volume, parent_cluster, short_name, &scan) < 0)
        return -1;
    if (!scan.match)
        return -1;
    if (out_size)
        *out_size = scan.match->file_size;
    return 0;
}
