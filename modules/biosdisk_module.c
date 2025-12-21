#include <stddef.h>

#include "module_api.h"

#include "bios_fallback.h"
#include "klog.h"

MODULE_METADATA("biosdisk", "0.2.0", MODULE_FLAG_AUTOSTART);

int module_init(void)
{
    if (bios_fallback_available())
        klog_info("biosdisk: BIOS fallback active");
    else
        klog_warn("biosdisk: fallback unavailable");
    return 0;
}

void module_exit(void)
{
}
