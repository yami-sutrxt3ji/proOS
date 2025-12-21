#include "bios_fallback.h"

#include <stdint.h>

#include "klog.h"
#include "memory.h"
#include "string.h"

#define BIOS_DAP_ADDRESS 0x00000500u
#define BIOS_BOUNCE_ADDRESS 0x00080000u
#define BIOS_MAX_SECTORS 64

struct bios_dap
{
    uint8_t size;
    uint8_t reserved;
    uint16_t count;
    uint16_t buffer_offset;
    uint16_t buffer_segment;
    uint64_t lba;
} __attribute__((packed));

static uint8_t fallback_drive = 0x80;
static int fallback_ready = 0;

static inline struct bios_dap *dap_ptr(void)
{
    return (struct bios_dap *)(uintptr_t)BIOS_DAP_ADDRESS;
}

static inline void *bounce_ptr(void)
{
    return (void *)(uintptr_t)BIOS_BOUNCE_ADDRESS;
}

extern int bios_thunk_read(uint8_t drive, uint16_t dap_segment, uint16_t dap_offset);

void bios_fallback_init(uint8_t boot_drive)
{
    fallback_drive = boot_drive;
    fallback_ready = 1;
    klog_info("bios: fallback ready");
}

int bios_fallback_available(void)
{
    return fallback_ready;
}

uint8_t bios_fallback_boot_drive(void)
{
    return fallback_drive;
}

static int read_chunk(uint8_t drive, uint64_t lba, uint16_t count, void *buffer)
{
    struct bios_dap *dap = dap_ptr();
    memset(dap, 0, sizeof(*dap));
    dap->size = 16;
    dap->count = count;
    dap->buffer_segment = (uint16_t)(BIOS_BOUNCE_ADDRESS >> 4);
    dap->buffer_offset = (uint16_t)(BIOS_BOUNCE_ADDRESS & 0x0F);
    dap->lba = lba;

    int status = bios_thunk_read(drive, 0x0000, (uint16_t)(BIOS_DAP_ADDRESS));
    if (status < 0)
        return status;

    memcpy(buffer, bounce_ptr(), (size_t)count * 512u);
    return 0;
}

int bios_fallback_read(uint8_t drive, uint64_t lba, uint32_t count, void *buffer)
{
    if (!fallback_ready || !buffer || count == 0)
        return -1;

    uint8_t *dst = (uint8_t *)buffer;
    uint32_t remaining = count;
    while (remaining > 0)
    {
        uint16_t chunk = (remaining > BIOS_MAX_SECTORS) ? BIOS_MAX_SECTORS : (uint16_t)remaining;
        if (read_chunk(drive, lba, chunk, dst) < 0)
            return -1;
        remaining -= chunk;
        lba += chunk;
        dst += (size_t)chunk * 512u;
    }
    return 0;
}
