#ifndef FAT16_H
#define FAT16_H

#include <stddef.h>
#include <stdint.h>

int fat16_init(const void *base, size_t size);
int fat16_ready(void);
int fat16_ls(char *out, size_t max_len);
int fat16_read(const char *path, char *out, size_t max_len);

#endif
