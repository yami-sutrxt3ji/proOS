#include "power.h"

#include "io.h"
#include "klog.h"

#include <stdint.h>

static void write_port(uint16_t port, uint16_t value)
{
    outw(port, value);
    io_wait();
}

void power_shutdown(void)
{
    klog_info("power: shutdown requested");

    /* Signal ACPI and legacy QEMU/Bochs power-off paths. */
    write_port(0x604, 0x2000);
    write_port(0xB004, 0x2000);
    write_port(0x4004, 0x340);

    outb(0x64, 0xFE);

    klog_warn("power: halt fallback");
    while (1)
    {
        __asm__ __volatile__("cli");
        __asm__ __volatile__("hlt");
    }
}
