#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

void memory_init(void);
void *kalloc(size_t size);
void *kalloc_zero(size_t size);

#endif
