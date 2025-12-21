#ifndef VOLMGR_H
#define VOLMGR_H

#include <stddef.h>

struct block_device;

#define VOLMGR_MAX_VOLUMES 16
#define VOLMGR_NAME_MAX    16

struct volume_info
{
    const char *name;
    const char *mount_path;
    struct block_device *device;
    unsigned int index;
};

void volmgr_init(void);
void volmgr_rescan(void);
size_t volmgr_volume_count(void);
const struct volume_info *volmgr_volume_at(size_t index);
struct block_device *volmgr_find_device(const char *name);

#endif
