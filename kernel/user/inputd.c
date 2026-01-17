#include "syslib.h"
#include "../service_types.h"
#include "../ipc_types.h"

void user_inputd(void)
{
    for (;;)
    {
        /* TODO: translate keyboard/mouse events via IPC */
        sys_sleep(10);
    }
}
