#ifndef BIOS_FALLBACK_H
#define BIOS_FALLBACK_H

#include <stdint.h>

void bios_fallback_init(uint8_t boot_drive);
int bios_fallback_available(void);
uint8_t bios_fallback_boot_drive(void);
int bios_fallback_read(uint8_t drive, uint64_t lba, uint32_t count, void *buffer);
int bios_fallback_write(uint8_t drive, uint64_t lba, uint32_t count, const void *buffer);

#endif
