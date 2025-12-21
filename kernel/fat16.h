#ifndef FAT16_H
#define FAT16_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

struct fatfs_volume;

int fat16_init(const void *base, size_t size);
int fat16_ready(void);
int fat16_type(void);
int fat16_ls(char *out, size_t max_len);
int fat16_read(const char *path, char *out, size_t max_len);
int fat16_read_file(const char *path, void *out, size_t max_len, size_t *out_size);
int fat16_file_size(const char *path, uint32_t *out_size);
int fat16_mount_volume(const char *name);
int fat16_write_file(const char *path, const void *data, size_t length);
int fat16_append_file(const char *path, const void *data, size_t length);
int fat16_remove(const char *path);
int fat16_mkdir(const char *path);

struct fatfs_volume *fat16_volume(void);
void fat16_configure_backing(uint32_t lba_start, uint32_t sector_count);

#endif
