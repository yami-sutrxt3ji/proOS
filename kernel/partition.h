#ifndef PARTITION_H
#define PARTITION_H

#include "blockdev.h"

void partition_init(void);
void partition_scan_device(struct block_device *device);
void partition_autoscan(void);

#endif
