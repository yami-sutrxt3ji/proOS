#include <stddef.h>

#include "module_api.h"

#include "devmgr.h"
#include "klog.h"
#include "vfs.h"

MODULE_METADATA("ps2mouse", "0.1.0", MODULE_FLAG_AUTOSTART);

static size_t local_strlen(const char *s)
{
    size_t len = 0;
    if (!s)
        return 0;
    while (s[len])
        ++len;
    return len;
}

static int ps2mouse_read(struct device_node *node, void *buffer, size_t length, size_t *out_read)
{
    (void)node;
    if (!buffer || length == 0)
        return -1;

    const char *msg = "mouse: awaiting driver\n";
    size_t msg_len = local_strlen(msg);
    if (msg_len == 0)
    {
        if (out_read)
            *out_read = 0;
        return 0;
    }

    if (msg_len >= length)
        msg_len = length - 1u;

    char *dst = (char *)buffer;
    for (size_t i = 0; i < msg_len; ++i)
        dst[i] = msg[i];
    dst[msg_len] = '\0';

    if (out_read)
        *out_read = msg_len;
    return 0;
}

static const struct device_ops ps2mouse_ops = {
    NULL,
    NULL,
    ps2mouse_read,
    NULL,
    NULL
};

int module_init(void)
{
    if (!devmgr_find("ps2ctrl0"))
    {
        struct device_descriptor ctrl_desc = {
            "ps2ctrl0",
            "bus.ps2",
            "platform0",
            NULL,
            DEVICE_FLAG_INTERNAL,
            NULL
        };
        if (devmgr_register_device(&ctrl_desc, NULL) < 0)
        {
            klog_warn("ps2mouse.driver: controller registration failed");
            return -1;
        }
    }

    struct device_descriptor desc = {
        "ps2mouse0",
        "input.mouse",
        "ps2ctrl0",
        &ps2mouse_ops,
        DEVICE_FLAG_PUBLISH,
        NULL
    };

    if (devmgr_register_device(&desc, NULL) < 0)
    {
        klog_warn("ps2mouse.driver: device registration failed");
        return -1;
    }

    klog_info("ps2mouse.driver: registered (stub)");

    const char *status = "ps2mouse: stub driver\n";
    vfs_write_file("/Devices/ps2mouse0.status", status, local_strlen(status));
    return 0;
}

void module_exit(void)
{
    devmgr_unregister_device("ps2mouse0");
    vfs_remove("/Devices/ps2mouse0.status");
}
